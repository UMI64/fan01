/*
 * Copyright (c) 2024 umi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HUSB238_H_
#define HUSB238_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HUSB238_PDO_COUNT 6

enum husb238_pdo_voltage {
	HUSB238_PDO_NOT_SELECT = 0x00,
	HUSB238_PDO_5V = 0x01,
	HUSB238_PDO_9V = 0x02,
	HUSB238_PDO_12V = 0x03,
	HUSB238_PDO_15V = 0x08,
	HUSB238_PDO_18V = 0x09,
	HUSB238_PDO_20V = 0x0A,
};

enum husb238_pdo_current {
	HUSB238_PDO_0_5A = 0x00,
	HUSB238_PDO_0_7A = 0x01,
	HUSB238_PDO_1_0A = 0x02,
	HUSB238_PDO_1_25A = 0x03,
	HUSB238_PDO_1_5A = 0x04,
	HUSB238_PDO_1_75A = 0x05,
	HUSB238_PDO_2_0A = 0x06,
	HUSB238_PDO_2_25A = 0x07,
	HUSB238_PDO_2_5A = 0x08,
	HUSB238_PDO_2_75A = 0x09,
	HUSB238_PDO_3_0A = 0x0A,
	HUSB238_PDO_3_25A = 0x0B,
	HUSB238_PDO_3_5A = 0x0C,
	HUSB238_PDO_4_0A = 0x0D,
	HUSB238_PDO_4_5A = 0x0E,
	HUSB238_PDO_5_0A = 0x0F,
};

enum husb238_pd_response {
	HUSB238_PD_RESP_NO_RESPONSE = 0x00,
	HUSB238_PD_RESP_SUCCESS = 0x01,
	HUSB238_PD_RESP_INVALID_CMD_OR_ARG = 0x03,
	HUSB238_PD_RESP_CMD_NOT_SUPPORTED = 0x04,
	HUSB238_PD_RESP_TX_FAIL_NO_GOODCRC = 0x05,
};

enum husb238_5v_current {
	HUSB238_5V_CURRENT_DEFAULT = 0x00,
	HUSB238_5V_CURRENT_1_5A = 0x01,
	HUSB238_5V_CURRENT_2_4A = 0x02,
	HUSB238_5V_CURRENT_3_0A = 0x03,
};

struct husb238_status0 {
	enum husb238_pdo_voltage src_pd_voltage;
	enum husb238_pdo_current src_pd_current;
};

struct husb238_status1 {
	uint8_t cc_dir;
	uint8_t attach;
	enum husb238_pd_response pd_response;
	uint8_t v5_voltage;
	enum husb238_5v_current v5_current;
};

struct husb238_pdo_cap {
	enum husb238_pdo_voltage voltage;
	bool detected;
	uint16_t max_current_ma;
	uint8_t raw;
};

uint8_t husb238_voltage_to_volts(enum husb238_pdo_voltage voltage);
enum husb238_pdo_voltage husb238_volts_to_pdo(uint8_t volts);
uint16_t husb238_current_code_to_ma(uint8_t code);

int husb238_get_status0(const struct device *dev, struct husb238_status0 *status0);
int husb238_get_status1(const struct device *dev, struct husb238_status1 *status1);
int husb238_refresh_pdo_caps(const struct device *dev);
int husb238_get_pdo_caps(const struct device *dev,
			 struct husb238_pdo_cap caps[HUSB238_PDO_COUNT]);
uint16_t husb238_read_pdo_cap_ma(const struct device *dev,
				 enum husb238_pdo_voltage voltage);
int husb238_set_pdo(const struct device *dev, enum husb238_pdo_voltage pdo);
int husb238_req_pdo(const struct device *dev);
int husb238_req_pdo_cap(const struct device *dev);
int husb238_req_reset(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* HUSB238_H_ */
