/* tcp_echo_epoll.c —— epoll LT 版 TCP echo 服务端（W3 教学样板，逐行注释）
 * 编译【Ubuntu】: gcc -std=gnu99 -Wall -Wextra -g tcp_echo_epoll.c -o tcp_echo_epoll
 * 运行【Ubuntu】: ./tcp_echo_epoll [端口]    默认 8888
 * 测试【Ubuntu】: 开 3 个终端各跑  nc 127.0.0.1 8888
 *
 * 与 select/poll 版的对比：
 *   select/poll: 每轮全量拷贝进内核 + O(n) 遍历找就绪
 *   epoll:       注册一次常驻内核 + epoll_wait 只返回就绪的 O(就绪数)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "frame.h"

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

    printf("[srv] listening %d (epoll LT version)\n", port);
    fflush(stdout);

    /* ③ 建 epoll 实例（ep 本身也是一个 fd） */
    int ep = epoll_create1(0);
    if (ep < 0) { perror("epoll_create1"); return 1; }

    /* ④ 把 lfd 注册进内核（一次，不是每轮！） */
    struct epoll_event ev;
    ev.events = EPOLLIN;          /* 关心"可读"事件（LT 模式，不加 EPOLLET） */
    ev.data.fd = lfd;             /* 自定义数据：这里塞 fd 本身 */
    if (epoll_ctl(ep, EPOLL_CTL_ADD, lfd, &ev) < 0) {
        perror("epoll_ctl"); return 1;
    }

    struct epoll_event evs[MAX_CLIENTS + 1];

    for (;;) {
        /* ⑤ 等：只返回就绪的 fd（填进 evs[] 数组） */
        int nready = epoll_wait(ep, evs, MAX_CLIENTS + 1, -1);
        if (nready < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait"); return 1;
        }

        /* ⑥ 遍历就绪的 fd（注意：evs[] 里只放就绪的，不用遍历全部） */
        for (int i = 0; i < nready; i++) {
            if (evs[i].data.fd == lfd) {
                /* 新客户来敲门 */
                int cfd = accept(lfd, NULL, NULL);
                if (cfd < 0) { perror("accept"); continue; }

                /* 把新客户也注册进内核（一次） */
                struct epoll_event cev;
                cev.events = EPOLLIN;
                cev.data.fd = cfd;
                if (epoll_ctl(ep, EPOLL_CTL_ADD, cfd, &cev) < 0) {
                    perror("epoll_ctl add client"); close(cfd); continue;
                }
                printf("[srv] client in fd=%d\n", cfd);
                fflush(stdout);
            } else {
                /* 业务：客户有数据来了 */
                int fd = evs[i].data.fd;
                char buf[512];
				uint32_t len;
                int ret = recv_frame(fd, buf, sizeof buf, &len);
                if (ret ==  0) {
					//收到一帧
					printf("[frame] len=%u %.*s\n", len, (int)len, buf);
					fflush(stdout);
                    send_frame(fd, buf, len);  	//回一帧
                } else if (ret == -1) {
				//对端关/错
                    printf("[srv] client out fd=%d\n", fd);
                    fflush(stdout);
                    /* close 后自动从 epoll 摘除（内核的红黑树） */
                    close(fd);
                } else {
				//ret == -2,非法长度，断开
                    printf("[srv] bad frame fd=%d\n", fd);
                    fflush(stdout);
                    close(fd);
                }
            }
        }
    }
    return 0;
}
