/**
 * @file    imu_frame.h
 * @brief   Sensor axes -> body frame (FRD) conversion.
 *
 * The ICM-42688-P (U4, rotation 0) is aligned with the PCB: +X towards the
 * right edge (J2), +Y towards the top edge (J1), +Z out of the top side
 * (silkscreen axis symbol next to U4). board_yaw selects which edge is the
 * nose, exactly like the mixer (mixer_board_axes()):
 *
 *     x_body = s . F,  y_body = s . R,  z_body = -s_z
 *     F = nose direction in board coordinates, R = F turned 90 deg clockwise
 */
#ifndef IMU_FRAME_H
#define IMU_FRAME_H

#include <stdint.h>
#include "maths.h"

vec3_t imu_frame_to_body(vec3_t sensor, int32_t board_yaw);

#endif /* IMU_FRAME_H */
