/* db_upload_file.h —— upload_queue 文件版头文件（W8）
 */
#ifndef DB_UPLOAD_FILE_H
#define DB_UPLOAD_FILE_H

#include <time.h>

typedef struct {
    char event_id[64];
    char payload[256];
    int status;   /* 0=待发 1=已发 */
} UploadItem;

int db_init_upload_queue(void);
int db_enqueue(const char *event_id, const char *payload);
int db_fetch_pending(UploadItem *batch, int max);
int db_mark_sent(const char *event_id);
int db_queue_depth(int *pending, int *sent);

#endif
