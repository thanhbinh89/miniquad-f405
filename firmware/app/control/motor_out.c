/**
 * @file    motor_out.c
 * @brief   Thrust linearisation, battery compensation, idle mapping.
 */
#include "motor_out.h"

#include <math.h>
#include "maths.h"

float motor_thrust_to_duty(float thrust, float a)
{
    float t = constrainf(thrust, 0.0f, 1.0f);
    if (a < 1e-3f) {
        return t;
    }
    float b = 1.0f - a;
    return (-b + sqrtf(b * b + 4.0f * a * t)) / (2.0f * a);
}

float motor_battery_factor(float vbat, bool present, float vref, float fmin, float fmax)
{
    if (!present || vbat < 2.0f) {
        return 1.0f;
    }
    return constrainf(vref / vbat, fmin, fmax);
}

void motor_output(const float thrust[4], float bat_factor, float idle, float duty_max,
                  float thr_lin, float duty[4])
{
    for (int i = 0; i < 4; i++) {
        float d = motor_thrust_to_duty(thrust[i], thr_lin) * bat_factor;
        d = constrainf(d, 0.0f, 1.0f);
        duty[i] = idle + (duty_max - idle) * d;
    }
}
