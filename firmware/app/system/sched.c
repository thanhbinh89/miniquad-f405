/**
 * @file    sched.c
 * @brief   Cooperative scheduler.
 */
#include "sched.h"

#include "systime.h"

static sched_task_t *s_tasks;
static uint32_t      s_count;
static uint32_t      s_busy_us;
static uint32_t      s_window_start;
static uint8_t       s_load;

void sched_init(sched_task_t *tasks, uint32_t count)
{
    s_tasks = tasks;
    s_count = count;
    uint32_t now = time_us();
    for (uint32_t i = 0u; i < count; i++) {
        tasks[i].next_us = now;
    }
    s_window_start = now;
}

void sched_run(void)
{
    for (uint32_t i = 0u; i < s_count; i++) {
        sched_task_t *t = &s_tasks[i];
        uint32_t now = time_us();
        if (t->period_us != 0u && (int32_t)(now - t->next_us) < 0) {
            continue;
        }
        t->fn();
        uint32_t end = time_us();
        t->last_us = end - now;
        if (t->last_us > t->max_us) {
            t->max_us = t->last_us;
        }
        t->runs++;
        if (t->period_us != 0u) {
            t->next_us += t->period_us;
            if ((int32_t)(end - t->next_us) > (int32_t)t->period_us) {
                t->next_us = end;           /* fell behind: do not burst */
            }
            s_busy_us += t->last_us;
        } else if (t->last_us > 2u) {
            s_busy_us += t->last_us;        /* count polling tasks only when they worked */
        }
    }

    uint32_t now = time_us();
    if ((now - s_window_start) >= 1000000u) {
        uint32_t pct = s_busy_us / 10000u;
        s_load = (uint8_t)((pct > 100u) ? 100u : pct);
        s_busy_us = 0u;
        s_window_start = now;
    }
}

uint8_t sched_cpu_load(void)
{
    return s_load;
}
