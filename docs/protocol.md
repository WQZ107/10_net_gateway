# 通信协议（设备 → 服务端）

> 版本：v1.0-net（2026-11-09）
> 通道：TCP 9000，**行 JSON**（一行一条，`\n` 结尾）；调试友好，便于 `nc` 手工验证。

## 1. 上行帧（设备 → 服务端）

```
{"device":"dev01","event_id":"dev01-<boot_ts>-<seq6>","ts":<秒>,"temp":<float>,"hum":<float>,"ax":<float>,"alert":<string|null>}\n
```

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| device | string | 是 | 设备号，固定 `dev01` |
| event_id | string | 是 | 幂等键，见 §3 |
| ts | int | 是 | 采集时间（Unix 秒） |
| temp | real | 是 | 温度 ℃，保留 1 位 |
| hum | real | 是 | 湿度 %RH，保留 1 位 |
| ax | real | 是 | 合加速度 g，保留 2 位 |
| alert | string/null | 是 | 事件类型；无事件为 `null` |

示例：

```json
{"device":"dev01","event_id":"dev01-1794229160-000123","ts":1794229283,"temp":25.5,"hum":60.1,"ax":0.98,"alert":null}
```

## 2. 下行应答（服务端 → 设备）

```
OK <event_id>\n
```

- 服务端**入库成功**（含重复命中主键）后回；
- 设备必须校验 `event_id` 与本条一致，才把队列行置为已发送（`status=1`）；
- 应答缺失/不一致 = 发送失败，保持 `status=0` 下轮重发。

## 3. event_id 规则

```
event_id = "dev01-" + boot_ts + "-" + 6 位十进制序号（从 000000 起）
```

- `boot_ts`：**每次启动取一次**，且保证**严格递增**（落盘 `/home/root/.event_boot_ts`，取 `max(now, last+1)`），防设备 RTC 时钟回退造成撞号；
- `seq`：本次启动内单调递增，多线程加锁；
- 结论：`event_id` 跨重启唯一；重发永远原样携带，不重新生成。

## 4. 可靠性约定

| 约定 | 说明 |
|---|---|
| 至少一次 | 设备收 OK 才标记已发；超时/失败保持待发，下轮重发 |
| 幂等 | 服务端 `events.event_id` 为主键，`INSERT OR IGNORE`；重复仍回 OK |
| 队列 | 设备侧 `upload_queue(status: 0=待发 1=已发)`，断电重启后继续补传 |
| 锁边界 | 队列读写（微秒级）在锁内；网络收发（秒级）在锁外 |

## 5. 二进制帧（可选通道）

用于高吞吐/低开销场景：

```
[4 字节大端长度][payload]
```

- 长度 = payload 字节数，`htonl`/`ntohl` 转换；
- `len == 0 || len > 缓冲区容量` 视为协议错误，直接断开；
- 实现见 `frame.c` 的 `send_frame` / `recv_frame`。
