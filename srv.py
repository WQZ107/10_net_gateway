#!/usr/bin/env python3
"""
srv.py —— Python TCP 服务端（W6，逐行注释）
作用：accept → 按行收 JSON → 按 event_id 去重 → 落 SQLite

运行【Ubuntu】: python3 srv.py
测试【Ubuntu】: nc 127.0.0.1 9000
"""
import json
import socket
import threading
import sqlite3
import time

# ① 建数据库（文件：server.db）
db = sqlite3.connect("server.db", check_same_thread=False)
db.execute("""CREATE TABLE IF NOT EXISTS events(
    event_id TEXT PRIMARY KEY,   -- 幂等键：唯一约束，重复 INSERT 会被忽略
    device TEXT,
    ts INTEGER,
    temp REAL,
    hum REAL,
    ax REAL,
    alert TEXT,
    recv_at REAL                 -- 服务端接收时间
)""")
db.commit()

# ② 锁（多线程写库要加锁）
lock = threading.Lock()


def handle(c, addr):
    """处理一个客户端连接"""
    print("[srv] in", addr, flush=True)
    f = c.makefile("r")   # 按行读

    for line in f:
        line = line.strip()
        if not line:
            continue

        # ③ 解析 JSON
        try:
            m = json.loads(line)
        except ValueError:
            print("[srv] bad json:", line[:60], flush=True)
            continue

        # ④ 入库（INSERT OR IGNORE：event_id 重复则忽略）
        with lock:
            cur = db.execute(
                "INSERT OR IGNORE INTO events VALUES(?,?,?,?,?,?,?,?)",
                (m.get("event_id"), m.get("device"), m.get("ts"),
                 m.get("temp"), m.get("hum"), m.get("ax"),
                 m.get("alert"), time.time())
            )
            db.commit()
            dup = " (dup)" if cur.rowcount == 0 else ""

        print("[srv] +1", m.get("event_id"), "temp=", m.get("temp"), dup, flush=True)

        # ⑤ 回 OK（客户端靠它判"发成功"）
        c.sendall(("OK " + str(m.get("event_id")) + "\n").encode())

    print("[srv] out", addr, flush=True)
    c.close()


# ⑥ 主循环：accept → 起线程处理
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", 9000))
s.listen(16)
print("[srv] listening :9000", flush=True)

while True:
    c, addr = s.accept()
    threading.Thread(target=handle, args=(c, addr), daemon=True).start()
