/*
 * Copyright (c) 2024 umi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(wifi_mgr, LOG_LEVEL_INF);

#define AP_NETMASK          "255.255.255.0"
#define AP_CHANNEL          6
#define MAX_SCAN_RESULTS    32
#define SCAN_TIMEOUT_MS     10000
#define CONNECT_TIMEOUT_S   20
#define DHCP_WAIT_MS        15000
#define WIFI_PROFILE_MAGIC  0x57503031U

enum wm_cmd_type {
	WM_CMD_RECONNECT = 1,
	WM_CMD_CONNECT_PROFILE,
	WM_CMD_DISCONNECT,
};

struct wm_cmd {
	enum wm_cmd_type type;
	struct wifi_profile profile;
};

struct wifi_profile_nvs {
	uint32_t magic;
	char ssid[33];
	char password[65];
	uint8_t static_en;
	char static_ip[16];
	char netmask[16];
	char gateway[16];
};

K_MSGQ_DEFINE(wm_cmd_q, sizeof(struct wm_cmd), 4, 4);

static K_MUTEX_DEFINE(wm_lock);
static K_MUTEX_DEFINE(scan_lock);
static K_SEM_DEFINE(scan_sem, 0, 1);
static K_SEM_DEFINE(connect_sem, 0, 1);
static K_SEM_DEFINE(disconnect_sem, 0, 1);
static K_SEM_DEFINE(ipv4_sem, 0, 1);

static void rssi_poll_work_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(rssi_poll_work, rssi_poll_work_fn);

static enum wifi_state cur_state = WIFI_STATE_IDLE;
static bool sta_link_up;
static bool ap_active;
static char cur_ssid[33];
static char cur_ip[16];
static char cur_ap_psk[16];
static int8_t cur_rssi;
static struct wifi_static_ip_cfg cur_static_ip = {
	.enabled = false,
	.netmask = AP_NETMASK,
};

static struct wifi_profile profiles[WIFI_MAX_PROFILES];
static uint8_t profile_count;
static bool profiles_cnt_loaded;

/* 兼容旧版本单 SSID 存储，首次加载自动迁移到 profiles[0] */
static char legacy_ssid[33];
static char legacy_password[65];
static struct wifi_static_ip_cfg legacy_static_ip = {
	.enabled = false,
	.netmask = AP_NETMASK,
};

static struct wifi_scan_result scan_results[MAX_SCAN_RESULTS];
static int scan_result_count;
static bool scan_in_progress;
static int64_t scan_start_ms;

/* ── Settings 持久化（wmgr/cnt, wmgr/p0..p4） ────────────────────── */

static int read_settings_str(settings_read_cb read_cb, void *cb_arg,
			     char *dst, size_t dst_sz)
{
	ssize_t rc = read_cb(cb_arg, dst, dst_sz - 1);

	if (rc <= 0) {
		dst[0] = '\0';
		return (rc == 0) ? -EINVAL : (int)rc;
	}
	dst[rc] = '\0';
	return 0;
}

static int wmgr_settings_set(const char *key, size_t len_rd,
			     settings_read_cb read_cb, void *cb_arg)
{
	ARG_UNUSED(len_rd);

	if (!key) {
		return -ENOENT;
	}

	if (!strcmp(key, "cnt")) {
		uint8_t cnt = 0;
		ssize_t rc = read_cb(cb_arg, &cnt, sizeof(cnt));

		if (rc != sizeof(cnt)) {
			return (rc >= 0) ? -EINVAL : (int)rc;
		}
		profile_count = MIN(cnt, (uint8_t)WIFI_MAX_PROFILES);
		profiles_cnt_loaded = true;
		return 0;
	}

	if (key[0] == 'p' && key[1] >= '0' && key[1] < ('0' + WIFI_MAX_PROFILES) &&
	    key[2] == '\0') {
		int idx = key[1] - '0';
		struct wifi_profile_nvs rec = {0};
		ssize_t rc = read_cb(cb_arg, &rec, sizeof(rec));

		if (rc != sizeof(rec) || rec.magic != WIFI_PROFILE_MAGIC) {
			return (rc >= 0) ? -EINVAL : (int)rc;
		}
		rec.ssid[sizeof(rec.ssid) - 1] = '\0';
		rec.password[sizeof(rec.password) - 1] = '\0';
		rec.static_ip[sizeof(rec.static_ip) - 1] = '\0';
		rec.netmask[sizeof(rec.netmask) - 1] = '\0';
		rec.gateway[sizeof(rec.gateway) - 1] = '\0';

		snprintf(profiles[idx].ssid, sizeof(profiles[idx].ssid), "%s", rec.ssid);
		snprintf(profiles[idx].password, sizeof(profiles[idx].password), "%s",
			 rec.password);
		profiles[idx].static_ip.enabled = (rec.static_en != 0);
		snprintf(profiles[idx].static_ip.ip, sizeof(profiles[idx].static_ip.ip),
			 "%s", rec.static_ip);
		snprintf(profiles[idx].static_ip.netmask,
			 sizeof(profiles[idx].static_ip.netmask), "%s",
			 rec.netmask[0] ? rec.netmask : AP_NETMASK);
		snprintf(profiles[idx].static_ip.gateway,
			 sizeof(profiles[idx].static_ip.gateway), "%s", rec.gateway);
		return 0;
	}

	/* 旧版本键兼容读取 */
	if (!strcmp(key, "ssid")) {
		return read_settings_str(read_cb, cb_arg,
					 legacy_ssid, sizeof(legacy_ssid));
	} else if (!strcmp(key, "password")) {
		return read_settings_str(read_cb, cb_arg,
					 legacy_password, sizeof(legacy_password));
	} else if (!strcmp(key, "static_en")) {
		uint8_t en = 0;

		if (read_cb(cb_arg, &en, sizeof(en)) == sizeof(en)) {
			legacy_static_ip.enabled = (en != 0);
		}
	} else if (!strcmp(key, "static_ip")) {
		return read_settings_str(read_cb, cb_arg, legacy_static_ip.ip,
					 sizeof(legacy_static_ip.ip));
	} else if (!strcmp(key, "static_mask")) {
		return read_settings_str(read_cb, cb_arg, legacy_static_ip.netmask,
					 sizeof(legacy_static_ip.netmask));
	} else if (!strcmp(key, "static_gw")) {
		return read_settings_str(read_cb, cb_arg, legacy_static_ip.gateway,
					 sizeof(legacy_static_ip.gateway));
	}

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(wmgr, "wmgr", NULL, wmgr_settings_set, NULL, NULL);

static bool is_valid_ipv4(const char *s, bool allow_zero)
{
	struct net_in_addr addr;

	if (!s || !s[0] || net_addr_pton(NET_AF_INET, s, &addr) != 0) {
		return false;
	}
	return allow_zero || (addr.s_addr != NET_INADDR_ANY);
}

static int validate_static_ip_cfg(const struct wifi_static_ip_cfg *in,
				  struct wifi_static_ip_cfg *out)
{
	if (!in || !out) {
		return -EINVAL;
	}

	memset(out, 0, sizeof(*out));
	out->enabled = in->enabled;

	snprintf(out->ip, sizeof(out->ip), "%s", in->ip);
	snprintf(out->netmask, sizeof(out->netmask), "%s",
		 in->netmask[0] ? in->netmask : AP_NETMASK);
	snprintf(out->gateway, sizeof(out->gateway), "%s", in->gateway);

	if (out->enabled) {
		if (!is_valid_ipv4(out->ip, false) ||
		    !is_valid_ipv4(out->netmask, false)) {
			return -EINVAL;
		}
		if (out->gateway[0] != '\0' && !is_valid_ipv4(out->gateway, false)) {
			return -EINVAL;
		}
	} else {
		if (out->ip[0] != '\0' && !is_valid_ipv4(out->ip, false)) {
			out->ip[0] = '\0';
		}
		if (!is_valid_ipv4(out->netmask, false)) {
			snprintf(out->netmask, sizeof(out->netmask), "%s", AP_NETMASK);
		}
		if (out->gateway[0] != '\0' && !is_valid_ipv4(out->gateway, false)) {
			out->gateway[0] = '\0';
		}
	}

	return 0;
}

static void delete_legacy_keys(void)
{
	static const char *const keys[] = {
		"wmgr/ssid", "wmgr/password", "wmgr/static_en",
		"wmgr/static_ip", "wmgr/static_mask", "wmgr/static_gw",
	};

	for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
		settings_delete(keys[i]);
	}
}

static int persist_profiles_locked(void)
{
	int rc = settings_save_one("wmgr/cnt", &profile_count, sizeof(profile_count));

	if (rc) {
		LOG_ERR("Failed to save profile count: %d", rc);
		return rc;
	}

	for (int i = 0; i < WIFI_MAX_PROFILES; i++) {
		char key[12];

		snprintf(key, sizeof(key), "wmgr/p%d", i);
		if (i < profile_count) {
			struct wifi_profile_nvs rec = {
				.magic = WIFI_PROFILE_MAGIC,
				.static_en = profiles[i].static_ip.enabled ? 1 : 0,
			};

			snprintf(rec.ssid, sizeof(rec.ssid), "%s", profiles[i].ssid);
			snprintf(rec.password, sizeof(rec.password), "%s",
				 profiles[i].password);
			snprintf(rec.static_ip, sizeof(rec.static_ip), "%s",
				 profiles[i].static_ip.ip);
			snprintf(rec.netmask, sizeof(rec.netmask), "%s",
				 profiles[i].static_ip.netmask);
			snprintf(rec.gateway, sizeof(rec.gateway), "%s",
				 profiles[i].static_ip.gateway);

			rc = settings_save_one(key, &rec, sizeof(rec));
			if (rc) {
				LOG_ERR("Failed to save %s: %d", key, rc);
				return rc;
			}
		} else {
			rc = settings_delete(key);
			if (rc && rc != -ENOENT) {
				return rc;
			}
		}
	}
	return 0;
}

static void compact_loaded_profiles(void)
{
	uint8_t valid = 0;

	for (uint8_t i = 0; i < profile_count && i < WIFI_MAX_PROFILES; i++) {
		if (profiles[i].ssid[0] != '\0') {
			if (valid != i) {
				profiles[valid] = profiles[i];
			}
			valid++;
		}
	}
	profile_count = valid;

	if (!profiles_cnt_loaded && profile_count == 0 && legacy_ssid[0] != '\0') {
		snprintf(profiles[0].ssid, sizeof(profiles[0].ssid), "%s", legacy_ssid);
		snprintf(profiles[0].password, sizeof(profiles[0].password), "%s",
			 legacy_password);
		validate_static_ip_cfg(&legacy_static_ip, &profiles[0].static_ip);
		profile_count = 1;
		persist_profiles_locked();
		delete_legacy_keys();
		LOG_INF("Migrated legacy WiFi SSID: %s", profiles[0].ssid);
	}

	if (profile_count > 0) {
		LOG_INF("Loaded %u saved WiFi profile(s), primary: %s",
			profile_count, profiles[0].ssid);
	}
}

static int find_profile_index_locked(const char *ssid)
{
	if (!ssid || !ssid[0]) {
		return -1;
	}
	for (uint8_t i = 0; i < profile_count; i++) {
		if (!strcmp(profiles[i].ssid, ssid)) {
			return i;
		}
	}
	return -1;
}

static void clear_all_profiles(void)
{
	k_mutex_lock(&wm_lock, K_FOREVER);
	memset(profiles, 0, sizeof(profiles));
	profile_count = 0;
	persist_profiles_locked();
	delete_legacy_keys();
	k_mutex_unlock(&wm_lock);
}

static void clear_sta_ipv4(struct net_if *sta)
{
	if (!sta) {
		return;
	}

	net_dhcpv4_stop(sta);
	if (!sta->config.ip.ipv4) {
		return;
	}

	struct net_if_ipv4 *ipv4 = sta->config.ip.ipv4;

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		if (ipv4->unicast[i].ipv4.is_used &&
		    ipv4->unicast[i].ipv4.address.family == NET_AF_INET) {
			struct net_in_addr addr = ipv4->unicast[i].ipv4.address.in_addr;

			net_if_ipv4_addr_rm(sta, &addr);
		}
	}

	struct net_in_addr zero = {0};

	net_if_ipv4_set_gw(sta, &zero);
}

static void extract_sta_ip(struct net_if *iface, char *buf, size_t len)
{
	if (!iface || !iface->config.ip.ipv4) {
		buf[0] = '\0';
		return;
	}

	struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		if (ipv4->unicast[i].ipv4.is_used &&
		    ipv4->unicast[i].ipv4.address.family == NET_AF_INET) {
			net_addr_ntop(NET_AF_INET,
				      &ipv4->unicast[i].ipv4.address.in_addr,
				      buf, len);
			return;
		}
	}
	buf[0] = '\0';
}

/* ── 网络事件处理 ─────────────────────────────────────────────────── */

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
				    uint64_t mgmt_event, struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_SCAN_RESULT: {
		const struct wifi_scan_result *entry =
			(const struct wifi_scan_result *)cb->info;

		k_mutex_lock(&scan_lock, K_FOREVER);
		if (scan_result_count < MAX_SCAN_RESULTS) {
			memcpy(&scan_results[scan_result_count], entry,
			       sizeof(struct wifi_scan_result));
			scan_result_count++;
		}
		k_mutex_unlock(&scan_lock);
		break;
	}
	case NET_EVENT_WIFI_SCAN_DONE:
		k_mutex_lock(&scan_lock, K_FOREVER);
		scan_in_progress = false;
		k_mutex_unlock(&scan_lock);
		k_sem_give(&scan_sem);
		break;

	case NET_EVENT_WIFI_CONNECT_RESULT: {
		const struct wifi_status *status = (const struct wifi_status *)cb->info;

		sta_link_up = (status && status->status == 0);
		LOG_INF("WiFi connect result: %d", status ? status->status : -1);
		k_sem_give(&connect_sem);
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		k_sem_give(&disconnect_sem);
		break;

	case NET_EVENT_WIFI_DISCONNECT_COMPLETE: {
		bool unexpected_drop = false;

		LOG_INF("WiFi STA disconnected");
		sta_link_up = false;
		k_work_cancel_delayable(&rssi_poll_work);
		k_sem_give(&ipv4_sem);
		k_mutex_lock(&wm_lock, K_FOREVER);
		if (cur_state == WIFI_STATE_CONNECTED) {
			cur_state = WIFI_STATE_IDLE;
			cur_ip[0] = '\0';
			cur_rssi = 0;
			unexpected_drop = (profile_count > 0);
		}
		k_mutex_unlock(&wm_lock);
		if (unexpected_drop) {
			struct wm_cmd cmd = {.type = WM_CMD_RECONNECT};

			k_msgq_put(&wm_cmd_q, &cmd, K_NO_WAIT);
		}
		break;
	}

	case NET_EVENT_IPV4_ADDR_ADD:
		if (iface == net_if_get_wifi_sta()) {
			char ip[16] = {0};

			extract_sta_ip(iface, ip, sizeof(ip));
			if (ip[0] != '\0') {
				k_mutex_lock(&wm_lock, K_FOREVER);
				snprintf(cur_ip, sizeof(cur_ip), "%s", ip);
				k_mutex_unlock(&wm_lock);
				k_sem_give(&ipv4_sem);
			}
		}
		break;

	default:
		break;
	}
}

static struct net_mgmt_event_callback wifi_mgmt_cb;
static struct net_mgmt_event_callback ipv4_mgmt_cb;

static void register_net_events(void)
{
	uint64_t wifi_events = NET_EVENT_WIFI_SCAN_RESULT |
			       NET_EVENT_WIFI_SCAN_DONE |
			       NET_EVENT_WIFI_CONNECT_RESULT |
			       NET_EVENT_WIFI_DISCONNECT_RESULT |
			       NET_EVENT_WIFI_DISCONNECT_COMPLETE;

	net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_mgmt_event_handler, wifi_events);
	net_mgmt_add_event_callback(&wifi_mgmt_cb);

	net_mgmt_init_event_callback(&ipv4_mgmt_cb, wifi_mgmt_event_handler,
				     NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_mgmt_cb);
}

/* ── STA / AP 控制 ────────────────────────────────────────────────── */

static void generate_ap_psk(char *out, size_t out_sz)
{
	size_t n = MIN((size_t)WIFI_AP_PSK_LEN, out_sz - 1);

	for (size_t i = 0; i < n; i++) {
		out[i] = (char)('0' + (sys_rand32_get() % 10U));
	}
	out[n] = '\0';
}

static int wifi_ap_stop(void)
{
	struct net_if *ap = net_if_get_wifi_sap();

	if (!ap || !ap_active) {
		return 0;
	}

	net_dhcpv4_server_stop(ap);
	int ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, ap, NULL, 0);

	if (ret) {
		LOG_WRN("NET_REQUEST_WIFI_AP_DISABLE failed: %d", ret);
	}
	ap_active = false;
	k_sleep(K_MSEC(100));
	return 0;
}

static int wifi_ap_start(void)
{
	struct net_if *sta = net_if_get_wifi_sta();
	struct net_if *ap = net_if_get_wifi_sap();

	if (!ap) {
		LOG_ERR("No WiFi SAP interface");
		return -ENODEV;
	}

	if (sta) {
		net_if_up(sta);
		k_sleep(K_MSEC(50));
	}
	net_if_up(ap);
	k_sleep(K_MSEC(50));

	struct net_in_addr addr, netmask;

	net_addr_pton(NET_AF_INET, WIFI_AP_IP, &addr);
	net_addr_pton(NET_AF_INET, AP_NETMASK, &netmask);

	net_if_ipv4_set_gw(ap, &addr);
	net_if_ipv4_addr_add(ap, &addr, NET_ADDR_MANUAL, 0);
	net_if_ipv4_set_netmask_by_addr(ap, &addr, &netmask);

	struct net_in_addr dhcp_start = addr;

	dhcp_start.s4_addr[3] += 10;
	net_dhcpv4_server_start(ap, &dhcp_start);

	char psk[16];

	k_mutex_lock(&wm_lock, K_FOREVER);
	if (cur_ap_psk[0] == '\0') {
		generate_ap_psk(cur_ap_psk, sizeof(cur_ap_psk));
	}
	snprintf(psk, sizeof(psk), "%s", cur_ap_psk);
	k_mutex_unlock(&wm_lock);

	struct wifi_connect_req_params params = {
		.ssid = (const uint8_t *)WIFI_AP_SSID,
		.ssid_length = strlen(WIFI_AP_SSID),
		.psk = (const uint8_t *)psk,
		.psk_length = strlen(psk),
		.security = WIFI_SECURITY_TYPE_PSK,
		.channel = AP_CHANNEL,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
		.bandwidth = WIFI_FREQ_BANDWIDTH_20MHZ,
	};

	int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, ap, &params, sizeof(params));

	if (ret) {
		LOG_ERR("Failed to enable AP: %d", ret);
		return ret;
	}

	k_work_cancel_delayable(&rssi_poll_work);
	ap_active = true;
	k_mutex_lock(&wm_lock, K_FOREVER);
	cur_state = WIFI_STATE_AP;
	strncpy(cur_ssid, WIFI_AP_SSID, sizeof(cur_ssid) - 1);
	cur_ssid[sizeof(cur_ssid) - 1] = '\0';
	strncpy(cur_ip, WIFI_AP_IP, sizeof(cur_ip) - 1);
	cur_ip[sizeof(cur_ip) - 1] = '\0';
	strncpy(cur_ap_psk, psk, sizeof(cur_ap_psk) - 1);
	cur_ap_psk[sizeof(cur_ap_psk) - 1] = '\0';
	cur_rssi = 0;
	k_mutex_unlock(&wm_lock);

	LOG_INF("AP started: SSID=%s, PSK=%s, IP=%s", WIFI_AP_SSID, psk, WIFI_AP_IP);
	return 0;
}

static int start_wifi_scan_async(struct net_if *sta)
{
	int ret = 0;

	net_if_up(sta);

	k_mutex_lock(&scan_lock, K_FOREVER);
	if (scan_in_progress && (k_uptime_get() - scan_start_ms) < SCAN_TIMEOUT_MS) {
		k_mutex_unlock(&scan_lock);
		return 0;
	}
	memset(scan_results, 0, sizeof(scan_results));
	scan_result_count = 0;
	scan_in_progress = true;
	scan_start_ms = k_uptime_get();
	k_sem_reset(&scan_sem);
	k_mutex_unlock(&scan_lock);

	ret = net_mgmt(NET_REQUEST_WIFI_SCAN, sta, NULL, 0);
	if (ret) {
		k_mutex_lock(&scan_lock, K_FOREVER);
		scan_in_progress = false;
		k_mutex_unlock(&scan_lock);
		if (ret == -EINPROGRESS || ret == -EALREADY) {
			return 0;
		}
		LOG_WRN("NET_REQUEST_WIFI_SCAN failed: %d", ret);
		return ret;
	}
	return 0;
}

static int wifi_scan_sync(struct net_if *sta)
{
	int ret = start_wifi_scan_async(sta);

	if (ret) {
		return ret;
	}
	ret = k_sem_take(&scan_sem, K_MSEC(SCAN_TIMEOUT_MS));
	if (ret) {
		k_mutex_lock(&scan_lock, K_FOREVER);
		scan_in_progress = false;
		k_mutex_unlock(&scan_lock);
		return -ETIMEDOUT;
	}
	return 0;
}

static uint8_t find_target_channel(struct net_if *sta, const char *ssid)
{
	size_t target_len = strlen(ssid);
	uint8_t ch = WIFI_CHANNEL_ANY;
	int8_t best_rssi = -128;

	for (int scan_try = 0; scan_try < 2 && ch == WIFI_CHANNEL_ANY; scan_try++) {
		if (wifi_scan_sync(sta) != 0) {
			continue;
		}
		k_mutex_lock(&scan_lock, K_FOREVER);
		for (int i = 0; i < scan_result_count; i++) {
			const struct wifi_scan_result *r = &scan_results[i];

			if (r->ssid_length == target_len &&
			    memcmp(r->ssid, ssid, target_len) == 0 &&
			    r->rssi > best_rssi) {
				best_rssi = r->rssi;
				ch = r->channel;
			}
		}
		k_mutex_unlock(&scan_lock);
	}

	if (ch != WIFI_CHANNEL_ANY) {
		LOG_INF("Found target '%s' on ch=%u rssi=%d", ssid, ch, best_rssi);
	} else {
		LOG_INF("Target '%s' not in pre-scan, trying any channel", ssid);
	}
	return ch;
}

static int apply_sta_static_ip(struct net_if *sta,
			       const struct wifi_static_ip_cfg *cfg,
			       char *ip_out, size_t ip_out_sz)
{
	struct net_in_addr ip_addr, mask_addr, gw_addr;

	if (net_addr_pton(NET_AF_INET, cfg->ip, &ip_addr) != 0 ||
	    net_addr_pton(NET_AF_INET, cfg->netmask, &mask_addr) != 0) {
		LOG_ERR("Invalid static IPv4 config: ip=%s mask=%s",
			cfg->ip, cfg->netmask);
		return -EINVAL;
	}

	net_dhcpv4_stop(sta);
	if (!net_if_ipv4_addr_add(sta, &ip_addr, NET_ADDR_MANUAL, 0)) {
		LOG_ERR("Failed to add static IPv4 addr: %s", cfg->ip);
		return -ENOMEM;
	}
	net_if_ipv4_set_netmask_by_addr(sta, &ip_addr, &mask_addr);

	if (cfg->gateway[0] != '\0' &&
	    net_addr_pton(NET_AF_INET, cfg->gateway, &gw_addr) == 0) {
		net_if_ipv4_set_gw(sta, &gw_addr);
	}

	extract_sta_ip(sta, ip_out, ip_out_sz);
	return (ip_out[0] != '\0') ? 0 : -EIO;
}

static int wifi_sta_connect_sync(const struct wifi_profile *prof, bool fast_skip)
{
	struct net_if *sta = net_if_get_wifi_sta();

	if (!sta) {
		return -ENODEV;
	}

	if (!prof || !prof->ssid[0]) {
		return -EINVAL;
	}

	const char *ssid = prof->ssid;
	const char *password = prof->password;
	const struct wifi_static_ip_cfg *static_cfg = &prof->static_ip;

	wifi_ap_stop();
	clear_sta_ipv4(sta);

	net_if_up(sta);
	k_sleep(K_MSEC(150));

	k_mutex_lock(&wm_lock, K_FOREVER);
	cur_state = WIFI_STATE_CONNECTING;
	snprintf(cur_ssid, sizeof(cur_ssid), "%s", ssid);
	cur_ip[0] = '\0';
	cur_rssi = 0;
	cur_static_ip = *static_cfg;
	k_mutex_unlock(&wm_lock);

	enum wifi_security_type security =
		(password[0] != '\0') ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;

	uint8_t target_ch = find_target_channel(sta, ssid);
	int max_attempts = (fast_skip && target_ch == WIFI_CHANNEL_ANY) ? 1 : 3;

	LOG_INF("STA connect req: ssid='%s' psk_len=%zu sec=%d ch=%u static=%d",
		ssid, strlen(password), (int)security, target_ch, static_cfg->enabled);

	struct wifi_connect_req_params params = {
		.ssid = (const uint8_t *)ssid,
		.ssid_length = strlen(ssid),
		.psk = (security == WIFI_SECURITY_TYPE_PSK) ? (const uint8_t *)password : NULL,
		.psk_length = (security == WIFI_SECURITY_TYPE_PSK) ? strlen(password) : 0,
		.security = security,
		.channel = target_ch,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
		.timeout = CONNECT_TIMEOUT_S,
	};

	int ret = -ECONNREFUSED;

	for (int attempt = 1; attempt <= max_attempts; attempt++) {
		sta_link_up = false;
		k_sem_reset(&connect_sem);
		k_sem_reset(&ipv4_sem);

		ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, sta, &params, sizeof(params));
		if (ret) {
			LOG_ERR("NET_REQUEST_WIFI_CONNECT failed (try %d): %d",
				attempt, ret);
			if (ret == -EALREADY) {
				net_mgmt(NET_REQUEST_WIFI_DISCONNECT, sta, NULL, 0);
				k_sem_take(&connect_sem, K_SECONDS(4));
			}
			k_sleep(K_MSEC(300));
			continue;
		}

		ret = k_sem_take(&connect_sem, K_SECONDS(CONNECT_TIMEOUT_S + 2));
		if (ret == 0 && sta_link_up) {
			break;
		}
		LOG_WRN("WiFi STA connect attempt %d failed (ret=%d)", attempt, ret);
		if (ret == -EAGAIN) {
			/* 超时后同步等待断开完成，避免残留的断连事件打断下一次重连 */
			if (net_mgmt(NET_REQUEST_WIFI_DISCONNECT, sta, NULL, 0) == 0) {
				k_sem_take(&connect_sem, K_SECONDS(4));
			}
		}
		ret = ret ? ret : -ECONNREFUSED;
		k_sleep(K_MSEC(300));
	}

	if (!sta_link_up) {
		LOG_WRN("WiFi STA connect failed or timed out");
		return ret;
	}

	char ip[16] = {0};

	if (static_cfg->enabled) {
		LOG_INF("WiFi L2 connected, applying static IP %s...", static_cfg->ip);
		int ip_ret = apply_sta_static_ip(sta, static_cfg, ip, sizeof(ip));

		if (ip_ret != 0 || !sta_link_up) {
			clear_sta_ipv4(sta);
			if (sta_link_up) {
				net_mgmt(NET_REQUEST_WIFI_DISCONNECT, sta, NULL, 0);
				k_sleep(K_MSEC(500));
			}
			return ip_ret ? ip_ret : -ECONNRESET;
		}
	} else {
		LOG_INF("WiFi L2 connected, starting DHCPv4...");
		net_dhcpv4_stop(sta);
		net_dhcpv4_start(sta);

		extract_sta_ip(sta, ip, sizeof(ip));
		if (ip[0] == '\0' && sta_link_up) {
			if (k_sem_take(&ipv4_sem, K_MSEC(DHCP_WAIT_MS)) == 0) {
				extract_sta_ip(sta, ip, sizeof(ip));
			}
		}

		if (!sta_link_up || ip[0] == '\0') {
			LOG_WRN("DHCPv4 failed or link dropped (link=%d ip='%s')",
				sta_link_up, ip);
			clear_sta_ipv4(sta);
			if (sta_link_up) {
				net_mgmt(NET_REQUEST_WIFI_DISCONNECT, sta, NULL, 0);
				k_sleep(K_MSEC(500));
			}
			return -ETIMEDOUT;
		}
	}

	struct wifi_iface_status istat = {0};
	int8_t rssi = 0;

	if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, sta, &istat, sizeof(istat)) == 0) {
		rssi = (int8_t)istat.rssi;
	}

	k_mutex_lock(&wm_lock, K_FOREVER);
	cur_state = WIFI_STATE_CONNECTED;
	snprintf(cur_ssid, sizeof(cur_ssid), "%s", ssid);
	if (ip[0] != '\0') {
		snprintf(cur_ip, sizeof(cur_ip), "%s", ip);
	}
	cur_rssi = rssi;
	cur_static_ip = *static_cfg;
	k_mutex_unlock(&wm_lock);

	k_work_reschedule(&rssi_poll_work, K_SECONDS(1));
	LOG_INF("STA connected: SSID=%s, IP=%s, RSSI=%d", cur_ssid, cur_ip, cur_rssi);
	return 0;
}

static void rssi_poll_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	if (wifi_manager_get_state() != WIFI_STATE_CONNECTED || !sta_link_up) {
		return;
	}

	struct net_if *sta = net_if_get_wifi_sta();

	if (sta) {
		struct wifi_iface_status istat = {0};

		if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, sta, &istat, sizeof(istat)) == 0 &&
		    istat.rssi != 0) {
			k_mutex_lock(&wm_lock, K_FOREVER);
			if (cur_state == WIFI_STATE_CONNECTED) {
				cur_rssi = (int8_t)istat.rssi;
			}
			k_mutex_unlock(&wm_lock);
		}
	}

	k_work_schedule(&rssi_poll_work, K_SECONDS(1));
}

static void wifi_worker_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	struct wm_cmd cmd;

	while (1) {
		k_msgq_get(&wm_cmd_q, &cmd, K_FOREVER);

		if (cmd.type == WM_CMD_RECONNECT) {
			struct wifi_profile list[WIFI_MAX_PROFILES];
			uint8_t n;

			k_mutex_lock(&wm_lock, K_FOREVER);
			n = profile_count;
			if (n > 0) {
				memcpy(list, profiles, n * sizeof(profiles[0]));
			}
			k_mutex_unlock(&wm_lock);

			if (n > 0) {
				bool ok = false;

				for (uint8_t i = 0; i < n; i++) {
					LOG_INF("Connecting WiFi [%u/%u]: %s",
						i + 1, n, list[i].ssid);
					if (wifi_sta_connect_sync(&list[i], (n > 1)) == 0) {
						ok = true;
						break;
					}
				}
				if (!ok) {
					LOG_WRN("All saved WiFi failed, falling back to AP mode");
					wifi_ap_start();
				}
			} else if (!ap_active) {
				LOG_INF("No saved WiFi profiles, starting AP mode");
				wifi_ap_start();
			}
		} else if (cmd.type == WM_CMD_CONNECT_PROFILE) {
			if (wifi_sta_connect_sync(&cmd.profile, false) != 0) {
				LOG_WRN("Connect to %s failed, falling back to AP",
					cmd.profile.ssid);
				wifi_ap_start();
			}
		} else if (cmd.type == WM_CMD_DISCONNECT) {
			struct net_if *sta = net_if_get_wifi_sta();

			k_mutex_lock(&wm_lock, K_FOREVER);
			cur_state = WIFI_STATE_IDLE;
			cur_ip[0] = '\0';
			cur_rssi = 0;
			k_mutex_unlock(&wm_lock);

			if (sta && sta_link_up) {
				k_sem_reset(&disconnect_sem);
				if (net_mgmt(NET_REQUEST_WIFI_DISCONNECT, sta, NULL, 0) == 0) {
					k_sem_take(&disconnect_sem, K_SECONDS(5));
				}
			}
			clear_sta_ipv4(sta);
			clear_all_profiles();
			wifi_ap_start();
		}
	}
}

K_THREAD_DEFINE(wifi_worker_id, 4096, wifi_worker_thread,
		NULL, NULL, NULL, 6, 0, 0);

/* ── 公共 API ──────────────────────────────────────────────────────── */

int wifi_manager_init(void)
{
	register_net_events();
	settings_load_subtree("wmgr");
	compact_loaded_profiles();

	/* 启动后异步使用保存的 WiFi 信息连接，失败则回退到 AP */
	struct wm_cmd cmd = {.type = WM_CMD_RECONNECT};

	k_msgq_put(&wm_cmd_q, &cmd, K_NO_WAIT);
	LOG_INF("WiFi manager initialized");
	return 0;
}

enum wifi_state wifi_manager_get_state(void)
{
	enum wifi_state st;

	k_mutex_lock(&wm_lock, K_FOREVER);
	st = cur_state;
	k_mutex_unlock(&wm_lock);
	return st;
}

void wifi_manager_get_info(struct wifi_info *info)
{
	if (!info) {
		return;
	}

	k_mutex_lock(&wm_lock, K_FOREVER);
	info->state = cur_state;
	snprintf(info->ssid, sizeof(info->ssid), "%s", cur_ssid);
	snprintf(info->ip, sizeof(info->ip), "%s", cur_ip);
	snprintf(info->ap_psk, sizeof(info->ap_psk), "%s", cur_ap_psk);
	info->rssi = cur_rssi;
	info->has_saved_creds = (profile_count > 0);
	info->saved_count = profile_count;
	if (profile_count > 0) {
		snprintf(info->saved_ssid, sizeof(info->saved_ssid), "%s",
			 profiles[0].ssid);
	} else {
		info->saved_ssid[0] = '\0';
	}
	if (cur_state == WIFI_STATE_CONNECTED || cur_state == WIFI_STATE_CONNECTING) {
		info->static_ip = cur_static_ip;
	} else if (profile_count > 0) {
		info->static_ip = profiles[0].static_ip;
	} else {
		memset(&info->static_ip, 0, sizeof(info->static_ip));
		snprintf(info->static_ip.netmask, sizeof(info->static_ip.netmask),
			 "%s", AP_NETMASK);
	}
	k_mutex_unlock(&wm_lock);
}

bool wifi_manager_is_connected(void)
{
	return wifi_manager_get_state() == WIFI_STATE_CONNECTED;
}

int wifi_manager_reconnect(void)
{
	k_mutex_lock(&wm_lock, K_FOREVER);
	enum wifi_state st = cur_state;
	bool has_creds = (profile_count > 0);
	k_mutex_unlock(&wm_lock);

	if (st == WIFI_STATE_CONNECTING) {
		return -EBUSY;
	}
	if (!has_creds && st == WIFI_STATE_AP) {
		return -ENOENT;
	}

	struct wm_cmd cmd = {.type = WM_CMD_RECONNECT};

	return k_msgq_put(&wm_cmd_q, &cmd, K_NO_WAIT);
}

int wifi_manager_save_profile(const char *orig_ssid,
			      const struct wifi_profile *prof,
			      bool keep_psk)
{
	if (!prof || !prof->ssid[0]) {
		return -EINVAL;
	}

	struct wifi_static_ip_cfg norm_ip;
	int rc = validate_static_ip_cfg(&prof->static_ip, &norm_ip);

	if (rc) {
		return rc;
	}

	k_mutex_lock(&wm_lock, K_FOREVER);
	int idx = (orig_ssid && orig_ssid[0])
			  ? find_profile_index_locked(orig_ssid)
			  : find_profile_index_locked(prof->ssid);

	if (idx >= 0 && orig_ssid && orig_ssid[0] &&
	    strcmp(orig_ssid, prof->ssid) != 0 &&
	    find_profile_index_locked(prof->ssid) >= 0) {
		k_mutex_unlock(&wm_lock);
		return -EEXIST;
	}

	if (idx < 0) {
		if (profile_count >= WIFI_MAX_PROFILES) {
			k_mutex_unlock(&wm_lock);
			return -ENOSPC;
		}
		idx = profile_count++;
		memset(&profiles[idx], 0, sizeof(profiles[idx]));
	}

	snprintf(profiles[idx].ssid, sizeof(profiles[idx].ssid), "%s", prof->ssid);
	if (!keep_psk || prof->password[0] != '\0') {
		snprintf(profiles[idx].password, sizeof(profiles[idx].password),
			 "%s", prof->password);
	}
	profiles[idx].static_ip = norm_ip;

	rc = persist_profiles_locked();
	k_mutex_unlock(&wm_lock);

	if (rc == 0) {
		LOG_INF("Saved WiFi profile #%d: ssid='%s' static=%d ip=%s",
			idx + 1, prof->ssid, norm_ip.enabled, norm_ip.ip);
	}
	return rc;
}

int wifi_manager_delete_profile(int index)
{
	k_mutex_lock(&wm_lock, K_FOREVER);
	if (index < 0 || index >= profile_count) {
		k_mutex_unlock(&wm_lock);
		return -ENOENT;
	}

	for (int i = index; i < (int)profile_count - 1; i++) {
		profiles[i] = profiles[i + 1];
	}
	profile_count--;
	memset(&profiles[profile_count], 0, sizeof(profiles[profile_count]));

	int rc = persist_profiles_locked();

	k_mutex_unlock(&wm_lock);
	return rc;
}

int wifi_manager_move_profile(int index, int delta)
{
	int target = index + delta;

	k_mutex_lock(&wm_lock, K_FOREVER);
	if (index < 0 || index >= profile_count ||
	    target < 0 || target >= profile_count || index == target) {
		k_mutex_unlock(&wm_lock);
		return -EINVAL;
	}

	struct wifi_profile tmp = profiles[index];

	profiles[index] = profiles[target];
	profiles[target] = tmp;

	int rc = persist_profiles_locked();

	k_mutex_unlock(&wm_lock);
	return rc;
}

int wifi_manager_connect_profile(int index)
{
	struct wm_cmd cmd = {.type = WM_CMD_CONNECT_PROFILE};

	k_mutex_lock(&wm_lock, K_FOREVER);
	if (cur_state == WIFI_STATE_CONNECTING) {
		k_mutex_unlock(&wm_lock);
		return -EBUSY;
	}
	if (index < 0 || index >= profile_count) {
		k_mutex_unlock(&wm_lock);
		return -ENOENT;
	}
	cmd.profile = profiles[index];
	k_mutex_unlock(&wm_lock);

	return k_msgq_put(&wm_cmd_q, &cmd, K_NO_WAIT);
}

int wifi_manager_connect_ssid(const char *ssid)
{
	struct wm_cmd cmd = {.type = WM_CMD_CONNECT_PROFILE};

	if (!ssid || !ssid[0]) {
		return -EINVAL;
	}

	k_mutex_lock(&wm_lock, K_FOREVER);
	if (cur_state == WIFI_STATE_CONNECTING) {
		k_mutex_unlock(&wm_lock);
		return -EBUSY;
	}
	int index = find_profile_index_locked(ssid);

	if (index < 0) {
		k_mutex_unlock(&wm_lock);
		return -ENOENT;
	}
	cmd.profile = profiles[index];
	k_mutex_unlock(&wm_lock);

	return k_msgq_put(&wm_cmd_q, &cmd, K_NO_WAIT);
}

int wifi_manager_connect_ex(const char *ssid, const char *password,
			    const struct wifi_static_ip_cfg *static_cfg)
{
	if (!ssid || !ssid[0]) {
		return -EINVAL;
	}

	struct wifi_profile prof = {0};

	snprintf(prof.ssid, sizeof(prof.ssid), "%s", ssid);
	if (password) {
		snprintf(prof.password, sizeof(prof.password), "%s", password);
	}

	if (static_cfg) {
		prof.static_ip = *static_cfg;
	} else {
		k_mutex_lock(&wm_lock, K_FOREVER);
		int existing = find_profile_index_locked(ssid);

		if (existing >= 0) {
			prof.static_ip = profiles[existing].static_ip;
		} else {
			snprintf(prof.static_ip.netmask,
				 sizeof(prof.static_ip.netmask), "%s", AP_NETMASK);
		}
		k_mutex_unlock(&wm_lock);
	}

	int rc = wifi_manager_save_profile(NULL, &prof, false);

	if (rc) {
		return rc;
	}

	struct wm_cmd cmd = {.type = WM_CMD_CONNECT_PROFILE};

	k_mutex_lock(&wm_lock, K_FOREVER);
	int idx = find_profile_index_locked(ssid);

	if (idx >= 0) {
		cmd.profile = profiles[idx];
	} else {
		cmd.profile = prof;
	}
	k_mutex_unlock(&wm_lock);

	return k_msgq_put(&wm_cmd_q, &cmd, K_NO_WAIT);
}

int wifi_manager_connect(const char *ssid, const char *password)
{
	return wifi_manager_connect_ex(ssid, password, NULL);
}

int wifi_manager_disconnect(void)
{
	struct wm_cmd cmd = {.type = WM_CMD_DISCONNECT};

	return k_msgq_put(&wm_cmd_q, &cmd, K_NO_WAIT);
}

static void json_escape_copy(char *dst, size_t dst_sz, const char *src)
{
	size_t out = 0;

	if (!dst || dst_sz == 0) {
		return;
	}
	if (!src) {
		dst[0] = '\0';
		return;
	}
	for (size_t i = 0; src[i] != '\0' && out + 1 < dst_sz; i++) {
		unsigned char c = (unsigned char)src[i];

		if (c == '"' || c == '\\') {
			if (out + 2 >= dst_sz) {
				break;
			}
			dst[out++] = '\\';
			dst[out++] = (char)c;
		} else if (c >= 0x20) {
			dst[out++] = (char)c;
		}
	}
	dst[out] = '\0';
}

int wifi_manager_get_status_json(char *buf, size_t buf_size)
{
	struct wifi_info info;
	char ssid_esc[68], saved_esc[68];

	wifi_manager_get_info(&info);
	json_escape_copy(ssid_esc, sizeof(ssid_esc), info.ssid);
	json_escape_copy(saved_esc, sizeof(saved_esc), info.saved_ssid);

	const char *mode_str = "OFF";

	switch (info.state) {
	case WIFI_STATE_CONNECTING:
		mode_str = "CONNECTING";
		break;
	case WIFI_STATE_CONNECTED:
		mode_str = "STA";
		break;
	case WIFI_STATE_AP:
		mode_str = "AP";
		break;
	default:
		break;
	}

	return snprintf(buf, buf_size,
			"{\"mode\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\","
			"\"rssi\":%d,\"saved_ssid\":\"%s\",\"saved_count\":%u,"
			"\"static_en\":%s,\"static_ip\":\"%s\","
			"\"netmask\":\"%s\",\"gateway\":\"%s\"}",
			mode_str, ssid_esc, info.ip, info.rssi, saved_esc,
			info.saved_count,
			info.static_ip.enabled ? "true" : "false",
			info.static_ip.ip, info.static_ip.netmask,
			info.static_ip.gateway);
}

int wifi_manager_get_profiles_json(char *buf, size_t buf_size)
{
	struct wifi_profile list[WIFI_MAX_PROFILES];
	uint8_t n;

	k_mutex_lock(&wm_lock, K_FOREVER);
	n = profile_count;
	if (n > 0) {
		memcpy(list, profiles, n * sizeof(profiles[0]));
	}
	k_mutex_unlock(&wm_lock);

	int pos = snprintf(buf, buf_size, "{\"max\":%d,\"profiles\":[",
			   WIFI_MAX_PROFILES);

	if (pos < 0 || pos >= (int)buf_size) {
		return -ENOSPC;
	}

	for (uint8_t i = 0; i < n; i++) {
		char ssid_esc[68];

		json_escape_copy(ssid_esc, sizeof(ssid_esc), list[i].ssid);
		int remain = (int)buf_size - pos;

		if (remain < 140) {
			break;
		}
		if (i > 0) {
			pos += snprintf(buf + pos, remain, ",");
			remain = (int)buf_size - pos;
		}
		pos += snprintf(buf + pos, remain,
				"{\"index\":%u,\"ssid\":\"%s\",\"has_psk\":%s,"
				"\"static_en\":%s,\"static_ip\":\"%s\","
				"\"netmask\":\"%s\",\"gateway\":\"%s\"}",
				i, ssid_esc,
				list[i].password[0] ? "true" : "false",
				list[i].static_ip.enabled ? "true" : "false",
				list[i].static_ip.ip,
				list[i].static_ip.netmask,
				list[i].static_ip.gateway);
	}

	pos += snprintf(buf + pos, buf_size - pos, "]}");
	return pos;
}

int wifi_manager_start_scan(void)
{
	struct net_if *sta = net_if_get_wifi_sta();

	if (!sta) {
		return -ENODEV;
	}
	if (wifi_manager_get_state() == WIFI_STATE_CONNECTING) {
		return -EBUSY;
	}
	return start_wifi_scan_async(sta);
}

int wifi_manager_get_scan_json(char *buf, size_t buf_size)
{
	struct wifi_scan_result dedup[MAX_SCAN_RESULTS];
	int dedup_cnt = 0;
	bool scanning;

	k_mutex_lock(&scan_lock, K_FOREVER);
	if (scan_in_progress && (k_uptime_get() - scan_start_ms) >= SCAN_TIMEOUT_MS) {
		scan_in_progress = false;
	}
	scanning = scan_in_progress;

	for (int i = 0; i < scan_result_count; i++) {
		const struct wifi_scan_result *r = &scan_results[i];

		if (r->ssid_length == 0) {
			continue;
		}
		int match = -1;

		for (int j = 0; j < dedup_cnt; j++) {
			if (dedup[j].ssid_length == r->ssid_length &&
			    memcmp(dedup[j].ssid, r->ssid, r->ssid_length) == 0) {
				match = j;
				break;
			}
		}
		if (match >= 0) {
			if (r->rssi > dedup[match].rssi) {
				dedup[match] = *r;
			}
		} else if (dedup_cnt < MAX_SCAN_RESULTS) {
			dedup[dedup_cnt++] = *r;
		}
	}
	k_mutex_unlock(&scan_lock);

	/* 按信号强度由强到弱排序 */
	for (int i = 0; i < dedup_cnt - 1; i++) {
		for (int j = i + 1; j < dedup_cnt; j++) {
			if (dedup[j].rssi > dedup[i].rssi) {
				struct wifi_scan_result tmp = dedup[i];

				dedup[i] = dedup[j];
				dedup[j] = tmp;
			}
		}
	}

	int pos = snprintf(buf, buf_size, "{\"scanning\":%s,\"ap_list\":[",
			   scanning ? "true" : "false");

	if (pos < 0 || pos >= (int)buf_size) {
		return -ENOSPC;
	}

	for (int i = 0; i < dedup_cnt; i++) {
		char raw_ssid[33];
		char esc_ssid[68];
		size_t len = MIN(dedup[i].ssid_length, sizeof(raw_ssid) - 1);

		memcpy(raw_ssid, dedup[i].ssid, len);
		raw_ssid[len] = '\0';
		json_escape_copy(esc_ssid, sizeof(esc_ssid), raw_ssid);

		int remain = (int)buf_size - pos;

		if (remain < 96) {
			break;
		}
		if (i > 0) {
			pos += snprintf(buf + pos, remain, ",");
			remain = (int)buf_size - pos;
		}
		pos += snprintf(buf + pos, remain,
				"{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}",
				esc_ssid, dedup[i].rssi,
				(dedup[i].security != WIFI_SECURITY_TYPE_NONE) ? "true" : "false");
	}

	pos += snprintf(buf + pos, buf_size - pos, "]}");
	return pos;
}

/* ── Shell 命令 ─────────────────────────────────────────────────────── */

static int cmd_wm_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	struct wifi_info info;

	wifi_manager_get_info(&info);
	shell_print(sh, "State: %d, SSID: %s, IP: %s, RSSI: %d",
		    info.state, info.ssid, info.ip, info.rssi);
	if (info.state == WIFI_STATE_AP && info.ap_psk[0]) {
		shell_print(sh, "AP PSK: %s", info.ap_psk);
	}

	k_mutex_lock(&wm_lock, K_FOREVER);
	shell_print(sh, "Saved profiles (%u/%d):", profile_count, WIFI_MAX_PROFILES);
	for (uint8_t i = 0; i < profile_count; i++) {
		if (profiles[i].static_ip.enabled) {
			shell_print(sh, "  #%u %s [static %s/%s gw %s]",
				    i + 1, profiles[i].ssid,
				    profiles[i].static_ip.ip,
				    profiles[i].static_ip.netmask,
				    profiles[i].static_ip.gateway[0]
					    ? profiles[i].static_ip.gateway : "-");
		} else {
			shell_print(sh, "  #%u %s [DHCP]", i + 1, profiles[i].ssid);
		}
	}
	k_mutex_unlock(&wm_lock);
	return 0;
}

static int cmd_wm_static(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 3) {
		shell_error(sh, "Usage: wmgr static <ssid> <on|off> [ip] [netmask] [gateway]");
		return -EINVAL;
	}

	k_mutex_lock(&wm_lock, K_FOREVER);
	int idx = find_profile_index_locked(argv[1]);

	if (idx < 0) {
		k_mutex_unlock(&wm_lock);
		shell_error(sh, "Profile '%s' not found", argv[1]);
		return -ENOENT;
	}
	struct wifi_profile prof = profiles[idx];

	k_mutex_unlock(&wm_lock);

	if (!strcmp(argv[2], "on") || !strcmp(argv[2], "1")) {
		prof.static_ip.enabled = true;
		if (argc < 4) {
			shell_error(sh, "IP required when enabling static IP");
			return -EINVAL;
		}
		snprintf(prof.static_ip.ip, sizeof(prof.static_ip.ip), "%s", argv[3]);
		snprintf(prof.static_ip.netmask, sizeof(prof.static_ip.netmask), "%s",
			 (argc >= 5) ? argv[4] : AP_NETMASK);
		snprintf(prof.static_ip.gateway, sizeof(prof.static_ip.gateway), "%s",
			 (argc >= 6) ? argv[5] : "");
	} else if (!strcmp(argv[2], "off") || !strcmp(argv[2], "0")) {
		prof.static_ip.enabled = false;
	} else {
		shell_error(sh, "Second arg must be on/off");
		return -EINVAL;
	}

	int rc = wifi_manager_save_profile(NULL, &prof, true);

	if (rc) {
		shell_error(sh, "Invalid static IP config (%d)", rc);
		return rc;
	}

	shell_print(sh, "Profile '%s' static IP %s",
		    prof.ssid, prof.static_ip.enabled ? "enabled" : "disabled (DHCP)");
	return 0;
}

static int cmd_wm_connect(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_error(sh, "Usage: wmgr connect <ssid> [password]");
		return -EINVAL;
	}

	const char *psk = (argc >= 3) ? argv[2] : "";

	wifi_manager_connect(argv[1], psk);
	shell_print(sh, "Connecting to %s...", argv[1]);
	return 0;
}

static int cmd_wm_reconnect(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	wifi_manager_reconnect();
	shell_print(sh, "Reconnecting saved WiFi in priority order...");
	return 0;
}

static int cmd_wm_scan(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int rc = wifi_manager_start_scan();

	if (rc) {
		shell_error(sh, "Start scan failed: %d", rc);
		return rc;
	}

	for (int i = 0; i < 25; i++) {
		k_sleep(K_MSEC(400));
		k_mutex_lock(&scan_lock, K_FOREVER);
		bool done = !scan_in_progress;
		k_mutex_unlock(&scan_lock);
		if (done) {
			break;
		}
	}

	static char buf[1024];

	wifi_manager_get_scan_json(buf, sizeof(buf));
	shell_print(sh, "%s", buf);
	return 0;
}

static int cmd_wm_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	wifi_manager_disconnect();
	shell_print(sh, "Cleared all WiFi profiles, switching to AP");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_wmgr,
	SHELL_CMD(status, NULL, "Show WiFi & saved profiles status", cmd_wm_status),
	SHELL_CMD(scan, NULL, "Scan WiFi and print JSON", cmd_wm_scan),
	SHELL_CMD(connect, NULL, "Connect & save WiFi: wmgr connect <ssid> [psk]", cmd_wm_connect),
	SHELL_CMD(static, NULL, "Set profile static IP: wmgr static <ssid> <on|off> [ip] [mask] [gw]",
		  cmd_wm_static),
	SHELL_CMD(reconnect, NULL, "Reconnect saved WiFi by priority", cmd_wm_reconnect),
	SHELL_CMD(clear, NULL, "Clear all saved WiFi and enter AP", cmd_wm_clear),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(wmgr, &sub_wmgr, "WiFi manager commands", NULL);
