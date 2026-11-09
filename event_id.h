#ifndef EVENT_ID_H
#define EVENT_ID_H

//初始化（main里调用一次）
void event_id_init(void);

//生成event_id（多线程安全）
void event_id_next(char *out, int cap);

#endif
