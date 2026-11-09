/* frame.h —— 帧编解码头文件（W4）
 * 作用：声明 read_full/write_full/send_frame/recv_frame
 * 用法：#include "frame.h"
 */
#ifndef FRAME_H
#define FRAME_H

#include <sys/types.h>
#include <stdint.h>

/* 从 fd 读满 need 字节（循环凑满）
 * 返回：满=need；对端关=已收数；错=-1
 */
ssize_t read_full(int fd, void *buf, size_t need);

/* 向 fd 写满 need 字节（循环凑满）
 * 返回：满=need；错=-1
 */
ssize_t write_full(int fd, const void *buf, size_t need);

/* 发送一帧 = [4B 大端长度][payload]
 * 返回：0=成功；-1=失败
 */
int send_frame(int fd, const void *payload, uint32_t len);

/* 接收一帧 = [4B 大端长度][payload]
 * 返回：0=成功；-1=对端关/错；-2=非法长度
 */
int recv_frame(int fd, char *buf, uint32_t cap, uint32_t *out_len);

#endif /* FRAME_H */
