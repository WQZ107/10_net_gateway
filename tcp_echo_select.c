/* tcp_echo_select.c —— select 版 TCP echo 服务端（W2 教学样板，逐行注释）
 * 编译【Ubuntu】: gcc -std=gnu99 -Wall -Wextra -g tcp_echo_select.c -o tcp_echo_select
 * 运行【Ubuntu】: ./tcp_echo_select [端口]    默认 8888
 * 测试【Ubuntu】: 开 3 个终端各跑  nc 127.0.0.1 8888
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
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

    printf("[srv] listening %d, waiting for clients...\n", port);
    fflush(stdout);

    /* ③ clients 数组管客户 fd（0=空位，初始全空） */
    int clients[MAX_CLIENTS] = {0};

    for (;;) {
        /* ④ 每轮重建 fd_set（W2 新增，坑1：select 会改写它） */
        fd_set rset;
        FD_ZERO(&rset);          /* 清空 */
        FD_SET(lfd, &rset);      /* 挂上监听 fd */
        int maxfd = lfd;         /* 初始最大 fd = lfd */

        /* 遍历所有已连客户，挂上去，顺手算 maxfd */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] > 0) {
                FD_SET(clients[i], &rset);
                if (clients[i] > maxfd) maxfd = clients[i];
            }
        }

        /* ⑤ 等：谁有事叫谁（阻塞在这里，直到有 fd 就绪） */
        /* 第一个参数 = maxfd+1（坑2：不是 fd 数量，是最大编号+1） */
        int nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nready < 0) {
            if (errno == EINTR) continue;   /* 被信号打断，重试 */
            perror("select"); return 1;
        }

        /* ⑥ 如果 lfd 就绪 = 有新客户来敲门 */
        if (FD_ISSET(lfd, &rset)) {
            int cfd = accept(lfd, NULL, NULL);
            if (cfd < 0) { perror("accept"); continue; }

            /* 找空位放进去 */
            int i;
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i] == 0) { clients[i] = cfd; break; }
            }
            if (i == MAX_CLIENTS) {
                /* 满了，直接拒绝（练习用，生产要优雅处理） */
                printf("[srv] too many clients, reject fd=%d\n", cfd);
                close(cfd);
            } else {
                printf("[srv] client in fd=%d (slot=%d)\n", cfd, i);
                fflush(stdout);
            }
        }

        /* ⑦ 遍历所有客户，谁有数据就处理（W1 的 read/write 逻辑） */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int fd = clients[i];
            if (fd <= 0) continue;          /* 空位跳过 */
            if (!FD_ISSET(fd, &rset)) continue;   /* 这个客户没数据，跳过 */

            char buf[512];
            ssize_t n = read(fd, buf, sizeof buf);
            if (n > 0) {
                write(fd, buf, n);          /* 收多少回多少 */
            } else if (n == 0) {
                printf("[srv] client out fd=%d (slot=%d)\n", fd, i);
                fflush(stdout);
                close(fd);                  /* 关掉 */
                clients[i] = 0;             /* 还回空位 */
            } else {
                if (errno == EINTR) continue;
                perror("read");
                close(fd);
                clients[i] = 0;
            }
        }
    }
    return 0;
}
