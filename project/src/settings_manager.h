/*
 * Copyright (c) 2024 umi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SETTINGS_MANAGER_H_
#define SETTINGS_MANAGER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int settings_manager_init(void);

uint8_t settings_get_pdo_voltage(void);
int settings_set_pdo_voltage(uint8_t pdo_voltage);

bool settings_get_fan_speed_use_storage(void);
int settings_set_fan_speed_use_storage(bool enable);

uint16_t settings_get_fan_speed(void);
int settings_set_fan_speed(uint16_t rpm);

bool settings_get_fan_switch(void);
int settings_set_fan_switch(bool sw);

void settings_get_pid(float *kp, float *ki, float *kd);
int settings_set_pid(float kp, float ki, float kd);

void settings_get_display_cfg(uint32_t *full_s, uint32_t *dim_s,
			      uint8_t *full_bright, uint8_t *dim_bright);
int settings_set_display_cfg(uint32_t full_s, uint32_t dim_s,
			     uint8_t full_bright, uint8_t dim_bright);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_MANAGER_H_ */
