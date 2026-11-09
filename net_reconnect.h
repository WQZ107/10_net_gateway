/* net_reconnect.h —— 断线重连与 SIGPIPE 处理头文件（W5）
 * 作用：声明 init_sigpipe/connect_with_backoff/safe_write/safe_read
 * 用法：#include "net_reconnect.h"
 */
#ifndef NET_RECONNECT_H
#define NET_RECONNECT_H

#include <sys/types.h>
#include <stdint.h>

/* 忽略 SIGPIPE 信号（程序开头调用一次）
 * 效果：write 到已断连接时返回 -1 + EPIPE，而不是进程被杀
 */
void init_sigpipe(void);

/* 连接失败后指数退避重试（1s→2s→4s 封顶），连上后返回 fd
 * 返回：成功=新 fd；失败=不会返回（一直重试）
 * 注意：连上后 delay 重置由外层管理（不在这里）
 */
int connect_with_backoff(const char *ip, uint16_t port);

/* 安全写（处理 SIGPIPE 和 EPIPE）
 * 返回：成功=写入字节数；失败=-1（EPIPE 时调用方应进重连）
 */
ssize_t safe_write(int fd, const void *buf, size_t len);

/* 安全读（处理 EINTR）
 * 返回：>0=收到 n 字节；0=对端关；-1=错误
 */
ssize_t safe_read(int fd, void *buf, size_t len);

#endif /* NET_RECONNECT_H */
