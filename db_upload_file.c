/* db_upload_file.c —— upload_queue 文件版（W8 跳过 SQLite，逐行注释）
 * 作用：用文件代替 SQLite，验证断网补传逻辑
 *
 * 编译【Ubuntu】:
 *   arm-linux-gnueabihf-gcc -std=gnu99 -Wall -Wextra -g \
 *       main_test.c thread_upload.c event_id.c frame.c net_reconnect.c db_upload_file.c \
 *       -o test_upload_arm -lpthread
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "db_upload_file.h"

extern pthread_mutex_t g_db_lock;

static FILE *fp = NULL;
static UploadItem queue[1000];
static int queue_size = 0;

int db_init_upload_queue(void)
{
    fp = fopen("/home/root/upload_queue.txt", "a+");
    if (!fp) {
        fprintf(stderr, "fopen fail\n");
        return -1;
    }
    return 0;
}

int db_enqueue(const char *event_id, const char *payload)
{
    pthread_mutex_lock(&g_db_lock);
    if (queue_size >= 1000) {
        pthread_mutex_unlock(&g_db_lock);
        return -1;
    }
    strncpy(queue[queue_size].event_id, event_id, sizeof(queue[queue_size].event_id) - 1);
    strncpy(queue[queue_size].payload, payload, sizeof(queue[queue_size].payload) - 1);
    queue[queue_size].status = 0;
    queue_size++;
    if (fp) fprintf(fp, "%s\n", payload);
    pthread_mutex_unlock(&g_db_lock);
    return 0;
}

int db_fetch_pending(UploadItem *batch, int max)
{
    pthread_mutex_lock(&g_db_lock);
    int n = 0;
    for (int i = 0; i < queue_size && n < max; i++) {
        if (queue[i].status == 0) {
            batch[n] = queue[i];
            n++;
        }
    }
    pthread_mutex_unlock(&g_db_lock);
    return n;
}

int db_mark_sent(const char *event_id)
{
    pthread_mutex_lock(&g_db_lock);
    for (int i = 0; i < queue_size; i++) {
        if (strcmp(queue[i].event_id, event_id) == 0) {
            queue[i].status = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_db_lock);
    return 0;
}

int db_queue_depth(int *pending, int *sent)
{
    pthread_mutex_lock(&g_db_lock);
    *pending = 0;
    *sent = 0;
    for (int i = 0; i < queue_size; i++) {
        if (queue[i].status == 0) (*pending)++;
        else (*sent)++;
    }
    pthread_mutex_unlock(&g_db_lock);
    return 0;
}
