/* event_id.c —— event_id 发生器（W9 修正版：boot_ts 单调递增，防时钟回退撞号）
 *
 * 问题背景（2026-11-09 实锤）：
 *   板子 RTC 无电池 → 每次断电时钟回退（实测慢了 17 天且原地打转）。
 *   event_id = dev01-<boot_ts>-<seq>，seq 每次启动从 0 重来；
 *   若两次启动 boot_ts 相同 → event_id 全部重复 → INSERT OR IGNORE 静默丢数据。
 *
 * 修法：把用过的 boot_ts 落盘到 /home/root/.event_boot_ts，
 *       启动时 boot_ts = max(now, last+1) → 即使时钟回退也严格递增。
 */
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include "event_id.h"

#define BOOT_TS_FILE "/home/root/.event_boot_ts"

static time_t boot_ts;
static unsigned seq = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static time_t load_last_boot_ts(void)
{
    time_t last = 0;
    FILE *f = fopen(BOOT_TS_FILE, "r");
    if (f) {
        if (fscanf(f, "%ld", (long *)&last) != 1) last = 0;
        fclose(f);
    }
    return last;
}

static void save_boot_ts(time_t ts)
{
    FILE *f = fopen(BOOT_TS_FILE, "w");
    if (f) {
        fprintf(f, "%ld\n", (long)ts);
        fclose(f);
    }
}

void event_id_init(void)
{
    time_t now  = time(NULL);
    time_t last = load_last_boot_ts();

    /* 核心：时钟可信就用时钟，时钟回退就用 last+1 —— 保证严格单调递增 */
    boot_ts = (now > last) ? now : last + 1;
    save_boot_ts(boot_ts);

    seq = 0;
    printf("[event_id] boot_ts=%ld (now=%ld, last=%ld)\n",
           (long)boot_ts, (long)now, (long)last);
    fflush(stdout);
}

void event_id_next(char *out, int cap)
{
    pthread_mutex_lock(&lock);
    snprintf(out, cap, "dev01-%ld-%06u", (long)boot_ts, seq++);
    pthread_mutex_unlock(&lock);
}
