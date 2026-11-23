/*
 * Copyright (c) 2024 umi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pd_manager.h"
#include "settings_manager.h"

#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(pd_mgr, LOG_LEVEL_INF);

#define HUSB238_NODE DT_NODELABEL(husb238)

static const struct device *husb_dev;
static enum husb238_pdo_voltage target_pdo = HUSB238_PDO_20V;

static const enum husb238_pdo_voltage supported_pdos[HUSB238_PDO_COUNT] = {
	HUSB238_PDO_5V,
	HUSB238_PDO_9V,
	HUSB238_PDO_12V,
	HUSB238_PDO_15V,
	HUSB238_PDO_18V,
	HUSB238_PDO_20V,
};

static const uint16_t pd_retry_delays_ms[] = { 150, 300, 500, 800, 1200 };
static uint8_t pd_retry_idx;
static bool pd_requested_caps;
static atomic_t pd_settled_flag = ATOMIC_INIT(0);
static struct k_work_delayable pd_retry_work;

static enum husb238_pdo_voltage pick_best_pdo(const struct husb238_pdo_cap caps[HUSB238_PDO_COUNT],
					      uint8_t target_volts, bool *any_detected)
{
	enum husb238_pdo_voltage best = HUSB238_PDO_NOT_SELECT;
	uint8_t best_v = 0;
	bool detected = false;

	for (int i = 0; i < HUSB238_PDO_COUNT; i++) {
		if (!caps[i].detected) {
			continue;
		}
		detected = true;
		uint8_t v = husb238_voltage_to_volts(caps[i].voltage);

		if (v <= target_volts && v >= best_v) {
			best_v = v;
			best = caps[i].voltage;
		}
	}

	if (any_detected) {
		*any_detected = detected;
	}
	return best;
}

static void pd_retry_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!husb_dev) {
		return;
	}

	struct husb238_status0 st0 = { 0 };
	struct husb238_status1 st1 = { 0 };
	struct husb238_pdo_cap caps[HUSB238_PDO_COUNT];
	uint8_t target_v = husb238_voltage_to_volts(target_pdo);
	int ret0 = husb238_get_status0(husb_dev, &st0);
	int ret1 = husb238_get_status1(husb_dev, &st1);
	int retc = husb238_get_pdo_caps(husb_dev, caps);
	uint8_t cur_v = (ret0 == 0) ? husb238_voltage_to_volts(st0.src_pd_voltage) : 0;

	if (ret0 == 0 && ret1 == 0 && st1.attach && cur_v == target_v &&
	    (st1.pd_response == HUSB238_PD_RESP_SUCCESS || cur_v > 5)) {
		if (!atomic_get(&pd_settled_flag)) {
			LOG_INF("PD contract settled: %uV", cur_v);
		}
		atomic_set(&pd_settled_flag, 1);
		pd_retry_idx = 0;
		k_work_reschedule(&pd_retry_work, K_MSEC(2000));
		return;
	}

	if (ret1 == 0 && !st1.attach) {
		atomic_set(&pd_settled_flag, 0);
		pd_retry_idx = 0;
		pd_requested_caps = false;
		k_work_reschedule(&pd_retry_work, K_MSEC(500));
		return;
	}

	bool any_detected = false;
	enum husb238_pdo_voltage req_pdo = target_pdo;

	if (retc == 0) {
		enum husb238_pdo_voltage best = pick_best_pdo(caps, target_v, &any_detected);

		if (best != HUSB238_PDO_NOT_SELECT) {
			req_pdo = best;
		}
	}

	uint8_t req_v = husb238_voltage_to_volts(req_pdo);

	if (ret1 == 0 && st1.attach && !any_detected && !pd_requested_caps) {
		(void)husb238_req_pdo_cap(husb_dev);
		pd_requested_caps = true;
	}

	if (cur_v != req_v) {
		atomic_set(&pd_settled_flag, 0);
		if (husb238_set_pdo(husb_dev, req_pdo) == 0) {
			(void)husb238_req_pdo(husb_dev);
			LOG_INF("PD retry #%u: cur=%uV req=%uV target=%uV attach=%u resp=%u",
				pd_retry_idx + 1, cur_v, req_v, target_v,
				(ret1 == 0) ? st1.attach : 0,
				(ret1 == 0) ? (uint8_t)st1.pd_response : 0);
		}
	} else if (any_detected && req_v < target_v && pd_retry_idx >= 2) {
		if (!atomic_get(&pd_settled_flag)) {
			LOG_INF("PD settled at max available %uV (target %uV)", cur_v, target_v);
		}
		atomic_set(&pd_settled_flag, 1);
		k_work_reschedule(&pd_retry_work, K_MSEC(2000));
		return;
	}

	if (pd_retry_idx < ARRAY_SIZE(pd_retry_delays_ms)) {
		k_work_reschedule(&pd_retry_work, K_MSEC(pd_retry_delays_ms[pd_retry_idx]));
		pd_retry_idx++;
	} else {
		LOG_WRN("PD negotiation monitor at %uV (target %uV)", cur_v, target_v);
		atomic_set(&pd_settled_flag, 1);
		k_work_reschedule(&pd_retry_work, K_MSEC(2000));
	}
}

bool pd_manager_is_settled(void)
{
	return atomic_get(&pd_settled_flag) != 0;
}

int pd_manager_init(enum husb238_pdo_voltage initial_pdo)
{
	husb_dev = DEVICE_DT_GET(HUSB238_NODE);
	if (!device_is_ready(husb_dev)) {
		LOG_ERR("HUSB238 device not ready");
		return -ENODEV;
	}

	struct husb238_pdo_cap caps[HUSB238_PDO_COUNT];
	int ret = husb238_get_pdo_caps(husb_dev, caps);

	if (ret == 0) {
		for (int i = 0; i < HUSB238_PDO_COUNT; i++) {
			if (caps[i].detected) {
				LOG_INF("Available PD %uV %u.%02uA",
					husb238_voltage_to_volts(caps[i].voltage),
					caps[i].max_current_ma / 1000,
					(caps[i].max_current_ma % 1000) / 10);
			}
		}
	} else {
		LOG_WRN("Unable to read HUSB238 PDO caps: %d", ret);
	}

	k_work_init_delayable(&pd_retry_work, pd_retry_work_handler);
	pd_requested_caps = false;
	return pd_manager_request_voltage(initial_pdo);
}

int pd_manager_request_voltage(enum husb238_pdo_voltage voltage)
{
	if (!husb_dev) {
		return -ENODEV;
	}

	if (husb238_voltage_to_volts(voltage) == 0) {
		voltage = HUSB238_PDO_20V;
	}

	target_pdo = voltage;
	pd_retry_idx = 0;
	atomic_set(&pd_settled_flag, 0);

	int ret = husb238_set_pdo(husb_dev, voltage);

	if (ret != 0) {
		LOG_WRN("husb238_set_pdo(%uV) failed: %d",
			husb238_voltage_to_volts(voltage), ret);
		k_work_reschedule(&pd_retry_work, K_MSEC(pd_retry_delays_ms[0]));
		return ret;
	}

	ret = husb238_req_pdo(husb_dev);
	if (ret != 0) {
		LOG_WRN("husb238_req_pdo failed: %d", ret);
		k_work_reschedule(&pd_retry_work, K_MSEC(pd_retry_delays_ms[0]));
		return ret;
	}

	LOG_INF("Requested PD %uV", husb238_voltage_to_volts(voltage));
	k_work_reschedule(&pd_retry_work, K_MSEC(pd_retry_delays_ms[0]));
	pd_retry_idx = 1;
	return 0;
}

enum husb238_pdo_voltage pd_manager_get_target_pdo(void)
{
	return target_pdo;
}

uint8_t pd_manager_get_target_volts(void)
{
	return husb238_voltage_to_volts(target_pdo);
}

enum husb238_pdo_voltage pd_manager_cycle_voltage(int dir)
{
	int idx = HUSB238_PDO_COUNT - 1;

	for (int i = 0; i < HUSB238_PDO_COUNT; i++) {
		if (supported_pdos[i] == target_pdo) {
			idx = i;
			break;
		}
	}

	if (dir >= 0) {
		idx = (idx + 1) % HUSB238_PDO_COUNT;
	} else {
		idx = (idx - 1 + HUSB238_PDO_COUNT) % HUSB238_PDO_COUNT;
	}

	enum husb238_pdo_voltage next = supported_pdos[idx];

	pd_manager_request_voltage(next);
	settings_set_pdo_voltage((uint8_t)next);
	return next;
}

int pd_manager_get_caps_json(char *buf, size_t buf_size)
{
	struct husb238_pdo_cap caps[HUSB238_PDO_COUNT];
	int pos = snprintf(buf, buf_size, "[");

	if (husb_dev && husb238_get_pdo_caps(husb_dev, caps) == 0) {
		bool first = true;

		for (int i = 0; i < HUSB238_PDO_COUNT; i++) {
			if (!caps[i].detected) {
				continue;
			}
			int rem = (int)buf_size - pos;

			if (rem < 32) {
				break;
			}
			pos += snprintf(buf + pos, rem, "%s{\"v\":%u,\"ma\":%u}",
					first ? "" : ",",
					husb238_voltage_to_volts(caps[i].voltage),
					caps[i].max_current_ma);
			first = false;
		}
	}
	pos += snprintf(buf + pos, buf_size - pos, "]");
	return pos;
}

/* ── Shell 命令 ─────────────────────────────────────────────────────── */

static int cmd_pd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!husb_dev) {
		shell_error(sh, "HUSB238 not initialized");
		return -ENODEV;
	}

	struct husb238_status0 st0;
	struct husb238_status1 st1;
	struct husb238_pdo_cap caps[HUSB238_PDO_COUNT];

	if (husb238_get_status0(husb_dev, &st0) == 0) {
		uint16_t ma = husb238_current_code_to_ma((uint8_t)st0.src_pd_current);

		shell_print(sh, "Contract: %uV %u.%02uA (target %uV)",
			    husb238_voltage_to_volts(st0.src_pd_voltage),
			    ma / 1000, (ma % 1000) / 10,
			    pd_manager_get_target_volts());
	} else {
		shell_warn(sh, "Failed to read STATUS0");
	}

	if (husb238_get_status1(husb_dev, &st1) == 0) {
		shell_print(sh, "Attach=%u CC=%u Resp=%u",
			    st1.attach, st1.cc_dir, st1.pd_response);
	}

	if (husb238_get_pdo_caps(husb_dev, caps) == 0) {
		for (int i = 0; i < HUSB238_PDO_COUNT; i++) {
			if (caps[i].detected) {
				shell_print(sh, "  PDO %2uV: %u.%02uA",
					    husb238_voltage_to_volts(caps[i].voltage),
					    caps[i].max_current_ma / 1000,
					    (caps[i].max_current_ma % 1000) / 10);
			}
		}
	}
	return 0;
}

static int cmd_pd_set(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_error(sh, "Usage: pd set <5|9|12|15|18|20>");
		return -EINVAL;
	}

	long volts = strtol(argv[1], NULL, 10);
	enum husb238_pdo_voltage pdo = husb238_volts_to_pdo((uint8_t)volts);

	if (pdo == HUSB238_PDO_NOT_SELECT) {
		shell_error(sh, "Unsupported PD voltage: %ld", volts);
		return -EINVAL;
	}

	int ret = pd_manager_request_voltage(pdo);

	if (ret == 0) {
		settings_set_pdo_voltage((uint8_t)pdo);
		shell_print(sh, "Requested and saved PD %ldV", volts);
	} else {
		shell_error(sh, "Failed to request PD %ldV: %d", volts, ret);
	}
	return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_pd,
	SHELL_CMD(status, NULL, "Show HUSB238 PD status and caps", cmd_pd_status),
	SHELL_CMD(set, NULL, "Request PD voltage: pd set <5|9|12|15|18|20>", cmd_pd_set),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(pd, &sub_pd, "USB-PD HUSB238 commands", NULL);
