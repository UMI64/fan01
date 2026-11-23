/*
 * Copyright (c) 2024 umi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DISPLAY_MANAGER_H_
#define DISPLAY_MANAGER_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int display_manager_init(void);
void display_manager_toggle_page(void);
bool display_manager_is_wifi_page(void);
void display_manager_request_redraw(void);

/* 唤醒屏幕并重置熄屏计时；若唤醒前处于熄屏状态则返回 true */
bool display_manager_wake(void);

/* 当 Web 修改了亮度/熄屏时间配置后通知立即应用 */
void display_manager_reload_cfg(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MANAGER_H_ */
