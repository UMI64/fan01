/*
 * Copyright (c) 2024 umi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/brownout_ll.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "display_manager.h"
#include "fan_ctrl.h"
#include "input_manager.h"
#include "pd_manager.h"
#include "settings_manager.h"
#include "web_server.h"
#include "wifi_manager.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static int early_brownout_cfg(void)
{
	/* 与原 ESP-IDF sdkconfig 对齐：关闭无消抖硬件模拟 BOD 复位，设为最低阈值 7 (2.51V) 并延长消抖周期 */
	brownout_ll_ana_reset_enable(false);
	brownout_ll_set_threshold(7);
	brownout_ll_reset_config(true, 0x3ff, BROWNOUT_RESET_LEVEL_SYSTEM);
	brownout_ll_bod_enable(true);
	return 0;
}

SYS_INIT(early_brownout_cfg, PRE_KERNEL_1, 0);

int main(void)
{
	LOG_INF("FAN01 starting (Zephyr)...");

	settings_manager_init();

	float kp, ki, kd;

	settings_get_pid(&kp, &ki, &kd);
	fan_ctrl_set_pid(kp, ki, kd);

	fan_ctrl_init();
	pd_manager_init((enum husb238_pdo_voltage)settings_get_pdo_voltage());
	display_manager_init();

	/* 等待 Type-C PD 电压协商完成（最多 800ms），避免在 5V 切换期触发 Wi-Fi 射频校准峰值电流导致欠压复位 */
	for (int i = 0; i < 16 && !pd_manager_is_settled(); i++) {
		k_msleep(50);
	}

	if (settings_get_fan_speed_use_storage()) {
		fan_ctrl_set_target_speed(settings_get_fan_speed());
		fan_ctrl_set_switch(settings_get_fan_switch());
	}

	wifi_manager_init();
	web_server_start();
	input_manager_init();

	/* MCUboot OTA 确认：当前镜像正常启动后标记为已确认 */
	if (!boot_is_img_confirmed()) {
		int rc = boot_write_img_confirmed();

		if (rc == 0) {
			LOG_INF("MCUboot image confirmed");
		} else {
			LOG_WRN("boot_write_img_confirmed failed: %d", rc);
		}
	}

	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
