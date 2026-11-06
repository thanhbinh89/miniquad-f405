/**
 * @file    alt_ctrl.h
 * @brief   Altitude hold: height P loop -> vertical speed PI loop -> thrust,
 *          around an estimated hover thrust.
 *
 * Stick centred: hold h_sp (set to the current height plus the stopping
 * distance when the stick is released). Stick moved: pure climb-rate
 * control, h_sp follows the estimate. The hover thrust is estimated from
 * the vertical acceleration (alt_ctrl_learn_hover): a = g (T / T_hover - 1).
 * The output is the collective for a level vehicle; the caller divides by
 * cos(tilt).
 */
#ifndef ALT_CTRL_H
#define ALT_CTRL_H

#include <stdbool.h>

typedef struct {
    float alt_p;        /* (m/s) per m      */
    float vel_p;        /* thrust per (m/s) */
    float vel_i;        /* thrust per m     */
    float vz_max;       /* m/s              */
    float alt_max;      /* m                */
} alt_ctrl_cfg_t;

typedef struct {
    float h_sp;
    float vz_sp;
    float i_term;
    float hover;
    float out;
    bool  holding;
} alt_ctrl_t;

void  alt_ctrl_init(alt_ctrl_t *c, float hover);
/** Bumpless engage at height @p h with current level collective @p collective. */
void  alt_ctrl_engage(alt_ctrl_t *c, float h, float collective);
/**
 * @param vz_cmd  commanded climb rate (m/s), used when @p hold is false
 * @param hold    hold the height set point (stick centred)
 */
float alt_ctrl_update(alt_ctrl_t *c, const alt_ctrl_cfg_t *cfg, float h_est, float vz_est,
                      float vz_cmd, bool hold, float dt);
/**
 * Hover thrust estimator: @p collective is the applied level collective,
 * @p acc_up the measured vertical acceleration (m/s^2, bias removed),
 * @p tau the averaging time constant (s). Call only while airborne.
 */
void  alt_ctrl_learn_hover(alt_ctrl_t *c, float collective, float acc_up, float tau, float dt);

#endif /* ALT_CTRL_H */
