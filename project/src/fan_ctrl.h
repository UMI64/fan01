/*
 * Copyright (c) 2024 umi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FAN_CTRL_H_
#define FAN_CTRL_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FAN_MAX_TARGET_RPM 8000U

int fan_ctrl_init(void);

void fan_ctrl_set_switch(bool sw);
bool fan_ctrl_get_switch(void);
void fan_ctrl_turn_switch(void);

void fan_ctrl_set_target_speed(uint16_t rpm);
uint16_t fan_ctrl_get_target_speed(void);
uint16_t fan_ctrl_get_speed(void);

float fan_ctrl_get_voltage(void);
float fan_ctrl_get_current(void);
float fan_ctrl_get_power(void);
float fan_ctrl_get_duty_cycle(void);
void fan_ctrl_set_pid(float kp, float ki, float kd);
void fan_ctrl_get_pid(float *kp, float *ki, float *kd);

#ifdef __cplusplus
}
#endif

#endif /* FAN_CTRL_H_ */
