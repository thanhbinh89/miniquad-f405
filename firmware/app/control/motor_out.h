/**
 * @file    motor_out.h
 * @brief   Motor thrust -> PWM duty for brushed (coreless) motors.
 *
 * 1. Thrust linearisation. Static thrust of a coreless motor + prop is
 *    roughly T(d) = a*d^2 + (1-a)*d (d = duty at the reference voltage;
 *    a = thr_lin). The inverse is used so the PID sees a linear plant.
 * 2. Battery sag compensation. The effective motor voltage is d * Vbat, so
 *    the duty is scaled by Vref / Vbat (low-pass filtered, clamped).
 * 3. Idle / max: armed motors spin at 'idle' duty with zero thrust demand.
 */
#ifndef MOTOR_OUT_H
#define MOTOR_OUT_H

#include <stdbool.h>

/** Inverse of T(d) = a d^2 + (1 - a) d on [0, 1]. */
float motor_thrust_to_duty(float thrust, float a);

/** Vref / Vbat clamped to [fmin, fmax]; 1.0 when no battery is present. */
float motor_battery_factor(float vbat, bool present, float vref, float fmin, float fmax);

/** Full chain for four motors. */
void  motor_output(const float thrust[4], float bat_factor, float idle, float duty_max,
                   float thr_lin, float duty[4]);

#endif /* MOTOR_OUT_H */
