/* tcp_echo_poll.c —— poll 版 TCP echo 服务端（W3 教学样板，逐行注释）
 * 编译【Ubuntu】: gcc -std=gnu99 -Wall -Wextra -g tcp_echo_poll.c -o tcp_echo_poll
 * 运行【Ubuntu】: ./tcp_echo_poll [端口]    默认 8888
 * 测试【Ubuntu】: 开 3 个终端各跑  nc 127.0.0.1 8888
 *
 * 与 select 版的对比：
 *   select: fd_set 位图 + FD_ZERO/FD_SET/FD_ISSET
 *   poll:   struct pollfd 数组 + events/revents
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_CLIENTS 64

int main(int argc, char *argv[])
{
    int port = (argc > 1) ? atoi(argv[1]) : 8888;

    /* ① 装电话机 + 端口复用（和 W1 一样） */
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) { perror("socket"); return 1; }
    int on = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);

    /* ② 绑号码 + 挂起等人打（和 W1 一样） */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind"); return 1;
    }
    if (listen(lfd, 5) < 0) { perror("listen"); return 1; }

    printf("[srv] listening %d (poll version)\n", port);
    fflush(stdout);

    /* ③ clients 数组管客户 fd（和 select 版一样） */
    int clients[MAX_CLIENTS] = {0};

    /* ④ pollfd 数组：[0]=lfd，[1..MAX_CLIENTS]=clients */
    struct pollfd pfd[MAX_CLIENTS + 1];

    for (;;) {
        /* ⑤ 每轮重建 pfd（poll 也会改写 revents，必须重建） */
        pfd[0].fd = lfd;
        pfd[0].events = POLLIN;      /* 关心"可读"事件 */
        pfd[0].revents = 0;
        int nfds = 1;                /* 数组长度：1(lfd) + 已连客户数 */

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] > 0) {
                pfd[nfds].fd = clients[i];
                pfd[nfds].events = POLLIN;
                pfd[nfds].revents = 0;
                nfds++;
            }
        }

        /* ⑥ 等：谁有事叫谁（和 select 一样，但参数是"数组长度"不是 maxfd+1） */
        int nready = poll(pfd, nfds, -1);
        if (nready < 0) {
            if (errno == EINTR) continue;
            perror("poll"); return 1;
        }

        /* ⑦ 如果 pfd[0] 就绪 = 有新客户来敲门 */
        if (pfd[0].revents & POLLIN) {
            int cfd = accept(lfd, NULL, NULL);
            if (cfd < 0) { perror("accept"); continue; }

            /* 找空位放进去 */
            int i;
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i] == 0) { clients[i] = cfd; break; }
            }
            if (i == MAX_CLIENTS) {
                printf("[srv] too many clients, reject fd=%d\n", cfd);
                close(cfd);
            } else {
                printf("[srv] client in fd=%d (slot=%d)\n", cfd, i);
                fflush(stdout);
            }
        }

        /* ⑧ 遍历业务（注意：从 pfd[1] 开始，因为 pfd[0] 是 lfd） */
        for (int i = 1; i < nfds; i++) {
            if (pfd[i].revents & POLLIN) {
                int fd = pfd[i].fd;
                char buf[512];
                ssize_t n = read(fd, buf, sizeof buf);
                if (n > 0) {
                    write(fd, buf, n);          /* 收多少回多少 */
                } else if (n == 0) {
                    printf("[srv] client out fd=%d\n", fd);
                    fflush(stdout);
                    close(fd);
                    /* 从 clients 里摘除（防泄漏） */
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (clients[j] == fd) { clients[j] = 0; break; }
                    }
                } else {
                    if (errno == EINTR) continue;
                    perror("read");
                    close(fd);
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (clients[j] == fd) { clients[j] = 0; break; }
                    }
                }
            }
        }
    }
    return 0;
}
