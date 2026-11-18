# 车载数据网关 · 可靠上报与断网补传

> 把「本地记录仪」升级为「能上云的车联网终端」——设备端 TCP 上报、服务端幂等落库、断网不丢不重。
> 嵌入式 Linux 应用层网络编程实战（2026-10-25 ~ 2026-11-09）。

[![Language](https://img.shields.io/badge/language-C-blue)](#)
[![Protocol](https://img.shields.io/badge/protocol-TCP%20%2B%20JSON-green)](#)
[![Reliability](https://img.shields.io/badge/delivery-exactly--once-brightgreen)](#)
[![Toolchain](https://img.shields.io/badge/toolchain-Linaro%204.9.4-orange)](#)

## 概览

这是毕业设计系统的**云端上报链路**。核心问题：车载终端在弱网/断网环境下，如何保证采集的数据既能上报、又不丢、又不重复？

答案是两个机制的组合：**至少一次投递**（本地队列 + 失败重发）+ **服务端幂等去重**（event_id 主键 + `INSERT OR IGNORE`）。断网期数据在本地 SQLite 队列持续累积，恢复后按批补传；服务端用幂等键拦住重发产生的重复。

实测：断网期队列峰值 128 条，恢复后按批补传，连续 8 代数据按代对账**全部闭合，重复 0、丢失 0**。

## 特性

- **TCP 自定义帧协议**：4 字节长度前缀 + `read_full/write_full`，解决粘包/半包
- **I/O 多路复用对比**：select / poll / epoll(LT) 三版服务端，3 客户端并发验证
- **可靠上报**：`upload_queue` 本地队列（status/retries/event_id），收到服务端 ACK 才置 1，失败留 0 反复重发
- **幂等去重**：`event_id = dev01-<boot_ts>-<seq>`，服务端以其为主键 `INSERT OR IGNORE`，自然去重
- **断线重连**：指数退避 1s→2s→4s 封顶 + 心跳保活；重连期间采集与入队不中断
- **拒绝长锁**：锁内只做微秒级 DB 操作，网络 IO 全部在锁外，避免阻塞采集线程
- **时钟回退兜底**：boot_ts 落盘单调递增（`max(now, last+1)`），防止 RTC 回退导致 event_id 撞号

## 架构

```
设备端（I.MX6U）                              服务端（VM Ubuntu）
┌────────────────────────────┐        ┌──────────────────────────┐
│ ① 取共享区最新数据           │        │ srv.py                   │
│ ② event_id = boot_ts + seq │        │  accept → 按行收 JSON     │
│ ③ 拼 JSON                   │  TCP   │           ↓              │
│ ④ 入队 upload_queue(SQLite) │ 9000   │  INSERT OR IGNORE         │
│ ⑤ 锁外 send → 收 "OK"       │ ←────→ │  (event_id 主键幂等)       │
│ ⑥ 收到 OK 置 status=1       │  行JSON│           ↓              │
│   失败留 0，下轮重发         │        │  server.db / events 表    │
└────────────────────────────┘        └──────────────────────────┘

断网期：队列持续 +1/s（不阻塞采集）
恢复后：50 条/批批量补传；重连退避 1s→2s→4s 封顶
```

通信协议详见 [docs/protocol.md](docs/protocol.md)，架构图见 [docs/arch.png](docs/arch.png)。

## 行 JSON 协议

```json
{"device":"dev01","event_id":"dev01-1794229160-000123","ts":1794229283,"temp":25.5,"hum":60.1,"ax":0.98,"alert":null}
```

服务端回 `OK <event_id>` 作为 ACK。选行 JSON 而非二进制，是为了 `nc` 手工验证友好。

## 快速开始

**服务端（VM）**

```bash
python3 srv.py                      # 监听 :9000，落盘 server.db
sqlite3 server.db 'SELECT COUNT(*) FROM events;'
```

**设备端（交叉编译 + 上板）**

```bash
arm-linux-gnueabihf-gcc -std=gnu99 -Wall -Wextra -g \
    -I/usr/local/arm/sqlite3/include \
    main_test.c thread_upload.c event_id.c frame.c net_reconnect.c db_upload.c \
    -L/usr/local/arm/sqlite3/lib -o test_upload_arm -lpthread -lsqlite3
scp -O test_upload_arm root@192.168.1.100:/home/root/
# 板上
./test_upload_arm
```

**断网实验**

```bash
# 杀服务端 → 观察板上队列增长：[up] queue=N 每秒 +1
# 恢复服务端 → 批量补传：[srv] +1 一秒几十条
```

## 实测数据

| 场景 | 结果 |
|---|---|
| 长断网 | 队列峰值 128 条，恢复后按批补传 |
| 按代对账 | 连续 8 代全部闭合，重复 0、丢失 0 |
| 重连 | 指数退避 1s→2s→4s，恢复后自动续传 |
| 时钟回退 | boot_ts 落盘单调化，无 event_id 撞号 |

## 目录结构

```
├── docs/                  # protocol.md（协议表）、arch.png/drawio（架构图）
├── srv.py                 # Python 服务端（按行收 JSON + 幂等落库）
├── thread_upload.c/.h     # 上报线程：入队/发送/收OK/断网补传
├── event_id.c/.h          # event_id 发生器（boot_ts 单调递增）
├── db_upload.c/.h         # upload_queue 的 SQLite 操作（锁内）
├── frame.c/.h             # 4B 长度前缀帧 + read_full/write_full
├── net_reconnect.c/.h     # SIGPIPE / 退避重连 / safe_read-write
├── main_test.c            # 最小测试版入口
└── tcp_echo_*.c           # W1~W3：阻塞/select/poll/epoll 各版本
```

## 已知边界

- MQTT 通道（QoS1 + LWT 在线检测）为下一阶段，尚未纳入本仓库
- 设备 RTC 无电池，时钟会回退——event_id 已用 boot_ts 落盘单调化兜底，日志时间戳仍以对时为准

## 相关仓库

- 数据来源：[08_linux_sensor_framework](https://github.com/WQZ107/08_linux_sensor_framework)
