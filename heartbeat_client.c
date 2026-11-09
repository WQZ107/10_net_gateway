/* heartbeat_client.c —— 带心跳的 TCP 客户端（W5 演示，逐行注释）
 * 作用：每 30s 发一帧心跳，演示应用层心跳
 *
 * 编译【Ubuntu】: gcc -std=gnu99 -Wall -Wextra -g heartbeat_client.c frame.c net_reconnect.c -o heartbeat_client
 * 运行【Ubuntu】: ./heartbeat_client [IP] [端口]    默认 127.0.0.1 8888
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include "frame.h"
#include "net_reconnect.h"

#define HEARTBEAT_INTERVAL 30   /* 心跳间隔：30 秒 */

int main(int argc, char *argv[])
{
    const char *ip = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? atoi(argv[2]) : 8888;

    /* ① 忽略 SIGPIPE（W5 核心） */
    init_sigpipe();

    /* ② 连接（带指数退避重连） */
    int fd = connect_with_backoff(ip, port);

    time_t last_ping = time(NULL);

    for (;;) {
        /* ③ 检查是否该发心跳 */
        time_t now = time(NULL);
        if (now - last_ping >= HEARTBEAT_INTERVAL) {
            char ping[64];
            snprintf(ping, sizeof(ping), "{\"ping\":%ld}", (long)now);
            if (send_frame(fd, ping, strlen(ping)) < 0) {
                /* 发送失败（EPIPE 等），进重连 */
                fprintf(stderr, "[net] send ping fail, reconnecting...\n");
                close(fd);
                fd = connect_with_backoff(ip, port);
            } else {
                printf("[cli] ping sent: %s\n", ping);
            }
            last_ping = now;
        }

        /* ④ 尝试收服务端回显（非阻塞或短超时，这里简化为阻塞） */
        char buf[512];
        uint32_t len;
        if (recv_frame(fd, buf, sizeof buf, &len) == 0) {
            printf("[cli] recv: %.*s\n", (int)len, buf);
        } else {
            /* 收失败（对端关/错），进重连 */
            fprintf(stderr, "[net] recv fail, reconnecting...\n");
            close(fd);
            fd = connect_with_backoff(ip, port);
        }
    }

    close(fd);
    return 0;
}
