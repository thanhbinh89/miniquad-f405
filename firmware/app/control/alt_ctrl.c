/**
 * @file    alt_ctrl.c
 * @brief   Altitude hold controller.
 */
#include "alt_ctrl.h"

#include "maths.h"

#define I_LIMIT         0.30f
#define HOVER_MIN       0.10f
#define HOVER_MAX       0.90f
#define BRAKE_TIME_S    0.30f       /* stopping distance = vz * BRAKE_TIME_S */
#define BRAKE_MAX_M     0.30f
#define GRAVITY         9.80665f

void alt_ctrl_init(alt_ctrl_t *c, float hover)
{
    c->h_sp = 0.0f;
    c->vz_sp = 0.0f;
    c->i_term = 0.0f;
    c->hover = constrainf(hover, HOVER_MIN, HOVER_MAX);
    c->out = c->hover;
    c->holding = true;
}

void alt_ctrl_engage(alt_ctrl_t *c, float h, float collective)
{
    c->h_sp = h;
    c->vz_sp = 0.0f;
    c->i_term = constrainf(collective - c->hover, -I_LIMIT, I_LIMIT);
    c->out = c->hover + c->i_term;
    c->holding = true;
}

float alt_ctrl_update(alt_ctrl_t *c, const alt_ctrl_cfg_t *cfg, float h_est, float vz_est,
                      float vz_cmd, bool hold, float dt)
{
    if (hold) {
        if (!c->holding) {
            /* stick released: stop where the current speed carries us */
            c->h_sp = h_est + constrainf(vz_est * BRAKE_TIME_S, -BRAKE_MAX_M, BRAKE_MAX_M);
            c->holding = true;
        }
        c->vz_sp = constrainf(cfg->alt_p * (c->h_sp - h_est), -cfg->vz_max, cfg->vz_max);
    } else {
        c->holding = false;
        c->vz_sp = constrainf(vz_cmd, -cfg->vz_max, cfg->vz_max);
        c->h_sp = h_est;
    }
    if (c->h_sp > cfg->alt_max) {
        c->h_sp = cfg->alt_max;
    }
    float limit = cfg->alt_p * (cfg->alt_max - h_est);
    if (c->vz_sp > limit) {
        c->vz_sp = limit;
    }

    float err = c->vz_sp - vz_est;
    c->i_term = constrainf(c->i_term + cfg->vel_i * err * dt, -I_LIMIT, I_LIMIT);
    c->out = constrainf(c->hover + cfg->vel_p * err + c->i_term, 0.0f, 1.0f);
    return c->out;
}

void alt_ctrl_learn_hover(alt_ctrl_t *c, float collective, float acc_up, float tau, float dt)
{
    float k = 1.0f + acc_up / GRAVITY;          /* T / T_hover */
    if (k < 0.6f || k > 1.6f || collective < 0.05f) {
        return;
    }
    float meas = collective / k;
    if (tau < dt) {
        tau = dt;
    }
    c->hover = constrainf(c->hover + (meas - c->hover) * (dt / tau), HOVER_MIN, HOVER_MAX);
}
