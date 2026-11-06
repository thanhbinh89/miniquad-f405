/**
 * @file    params.h
 * @brief   Parameter registry built from params_list.h.
 *
 * Values travel as raw 32-bit words: IEEE-754 bits for FLOAT, two's
 * complement for INT/BOOL. Every set operation clamps to [min, max] and
 * bumps g_cfg_revision so that consumers can refresh derived data.
 */
#ifndef PARAMS_H
#define PARAMS_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "proto.h"

typedef struct {
    const char *name;
    uint8_t     type;           /* PARAM_T_* */
    uint8_t     flags;          /* PARAM_F_* */
    void       *ptr;            /* into g_cfg */
    float       def;
    float       min;
    float       max;
} param_def_t;

uint16_t           params_count(void);
const param_def_t *params_def(uint16_t index);
/** Index of @p name or -1. */
int                params_find(const char *name);

uint32_t params_get_raw(uint16_t index);
/** Clamp and store; returns the raw value actually stored. */
uint32_t params_set_raw(uint16_t index, uint32_t raw);
float    params_get(uint16_t index);
void     params_set(uint16_t index, float value);

uint32_t params_raw_min(uint16_t index);
uint32_t params_raw_max(uint16_t index);
uint32_t params_raw_default(uint16_t index);

void     params_reset_defaults(void);

/** FNV-1a 32-bit hash of a parameter name (storage key). */
uint32_t params_name_hash(const char *name);
/** Hash over names, types and order of the whole table. */
uint32_t params_table_hash(void);

#endif /* PARAMS_H */
