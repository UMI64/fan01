/*
 * Copyright (c) 2024 umi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "settings_manager.h"
#include "husb238.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(settings_mgr, LOG_LEVEL_INF);

static uint8_t cfg_pdo_voltage = HUSB238_PDO_20V;
static bool cfg_fan_speed_use_storage = true;
static uint16_t cfg_fan_speed = 0;
static bool cfg_fan_switch = false;
static float cfg_pid_kp = 0.0008f;
static float cfg_pid_ki = 0.0f;
static float cfg_pid_kd = 0.0005f;
static uint32_t cfg_disp_full_s = 60;
static uint32_t cfg_disp_dim_s = 120;
static uint8_t cfg_disp_full_bright = 255;
static uint8_t cfg_disp_dim_bright = 50;

static int fan01_settings_set(const char *key, size_t len_rd,
			      settings_read_cb read_cb, void *cb_arg)
{
	if (!key) {
		return -ENOENT;
	}

	if (!strcmp(key, "pdo_voltage") && len_rd == sizeof(cfg_pdo_voltage)) {
		ssize_t rc = read_cb(cb_arg, &cfg_pdo_voltage, sizeof(cfg_pdo_voltage));
		return (rc == sizeof(cfg_pdo_voltage)) ? 0 : -EINVAL;
	}
	if (!strcmp(key, "fan_speed_use_storage") && len_rd == sizeof(cfg_fan_speed_use_storage)) {
		ssize_t rc = read_cb(cb_arg, &cfg_fan_speed_use_storage,
				     sizeof(cfg_fan_speed_use_storage));
		return (rc == sizeof(cfg_fan_speed_use_storage)) ? 0 : -EINVAL;
	}
	if (!strcmp(key, "fan_speed") && len_rd == sizeof(cfg_fan_speed)) {
		ssize_t rc = read_cb(cb_arg, &cfg_fan_speed, sizeof(cfg_fan_speed));
		return (rc == sizeof(cfg_fan_speed)) ? 0 : -EINVAL;
	}
	if (!strcmp(key, "fan_switch") && len_rd == sizeof(cfg_fan_switch)) {
		ssize_t rc = read_cb(cb_arg, &cfg_fan_switch, sizeof(cfg_fan_switch));
		return (rc == sizeof(cfg_fan_switch)) ? 0 : -EINVAL;
	}
	if (!strcmp(key, "pid_kp") && len_rd == sizeof(cfg_pid_kp)) {
		ssize_t rc = read_cb(cb_arg, &cfg_pid_kp, sizeof(cfg_pid_kp));
		return (rc == sizeof(cfg_pid_kp)) ? 0 : -EINVAL;
	}
	if (!strcmp(key, "pid_ki") && len_rd == sizeof(cfg_pid_ki)) {
		ssize_t rc = read_cb(cb_arg, &cfg_pid_ki, sizeof(cfg_pid_ki));
		return (rc == sizeof(cfg_pid_ki)) ? 0 : -EINVAL;
	}
	if (!strcmp(key, "pid_kd") && len_rd == sizeof(cfg_pid_kd)) {
		ssize_t rc = read_cb(cb_arg, &cfg_pid_kd, sizeof(cfg_pid_kd));
		return (rc == sizeof(cfg_pid_kd)) ? 0 : -EINVAL;
	}
	if (!strcmp(key, "disp_full_s") && len_rd == sizeof(cfg_disp_full_s)) {
		ssize_t rc = read_cb(cb_arg, &cfg_disp_full_s, sizeof(cfg_disp_full_s));
		return (rc == sizeof(cfg_disp_full_s)) ? 0 : -EINVAL;
	}
	if (!strcmp(key, "disp_dim_s") && len_rd == sizeof(cfg_disp_dim_s)) {
		ssize_t rc = read_cb(cb_arg, &cfg_disp_dim_s, sizeof(cfg_disp_dim_s));
		return (rc == sizeof(cfg_disp_dim_s)) ? 0 : -EINVAL;
	}
	if (!strcmp(key, "disp_full_br") && len_rd == sizeof(cfg_disp_full_bright)) {
		ssize_t rc = read_cb(cb_arg, &cfg_disp_full_bright, sizeof(cfg_disp_full_bright));
		return (rc == sizeof(cfg_disp_full_bright)) ? 0 : -EINVAL;
	}
	if (!strcmp(key, "disp_dim_br") && len_rd == sizeof(cfg_disp_dim_bright)) {
		ssize_t rc = read_cb(cb_arg, &cfg_disp_dim_bright, sizeof(cfg_disp_dim_bright));
		return (rc == sizeof(cfg_disp_dim_bright)) ? 0 : -EINVAL;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(fan01, "fan01", NULL, fan01_settings_set, NULL, NULL);

int settings_manager_init(void)
{
	int rc = settings_subsys_init();

	if (rc) {
		LOG_ERR("settings_subsys_init failed: %d", rc);
		return rc;
	}

	rc = settings_load_subtree("fan01");
	if (rc) {
		LOG_WRN("settings_load_subtree failed: %d", rc);
	}

	LOG_INF("Settings loaded: pdo=%u, mem=%d, speed=%u, sw=%d",
		cfg_pdo_voltage, cfg_fan_speed_use_storage,
		cfg_fan_speed, cfg_fan_switch);
	return 0;
}

uint8_t settings_get_pdo_voltage(void)
{
	return cfg_pdo_voltage;
}

int settings_set_pdo_voltage(uint8_t pdo_voltage)
{
	cfg_pdo_voltage = pdo_voltage;
	return settings_save_one("fan01/pdo_voltage", &cfg_pdo_voltage,
				 sizeof(cfg_pdo_voltage));
}

bool settings_get_fan_speed_use_storage(void)
{
	return cfg_fan_speed_use_storage;
}

int settings_set_fan_speed_use_storage(bool enable)
{
	cfg_fan_speed_use_storage = enable;
	return settings_save_one("fan01/fan_speed_use_storage",
				 &cfg_fan_speed_use_storage,
				 sizeof(cfg_fan_speed_use_storage));
}

uint16_t settings_get_fan_speed(void)
{
	return cfg_fan_speed;
}

int settings_set_fan_speed(uint16_t rpm)
{
	cfg_fan_speed = rpm;
	return settings_save_one("fan01/fan_speed", &cfg_fan_speed,
				 sizeof(cfg_fan_speed));
}

bool settings_get_fan_switch(void)
{
	return cfg_fan_switch;
}

int settings_set_fan_switch(bool sw)
{
	cfg_fan_switch = sw;
	return settings_save_one("fan01/fan_switch", &cfg_fan_switch,
				 sizeof(cfg_fan_switch));
}

void settings_get_pid(float *kp, float *ki, float *kd)
{
	if (kp) {
		*kp = cfg_pid_kp;
	}
	if (ki) {
		*ki = cfg_pid_ki;
	}
	if (kd) {
		*kd = cfg_pid_kd;
	}
}

int settings_set_pid(float kp, float ki, float kd)
{
	cfg_pid_kp = kp;
	cfg_pid_ki = ki;
	cfg_pid_kd = kd;

	int rc = settings_save_one("fan01/pid_kp", &cfg_pid_kp, sizeof(cfg_pid_kp));

	rc |= settings_save_one("fan01/pid_ki", &cfg_pid_ki, sizeof(cfg_pid_ki));
	rc |= settings_save_one("fan01/pid_kd", &cfg_pid_kd, sizeof(cfg_pid_kd));
	return rc;
}

void settings_get_display_cfg(uint32_t *full_s, uint32_t *dim_s,
			      uint8_t *full_bright, uint8_t *dim_bright)
{
	if (full_s) {
		*full_s = cfg_disp_full_s;
	}
	if (dim_s) {
		*dim_s = cfg_disp_dim_s;
	}
	if (full_bright) {
		*full_bright = cfg_disp_full_bright;
	}
	if (dim_bright) {
		*dim_bright = cfg_disp_dim_bright;
	}
}

int settings_set_display_cfg(uint32_t full_s, uint32_t dim_s,
			     uint8_t full_bright, uint8_t dim_bright)
{
	cfg_disp_full_s = full_s;
	cfg_disp_dim_s = dim_s;
	cfg_disp_full_bright = full_bright;
	cfg_disp_dim_bright = dim_bright;

	int rc = settings_save_one("fan01/disp_full_s", &cfg_disp_full_s,
				   sizeof(cfg_disp_full_s));

	rc |= settings_save_one("fan01/disp_dim_s", &cfg_disp_dim_s,
				sizeof(cfg_disp_dim_s));
	rc |= settings_save_one("fan01/disp_full_br", &cfg_disp_full_bright,
				sizeof(cfg_disp_full_bright));
	rc |= settings_save_one("fan01/disp_dim_br", &cfg_disp_dim_bright,
				sizeof(cfg_disp_dim_bright));
	return rc;
}
