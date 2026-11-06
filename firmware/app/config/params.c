/**
 * @file    params.c
 * @brief   Parameter registry and the global configuration instance.
 */
#include "params.h"

#include <math.h>
#include <string.h>
#include "params_list.h"

config_t          g_cfg;
volatile uint32_t g_cfg_revision;

#define PT_FLOAT    PARAM_T_FLOAT
#define PT_INT      PARAM_T_INT
#define PT_BOOL     PARAM_T_BOOL

#define PARAM_ENTRY(name_, type_, field_, def_, min_, max_, flags_) \
    { #name_, PT_##type_, (flags_), (void *)&g_cfg.field_, (float)(def_), (float)(min_), (float)(max_) },

static const param_def_t s_params[] = {
    PARAMS_LIST(PARAM_ENTRY)
};

#define PARAM_NUM   (sizeof(s_params) / sizeof(s_params[0]))

_Static_assert(PARAM_NUM < 65535u, "too many parameters");

/* compile-time check: every name fits PARAM_NAME_MAX */
#define PARAM_NAME_CHECK(name_, type_, field_, def_, min_, max_, flags_) \
    _Static_assert(sizeof(#name_) <= PARAM_NAME_MAX, "parameter name too long: " #name_);
PARAMS_LIST(PARAM_NAME_CHECK)

uint16_t params_count(void)
{
    return (uint16_t)PARAM_NUM;
}

const param_def_t *params_def(uint16_t index)
{
    return (index < PARAM_NUM) ? &s_params[index] : NULL;
}

int params_find(const char *name)
{
    for (uint16_t i = 0u; i < PARAM_NUM; i++) {
        if (strcmp(s_params[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static uint32_t float_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

static float bits_float(uint32_t u)
{
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

static uint32_t to_raw(const param_def_t *p, float v)
{
    if (p->type == PARAM_T_FLOAT) {
        return float_bits(v);
    }
    int32_t i = (int32_t)lrintf(v);
    return (uint32_t)i;
}

uint32_t params_get_raw(uint16_t index)
{
    const param_def_t *p = params_def(index);
    if (p == NULL) {
        return 0u;
    }
    uint32_t u;
    memcpy(&u, p->ptr, sizeof(u));
    return u;
}

float params_get(uint16_t index)
{
    const param_def_t *p = params_def(index);
    if (p == NULL) {
        return 0.0f;
    }
    uint32_t u = params_get_raw(index);
    return (p->type == PARAM_T_FLOAT) ? bits_float(u) : (float)(int32_t)u;
}

static void store(const param_def_t *p, float v)
{
    if (!isfinite(v)) {
        return;                     /* reject NaN / inf, keep the old value */
    }
    if (v < p->min) {
        v = p->min;
    }
    if (v > p->max) {
        v = p->max;
    }
    if (p->type == PARAM_T_FLOAT) {
        *(float *)p->ptr = v;
    } else {
        int32_t i = (int32_t)lrintf(v);
        if (p->type == PARAM_T_BOOL) {
            i = (i != 0) ? 1 : 0;
        }
        *(int32_t *)p->ptr = i;
    }
    g_cfg_revision++;
}

uint32_t params_set_raw(uint16_t index, uint32_t raw)
{
    const param_def_t *p = params_def(index);
    if (p == NULL || (p->flags & PARAM_F_READONLY) != 0u) {
        return params_get_raw(index);
    }
    float v = (p->type == PARAM_T_FLOAT) ? bits_float(raw) : (float)(int32_t)raw;
    store(p, v);
    return params_get_raw(index);
}

void params_set(uint16_t index, float value)
{
    const param_def_t *p = params_def(index);
    if (p != NULL) {
        store(p, value);
    }
}

uint32_t params_raw_min(uint16_t index)
{
    const param_def_t *p = params_def(index);
    return (p != NULL) ? to_raw(p, p->min) : 0u;
}

uint32_t params_raw_max(uint16_t index)
{
    const param_def_t *p = params_def(index);
    return (p != NULL) ? to_raw(p, p->max) : 0u;
}

uint32_t params_raw_default(uint16_t index)
{
    const param_def_t *p = params_def(index);
    return (p != NULL) ? to_raw(p, p->def) : 0u;
}

void params_reset_defaults(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    for (uint16_t i = 0u; i < PARAM_NUM; i++) {
        store(&s_params[i], s_params[i].def);
    }
}

uint32_t params_name_hash(const char *name)
{
    uint32_t h = 0x811C9DC5u;
    while (*name != '\0') {
        h ^= (uint8_t)*name++;
        h *= 0x01000193u;
    }
    return h;
}

uint32_t params_table_hash(void)
{
    uint32_t h = 0x811C9DC5u;
    for (uint16_t i = 0u; i < PARAM_NUM; i++) {
        h ^= params_name_hash(s_params[i].name);
        h *= 0x01000193u;
        h ^= s_params[i].type;
        h *= 0x01000193u;
    }
    return h;
}
