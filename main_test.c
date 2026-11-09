/* main_test.c —— W8 断网补传测试版（逐行注释）
 * 作用：模拟数据 + thread_upload，验证断网补传
 *
 * 编译【Ubuntu】:
 *   arm-linux-gnueabihf-gcc -std=gnu99 -Wall -Wextra -g \
 *       main_test.c thread_upload.c event_id.c frame.c net_reconnect.c db_upload.c \
 *       -o test_upload_arm -lpthread -lsqlite3
 * 运行【板子】: ./test_upload_arm
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sqlite3.h>
#include "common.h"
#include "event_id.h"
#include "db_upload.h"
#include "thread_upload.h"

/* ── 全局定义（thread_upload.c 需要）── */
DataPoint g_latest;
pthread_mutex_t g_latest_lock = PTHREAD_MUTEX_INITIALIZER;
volatile int g_running = 1;
AccelData g_accel = {0.0f, 0.0f, 0.0f, 0.0f};
pthread_mutex_t g_accel_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_gps_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_db_lock = PTHREAD_MUTEX_INITIALIZER;
sqlite3 *g_db;   /* 全局数据库句柄 */

void sigint_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

int main(void)
{
    signal(SIGINT, sigint_handler);
    event_id_init();

    /* 打开数据库 */
    if (sqlite3_open("/home/root/upload_queue.db", &g_db) != SQLITE_OK) {
        fprintf(stderr, "sqlite3_open fail\n");
        return -1;
    }
    if (db_init_upload_queue(g_db) != 0) {
        fprintf(stderr, "db_init_upload_queue fail\n");
        return -1;
    }

    printf("=== W8 断网补传测试版 ===\n");
    printf("按 Ctrl+C 退出\n\n");

    /* 初始值 */
    pthread_mutex_lock(&g_latest_lock);
    g_latest.temperature = 25.0f;
    g_latest.humidity    = 60.0f;
    g_latest.accel_total = 0.98f;
    pthread_mutex_unlock(&g_latest_lock);

    pthread_t tid;
    pthread_create(&tid, NULL, thread_upload, NULL);

    /* 模拟数据变化 */
    while (g_running) {
        sleep(1);
        pthread_mutex_lock(&g_latest_lock);
        g_latest.temperature += 0.5f;
        g_latest.humidity    += 0.1f;
        pthread_mutex_unlock(&g_latest_lock);
    }

    sqlite3_close(g_db);
    printf("程序已退出\n");
    return 0;
}
