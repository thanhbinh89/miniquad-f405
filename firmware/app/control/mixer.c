/**
 * @file    mixer.c
 * @brief   Quad-X mixer.
 */
#include "mixer.h"

#include <math.h>
#include <string.h>
#include "maths.h"

/* motor positions on the PCB (x right, y towards the top edge / J1) */
static const float BOARD_X[MIXER_MOTORS] = { +1.0f, -1.0f, -1.0f, +1.0f };
static const float BOARD_Y[MIXER_MOTORS] = { +1.0f, +1.0f, -1.0f, -1.0f };

void mixer_board_axes(int32_t board_yaw, float *fx, float *fy)
{
    switch (board_yaw & 3) {
    case 1:  *fx = 1.0f;  *fy = 0.0f;  break;    /* nose = right edge (J2)  */
    case 2:  *fx = 0.0f;  *fy = -1.0f; break;    /* nose = bottom edge      */
    case 3:  *fx = -1.0f; *fy = 0.0f;  break;    /* nose = left edge        */
    default: *fx = 0.0f;  *fy = 1.0f;  break;    /* nose = top edge (J1)    */
    }
}

void mixer_init_geometry(mixer_geom_t *g, int32_t board_yaw, bool props_out)
{
    float fx, fy;
    mixer_board_axes(board_yaw, &fx, &fy);
    /* right = front rotated 90 deg clockwise (seen from above): (fy, -fx) */
    for (int i = 0; i < MIXER_MOTORS; i++) {
        float x = BOARD_X[i] * fx + BOARD_Y[i] * fy;
        float y = BOARD_X[i] * fy - BOARD_Y[i] * fx;
        g->pos_x[i] = x;
        g->pos_y[i] = y;
        g->roll[i] = -y;            /* lifting the left side rolls right */
        g->pitch[i] = x;            /* lifting the front pitches up      */
        float spin = (x * y > 0.0f) ? 1.0f : -1.0f;     /* +1 = CCW (props-in FR/RL) */
        g->yaw[i] = props_out ? -spin : spin;           /* CCW prop -> +yaw torque */
    }
}

void mixer_mix(const mixer_geom_t *g, float collective, float roll, float pitch, float yaw,
               bool airmode, mixer_out_t *out)
{
    float rp[MIXER_MOTORS];
    float yw[MIXER_MOTORS];
    float rp_min = 0.0f, rp_max = 0.0f;

    out->saturated = false;
    out->rp_scale = 1.0f;
    out->yaw_scale = 1.0f;

    for (int i = 0; i < MIXER_MOTORS; i++) {
        rp[i] = g->roll[i] * roll + g->pitch[i] * pitch;
        yw[i] = g->yaw[i] * yaw;
        if (i == 0 || rp[i] < rp_min) {
            rp_min = rp[i];
        }
        if (i == 0 || rp[i] > rp_max) {
            rp_max = rp[i];
        }
    }

    float rp_span = rp_max - rp_min;
    if (rp_span > 1.0f) {
        /* roll/pitch alone exceed the motor range: scale them, drop yaw */
        float s = 1.0f / rp_span;
        for (int i = 0; i < MIXER_MOTORS; i++) {
            rp[i] *= s;
            yw[i] = 0.0f;
        }
        out->rp_scale = s;
        out->yaw_scale = 0.0f;
        out->saturated = true;
    } else {
        float t_min = 0.0f, t_max = 0.0f;
        for (int i = 0; i < MIXER_MOTORS; i++) {
            float t = rp[i] + yw[i];
            if (i == 0 || t < t_min) {
                t_min = t;
            }
            if (i == 0 || t > t_max) {
                t_max = t;
            }
        }
        if ((t_max - t_min) > 1.0f && fabsf(yaw) > 1e-6f) {
            /* yaw gets what is left (conservative: yaw span = 2|yaw|) */
            float k = constrainf((1.0f - rp_span) / (2.0f * fabsf(yaw)), 0.0f, 1.0f);
            for (int i = 0; i < MIXER_MOTORS; i++) {
                yw[i] *= k;
            }
            out->yaw_scale = k;
            out->saturated = true;
        }
    }

    float m[MIXER_MOTORS];
    float m_min = 0.0f, m_max = 0.0f;
    for (int i = 0; i < MIXER_MOTORS; i++) {
        m[i] = rp[i] + yw[i];
        if (i == 0 || m[i] < m_min) {
            m_min = m[i];
        }
        if (i == 0 || m[i] > m_max) {
            m_max = m[i];
        }
    }

    float c = constrainf(collective, 0.0f, 1.0f);
    if (airmode) {
        float c2 = constrainf(c, -m_min, 1.0f - m_max);
        if (c2 != c) {
            out->saturated = true;
        }
        c = c2;
    } else {
        if (c + m_min < 0.0f && m_min < 0.0f) {
            /* not enough room below: reduce authority with the collective */
            float s = constrainf(c / -m_min, 0.0f, 1.0f);
            for (int i = 0; i < MIXER_MOTORS; i++) {
                m[i] *= s;
            }
            m_max *= s;
            out->saturated = true;
        }
        if (c + m_max > 1.0f) {
            c = 1.0f - m_max;       /* keep authority at full throttle */
            out->saturated = true;
        }
    }

    for (int i = 0; i < MIXER_MOTORS; i++) {
        out->motor[i] = constrainf(c + m[i], 0.0f, 1.0f);
    }
    out->collective = c;
}
