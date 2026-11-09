import socket, time, struct

s = socket.create_connection(("127.0.0.1", 8888))

# 第 1 帧：分两次发（半包）
msg = b'{"n":1}'
frame = struct.pack(">I", len(msg)) + msg
s.send(frame[:2]); time.sleep(0.3)   # 先发 2 字节
s.send(frame[2:]);    time.sleep(0.3)   # 补完 → 服务端应解出第 1 帧

# 第 2、3 帧：粘着发（粘包）
m2 = b'{"n":2}'
m3 = b'{"n":3}'
f2 = struct.pack(">I", len(m2)) + m2
f3 = struct.pack(">I", len(m3)) + m3
s.send(f2 + f3)   # 两条粘着发 → 服务端应解出 2 帧

time.sleep(1)
s.close()
