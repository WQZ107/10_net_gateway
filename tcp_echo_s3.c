#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	int port = (argc > 1) ? atoi(argv[1]) : 8888;

    /* ① 装电话机：AF_INET=IPv4，SOCK_STREAM=TCP，第三个参数填 0
     *    返回一个 fd（这里是 lfd=listen fd），失败返回 -1 */
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) { perror("socket"); return 1; }

    /* ② 端口复用：不加这句，进程刚退出就重启常报 EADDRINUSE
     *    （主动关闭方会留 TIME_WAIT 约 60 秒，端口被占着） */
    int on = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);

    /* ③ 绑号码：把 IP + 端口填进 sockaddr_in，再 bind 到 lfd */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);          /* 先清零，剩下的字段才是 0 不是垃圾 */
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);        /* 端口是 16 位，必须转网络序（大端） */
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  /* 0.0.0.0=本机任意网卡都能收 */
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind"); return 1;          /* 强转是因为 bind 只认通用 sockaddr */
    }

    /* ④ 挂起等人打：backlog=同时排队等 accept 的上限，不是最大连接数 */
    if (listen(lfd, 5) < 0) { perror("listen"); return 1; }
    printf("[srv] listening %d\n",port);
    fflush(stdout);                        /* 重定向到文件时行缓冲不刷，手动刷 */

    for (;;) {
        /* ⑤ 接电话：accept 返回的是"新 fd" cfd（和这个客户通话用）；
         *    lfd 原地不动，继续守门接下一个客户。后两个参数可以填 NULL */
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) { perror("accept"); continue; }
        printf("[srv] client in fd=%d\n", cfd);
        fflush(stdout);

        for (;;) {
            char buf[512];
            ssize_t n = read(cfd, buf, sizeof buf);
            if (n > 0)  { write(cfd, buf, n); continue; }        /* 收多少回多少（小数据够用；大块要 write_full，W4 学） */
            if (n == 0) { printf("[srv] client out\n"); break; } /* 0=对端正常关闭 */
            if (errno == EINTR) continue;                        /* 被信号打断，不是错，重试 */
            perror("read"); break;                               /* 真错了 */
        }
        close(cfd);            /* cfd 用完必须关，否则反复连断会 fd 泄漏 */
    }
    return 0;
}
