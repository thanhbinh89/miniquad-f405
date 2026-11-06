/**
 * @file    motor_pwm.h
 * @brief   TIM3 CH1..CH4 edge-aligned PWM for the four brushed motors.
 *
 * 32 kHz (ARR = 2624, 2625 steps). Compare registers are preloaded and
 * latched at the update event, so the four duties always change together
 * and glitch-free - no DMA needed for that.
 */
#ifndef MOTOR_PWM_H
#define MOTOR_PWM_H

void motor_pwm_init(void);
/** duty[0..3] in [0, 1] for M1..M4. */
void motor_pwm_write(const float duty[4]);
/** All outputs low immediately (safe from any context). */
void motor_pwm_stop(void);

#endif /* MOTOR_PWM_H */
