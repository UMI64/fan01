/*
 * Copyright (c) 2024 umi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WIFI_MANAGER_H_
#define WIFI_MANAGER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_AP_SSID      "FAN01"
#define WIFI_AP_PSK_LEN   8
#define WIFI_AP_IP        "192.168.4.1"
#define WIFI_MAX_PROFILES 5

enum wifi_state {
	WIFI_STATE_IDLE = 0,
	WIFI_STATE_CONNECTING,
	WIFI_STATE_CONNECTED,
	WIFI_STATE_AP,
};

struct wifi_static_ip_cfg {
	bool enabled;
	char ip[16];
	char netmask[16];
	char gateway[16];
};

struct wifi_profile {
	char ssid[33];
	char password[65];
	struct wifi_static_ip_cfg static_ip;
};

struct wifi_info {
	enum wifi_state state;
	char ssid[33];
	char ip[16];
	char ap_psk[16];
	int8_t rssi;
	bool has_saved_creds;
	uint8_t saved_count;
	char saved_ssid[33];
	struct wifi_static_ip_cfg static_ip;
};

int wifi_manager_init(void);
enum wifi_state wifi_manager_get_state(void);
void wifi_manager_get_info(struct wifi_info *info);
bool wifi_manager_is_connected(void);

/* 异步触发按优先级顺序重连已保存的 WiFi 列表（若无保存信息则保持 AP） */
int wifi_manager_reconnect(void);

/* 供 Web / Shell 使用的接口 */
int wifi_manager_get_status_json(char *buf, size_t buf_size);
int wifi_manager_start_scan(void);
int wifi_manager_get_scan_json(char *buf, size_t buf_size);
int wifi_manager_get_profiles_json(char *buf, size_t buf_size);
int wifi_manager_save_profile(const char *orig_ssid,
			      const struct wifi_profile *prof,
			      bool keep_psk);
int wifi_manager_delete_profile(int index);
int wifi_manager_move_profile(int index, int delta);
int wifi_manager_connect_profile(int index);
int wifi_manager_connect_ssid(const char *ssid);
int wifi_manager_connect_ex(const char *ssid, const char *password,
			    const struct wifi_static_ip_cfg *static_cfg);
int wifi_manager_connect(const char *ssid, const char *password);
int wifi_manager_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H_ */
