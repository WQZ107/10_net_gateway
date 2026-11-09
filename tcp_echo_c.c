/* tcp_echo_c.c —— TCP 客户端（W1 教学样板，逐行注释）
 * 编译【Ubuntu】: gcc -std=gnu99 -Wall -Wextra -g tcp_echo_c.c -o tcp_echo_c
 * 运行【Ubuntu】: ./tcp_echo_c [IP] [端口]    默认 127.0.0.1 8888
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char *argv[])
{
    const char *ip = (argc > 1) ? argv[1] : "127.0.0.1";
    int port       = (argc > 2) ? atoi(argv[2]) : 8888;

    /* ① 装电话机（和服务端第一步一模一样） */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    /* ② 填"对方"的地址：客户端不用 INADDR_ANY，要填对方的 IP
     *    inet_pton 把字符串 "127.0.0.1" 转成二进制，返回 1 才算成功 */
    struct sockaddr_in srv;
    memset(&srv, 0, sizeof srv);
    srv.sin_family = AF_INET;
    srv.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &srv.sin_addr) != 1) {
        fprintf(stderr, "bad ip: %s\n", ip); return 1;
    }

    /* ③ 拨号：connect 成功返回 0=三次握手完成；失败立刻返回 -1
     *    对方没监听 -> ECONNREFUSED（Connection refused）
     *    对方不可达 -> ETIMEDOUT / ENETUNREACH */
    if (connect(fd, (struct sockaddr *)&srv, sizeof srv) < 0) {
        perror("connect"); return 1;
    }
    printf("[cli] connected %s:%d，输入内容回车发送（Ctrl+D 退出）\n", ip, port);

    /* ④ 通话：从键盘一行一行读，发出去，再把回显打出来 */
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        if (write(fd, line, strlen(line)) < 0) { perror("write"); break; }
        char buf[512];
        ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) { printf("\n[cli] server closed\n"); break; }  /* 0=对端关，-1=错误 */
        fwrite(buf, 1, n, stdout);
    }
    close(fd);
    return 0;
}
