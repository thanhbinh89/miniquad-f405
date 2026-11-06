/**
 * @file    imu_frame.c
 * @brief   Sensor -> body frame rotation.
 */
#include "imu_frame.h"

#include "mixer.h"

vec3_t imu_frame_to_body(vec3_t s, int32_t board_yaw)
{
    float fx, fy;
    mixer_board_axes(board_yaw, &fx, &fy);
    return v3(fx * s.x + fy * s.y,      /* forward */
              fy * s.x - fx * s.y,      /* right   */
              -s.z);                    /* down    */
}
