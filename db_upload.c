/* db_upload.c —— upload_queue 数据库操作（W8，逐行注释）
 * 作用：断网补传队列（INSERT/SELECT/UPDATE，锁内不做网络 IO）
 *
 * 编译【Ubuntu】: arm-linux-gnueabihf-gcc ... db_upload.c ...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <pthread.h>
#include "db_upload.h"

extern pthread_mutex_t g_db_lock;   /* 全局数据库锁（main.c 定义） */

/* 初始化 upload_queue 表 */
int db_init_upload_queue(sqlite3 *db)
{
    const char *sql = "CREATE TABLE IF NOT EXISTS upload_queue("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "event_id TEXT UNIQUE,"
        "payload TEXT,"
        "status INTEGER DEFAULT 0,"   /* 0=待发 1=已发 */
        "retries INTEGER DEFAULT 0,"
        "created_at INTEGER)";

    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "[db] init upload_queue fail: %s\n", err);
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

/* ① INSERT 进队列（锁内） */
int db_enqueue(sqlite3 *db, const char *event_id, const char *payload)
{
    pthread_mutex_lock(&g_db_lock);
    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR IGNORE INTO upload_queue(event_id, payload, status, retries, created_at) VALUES(?,?,0,0,?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&g_db_lock);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, event_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, payload, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, time(NULL));
    int ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_lock);
    return (ret == SQLITE_DONE) ? 0 : -1;
}

/* ② 取一批（锁内拷贝，锁外用） */
int db_fetch_pending(sqlite3 *db, UploadItem *batch, int max)
{
    pthread_mutex_lock(&g_db_lock);
    sqlite3_stmt *stmt;
    const char *sql = "SELECT event_id, payload FROM upload_queue WHERE status=0 ORDER BY id LIMIT ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&g_db_lock);
        return -1;
    }
    sqlite3_bind_int(stmt, 1, max);

    int n = 0;
    while (n < max && sqlite3_step(stmt) == SQLITE_ROW) {
        const char *eid = (const char *)sqlite3_column_text(stmt, 0);
        const char *pl  = (const char *)sqlite3_column_text(stmt, 1);
        if (eid) strncpy(batch[n].event_id, eid, sizeof(batch[n].event_id) - 1);
        if (pl)  strncpy(batch[n].payload,  pl,  sizeof(batch[n].payload)  - 1);
        n++;
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_lock);
    return n;
}

/* ③ 标记已发（锁内） */
int db_mark_sent(sqlite3 *db, const char *event_id)
{
    pthread_mutex_lock(&g_db_lock);
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE upload_queue SET status=1 WHERE event_id=?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&g_db_lock);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, event_id, -1, SQLITE_STATIC);
    int ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_lock);
    return (ret == SQLITE_DONE) ? 0 : -1;
}

/* 查询队列深度（调试用） */
int db_queue_depth(sqlite3 *db, int *pending, int *sent)
{
    *pending = 0;   /* 先赋初值：即使后面失败，调用方也不会打印垃圾值 */
    *sent = 0;
    pthread_mutex_lock(&g_db_lock);
    sqlite3_stmt *stmt;
    const char *sql = "SELECT status, COUNT(*) FROM upload_queue GROUP BY status";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&g_db_lock);
        return -1;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int status = sqlite3_column_int(stmt, 0);
        int cnt = sqlite3_column_int(stmt, 1);
        if (status == 0) *pending = cnt;
        else if (status == 1) *sent = cnt;
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_lock);
    return 0;
}
