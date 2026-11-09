/* thread_upload.c —— 上报线程（W9 修正版：断网期照常入队，重连不阻塞采集）
 *
 * 核心改动（对比 W8 版）：
 *   W8：发送失败 → connect_with_backoff() 一直循环重连且不返回 → 采集停摆，断网期只攒 1 条
 *   W9：重连改为"每轮最多试一次"，失败就记下时间下轮再说 → 入队永远 1 秒 1 条不中断
 *
 * 编译【Ubuntu】:
 *   arm-linux-gnueabihf-gcc -std=gnu99 -Wall -Wextra -g \
 *       -I/usr/local/arm/sqlite3/include \
 *       main_test.c thread_upload.c event_id.c frame.c net_reconnect.c db_upload.c \
 *       -L/usr/local/arm/sqlite3/lib -o test_upload_arm -lpthread -lsqlite3
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "common.h"
#include "event_id.h"
#include "frame.h"
#include "net_reconnect.h"
#include "db_upload.h"
#include "thread_upload.h"

extern DataPoint g_latest;
extern pthread_mutex_t g_latest_lock;
extern pthread_mutex_t g_db_lock;
extern sqlite3 *g_db;
extern volatile int g_running;

ssize_t write_full(int fd, const void *buf, size_t need);
ssize_t read_full(int fd, void *buf, size_t need);

/* 尝试连接一次（不循环、不阻塞）：成功返回 fd，失败返回 -1 */
static int try_connect_once(const char *ip, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof srv);
    srv.sin_family = AF_INET;
    srv.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &srv.sin_addr);

    if (connect(fd, (struct sockaddr *)&srv, sizeof srv) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void *thread_upload(void *arg)
{
    (void)arg;
    const char *ip = "192.168.1.20";
    int port = 9000;

    event_id_init();

    int fd = -1;              /* -1 = 当前无连接 */
    int delay_ms = 1000;      /* 重连退避：1s→2s→4s 封顶 */
    time_t last_try = 0;      /* 上次尝试连接的时刻 */
    int sent_count = 0;

    while (g_running) {
        /* ① 入队：断网与否都照常，1 秒 1 条（这是本版的核心） */
        DataPoint dp;
        pthread_mutex_lock(&g_latest_lock);
        dp = g_latest;
        pthread_mutex_unlock(&g_latest_lock);

        char event_id[64];
        event_id_next(event_id, sizeof(event_id));

        char payload[256];
        snprintf(payload, sizeof(payload),
            "{\"device\":\"dev01\",\"event_id\":\"%s\",\"ts\":%ld,"
            "\"temp\":%.1f,\"hum\":%.1f,\"ax\":%.2f,\"alert\":null}",
            event_id, time(NULL), dp.temperature, dp.humidity, dp.accel_total);

        /* W9：入队失败必须可见（撞号/锁冲突都会返回非 0，绝不静默） */
        if (db_enqueue(g_db, event_id, payload) != 0)
            fprintf(stderr, "[up] enqueue FAIL event_id=%s\n", event_id);

        /* ② 取一批（锁内拷贝，锁外用） */
        UploadItem batch[50];
        int n = db_fetch_pending(g_db, batch, 50);

        /* ③ 没连接：按退避节奏每轮最多试连一次（不阻塞采集） */
        if (fd < 0 && n > 0) {
            time_t now = time(NULL);
            if (now - last_try >= delay_ms / 1000) {
                fd = try_connect_once(ip, port);
                if (fd >= 0) {
                    printf("[net] connected to %s:%d (fd=%d)\n", ip, port, fd);
                    fflush(stdout);
                    delay_ms = 1000;                  /* 连上重置退避 */
                } else {
                    fprintf(stderr, "[net] reconnect fail, sleep %dms\n", delay_ms);
                    if (delay_ms < 4000) delay_ms *= 2;
                    last_try = now;
                }
            }
        }

        /* ④ 有连接就发送（锁外，网络 IO） */
        if (fd >= 0) {
            for (int i = 0; i < n; i++) {
                if (write_full(fd, batch[i].payload, strlen(batch[i].payload)) < 0 ||
                    write_full(fd, "\n", 1) < 0) {
                    close(fd); fd = -1; last_try = time(NULL);
                    break;
                }

                /* 收 OK：select 3 秒超时，防僵尸连接把线程挂死 */
                fd_set rset;
                FD_ZERO(&rset);
                FD_SET(fd, &rset);
                struct timeval tv = {3, 0};
                int r = select(fd + 1, &rset, NULL, NULL, &tv);
                if (r <= 0) {
                    close(fd); fd = -1; last_try = time(NULL);
                    break;
                }

                char buf[128];
                int j = 0;
                while (j < (int)(sizeof(buf) - 1)) {
                    ssize_t rn = read_full(fd, &buf[j], 1);
                    if (rn <= 0) break;
                    if (buf[j] == '\n') { buf[j] = '\0'; break; }
                    j++;
                }
                if (j == 0) { close(fd); fd = -1; last_try = time(NULL); break; }

                if (strncmp(buf, "OK ", 3) == 0 && strstr(buf, batch[i].event_id) != NULL) {
                    db_mark_sent(g_db, batch[i].event_id);   /* 锁内 UPDATE */
                    sent_count++;
                } else {
                    close(fd); fd = -1; last_try = time(NULL);
                    break;
                }
            }
        }

        /* ⑤ 日志（每 10 秒） */
        static time_t last_log = 0;
        if (time(NULL) - last_log >= 10) {
            int pending = 0, sent = 0;
            if (db_queue_depth(g_db, &pending, &sent) == 0) {
                printf("[up] queue=%d dbsent=%d runsent=%d\n", pending, sent, sent_count);
                fflush(stdout);
            }
            last_log = time(NULL);
        }

        sleep(1);
    }

    if (fd >= 0) close(fd);
    return NULL;
}
