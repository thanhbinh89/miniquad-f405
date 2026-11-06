/**
 * @file    sched.h
 * @brief   Cooperative main-loop scheduler with execution statistics.
 *
 * Everything time critical runs in interrupts (flight loop, DMA). The main
 * loop only runs background tasks; each task must return quickly.
 * period_us = 0 means "every iteration".
 */
#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>

typedef struct {
    const char *name;
    void      (*fn)(void);
    uint32_t    period_us;
    /* runtime statistics */
    uint32_t    next_us;
    uint32_t    last_us;        /* duration of the last run */
    uint32_t    max_us;
    uint32_t    runs;
} sched_task_t;

void    sched_init(sched_task_t *tasks, uint32_t count);
/** Run every task that is due once. Call forever from main(). */
void    sched_run(void);
/** Share of the last second spent inside tasks (0..100 %). */
uint8_t sched_cpu_load(void);

#endif /* SCHED_H */
