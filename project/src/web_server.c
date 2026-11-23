/*
 * Copyright (c) 2024 umi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "web_server.h"
#include "display_manager.h"
#include "fan_ctrl.h"
#include "pd_manager.h"
#include "settings_manager.h"
#include "wifi_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/app_version.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/version.h>

LOG_MODULE_REGISTER(web_server, LOG_LEVEL_INF);

static uint16_t web_service_port = 80;

static const uint8_t index_html_gz[] = {
#include "index.html.gz.inc"
};

static struct http_resource_detail_static index_html_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_STATIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_encoding = "gzip",
		.content_type = "text/html",
	},
	.static_data = index_html_gz,
	.static_data_len = sizeof(index_html_gz),
};

/* ── 延迟重启（确保 OTA HTTP 响应先发完） ──────────────────────────── */

static void ota_reboot_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_INF("Rebooting into new OTA image...");
	sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DELAYABLE_DEFINE(ota_reboot_work, ota_reboot_work_fn);

/* ── 简易 JSON 提取辅助函数 ────────────────────────────────────────── */

static bool extract_json_str(const char *body, const char *key,
			     char *out, size_t out_sz)
{
	if (!body || !out || out_sz == 0) {
		return false;
	}

	char pat[36];

	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(body, pat);

	if (!p) {
		return false;
	}
	p = strchr(p + strlen(pat), ':');
	if (!p) {
		return false;
	}
	p = strchr(p, '"');
	if (!p) {
		return false;
	}
	p++;

	size_t i = 0;

	while (*p && *p != '"' && i < out_sz - 1) {
		out[i++] = *p++;
	}
	out[i] = '\0';
	return true;
}

static bool extract_json_bool(const char *body, const char *key, bool *val)
{
	if (!body || !val) {
		return false;
	}

	char pat[36];

	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(body, pat);

	if (!p) {
		return false;
	}
	p = strchr(p + strlen(pat), ':');
	if (!p) {
		return false;
	}
	p++;
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	if (strncmp(p, "true", 4) == 0) {
		*val = true;
		return true;
	}
	if (strncmp(p, "false", 5) == 0) {
		*val = false;
		return true;
	}
	return false;
}

static bool extract_json_float(const char *body, const char *key, float *val)
{
	if (!body || !val) {
		return false;
	}

	char pat[36];

	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(body, pat);

	if (!p) {
		return false;
	}
	p = strchr(p + strlen(pat), ':');
	if (!p) {
		return false;
	}
	char *end = NULL;
	float v = strtof(p + 1, &end);

	if (end == p + 1) {
		return false;
	}
	*val = v;
	return true;
}

static bool extract_json_int(const char *body, const char *key, int *val)
{
	float f = 0.0f;

	if (!val || !extract_json_float(body, key, &f)) {
		return false;
	}
	*val = (int)f;
	return true;
}

static bool collect_http_body(enum http_transaction_status status,
			      const struct http_request_ctx *request_ctx,
			      char *buf, size_t buf_sz, size_t *cursor)
{
	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		*cursor = 0;
		return false;
	}

	if (request_ctx->data && request_ctx->data_len > 0) {
		size_t avail = buf_sz - 1 - *cursor;
		size_t copy_len = MIN(request_ctx->data_len, avail);

		memcpy(buf + *cursor, request_ctx->data, copy_len);
		*cursor += copy_len;
	}

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return false;
	}

	buf[*cursor] = '\0';
	*cursor = 0;
	return true;
}

static void set_json_resp(struct http_response_ctx *response_ctx, const char *body, size_t len)
{
	static const struct http_header hdr = {"Content-Type", "application/json"};

	response_ctx->body = (const uint8_t *)body;
	response_ctx->body_len = len;
	response_ctx->final_chunk = true;
	response_ctx->headers = &hdr;
	response_ctx->header_count = 1;
}

/* ── /api/device/info ─────────────────────────────────────────────── */

static int api_device_info_handler(struct http_client_ctx *client,
				   enum http_transaction_status status,
				   const struct http_request_ctx *request_ctx,
				   struct http_response_ctx *response_ctx,
				   void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	static char buf[192];
	int len = snprintf(buf, sizeof(buf),
			   "{\"name\":\"FAN01\",\"version\":\"%s\",\"zephyr\":\"%s\","
			   "\"uptime\":%" PRId64 ",\"confirmed\":%s}",
			   APP_VERSION_STRING,
			   KERNEL_VERSION_STRING,
			   (int64_t)(k_uptime_get() / 1000),
			   boot_is_img_confirmed() ? "true" : "false");

	set_json_resp(response_ctx, buf, len);
	return 0;
}

static struct http_resource_detail_dynamic api_device_info_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = api_device_info_handler,
};

/* ── /api/wifi/status, scan, connect, disconnect, reconnect ───────── */

static int api_wifi_status_handler(struct http_client_ctx *client,
				   enum http_transaction_status status,
				   const struct http_request_ctx *request_ctx,
				   struct http_response_ctx *response_ctx,
				   void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	static char buf[384];
	int len = wifi_manager_get_status_json(buf, sizeof(buf));

	set_json_resp(response_ctx, buf, len);
	return 0;
}

static struct http_resource_detail_dynamic api_wifi_status_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = api_wifi_status_handler,
};

static int api_wifi_scan_start_handler(struct http_client_ctx *client,
				       enum http_transaction_status status,
				       const struct http_request_ctx *request_ctx,
				       struct http_response_ctx *response_ctx,
				       void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	int ret = wifi_manager_start_scan();
	const char *json = (ret == 0)
		? "{\"success\":true,\"scanning\":true}"
		: "{\"success\":false,\"scanning\":false}";

	set_json_resp(response_ctx, json, strlen(json));
	return 0;
}

static struct http_resource_detail_dynamic api_wifi_scan_start_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = api_wifi_scan_start_handler,
};

static int api_wifi_scan_handler(struct http_client_ctx *client,
				 enum http_transaction_status status,
				 const struct http_request_ctx *request_ctx,
				 struct http_response_ctx *response_ctx,
				 void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	static char buf[2048];
	int len = wifi_manager_get_scan_json(buf, sizeof(buf));

	if (len < 0) {
		const char *err = "{\"scanning\":false,\"ap_list\":[]}";

		set_json_resp(response_ctx, err, strlen(err));
	} else {
		set_json_resp(response_ctx, buf, len);
	}
	return 0;
}

static struct http_resource_detail_dynamic api_wifi_scan_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = api_wifi_scan_handler,
};

static int api_wifi_profiles_handler(struct http_client_ctx *client,
				     enum http_transaction_status status,
				     const struct http_request_ctx *request_ctx,
				     struct http_response_ctx *response_ctx,
				     void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	static char buf[1280];
	int len = wifi_manager_get_profiles_json(buf, sizeof(buf));

	if (len < 0) {
		const char *err = "{\"max\":5,\"profiles\":[]}";

		set_json_resp(response_ctx, err, strlen(err));
	} else {
		set_json_resp(response_ctx, buf, len);
	}
	return 0;
}

static struct http_resource_detail_dynamic api_wifi_profiles_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = api_wifi_profiles_handler,
};

static const char *profile_err_json(int ret)
{
	switch (ret) {
	case 0:
		return "{\"success\":true}";
	case -EINVAL:
		return "{\"success\":false,\"message\":\"Invalid SSID or static IP\"}";
	case -ENOSPC:
		return "{\"success\":false,\"message\":\"Max profiles reached\"}";
	case -EEXIST:
		return "{\"success\":false,\"message\":\"SSID already exists\"}";
	case -EBUSY:
		return "{\"success\":false,\"message\":\"Busy\"}";
	default:
		return "{\"success\":false,\"message\":\"Failed\"}";
	}
}

static void parse_profile_fields(const char *body, char *orig_ssid, size_t orig_sz,
				 struct wifi_profile *prof, bool *keep_psk)
{
	memset(prof, 0, sizeof(*prof));
	orig_ssid[0] = '\0';
	*keep_psk = false;

	extract_json_str(body, "orig_ssid", orig_ssid, orig_sz);
	extract_json_str(body, "ssid", prof->ssid, sizeof(prof->ssid));
	extract_json_str(body, "password", prof->password, sizeof(prof->password));
	extract_json_bool(body, "keep_psk", keep_psk);
	extract_json_bool(body, "static_en", &prof->static_ip.enabled);
	extract_json_str(body, "static_ip", prof->static_ip.ip,
			 sizeof(prof->static_ip.ip));
	extract_json_str(body, "netmask", prof->static_ip.netmask,
			 sizeof(prof->static_ip.netmask));
	extract_json_str(body, "gateway", prof->static_ip.gateway,
			 sizeof(prof->static_ip.gateway));
}

static int api_wifi_profile_save_handler(struct http_client_ctx *client,
					 enum http_transaction_status status,
					 const struct http_request_ctx *request_ctx,
					 struct http_response_ctx *response_ctx,
					 void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	static char body[512];
	static size_t cursor;

	if (!collect_http_body(status, request_ctx, body, sizeof(body), &cursor)) {
		return 0;
	}

	char orig_ssid[33];
	struct wifi_profile prof;
	bool keep_psk;

	parse_profile_fields(body, orig_ssid, sizeof(orig_ssid), &prof, &keep_psk);

	int ret = (prof.ssid[0] == '\0')
			  ? -EINVAL
			  : wifi_manager_save_profile(orig_ssid, &prof, keep_psk);
	const char *json = profile_err_json(ret);

	set_json_resp(response_ctx, json, strlen(json));
	return 0;
}

static struct http_resource_detail_dynamic api_wifi_profile_save_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = api_wifi_profile_save_handler,
};

static int api_wifi_profile_move_handler(struct http_client_ctx *client,
					 enum http_transaction_status status,
					 const struct http_request_ctx *request_ctx,
					 struct http_response_ctx *response_ctx,
					 void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	static char body[128];
	static size_t cursor;

	if (!collect_http_body(status, request_ctx, body, sizeof(body), &cursor)) {
		return 0;
	}

	int idx = -1, dir = 0;

	extract_json_int(body, "index", &idx);
	extract_json_int(body, "dir", &dir);

	int ret = wifi_manager_move_profile(idx, dir);
	const char *json = profile_err_json(ret);

	set_json_resp(response_ctx, json, strlen(json));
	return 0;
}

static struct http_resource_detail_dynamic api_wifi_profile_move_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = api_wifi_profile_move_handler,
};

static int api_wifi_profile_delete_handler(struct http_client_ctx *client,
					   enum http_transaction_status status,
					   const struct http_request_ctx *request_ctx,
					   struct http_response_ctx *response_ctx,
					   void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	static char body[128];
	static size_t cursor;

	if (!collect_http_body(status, request_ctx, body, sizeof(body), &cursor)) {
		return 0;
	}

	int idx = -1;

	extract_json_int(body, "index", &idx);

	int ret = wifi_manager_delete_profile(idx);
	const char *json = profile_err_json(ret);

	set_json_resp(response_ctx, json, strlen(json));
	return 0;
}

static struct http_resource_detail_dynamic api_wifi_profile_delete_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = api_wifi_profile_delete_handler,
};

static int api_wifi_connect_handler(struct http_client_ctx *client,
				    enum http_transaction_status status,
				    const struct http_request_ctx *request_ctx,
				    struct http_response_ctx *response_ctx,
				    void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	static char body[512];
	static size_t cursor;

	if (!collect_http_body(status, request_ctx, body, sizeof(body), &cursor)) {
		return 0;
	}

	char orig_ssid[33];
	struct wifi_profile prof;
	bool keep_psk;
	int idx = -1;
	int ret;

	parse_profile_fields(body, orig_ssid, sizeof(orig_ssid), &prof, &keep_psk);
	if (prof.ssid[0] == '\0' && extract_json_int(body, "index", &idx)) {
		ret = wifi_manager_connect_profile(idx);
	} else if (prof.ssid[0] == '\0') {
		ret = -EINVAL;
	} else {
		ret = wifi_manager_save_profile(orig_ssid, &prof, keep_psk);
		if (ret == 0) {
			ret = wifi_manager_connect_ssid(prof.ssid);
		}
	}

	const char *json = profile_err_json(ret);

	set_json_resp(response_ctx, json, strlen(json));
	return 0;
}

static struct http_resource_detail_dynamic api_wifi_connect_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = api_wifi_connect_handler,
};

static int api_wifi_disconnect_handler(struct http_client_ctx *client,
				       enum http_transaction_status status,
				       const struct http_request_ctx *request_ctx,
				       struct http_response_ctx *response_ctx,
				       void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	wifi_manager_disconnect();
	const char *json = "{\"success\":true}";

	set_json_resp(response_ctx, json, strlen(json));
	return 0;
}

static struct http_resource_detail_dynamic api_wifi_disconnect_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = api_wifi_disconnect_handler,
};

static int api_wifi_reconnect_handler(struct http_client_ctx *client,
				      enum http_transaction_status status,
				      const struct http_request_ctx *request_ctx,
				      struct http_response_ctx *response_ctx,
				      void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	int ret = wifi_manager_reconnect();
	const char *json = (ret == 0) ? "{\"success\":true}" : "{\"success\":false}";

	set_json_resp(response_ctx, json, strlen(json));
	return 0;
}

static struct http_resource_detail_dynamic api_wifi_reconnect_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = api_wifi_reconnect_handler,
};

/* ── /api/fan/status & /api/fan/control ───────────────────────────── */

static int api_fan_status_handler(struct http_client_ctx *client,
				  enum http_transaction_status status,
				  const struct http_request_ctx *request_ctx,
				  struct http_response_ctx *response_ctx,
				  void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	static char buf[224];
	int len = snprintf(buf, sizeof(buf),
			   "{\"rpm\":%u,\"target_rpm\":%u,\"switch\":%s,"
			   "\"voltage\":%.2f,\"current\":%.2f,\"power\":%.2f,"
			   "\"duty\":%.1f,\"pd_volts\":%u}",
			   fan_ctrl_get_speed(),
			   fan_ctrl_get_target_speed(),
			   fan_ctrl_get_switch() ? "true" : "false",
			   (double)fan_ctrl_get_voltage(),
			   (double)fan_ctrl_get_current(),
			   (double)fan_ctrl_get_power(),
			   (double)fan_ctrl_get_duty_cycle(),
			   pd_manager_get_target_volts());

	set_json_resp(response_ctx, buf, len);
	return 0;
}

static struct http_resource_detail_dynamic api_fan_status_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = api_fan_status_handler,
};

static int api_fan_control_handler(struct http_client_ctx *client,
				   enum http_transaction_status status,
				   const struct http_request_ctx *request_ctx,
				   struct http_response_ctx *response_ctx,
				   void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	static char body[160];
	static size_t cursor;

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		cursor = 0;
		return 0;
	}

	if (request_ctx->data && request_ctx->data_len > 0) {
		size_t avail = sizeof(body) - 1 - cursor;
		size_t copy_len = MIN(request_ctx->data_len, avail);

		memcpy(body + cursor, request_ctx->data, copy_len);
		cursor += copy_len;
	}

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	body[cursor] = '\0';
	cursor = 0;

	bool sw;

	if (extract_json_bool(body, "switch", &sw)) {
		fan_ctrl_set_switch(sw);
		settings_set_fan_switch(sw);
		display_manager_wake();
	}

	float rpm_f;

	if (extract_json_float(body, "target_rpm", &rpm_f)) {
		long rpm = (long)rpm_f;

		if (rpm < 0) {
			rpm = 0;
		} else if (rpm > (long)FAN_MAX_TARGET_RPM) {
			rpm = (long)FAN_MAX_TARGET_RPM;
		}
		fan_ctrl_set_target_speed((uint16_t)rpm);
		settings_set_fan_speed((uint16_t)rpm);
		display_manager_wake();
	}

	const char *json = "{\"success\":true}";

	set_json_resp(response_ctx, json, strlen(json));
	return 0;
}

static struct http_resource_detail_dynamic api_fan_control_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = api_fan_control_handler,
};

/* ── /api/config (PD 电压、开机记忆、PID 参数) ─────────────────────── */

static int api_config_handler(struct http_client_ctx *client,
			      enum http_transaction_status status,
			      const struct http_request_ctx *request_ctx,
			      struct http_response_ctx *response_ctx,
			      void *user_data)
{
	ARG_UNUSED(user_data);

	static char body[384];
	static size_t cursor;

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		cursor = 0;
		return 0;
	}

	if (client->method == HTTP_GET) {
		if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
			return 0;
		}

		static char buf[512];
		char caps_buf[160];
		float kp, ki, kd;
		uint32_t full_s, dim_s;
		uint8_t full_br, dim_br;

		fan_ctrl_get_pid(&kp, &ki, &kd);
		pd_manager_get_caps_json(caps_buf, sizeof(caps_buf));
		settings_get_display_cfg(&full_s, &dim_s, &full_br, &dim_br);

		int len = snprintf(buf, sizeof(buf),
				   "{\"pd_volts\":%u,\"mem_enabled\":%s,"
				   "\"kp\":%.6f,\"ki\":%.6f,\"kd\":%.6f,"
				   "\"disp_full_s\":%" PRIu32 ",\"disp_dim_s\":%" PRIu32 ","
				   "\"disp_full_bright\":%u,\"disp_dim_bright\":%u,"
				   "\"pd_caps\":%s}",
				   pd_manager_get_target_volts(),
				   settings_get_fan_speed_use_storage() ? "true" : "false",
				   (double)kp, (double)ki, (double)kd,
				   full_s, dim_s, (unsigned int)full_br, (unsigned int)dim_br, caps_buf);

		set_json_resp(response_ctx, buf, len);
		return 0;
	}

	/* POST 更新配置 */
	if (request_ctx->data && request_ctx->data_len > 0) {
		size_t avail = sizeof(body) - 1 - cursor;
		size_t copy_len = MIN(request_ctx->data_len, avail);

		memcpy(body + cursor, request_ctx->data, copy_len);
		cursor += copy_len;
	}

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	body[cursor] = '\0';
	cursor = 0;

	float v;

	if (extract_json_float(body, "pd_volts", &v)) {
		enum husb238_pdo_voltage pdo = husb238_volts_to_pdo((uint8_t)v);

		if (pdo != HUSB238_PDO_NOT_SELECT) {
			pd_manager_request_voltage(pdo);
			settings_set_pdo_voltage((uint8_t)pdo);
		}
	}

	bool mem;

	if (extract_json_bool(body, "mem_enabled", &mem)) {
		settings_set_fan_speed_use_storage(mem);
	}

	float kp, ki, kd;
	bool pid_changed = false;

	fan_ctrl_get_pid(&kp, &ki, &kd);
	if (extract_json_float(body, "kp", &kp)) {
		pid_changed = true;
	}
	if (extract_json_float(body, "ki", &ki)) {
		pid_changed = true;
	}
	if (extract_json_float(body, "kd", &kd)) {
		pid_changed = true;
	}
	if (pid_changed) {
		fan_ctrl_set_pid(kp, ki, kd);
		settings_set_pid(kp, ki, kd);
	}

	uint32_t full_s, dim_s;
	uint8_t full_br, dim_br;
	bool disp_changed = false;
	float dv;

	settings_get_display_cfg(&full_s, &dim_s, &full_br, &dim_br);
	if (extract_json_float(body, "disp_full_s", &dv)) {
		full_s = (dv < 0) ? 0 : (uint32_t)dv;
		disp_changed = true;
	}
	if (extract_json_float(body, "disp_dim_s", &dv)) {
		dim_s = (dv < 0) ? 0 : (uint32_t)dv;
		disp_changed = true;
	}
	if (extract_json_float(body, "disp_full_bright", &dv)) {
		full_br = (dv < 1) ? 1 : ((dv > 255) ? 255 : (uint8_t)dv);
		disp_changed = true;
	}
	if (extract_json_float(body, "disp_dim_bright", &dv)) {
		dim_br = (dv < 1) ? 1 : ((dv > 255) ? 255 : (uint8_t)dv);
		disp_changed = true;
	}
	if (disp_changed) {
		settings_set_display_cfg(full_s, dim_s, full_br, dim_br);
		display_manager_reload_cfg();
	}

	const char *json = "{\"success\":true}";

	set_json_resp(response_ctx, json, strlen(json));
	return 0;
}

static struct http_resource_detail_dynamic api_config_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST),
	},
	.cb = api_config_handler,
};

/* ── /api/ota/upload (流式写入 slot1 + MCUboot 升级) ──────────────── */

static int api_ota_upload_handler(struct http_client_ctx *client,
				  enum http_transaction_status status,
				  const struct http_request_ctx *request_ctx,
				  struct http_response_ctx *response_ctx,
				  void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	static struct flash_img_context ota_ctx;
	static bool ota_active;
	static bool ota_failed;
	static size_t ota_bytes;
	static char resp_buf[160];

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		ota_active = false;
		ota_failed = false;
		ota_bytes = 0;
		return 0;
	}

	if (!ota_active && !ota_failed) {
		int rc = flash_img_init(&ota_ctx);

		if (rc != 0) {
			LOG_ERR("flash_img_init failed: %d", rc);
			ota_failed = true;
		} else {
			ota_active = true;
			ota_bytes = 0;
			LOG_INF("OTA upload started");
		}
	}

	bool is_final = (status == HTTP_SERVER_REQUEST_DATA_FINAL);

	if (ota_active && !ota_failed &&
	    (request_ctx->data_len > 0 || is_final)) {
		int rc = flash_img_buffered_write(&ota_ctx,
						  request_ctx->data,
						  request_ctx->data_len,
						  is_final);
		if (rc != 0) {
			LOG_ERR("flash_img_buffered_write failed at %zu: %d",
				ota_bytes, rc);
			ota_failed = true;
		} else {
			ota_bytes += request_ctx->data_len;
		}
	}

	if (!is_final) {
		return 0;
	}

	ota_active = false;

	if (ota_failed || ota_bytes < 64) {
		ota_failed = false;
		int len = snprintf(resp_buf, sizeof(resp_buf),
				   "{\"success\":false,\"message\":\"Flash write failed\"}");

		response_ctx->status = HTTP_400_BAD_REQUEST;
		set_json_resp(response_ctx, resp_buf, len);
		return 0;
	}

	struct mcuboot_img_header hdr;
	uint8_t slot_id = flash_img_get_upload_slot();

	if (boot_read_bank_header(slot_id, &hdr, sizeof(hdr)) != 0) {
		LOG_ERR("Invalid MCUboot image header in slot %u", slot_id);
		int len = snprintf(resp_buf, sizeof(resp_buf),
				   "{\"success\":false,\"message\":\"Invalid zephyr.signed.bin\"}");

		response_ctx->status = HTTP_400_BAD_REQUEST;
		set_json_resp(response_ctx, resp_buf, len);
		return 0;
	}

	int rc = boot_request_upgrade(BOOT_UPGRADE_TEST);

	if (rc != 0) {
		LOG_ERR("boot_request_upgrade failed: %d", rc);
		int len = snprintf(resp_buf, sizeof(resp_buf),
				   "{\"success\":false,\"message\":\"Set upgrade flag failed\"}");

		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		set_json_resp(response_ctx, resp_buf, len);
		return 0;
	}

	LOG_INF("OTA image ready (%zu bytes), v%u.%u.%u, rebooting...",
		ota_bytes, hdr.h.v1.sem_ver.major,
		hdr.h.v1.sem_ver.minor, hdr.h.v1.sem_ver.revision);

	int len = snprintf(resp_buf, sizeof(resp_buf),
			   "{\"success\":true,\"bytes\":%zu,\"version\":\"%u.%u.%u\"}",
			   ota_bytes, hdr.h.v1.sem_ver.major,
			   hdr.h.v1.sem_ver.minor, hdr.h.v1.sem_ver.revision);

	set_json_resp(response_ctx, resp_buf, len);
	k_work_schedule(&ota_reboot_work, K_MSEC(600));
	return 0;
}

static struct http_resource_detail_dynamic api_ota_upload_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = api_ota_upload_handler,
};

HTTP_SERVICE_DEFINE(web_service, NULL, &web_service_port,
		    CONFIG_HTTP_SERVER_MAX_CLIENTS, 10, NULL, NULL, NULL);

HTTP_RESOURCE_DEFINE(index_html, web_service, "/", &index_html_detail);
HTTP_RESOURCE_DEFINE(api_device_info, web_service, "/api/device/info", &api_device_info_detail);
HTTP_RESOURCE_DEFINE(api_wifi_status, web_service, "/api/wifi/status", &api_wifi_status_detail);
HTTP_RESOURCE_DEFINE(api_wifi_scan_start, web_service, "/api/wifi/scan/start", &api_wifi_scan_start_detail);
HTTP_RESOURCE_DEFINE(api_wifi_scan, web_service, "/api/wifi/scan", &api_wifi_scan_detail);
HTTP_RESOURCE_DEFINE(api_wifi_profiles, web_service, "/api/wifi/profiles", &api_wifi_profiles_detail);
HTTP_RESOURCE_DEFINE(api_wifi_profile_save, web_service, "/api/wifi/profile/save", &api_wifi_profile_save_detail);
HTTP_RESOURCE_DEFINE(api_wifi_profile_move, web_service, "/api/wifi/profile/move", &api_wifi_profile_move_detail);
HTTP_RESOURCE_DEFINE(api_wifi_profile_delete, web_service, "/api/wifi/profile/delete", &api_wifi_profile_delete_detail);
HTTP_RESOURCE_DEFINE(api_wifi_connect, web_service, "/api/wifi/connect", &api_wifi_connect_detail);
HTTP_RESOURCE_DEFINE(api_wifi_disconnect, web_service, "/api/wifi/disconnect", &api_wifi_disconnect_detail);
HTTP_RESOURCE_DEFINE(api_wifi_reconnect, web_service, "/api/wifi/reconnect", &api_wifi_reconnect_detail);
HTTP_RESOURCE_DEFINE(api_fan_status, web_service, "/api/fan/status", &api_fan_status_detail);
HTTP_RESOURCE_DEFINE(api_fan_control, web_service, "/api/fan/control", &api_fan_control_detail);
HTTP_RESOURCE_DEFINE(api_config, web_service, "/api/config", &api_config_detail);
HTTP_RESOURCE_DEFINE(api_ota_upload, web_service, "/api/ota/upload", &api_ota_upload_detail);

int web_server_start(void)
{
	int ret = http_server_start();

	if (ret < 0) {
		LOG_ERR("Failed to start HTTP server: %d", ret);
		return ret;
	}
	LOG_INF("Web server started on port %d", web_service_port);
	return 0;
}
