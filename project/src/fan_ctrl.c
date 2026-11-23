/*
 * Copyright (c) 2024 umi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fan_ctrl.h"
#include "settings_manager.h"

#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(fan_ctrl, LOG_LEVEL_INF);

#define FAN_TASK_CYCLE_MS    50
#define FAN_STACK_SIZE       2048
#define FAN_THREAD_PRIORITY  5
#define ADC_AVG_SAMPLES      16
#define TACH_TIMEOUT_MS      1000

/* ── DeviceTree 硬件规格 ───────────────────────────────────────────── */

static const struct gpio_dt_spec fan_power_gpio =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), fan_power_gpios);
static const struct gpio_dt_spec fan_tach_gpio =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), fan_tach_gpios);
static const struct pwm_dt_spec fan_pwm =
	PWM_DT_SPEC_GET(DT_PATH(zephyr_user));

static const struct adc_dt_spec adc_current_spec =
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
static const struct adc_dt_spec adc_fan_v_spec =
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);
static const struct adc_dt_spec adc_in_v_spec =
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2);

/* ── PID 控制器（参数与原 ESP-IDF 工程一致） ──────────────────────── */

struct pid_ctrl {
	float p;
	float i;
	float d;
	float i_min;
	float i_max;
	float diff;
	float diff_sum;
	float target_val;
};

static struct pid_ctrl fan_pid = {
	.p = 0.0008f,
	.i = 0.0f,
	.d = 0.0005f,
	.i_min = -0.5f,
	.i_max = 0.5f,
};

static float clamp_f(float v, float min_v, float max_v)
{
	if (v < min_v) {
		return min_v;
	}
	if (v > max_v) {
		return max_v;
	}
	return v;
}

static void pid_reset(struct pid_ctrl *pid)
{
	pid->diff = 0.0f;
	pid->diff_sum = 0.0f;
}

static float pid_calc(struct pid_ctrl *pid, float now_val)
{
	float diff_change = pid->target_val - now_val - pid->diff;

	pid->diff += diff_change;
	pid->diff_sum = clamp_f(pid->diff_sum + pid->diff, pid->i_min, pid->i_max);

	return pid->diff * pid->p + pid->diff_sum * pid->i + diff_change * pid->d;
}

/* ── 状态变量 ──────────────────────────────────────────────────────── */

static bool fan_power_switch;
static float current_duty_cycle;

static int32_t filter_voltage_mv;
static int32_t filter_current_ma;
static int32_t filter_power_mw;

/* 测速状态 */
static struct gpio_callback tach_cb;
static uint32_t tach_last_cyc;
static bool tach_has_prev;
static atomic_t tach_rpm_atomic = ATOMIC_INIT(0);
static atomic_t tach_last_ms_atomic = ATOMIC_INIT(0);

static K_SEM_DEFINE(fan_start_sem, 0, 1);

static void tach_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	uint32_t now_cyc = k_cycle_get_32();

	if (tach_has_prev) {
		uint32_t diff_cyc = now_cyc - tach_last_cyc;
		uint64_t diff_us = k_cyc_to_us_near64(diff_cyc);

		/* 过滤毛刺脉冲 (<500us 对应 >60000RPM) */
		if (diff_us >= 500U) {
			uint32_t rpm = (uint32_t)(0.5f * (60000000.0f / (float)diff_us));

			atomic_set(&tach_rpm_atomic, (atomic_val_t)rpm);
			atomic_set(&tach_last_ms_atomic, (atomic_val_t)k_uptime_get_32());
			tach_last_cyc = now_cyc;
		}
	} else {
		tach_last_cyc = now_cyc;
		tach_has_prev = true;
		atomic_set(&tach_last_ms_atomic, (atomic_val_t)k_uptime_get_32());
	}
}

static int read_adc_avg_mv(const struct adc_dt_spec *spec, int32_t *out_mv)
{
	uint16_t raw_buf = 0;
	struct adc_sequence seq = {
		.buffer = &raw_buf,
		.buffer_size = sizeof(raw_buf),
	};
	int32_t sum_mv = 0;
	int valid = 0;

	adc_sequence_init_dt(spec, &seq);

	for (int i = 0; i < ADC_AVG_SAMPLES; i++) {
		int err = adc_read_dt(spec, &seq);

		if (err == 0) {
			int32_t mv = (int32_t)raw_buf;

			err = adc_raw_to_millivolts_dt(spec, &mv);
			if (err == 0) {
				sum_mv += mv;
				valid++;
			}
		}
	}

	if (valid == 0) {
		return -EIO;
	}

	*out_mv = sum_mv / valid;
	return 0;
}

static void fan_set_duty_cycle(float duty_pct)
{
	current_duty_cycle = clamp_f(duty_pct, 0.0f, 100.0f);
	uint32_t pulse_ns = (uint32_t)(((double)fan_pwm.period * (double)current_duty_cycle) / 100.0);
	int err = pwm_set_pulse_dt(&fan_pwm, pulse_ns);

	if (err != 0) {
		LOG_ERR("pwm_set_pulse_dt failed: %d", err);
	}
}

static void fan_read_power(void)
{
	int32_t raw_v_mv = 0;
	int32_t raw_i_mv = 0;

	if (read_adc_avg_mv(&adc_in_v_spec, &raw_v_mv) == 0) {
		int32_t voltage_mv = (raw_v_mv * 27500) / 2500;

		filter_voltage_mv = (filter_voltage_mv * 3 + voltage_mv) / 4;
	}

	if (read_adc_avg_mv(&adc_current_spec, &raw_i_mv) == 0) {
		int32_t current_ma = (raw_i_mv * 2020) / 2500;

		filter_current_ma = (filter_current_ma * 3 + current_ma) / 4;
	}

	filter_power_mw = (filter_current_ma * filter_voltage_mv) / 1000;
}

static void fan_thread_fn(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	k_sem_take(&fan_start_sem, K_FOREVER);

	while (1) {
		k_sleep(K_MSEC(FAN_TASK_CYCLE_MS));

		/* 超时未收到脉冲则转速归零 */
		uint32_t now_ms = k_uptime_get_32();
		uint32_t last_ms = (uint32_t)atomic_get(&tach_last_ms_atomic);

		if ((now_ms - last_ms) > TACH_TIMEOUT_MS) {
			atomic_set(&tach_rpm_atomic, 0);
		}

		fan_read_power();

		if (fan_power_switch) {
			float delta = pid_calc(&fan_pid, (float)fan_ctrl_get_speed());

			fan_set_duty_cycle(current_duty_cycle + delta);
		} else {
			pid_reset(&fan_pid);
			fan_set_duty_cycle(0.0f);
		}
	}
}

K_THREAD_DEFINE(fan_thread_id, FAN_STACK_SIZE, fan_thread_fn,
		NULL, NULL, NULL, FAN_THREAD_PRIORITY, 0, 0);

/* ── 公共 API ──────────────────────────────────────────────────────── */

int fan_ctrl_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&fan_power_gpio)) {
		LOG_ERR("Fan power GPIO not ready");
		return -ENODEV;
	}
	/* 配置开漏输出，初始关闭供电，由 fan_ctrl_set_switch 联动控制 */
	err = gpio_pin_configure_dt(&fan_power_gpio,
				      fan_power_switch ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		LOG_ERR("Fan power GPIO config failed: %d", err);
		return err;
	}

	if (!gpio_is_ready_dt(&fan_tach_gpio)) {
		LOG_ERR("Fan tach GPIO not ready");
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&fan_tach_gpio, GPIO_INPUT);
	if (err != 0) {
		LOG_ERR("Fan tach GPIO config failed: %d", err);
		return err;
	}

	gpio_init_callback(&tach_cb, tach_isr, BIT(fan_tach_gpio.pin));
	err = gpio_add_callback(fan_tach_gpio.port, &tach_cb);
	if (err != 0) {
		LOG_ERR("Tach add_callback failed: %d", err);
		return err;
	}
	err = gpio_pin_interrupt_configure_dt(&fan_tach_gpio, GPIO_INT_EDGE_RISING);
	if (err != 0) {
		LOG_ERR("Tach int config failed: %d", err);
		return err;
	}

	if (!pwm_is_ready_dt(&fan_pwm)) {
		LOG_ERR("Fan PWM device not ready");
		return -ENODEV;
	}
	fan_set_duty_cycle(0.0f);

	const struct adc_dt_spec *adcs[] = {
		&adc_current_spec,
		&adc_fan_v_spec,
		&adc_in_v_spec,
	};

	for (size_t i = 0; i < ARRAY_SIZE(adcs); i++) {
		if (!adc_is_ready_dt(adcs[i])) {
			LOG_ERR("ADC channel %zu not ready", i);
			return -ENODEV;
		}
		err = adc_channel_setup_dt(adcs[i]);
		if (err != 0) {
			LOG_ERR("ADC channel %zu setup failed: %d", i, err);
			return err;
		}
	}

	k_sem_give(&fan_start_sem);
	LOG_INF("Fan controller initialized");
	return 0;
}

void fan_ctrl_set_switch(bool sw)
{
	if (fan_power_switch != sw) {
		fan_power_switch = sw;
		gpio_pin_set_dt(&fan_power_gpio, sw ? 1 : 0);
		LOG_INF("Fan power %s", sw ? "ON" : "OFF");
	}
}

bool fan_ctrl_get_switch(void)
{
	return fan_power_switch;
}

void fan_ctrl_turn_switch(void)
{
	fan_ctrl_set_switch(!fan_power_switch);
}

void fan_ctrl_set_target_speed(uint16_t rpm)
{
	if (rpm > FAN_MAX_TARGET_RPM) {
		rpm = FAN_MAX_TARGET_RPM;
	}
	fan_pid.target_val = (float)rpm;
}

uint16_t fan_ctrl_get_target_speed(void)
{
	return (uint16_t)fan_pid.target_val;
}

uint16_t fan_ctrl_get_speed(void)
{
	return (uint16_t)atomic_get(&tach_rpm_atomic);
}

float fan_ctrl_get_voltage(void)
{
	return (float)filter_voltage_mv / 1000.0f;
}

float fan_ctrl_get_current(void)
{
	return (float)filter_current_ma / 1000.0f;
}

float fan_ctrl_get_power(void)
{
	return (float)filter_power_mw / 1000.0f;
}

float fan_ctrl_get_duty_cycle(void)
{
	return current_duty_cycle;
}

void fan_ctrl_set_pid(float kp, float ki, float kd)
{
	fan_pid.p = kp;
	fan_pid.i = ki;
	fan_pid.d = kd;
	pid_reset(&fan_pid);
}

void fan_ctrl_get_pid(float *kp, float *ki, float *kd)
{
	if (kp) {
		*kp = fan_pid.p;
	}
	if (ki) {
		*ki = fan_pid.i;
	}
	if (kd) {
		*kd = fan_pid.d;
	}
}

/* ── Shell 命令 ─────────────────────────────────────────────────────── */

static int cmd_fan_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Switch: %s, Duty: %.1f%%",
		    fan_ctrl_get_switch() ? "ON" : "OFF",
		    (double)fan_ctrl_get_duty_cycle());
	shell_print(sh, "Target: %u RPM, Actual: %u RPM",
		    fan_ctrl_get_target_speed(),
		    fan_ctrl_get_speed());
	shell_print(sh, "U=%.2fV, I=%.2fA, P=%.2fW",
		    (double)fan_ctrl_get_voltage(),
		    (double)fan_ctrl_get_current(),
		    (double)fan_ctrl_get_power());
	return 0;
}

static int cmd_fan_switch(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_error(sh, "Usage: fan switch <on|off>");
		return -EINVAL;
	}

	bool on = (!strcmp(argv[1], "on") || !strcmp(argv[1], "1"));

	fan_ctrl_set_switch(on);
	settings_set_fan_switch(on);
	shell_print(sh, "Fan switch set to %s", on ? "ON" : "OFF");
	return 0;
}

static int cmd_fan_speed(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_error(sh, "Usage: fan speed <0..8000>");
		return -EINVAL;
	}

	long rpm = strtol(argv[1], NULL, 10);

	if (rpm < 0) {
		rpm = 0;
	} else if (rpm > FAN_MAX_TARGET_RPM) {
		rpm = FAN_MAX_TARGET_RPM;
	}

	fan_ctrl_set_target_speed((uint16_t)rpm);
	settings_set_fan_speed((uint16_t)rpm);
	shell_print(sh, "Target speed set to %ld RPM", rpm);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_fan,
	SHELL_CMD(status, NULL, "Show fan status", cmd_fan_status),
	SHELL_CMD(switch, NULL, "Set fan switch: fan switch <on|off>", cmd_fan_switch),
	SHELL_CMD(speed, NULL, "Set target speed: fan speed <rpm>", cmd_fan_speed),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(fan, &sub_fan, "Fan control commands", NULL);
