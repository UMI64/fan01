/*
 * Copyright (c) 2024 umi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PD_MANAGER_H_
#define PD_MANAGER_H_

#include "husb238.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int pd_manager_init(enum husb238_pdo_voltage initial_pdo);
bool pd_manager_is_settled(void);
int pd_manager_request_voltage(enum husb238_pdo_voltage voltage);
enum husb238_pdo_voltage pd_manager_get_target_pdo(void);
uint8_t pd_manager_get_target_volts(void);
enum husb238_pdo_voltage pd_manager_cycle_voltage(int dir);
int pd_manager_get_caps_json(char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* PD_MANAGER_H_ */
