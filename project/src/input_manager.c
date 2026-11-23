/*
 * Copyright (c) 2024 umi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "input_manager.h"
#include "display_manager.h"
#include "fan_ctrl.h"
#include "settings_manager.h"
#include "wifi_manager.h"

#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(input_mgr, LOG_LEVEL_INF);

#define PRESS_SHORT_MS     300
#define DIR_SHORT_MS       350
#define DIR_LONG_MS        700
#define DIR_REPEAT_MS      100

struct key_state {
	bool pressed;
	bool swallow_release;
	int64_t press_ms;
	struct k_work_delayable repeat_work;
};

static struct key_state key_press;
static struct key_state key_left;
static struct key_state key_right;

static void change_fan_speed(int32_t delta)
{
	int32_t target = (int32_t)fan_ctrl_get_target_speed() + delta;

	if (target < 0) {
		target = 0;
	} else if (target > (int32_t)FAN_MAX_TARGET_RPM) {
		target = (int32_t)FAN_MAX_TARGET_RPM;
	}
	fan_ctrl_set_target_speed((uint16_t)target);
	display_manager_request_redraw();
}

static void dir_repeat_handler(struct k_work *work, int dir, struct key_state *st)
{
	ARG_UNUSED(work);

	if (!st->pressed) {
		return;
	}

	if (!display_manager_is_wifi_page()) {
		change_fan_speed(dir * 200);
	}
	k_work_schedule(&st->repeat_work, K_MSEC(DIR_REPEAT_MS));
}

static void left_repeat_work_fn(struct k_work *work)
{
	dir_repeat_handler(work, -1, &key_left);
}

static void right_repeat_work_fn(struct k_work *work)
{
	dir_repeat_handler(work, 1, &key_right);
}

static void handle_press_release(int64_t held_ms)
{
	if (held_ms >= PRESS_SHORT_MS) {
		/* 长按丝滑切换页面 */
		display_manager_toggle_page();
		return;
	}

	if (display_manager_is_wifi_page()) {
		struct wifi_info info;

		wifi_manager_get_info(&info);
		/* 第二页面为 AP 模式且存在已保存 WiFi 配置时，短按触发重连 */
		if (info.state == WIFI_STATE_AP && info.has_saved_creds) {
			LOG_INF("Short press on WiFi page (AP mode): reconnecting %s...",
				info.saved_ssid);
			wifi_manager_reconnect();
			display_manager_request_redraw();
		}
	} else {
		/* 主页面保持原逻辑：短按开关风扇 */
		fan_ctrl_turn_switch();
		settings_set_fan_switch(fan_ctrl_get_switch());
		display_manager_request_redraw();
	}
}

static void handle_dir_release(int dir, int64_t held_ms)
{
	if (display_manager_is_wifi_page()) {
		return;
	}

	if (held_ms < DIR_SHORT_MS) {
		change_fan_speed(dir * 100);
	} else if (held_ms < DIR_LONG_MS) {
		change_fan_speed(dir * 1000);
	}

	settings_set_fan_speed(fan_ctrl_get_target_speed());
}

static void button_input_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type != INPUT_EV_KEY) {
		return;
	}

	int64_t now = k_uptime_get();
	bool pressed = (evt->value != 0);

	switch (evt->code) {
	case INPUT_KEY_ENTER:
		if (pressed) {
			key_press.pressed = true;
			key_press.swallow_release = display_manager_wake();
			key_press.press_ms = now;
		} else if (key_press.pressed) {
			bool swallow = key_press.swallow_release;

			key_press.pressed = false;
			key_press.swallow_release = false;
			if (!swallow) {
				handle_press_release(now - key_press.press_ms);
			}
		}
		break;

	case INPUT_KEY_LEFT:
		if (pressed) {
			bool was_off = display_manager_wake();

			key_left.pressed = true;
			key_left.swallow_release = was_off;
			key_left.press_ms = now;
			if (!was_off) {
				k_work_schedule(&key_left.repeat_work, K_MSEC(DIR_LONG_MS));
			}
		} else if (key_left.pressed) {
			bool swallow = key_left.swallow_release;

			key_left.pressed = false;
			key_left.swallow_release = false;
			k_work_cancel_delayable(&key_left.repeat_work);
			if (!swallow) {
				handle_dir_release(-1, now - key_left.press_ms);
			}
		}
		break;

	case INPUT_KEY_RIGHT:
		if (pressed) {
			bool was_off = display_manager_wake();

			key_right.pressed = true;
			key_right.swallow_release = was_off;
			key_right.press_ms = now;
			if (!was_off) {
				k_work_schedule(&key_right.repeat_work, K_MSEC(DIR_LONG_MS));
			}
		} else if (key_right.pressed) {
			bool swallow = key_right.swallow_release;

			key_right.pressed = false;
			key_right.swallow_release = false;
			k_work_cancel_delayable(&key_right.repeat_work);
			if (!swallow) {
				handle_dir_release(1, now - key_right.press_ms);
			}
		}
		break;

	default:
		break;
	}
}

INPUT_CALLBACK_DEFINE(NULL, button_input_cb, NULL);

int input_manager_init(void)
{
	k_work_init_delayable(&key_left.repeat_work, left_repeat_work_fn);
	k_work_init_delayable(&key_right.repeat_work, right_repeat_work_fn);
	LOG_INF("Input manager initialized");
	return 0;
}
