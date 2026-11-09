/* db_upload.h —— upload_queue 头文件（W8）
 * 作用：声明 db_init_upload_queue/db_enqueue/db_fetch_pending/db_mark_sent/db_queue_depth
 */
#ifndef DB_UPLOAD_H
#define DB_UPLOAD_H

#include <sqlite3.h>
#include <time.h>

/* 上传队列项 */
typedef struct {
    char event_id[64];
    char payload[256];
} UploadItem;

/* 初始化 upload_queue 表 */
int db_init_upload_queue(sqlite3 *db);

/* ① INSERT 进队列（锁内） */
int db_enqueue(sqlite3 *db, const char *event_id, const char *payload);

/* ② 取一批（锁内拷贝，锁外用） */
int db_fetch_pending(sqlite3 *db, UploadItem *batch, int max);

/* ③ 标记已发（锁内） */
int db_mark_sent(sqlite3 *db, const char *event_id);

/* 查询队列深度（调试用） */
int db_queue_depth(sqlite3 *db, int *pending, int *sent);

#endif /* DB_UPLOAD_H */
