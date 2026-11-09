/* net_reconnect.c —— 断线重连与 SIGPIPE 处理（W5 核心工具，逐行注释）
 * 作用：对端崩溃/断开时程序不崩、自动重连
 *
 * 编译【Ubuntu】: gcc -std=gnu99 -Wall -Wextra -g net_reconnect.c -o net_reconnect_test
 * （实际使用时作为模块，被其他程序 #include "net_reconnect.h"）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "net_reconnect.h"

/* ==================== init_sigpipe ==================== */
/* 作用：忽略 SIGPIPE 信号（对端断开时 write 不再触发 SIGPIPE 杀进程）
 * 时机：程序开头调用一次
 * 效果：write 返回 -1 + errno=EPIPE，而不是进程被杀
 */
void init_sigpipe(void)
{
    signal(SIGPIPE, SIG_IGN);   /* 忽略 SIGPIPE */
    printf("[net] SIGPIPE ignored\n");
}

/* ==================== connect_with_backoff ==================== */
/* 作用：连接失败后指数退避重试（1s→2s→4s 封顶），连上后返回 fd
 * 参数：ip=对方 IP，port=对方端口
 * 返回：成功=新 fd；失败=不会返回（一直重试，直到连上或被信号打断）
 *
 * 关键点：
 *   1. 每次重新 socket()（失败的 fd 不能复用）
 *   2. delay 指数增长（1s→2s→4s 封顶）
 *   3. 连上后 delay 重置由外层管理（不在这里）
 */
int connect_with_backoff(const char *ip, uint16_t port)
{
    int delay_ms = 1000;   /* 初值 1s */

    for (;;) {
        /* ① 每次重新 socket()（失败的 fd 不能复用） */
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            perror("socket");
            sleep(1);   /* socket 都失败了，等 1s 再试 */
            continue;
        }

        /* ② 填对方地址 */
        struct sockaddr_in srv;
        memset(&srv, 0, sizeof srv);
        srv.sin_family = AF_INET;
        srv.sin_port   = htons(port);
        inet_pton(AF_INET, ip, &srv.sin_addr);

        /* ③ 连接 */
        if (connect(fd, (struct sockaddr *)&srv, sizeof srv) == 0) {
            /* 连上，返回 fd（delay 由外层重置） */
            printf("[net] connected to %s:%d (fd=%d)\n", ip, port, fd);
            return fd;
        }

        /* ④ 连接失败，关闭 fd，进入退避等待 */
        close(fd);
        fprintf(stderr, "[net] reconnect fail, sleep %dms\n", delay_ms);
        usleep(delay_ms * 1000);

        /* ⑤ 指数退避（封顶 4s） */
        if (delay_ms < 4000) {
            delay_ms *= 2;
        }
    }
}

/* ==================== safe_write ==================== */
/* 作用：安全写（处理 SIGPIPE 和 EPIPE）
 * 参数：fd=连接，buf=数据，len=长度
 * 返回：成功=写入字节数；失败=-1（EPIPE 时调用方应进重连）
 */
ssize_t safe_write(int fd, const void *buf, size_t len)
{
    ssize_t n = write(fd, buf, len);
    if (n < 0) {
        if (errno == EPIPE) {
            /* 对端断开，不是致命错误 */
            fprintf(stderr, "[net] write EPIPE (peer closed)\n");
        } else if (errno == EINTR) {
            /* 被信号打断，重试 */
            return safe_write(fd, buf, len);
        } else {
            perror("write");
        }
    }
    return n;
}

/* ==================== safe_read ==================== */
/* 作用：安全读（处理 EINTR）
 * 参数：fd=连接，buf=缓冲，len=长度
 * 返回：>0=收到 n 字节；0=对端关；-1=错误
 */
ssize_t safe_read(int fd, void *buf, size_t len)
{
    ssize_t n = read(fd, buf, len);
    if (n < 0) {
        if (errno == EINTR) {
            /* 被信号打断，重试 */
            return safe_read(fd, buf, len);
        }
        perror("read");
    }
    return n;
}
