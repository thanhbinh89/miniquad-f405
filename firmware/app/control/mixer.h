/**
 * @file    mixer.h
 * @brief   Quad-X mixer with board orientation, prop direction and
 *          desaturation (roll/pitch priority over yaw).
 *
 * Motor positions are fixed by the PCB (top view, x right, y top edge):
 *   M1 (+1,+1)  M2 (-1,+1)  M3 (-1,-1)  M4 (+1,-1)
 * board_yaw selects which edge is the nose (0 top/J1, 1 right/J2, 2 bottom,
 * 3 left). Props-in: front-right and rear-left spin CCW (seen from above).
 *
 * Body frame FRD: +roll = right side down, +pitch = nose up, +yaw = nose
 * right. Inputs are normalised torques, output motor thrusts in [0, 1].
 */
#ifndef MIXER_H
#define MIXER_H

#include <stdbool.h>
#include <stdint.h>

#define MIXER_MOTORS    4

typedef struct {
    float roll[MIXER_MOTORS];
    float pitch[MIXER_MOTORS];
    float yaw[MIXER_MOTORS];
    float pos_x[MIXER_MOTORS];      /* body frame, forward */
    float pos_y[MIXER_MOTORS];      /* body frame, right   */
} mixer_geom_t;

typedef struct {
    float motor[MIXER_MOTORS];
    float collective;               /* collective actually applied */
    float rp_scale;                 /* < 1 when roll/pitch were scaled down */
    float yaw_scale;                /* < 1 when yaw was limited             */
    bool  saturated;
} mixer_out_t;

/** Body-frame axes of the board for a given nose direction. */
void mixer_board_axes(int32_t board_yaw, float *fx, float *fy);

void mixer_init_geometry(mixer_geom_t *g, int32_t board_yaw, bool props_out);

/**
 * @param airmode  keep full authority at zero collective (raise collective)
 */
void mixer_mix(const mixer_geom_t *g, float collective, float roll, float pitch, float yaw,
               bool airmode, mixer_out_t *out);

#endif /* MIXER_H */
