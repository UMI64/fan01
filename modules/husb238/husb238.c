/*
 * Copyright (c) 2024 umi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT hynetek_husb238

#include "husb238.h"

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(husb238, LOG_LEVEL_INF);

#define HUSB238_REG_PD_STATUS0   0x00
#define HUSB238_REG_PD_STATUS1   0x01
#define HUSB238_REG_SRC_PDO_5V   0x02
#define HUSB238_REG_SRC_PDO_9V   0x03
#define HUSB238_REG_SRC_PDO_12V  0x04
#define HUSB238_REG_SRC_PDO_15V  0x05
#define HUSB238_REG_SRC_PDO_18V  0x06
#define HUSB238_REG_SRC_PDO_20V  0x07
#define HUSB238_REG_SRC_PDO      0x08
#define HUSB238_REG_GO_COMMAND   0x09

#define HUSB238_CMD_REQUEST_PDO     0x01
#define HUSB238_CMD_GET_SRC_CAP     0x04
#define HUSB238_CMD_HARD_RESET      0x10

struct husb238_config {
	struct i2c_dt_spec i2c;
};

struct husb238_data {
	struct k_mutex lock;
	uint8_t pdo_raw[HUSB238_PDO_COUNT];
	struct husb238_status0 status0;
	struct husb238_status1 status1;
};

static const enum husb238_pdo_voltage pdo_table[HUSB238_PDO_COUNT] = {
	HUSB238_PDO_5V,
	HUSB238_PDO_9V,
	HUSB238_PDO_12V,
	HUSB238_PDO_15V,
	HUSB238_PDO_18V,
	HUSB238_PDO_20V,
};

static int pdo_to_index(enum husb238_pdo_voltage voltage)
{
	for (int i = 0; i < HUSB238_PDO_COUNT; i++) {
		if (pdo_table[i] == voltage) {
			return i;
		}
	}
	return -1;
}

uint8_t husb238_voltage_to_volts(enum husb238_pdo_voltage voltage)
{
	switch (voltage) {
	case HUSB238_PDO_5V:
		return 5;
	case HUSB238_PDO_9V:
		return 9;
	case HUSB238_PDO_12V:
		return 12;
	case HUSB238_PDO_15V:
		return 15;
	case HUSB238_PDO_18V:
		return 18;
	case HUSB238_PDO_20V:
		return 20;
	default:
		return 0;
	}
}

enum husb238_pdo_voltage husb238_volts_to_pdo(uint8_t volts)
{
	switch (volts) {
	case 5:
		return HUSB238_PDO_5V;
	case 9:
		return HUSB238_PDO_9V;
	case 12:
		return HUSB238_PDO_12V;
	case 15:
		return HUSB238_PDO_15V;
	case 18:
		return HUSB238_PDO_18V;
	case 20:
		return HUSB238_PDO_20V;
	default:
		return HUSB238_PDO_NOT_SELECT;
	}
}

uint16_t husb238_current_code_to_ma(uint8_t code)
{
	static const uint16_t ma_table[16] = {
		500, 700, 1000, 1250, 1500, 1750, 2000, 2250,
		2500, 2750, 3000, 3250, 3500, 4000, 4500, 5000,
	};

	return ma_table[code & 0x0F];
}

static int husb238_reg_read(const struct device *dev, uint8_t reg, uint8_t *val)
{
	const struct husb238_config *cfg = dev->config;

	return i2c_reg_read_byte_dt(&cfg->i2c, reg, val);
}

static int husb238_reg_write(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct husb238_config *cfg = dev->config;

	return i2c_reg_write_byte_dt(&cfg->i2c, reg, val);
}

static enum husb238_pdo_voltage status0_code_to_pdo(uint8_t code)
{
	static const enum husb238_pdo_voltage map[7] = {
		HUSB238_PDO_NOT_SELECT,
		HUSB238_PDO_5V,
		HUSB238_PDO_9V,
		HUSB238_PDO_12V,
		HUSB238_PDO_15V,
		HUSB238_PDO_18V,
		HUSB238_PDO_20V,
	};

	return (code < ARRAY_SIZE(map)) ? map[code] : HUSB238_PDO_NOT_SELECT;
}

int husb238_get_status0(const struct device *dev, struct husb238_status0 *status0)
{
	struct husb238_data *data = dev->data;
	uint8_t raw = 0;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = husb238_reg_read(dev, HUSB238_REG_PD_STATUS0, &raw);
	if (ret == 0) {
		data->status0.src_pd_voltage = status0_code_to_pdo(raw >> 4);
		data->status0.src_pd_current = (enum husb238_pdo_current)(raw & 0x0F);
		if (status0) {
			*status0 = data->status0;
		}
	}
	k_mutex_unlock(&data->lock);
	return ret;
}

int husb238_get_status1(const struct device *dev, struct husb238_status1 *status1)
{
	struct husb238_data *data = dev->data;
	uint8_t raw = 0;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = husb238_reg_read(dev, HUSB238_REG_PD_STATUS1, &raw);
	if (ret == 0) {
		data->status1.cc_dir = (raw >> 7) & 0x01;
		data->status1.attach = (raw >> 6) & 0x01;
		data->status1.pd_response = (enum husb238_pd_response)((raw >> 3) & 0x07);
		data->status1.v5_voltage = (raw >> 2) & 0x01;
		data->status1.v5_current = (enum husb238_5v_current)(raw & 0x03);
		if (status1) {
			*status1 = data->status1;
		}
	}
	k_mutex_unlock(&data->lock);
	return ret;
}

int husb238_refresh_pdo_caps(const struct device *dev)
{
	struct husb238_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);
	for (int i = 0; i < HUSB238_PDO_COUNT; i++) {
		uint8_t raw = 0;
		uint8_t reg = (uint8_t)(HUSB238_REG_SRC_PDO_5V + i);

		ret = husb238_reg_read(dev, reg, &raw);
		if (ret != 0) {
			LOG_WRN("Failed to read PDO reg 0x%02x: %d", reg, ret);
			break;
		}
		data->pdo_raw[i] = raw;
	}
	k_mutex_unlock(&data->lock);
	return ret;
}

int husb238_get_pdo_caps(const struct device *dev,
			 struct husb238_pdo_cap caps[HUSB238_PDO_COUNT])
{
	struct husb238_data *data = dev->data;
	int ret = husb238_refresh_pdo_caps(dev);

	if (ret != 0) {
		return ret;
	}

	if (caps) {
		k_mutex_lock(&data->lock, K_FOREVER);
		for (int i = 0; i < HUSB238_PDO_COUNT; i++) {
			uint8_t raw = data->pdo_raw[i];

			caps[i].voltage = pdo_table[i];
			caps[i].raw = raw;
			caps[i].detected = (raw & 0x80U) != 0;
			caps[i].max_current_ma =
				caps[i].detected ? husb238_current_code_to_ma(raw) : 0;
		}
		k_mutex_unlock(&data->lock);
	}
	return 0;
}

uint16_t husb238_read_pdo_cap_ma(const struct device *dev,
				 enum husb238_pdo_voltage voltage)
{
	struct husb238_data *data = dev->data;
	int idx = pdo_to_index(voltage);
	uint16_t ma = 0;

	if (idx < 0) {
		return 0;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	uint8_t raw = data->pdo_raw[idx];

	if (raw & 0x80U) {
		ma = husb238_current_code_to_ma(raw);
	}
	k_mutex_unlock(&data->lock);
	return ma;
}

int husb238_set_pdo(const struct device *dev, enum husb238_pdo_voltage pdo)
{
	struct husb238_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = husb238_reg_write(dev, HUSB238_REG_SRC_PDO, (uint8_t)((uint8_t)pdo << 4));
	k_mutex_unlock(&data->lock);
	return ret;
}

int husb238_req_pdo(const struct device *dev)
{
	struct husb238_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = husb238_reg_write(dev, HUSB238_REG_GO_COMMAND, HUSB238_CMD_REQUEST_PDO);
	k_mutex_unlock(&data->lock);
	return ret;
}

int husb238_req_pdo_cap(const struct device *dev)
{
	struct husb238_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = husb238_reg_write(dev, HUSB238_REG_GO_COMMAND, HUSB238_CMD_GET_SRC_CAP);
	k_mutex_unlock(&data->lock);
	return ret;
}

int husb238_req_reset(const struct device *dev)
{
	struct husb238_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = husb238_reg_write(dev, HUSB238_REG_GO_COMMAND, HUSB238_CMD_HARD_RESET);
	k_mutex_unlock(&data->lock);
	return ret;
}

static int husb238_init(const struct device *dev)
{
	const struct husb238_config *cfg = dev->config;
	struct husb238_data *data = dev->data;

	k_mutex_init(&data->lock);

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C bus %s not ready", cfg->i2c.bus->name);
		return -ENODEV;
	}

	int ret = husb238_refresh_pdo_caps(dev);

	for (int retry = 0; ret != 0 && retry < 2; retry++) {
		k_msleep(20);
		ret = husb238_refresh_pdo_caps(dev);
	}
	if (ret != 0) {
		LOG_WRN("HUSB238 not responding at boot (0x%02x)", cfg->i2c.addr);
	} else {
		LOG_INF("HUSB238 initialized (0x%02x)", cfg->i2c.addr);
	}

	return 0;
}

#define HUSB238_INIT(n)                                                              \
	static const struct husb238_config husb238_cfg_##n = {                       \
		.i2c = I2C_DT_SPEC_INST_GET(n),                                      \
	};                                                                           \
	static struct husb238_data husb238_data_##n;                                 \
	DEVICE_DT_INST_DEFINE(n, husb238_init, NULL, &husb238_data_##n,              \
			      &husb238_cfg_##n, POST_KERNEL,                         \
			      CONFIG_HUSB238_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(HUSB238_INIT)
