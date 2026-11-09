/* frame.c —— 帧编解码（W4 核心工具，逐行注释）
 * 作用：解决 TCP 字节流没有边界的问题（粘包/半包）
 * 格式：[4B 大端长度][payload]
 *
 * 编译【Ubuntu】: gcc -std=gnu99 -Wall -Wextra -g frame.c -o frame_test
 * （实际使用时 frame.c 作为模块，被其他程序 #include "frame.h"）
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include "frame.h"

/* ==================== read_full ==================== */
/* 作用：从 fd 读满 need 字节（read 可能"读不满"，所以要循环凑满）
 * 返回：满=need；对端关=已收数；错=-1
 */
ssize_t read_full(int fd, void *buf, size_t need)
{
    char *p = buf;
    size_t left = need;

    while (left > 0) {
        ssize_t n = read(fd, p, left);
        if (n == 0) {
            /* 对端关闭（FIN），返回已收数 */
            return need - left;
        }
        if (n < 0) {
            if (errno == EINTR) {
                /* 被信号打断，不是错，重试 */
                continue;
            }
            /* 真错了 */
            return -1;
        }
        /* 读到了 n 字节，继续凑剩下的 */
        p += n;
        left -= n;
    }
    return need;   /* 收满 */
}

/* ==================== write_full ==================== */
/* 作用：向 fd 写满 need 字节（write 也可能"写不满"，所以要循环凑满）
 * 返回：满=need；错=-1
 */
ssize_t write_full(int fd, const void *buf, size_t need)
{
    const char *p = buf;
    size_t left = need;

    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                /* 被信号打断，重试 */
                continue;
            }
            /* 写失败或对端断开 */
            return -1;
        }
        /* 写出了 n 字节，继续写剩下的 */
        p += n;
        left -= n;
    }
    return need;   /* 写满 */
}

/* ==================== send_frame ==================== */
/* 作用：发送一帧 = [4B 大端长度][payload]
 * 参数：fd=连接，payload=数据，len=数据长度
 * 返回：0=成功；-1=失败
 */
int send_frame(int fd, const void *payload, uint32_t len)
{
    /* ① 长度转网络序（大端） */
    uint32_t be = htonl(len);

    /* ② 先写 4 字节长度 */
    if (write_full(fd, &be, 4) < 0) {
        return -1;
    }

    /* ③ 再写 payload */
    if (write_full(fd, payload, len) < 0) {
        return -1;
    }

    return 0;
}

/* ==================== recv_frame ==================== */
/* 作用：接收一帧 = [4B 大端长度][payload]
 * 参数：fd=连接，buf=接收缓冲，cap=缓冲大小，out_len=实际收到的长度
 * 返回：0=成功；-1=对端关/错；-2=非法长度
 */
int recv_frame(int fd, char *buf, uint32_t cap, uint32_t *out_len)
{
    uint32_t be;

    /* ① 先读 4 字节长度 */
    if (read_full(fd, &be, 4) != 4) {
        return -1;   /* 对端关/错 */
    }

    /* ② 长度转回主机序 */
    uint32_t len = ntohl(be);

    /* ③ 校验长度合法性（防恶意/损坏帧） */
    if (len == 0 || len > cap) {
        return -2;   /* 非法长度，断开 */
    }

    /* ④ 再读 payload */
    if (read_full(fd, buf, len) != (ssize_t)len) {
        return -1;
    }

    *out_len = len;
    return 0;
}
