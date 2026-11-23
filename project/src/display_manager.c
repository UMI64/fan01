/*
 * Copyright (c) 2024 umi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "display_manager.h"
#include "fan_ctrl.h"
#include "settings_manager.h"
#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(display_mgr, LOG_LEVEL_INF);

#define DISPLAY_NODE  DT_CHOSEN(zephyr_display)
#define OLED_I2C_BUS  DT_BUS(DISPLAY_NODE)
#define OLED_I2C_ADDR DT_REG_ADDR(DISPLAY_NODE)
#define DISP_W        128
#define DISP_H        32
#define DISP_PAGES    (DISP_H / 8)
#define DISP_BUF_SZ   (DISP_W * DISP_PAGES)

#define ANIM_DURATION_MS 220
#define RENDER_ANIM_MS   16
#define RENDER_BLINK_MS  100
#define RENDER_IDLE_MS   120
#define WIFI_BLINK_MS    300

static const struct device *display_dev;
static const struct device *oled_i2c_dev;
static bool display_ready;
static bool boot_sync_done;
static uint8_t frame_buf[DISP_BUF_SZ];

static const struct display_buffer_descriptor frame_desc = {
	.buf_size = DISP_BUF_SZ,
	.width = DISP_W,
	.height = DISP_H,
	.pitch = DISP_W,
};

/* 标准 5x7 ASCII 点阵字体（按列排列，6px 等宽步进） */
static const uint8_t font_5x7[95][5] = {
	{0x00, 0x00, 0x00, 0x00, 0x00}, /* ' ' */
	{0x00, 0x00, 0x5F, 0x00, 0x00}, /* '!' */
	{0x00, 0x07, 0x00, 0x07, 0x00}, /* '"' */
	{0x14, 0x7F, 0x14, 0x7F, 0x14}, /* '#' */
	{0x24, 0x2A, 0x7F, 0x2A, 0x12}, /* '$' */
	{0x23, 0x13, 0x08, 0x64, 0x62}, /* '%' */
	{0x36, 0x49, 0x55, 0x22, 0x50}, /* '&' */
	{0x00, 0x05, 0x03, 0x00, 0x00}, /* ''' */
	{0x00, 0x1C, 0x22, 0x41, 0x00}, /* '(' */
	{0x00, 0x41, 0x22, 0x1C, 0x00}, /* ')' */
	{0x14, 0x08, 0x3E, 0x08, 0x14}, /* '*' */
	{0x08, 0x08, 0x3E, 0x08, 0x08}, /* '+' */
	{0x00, 0x50, 0x30, 0x00, 0x00}, /* ',' */
	{0x08, 0x08, 0x08, 0x08, 0x08}, /* '-' */
	{0x00, 0x60, 0x60, 0x00, 0x00}, /* '.' */
	{0x20, 0x10, 0x08, 0x04, 0x02}, /* '/' */
	{0x3E, 0x51, 0x49, 0x45, 0x3E}, /* '0' */
	{0x00, 0x42, 0x7F, 0x40, 0x00}, /* '1' */
	{0x42, 0x61, 0x51, 0x49, 0x46}, /* '2' */
	{0x21, 0x41, 0x45, 0x4B, 0x31}, /* '3' */
	{0x18, 0x14, 0x12, 0x7F, 0x10}, /* '4' */
	{0x27, 0x45, 0x45, 0x45, 0x39}, /* '5' */
	{0x3C, 0x4A, 0x49, 0x49, 0x30}, /* '6' */
	{0x01, 0x71, 0x09, 0x05, 0x03}, /* '7' */
	{0x36, 0x49, 0x49, 0x49, 0x36}, /* '8' */
	{0x06, 0x49, 0x49, 0x29, 0x1E}, /* '9' */
	{0x00, 0x36, 0x36, 0x00, 0x00}, /* ':' */
	{0x00, 0x56, 0x36, 0x00, 0x00}, /* ';' */
	{0x08, 0x14, 0x22, 0x41, 0x00}, /* '<' */
	{0x14, 0x14, 0x14, 0x14, 0x14}, /* '=' */
	{0x00, 0x41, 0x22, 0x14, 0x08}, /* '>' */
	{0x02, 0x01, 0x51, 0x09, 0x06}, /* '?' */
	{0x32, 0x49, 0x79, 0x41, 0x3E}, /* '@' */
	{0x7E, 0x11, 0x11, 0x11, 0x7E}, /* 'A' */
	{0x7F, 0x49, 0x49, 0x49, 0x36}, /* 'B' */
	{0x3E, 0x41, 0x41, 0x41, 0x22}, /* 'C' */
	{0x7F, 0x41, 0x41, 0x22, 0x1C}, /* 'D' */
	{0x7F, 0x49, 0x49, 0x49, 0x41}, /* 'E' */
	{0x7F, 0x09, 0x09, 0x09, 0x01}, /* 'F' */
	{0x3E, 0x41, 0x49, 0x49, 0x7A}, /* 'G' */
	{0x7F, 0x08, 0x08, 0x08, 0x7F}, /* 'H' */
	{0x00, 0x41, 0x7F, 0x41, 0x00}, /* 'I' */
	{0x20, 0x40, 0x41, 0x3F, 0x01}, /* 'J' */
	{0x7F, 0x08, 0x14, 0x22, 0x41}, /* 'K' */
	{0x7F, 0x40, 0x40, 0x40, 0x40}, /* 'L' */
	{0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* 'M' */
	{0x7F, 0x04, 0x08, 0x10, 0x7F}, /* 'N' */
	{0x3E, 0x41, 0x41, 0x41, 0x3E}, /* 'O' */
	{0x7F, 0x09, 0x09, 0x09, 0x06}, /* 'P' */
	{0x3E, 0x41, 0x51, 0x21, 0x5E}, /* 'Q' */
	{0x7F, 0x09, 0x19, 0x29, 0x46}, /* 'R' */
	{0x46, 0x49, 0x49, 0x49, 0x31}, /* 'S' */
	{0x01, 0x01, 0x7F, 0x01, 0x01}, /* 'T' */
	{0x3F, 0x40, 0x40, 0x40, 0x3F}, /* 'U' */
	{0x1F, 0x20, 0x40, 0x20, 0x1F}, /* 'V' */
	{0x3F, 0x40, 0x38, 0x40, 0x3F}, /* 'W' */
	{0x63, 0x14, 0x08, 0x14, 0x63}, /* 'X' */
	{0x07, 0x08, 0x70, 0x08, 0x07}, /* 'Y' */
	{0x61, 0x51, 0x49, 0x45, 0x43}, /* 'Z' */
	{0x00, 0x7F, 0x41, 0x41, 0x00}, /* '[' */
	{0x02, 0x04, 0x08, 0x10, 0x20}, /* '\' */
	{0x00, 0x41, 0x41, 0x7F, 0x00}, /* ']' */
	{0x04, 0x02, 0x01, 0x02, 0x04}, /* '^' */
	{0x40, 0x40, 0x40, 0x40, 0x40}, /* '_' */
	{0x00, 0x01, 0x02, 0x04, 0x00}, /* '`' */
	{0x20, 0x54, 0x54, 0x54, 0x78}, /* 'a' */
	{0x7F, 0x48, 0x44, 0x44, 0x38}, /* 'b' */
	{0x38, 0x44, 0x44, 0x44, 0x20}, /* 'c' */
	{0x38, 0x44, 0x44, 0x48, 0x7F}, /* 'd' */
	{0x38, 0x54, 0x54, 0x54, 0x18}, /* 'e' */
	{0x08, 0x7E, 0x09, 0x01, 0x02}, /* 'f' */
	{0x0C, 0x52, 0x52, 0x52, 0x3E}, /* 'g' */
	{0x7F, 0x08, 0x04, 0x04, 0x78}, /* 'h' */
	{0x00, 0x44, 0x7D, 0x40, 0x00}, /* 'i' */
	{0x20, 0x40, 0x44, 0x3D, 0x00}, /* 'j' */
	{0x7F, 0x10, 0x28, 0x44, 0x00}, /* 'k' */
	{0x00, 0x41, 0x7F, 0x40, 0x00}, /* 'l' */
	{0x7C, 0x04, 0x18, 0x04, 0x78}, /* 'm' */
	{0x7C, 0x08, 0x04, 0x04, 0x78}, /* 'n' */
	{0x38, 0x44, 0x44, 0x44, 0x38}, /* 'o' */
	{0x7C, 0x14, 0x14, 0x14, 0x08}, /* 'p' */
	{0x08, 0x14, 0x14, 0x18, 0x7C}, /* 'q' */
	{0x7C, 0x08, 0x04, 0x04, 0x08}, /* 'r' */
	{0x48, 0x54, 0x54, 0x54, 0x20}, /* 's' */
	{0x04, 0x3F, 0x44, 0x40, 0x20}, /* 't' */
	{0x3C, 0x40, 0x40, 0x20, 0x7C}, /* 'u' */
	{0x1C, 0x20, 0x40, 0x20, 0x1C}, /* 'v' */
	{0x3C, 0x40, 0x30, 0x40, 0x3C}, /* 'w' */
	{0x44, 0x28, 0x10, 0x28, 0x44}, /* 'x' */
	{0x0C, 0x50, 0x50, 0x50, 0x3C}, /* 'y' */
	{0x44, 0x64, 0x54, 0x4C, 0x44}, /* 'z' */
	{0x00, 0x08, 0x36, 0x41, 0x00}, /* '{' */
	{0x00, 0x00, 0x7F, 0x00, 0x00}, /* '|' */
	{0x00, 0x41, 0x36, 0x08, 0x00}, /* '}' */
	{0x08, 0x04, 0x08, 0x10, 0x08}, /* '~' */
};

/* 11x9 WiFi 图标（按列排列，每列 2 字节，bit0..bit8） */
static const uint16_t wifi_icon_cols[11] = {
	0x004, /* col 0:   .     */
	0x00A, /* col 1:  . .    */
	0x011, /* col 2: .   .   */
	0x024, /* col 3:         */
	0x04A, /* col 4:         */
	0x155, /* col 5: center  */
	0x04A, /* col 6:         */
	0x024, /* col 7:         */
	0x011, /* col 8:         */
	0x00A, /* col 9:         */
	0x004, /* col 10:        */
};

/* ── 动画插值 (Cubic Ease-Out) ────────────────────────────────────── */

struct anim_coord {
	int start;
	int target;
	int current;
	int64_t start_ms;
	int duration_ms;
};

enum disp_pwr_state {
	DISP_PWR_FULL = 0,
	DISP_PWR_DIM,
	DISP_PWR_OFF,
};

static K_MUTEX_DEFINE(ui_lock);
static K_SEM_DEFINE(ui_wake_sem, 0, 1);

static bool on_wifi_page;
static struct anim_coord main_y = {.target = 0, .current = 0};
static struct anim_coord wifi_y = {.target = DISP_H, .current = DISP_H};

static enum disp_pwr_state pwr_state = DISP_PWR_FULL;
static int64_t last_activity_ms;
static bool cfg_dirty = true;
static uint8_t applied_contrast = 255;

static void anim_set_target(struct anim_coord *a, int target, int duration_ms)
{
	a->start = a->current;
	a->target = target;
	a->start_ms = k_uptime_get();
	a->duration_ms = duration_ms;
}

static bool anim_step(struct anim_coord *a, int64_t now_ms)
{
	if (a->current == a->target) {
		return false;
	}

	if (a->duration_ms <= 0 || (now_ms - a->start_ms) >= a->duration_ms) {
		a->current = a->target;
		return false;
	}

	float t = (float)(now_ms - a->start_ms) / (float)a->duration_ms;
	float inv = 1.0f - t;
	float ease = 1.0f - inv * inv * inv;

	a->current = a->start + (int)((float)(a->target - a->start) * ease +
				      ((a->target >= a->start) ? 0.5f : -0.5f));
	return true;
}

/* ── 帧缓冲绘图原语 ───────────────────────────────────────────────── */

static void fb_set_pixel(int x, int y)
{
	if (x < 0 || x >= DISP_W || y < 0 || y >= DISP_H) {
		return;
	}
	frame_buf[(y >> 3) * DISP_W + x] |= BIT(y & 0x07);
}

static void fb_fill_rect(int x, int y, int w, int h)
{
	for (int dy = 0; dy < h; dy++) {
		for (int dx = 0; dx < w; dx++) {
			fb_set_pixel(x + dx, y + dy);
		}
	}
}

static void fb_draw_char(int x, int y, char c)
{
	if (c < 0x20 || c > 0x7E) {
		c = '?';
	}
	const uint8_t *glyph = font_5x7[c - 0x20];

	for (int col = 0; col < 5; col++) {
		uint8_t bits = glyph[col];

		for (int row = 0; row < 7; row++) {
			if (bits & BIT(row)) {
				fb_set_pixel(x + col, y + row);
			}
		}
	}
}

static void fb_draw_str(int x, int y, const char *str)
{
	while (*str) {
		fb_draw_char(x, y, *str++);
		x += 6;
	}
}

static void fb_draw_str_clip(int x, int y, const char *str, int max_chars)
{
	for (int i = 0; i < max_chars && str[i] != '\0'; i++) {
		fb_draw_char(x + i * 6, y, str[i]);
	}
}

/*
 * 12x20 粗体圆角七段数字渲染（用于主页面左上角大号转速显示）
 * 7段编码 bit0..bit6: a(top), b(top-R), c(bot-R), d(bot), e(bot-L), f(top-L), g(mid)
 */
static void fb_draw_big_digit(int x, int y, uint8_t digit)
{
	static const uint8_t seg_lut[10] = {
		0x3F, /* 0: a b c d e f */
		0x06, /* 1: b c */
		0x5B, /* 2: a b d e g */
		0x4F, /* 3: a b c d g */
		0x66, /* 4: b c f g */
		0x6D, /* 5: a c d f g */
		0x7D, /* 6: a c d e f g */
		0x07, /* 7: a b c */
		0x7F, /* 8: all */
		0x6F, /* 9: a b c d f g */
	};

	if (digit > 9) {
		return;
	}

	uint8_t s = seg_lut[digit];

	if (s & BIT(0)) { /* a: 顶横 */
		fb_fill_rect(x + 1, y + 0, 10, 3);
	}
	if (s & BIT(1)) { /* b: 右上竖 */
		fb_fill_rect(x + 9, y + 1, 3, 8);
	}
	if (s & BIT(2)) { /* c: 右下竖 */
		fb_fill_rect(x + 9, y + 11, 3, 8);
	}
	if (s & BIT(3)) { /* d: 底横 */
		fb_fill_rect(x + 1, y + 17, 10, 3);
	}
	if (s & BIT(4)) { /* e: 左下竖 */
		fb_fill_rect(x + 0, y + 11, 3, 8);
	}
	if (s & BIT(5)) { /* f: 左上竖 */
		fb_fill_rect(x + 0, y + 1, 3, 8);
	}
	if (s & BIT(6)) { /* g: 中横 */
		fb_fill_rect(x + 1, y + 8, 10, 3);
	}
}

/* 右对齐绘制大号 RPM，保证个/十/百/千/万位 X 坐标固定不跳动 */
static void fb_draw_big_rpm_right(int right_x, int y, uint16_t rpm)
{
	char digits[6];
	int len = snprintf(digits, sizeof(digits), "%u", rpm);
	int x = right_x - 12;

	for (int i = len - 1; i >= 0; i--) {
		fb_draw_big_digit(x, y, (uint8_t)(digits[i] - '0'));
		x -= 14;
	}
}

static void fb_draw_wifi_icon(int x, int y)
{
	/* 简洁清晰的 11x9 扇形 WiFi 图标 */
	static const uint16_t icon[11] = {
		0x004, /* .       */
		0x002, /* .       */
		0x019, /* arcs    */
		0x024,
		0x082,
		0x149, /* dot+top */
		0x082,
		0x024,
		0x019,
		0x002,
		0x004,
	};

	for (int col = 0; col < 11; col++) {
		uint16_t bits = icon[col];

		for (int row = 0; row < 9; row++) {
			if (bits & BIT(row)) {
				fb_set_pixel(x + col, y + row);
			}
		}
	}
}

static void fb_draw_ap_icon(int x, int y)
{
	/* AP 发射塔图标 11x9 */
	fb_fill_rect(x + 5, y + 3, 1, 6);
	fb_fill_rect(x + 4, y + 2, 3, 2);
	fb_set_pixel(x + 2, y + 1);
	fb_set_pixel(x + 1, y + 2);
	fb_set_pixel(x + 1, y + 3);
	fb_set_pixel(x + 2, y + 4);
	fb_set_pixel(x + 8, y + 1);
	fb_set_pixel(x + 9, y + 2);
	fb_set_pixel(x + 9, y + 3);
	fb_set_pixel(x + 8, y + 4);
}

static void fb_draw_rssi_bars(int x, int y, int bars)
{
	/* 4 阶信号柱 (11x7，与上方 11x9 WiFi 图标左对齐) */
	static const uint8_t heights[4] = {2, 4, 6, 7};

	for (int i = 0; i < 4; i++) {
		int bx = x + i * 3;
		int h = heights[i];

		if (i < bars) {
			fb_fill_rect(bx, y + 7 - h, 2, h);
		} else {
			/* 未达到的柱位画 1px 底座点，提示总格数 */
			fb_fill_rect(bx, y + 6, 2, 1);
		}
	}
}

static int rssi_to_bars(int8_t rssi)
{
	if (rssi >= 0) {
		return 0;
	}
	if (rssi >= -55) {
		return 4;
	}
	if (rssi >= -67) {
		return 3;
	}
	if (rssi >= -78) {
		return 2;
	}
	if (rssi >= -90) {
		return 1;
	}
	return 0;
}

/* ── 页面绘制 ─────────────────────────────────────────────────────── */

static void draw_main_page(int oy, int64_t now_ms, const struct wifi_info *winfo)
{
	if (oy <= -DISP_H || oy >= DISP_H) {
		return;
	}

	/* 左上角: 实时转速大数字（右对齐在 x=78，防止位数变化跳动） */
	fb_draw_big_rpm_right(78, oy + 1, fan_ctrl_get_speed());

	/* 左下角: 开关状态放左边，目标转速右对齐放右边 */
	fb_draw_str(0, oy + 24, fan_ctrl_get_switch() ? "ON" : "OFF");

	char target_str[20];
	int tlen = snprintf(target_str, sizeof(target_str), "TS:%u",
			    fan_ctrl_get_target_speed());
	int tx = 79 - tlen * 6;

	if (tx < 22) {
		tx = 22;
	}
	fb_draw_str(tx, oy + 24, target_str);

	/* 右侧状态栏分隔线 */
	fb_fill_rect(82, oy + 1, 1, 30);

	/* 右侧状态栏 (x=85..127)，不显示 IP */
	bool blink_on = ((now_ms / WIFI_BLINK_MS) % 2) == 0;

	if (winfo->state == WIFI_STATE_CONNECTING) {
		if (blink_on) {
			fb_draw_wifi_icon(86, oy + 1);
		}
		fb_draw_str(101, oy + 2, "LINK");
		fb_draw_rssi_bars(86, oy + 13, 0);
		fb_draw_str(101, oy + 13, "--");
	} else if (winfo->state == WIFI_STATE_CONNECTED) {
		char rstr[8];

		fb_draw_wifi_icon(86, oy + 1);
		fb_draw_str(101, oy + 2, "STA");
		fb_draw_rssi_bars(86, oy + 13, rssi_to_bars(winfo->rssi));
		if (winfo->rssi < 0) {
			snprintf(rstr, sizeof(rstr), "%d", (int)winfo->rssi);
		} else {
			snprintf(rstr, sizeof(rstr), "--");
		}
		fb_draw_str_clip(101, oy + 13, rstr, 4);
	} else {
		/* AP 模式或空闲 */
		fb_draw_ap_icon(86, oy + 1);
		fb_draw_str(101, oy + 2, "AP");
		fb_draw_rssi_bars(86, oy + 13, 0);
		fb_draw_str(101, oy + 13, "--");
	}

	/* 右下角显示输入电压 (CH3) */
	char vstr[10];

	snprintf(vstr, sizeof(vstr), "%.1fV", (double)fan_ctrl_get_voltage());
	fb_draw_str_clip(86, oy + 24, vstr, 7);
}

static void draw_wifi_page(int oy, int64_t now_ms, const struct wifi_info *winfo)
{
	if (oy <= -DISP_H || oy >= DISP_H) {
		return;
	}

	bool blink_on = ((now_ms / WIFI_BLINK_MS) % 2) == 0;
	char line[48];

	if (winfo->state == WIFI_STATE_CONNECTED) {
		fb_draw_str(0, oy + 1, "WIFI: STA OK");
		fb_draw_wifi_icon(116, oy + 0);

		snprintf(line, sizeof(line), "SSID:%s", winfo->ssid);
		fb_draw_str_clip(0, oy + 12, line, 21);

		snprintf(line, sizeof(line), "IP:%s",
			 winfo->ip[0] ? winfo->ip : "0.0.0.0");
		fb_draw_str_clip(0, oy + 23, line, 21);
	} else if (winfo->state == WIFI_STATE_CONNECTING) {
		fb_draw_str(0, oy + 1, "WIFI: CONNECTING");
		if (blink_on) {
			fb_draw_wifi_icon(116, oy + 0);
		}

		snprintf(line, sizeof(line), "SSID:%s", winfo->ssid);
		fb_draw_str_clip(0, oy + 12, line, 21);
		fb_draw_str(0, oy + 23, "Connecting...");
	} else {
		fb_draw_str(0, oy + 1,
			    winfo->has_saved_creds ? "WIFI: AP [Reconn]"
						   : "WIFI: AP MODE");
		fb_draw_ap_icon(116, oy + 0);

		snprintf(line, sizeof(line), "AP:%s", winfo->ssid);
		fb_draw_str_clip(0, oy + 12, line, 9);

		if (winfo->ap_psk[0]) {
			char psk_line[20];
			int plen = snprintf(psk_line, sizeof(psk_line), "PW:%s",
					    winfo->ap_psk);
			int px = (DISP_W + 1) - plen * 6;

			if (px < 58) {
				px = 58;
			}
			fb_draw_str_clip(px, oy + 12, psk_line,
					 (DISP_W - px + 1) / 6);
		}

		snprintf(line, sizeof(line), "IP:%s",
			 winfo->ip[0] ? winfo->ip : "192.168.4.1");
		fb_draw_str_clip(0, oy + 23, line, 21);
	}
}

/* ── 渲染线程 ─────────────────────────────────────────────────────── */

static int ssd1306_i2c_sync(uint8_t contrast, bool full_init, bool turn_on)
{
	if (!oled_i2c_dev || !device_is_ready(oled_i2c_dev)) {
		return -ENODEV;
	}

	if (full_init) {
		uint8_t init_cmds[] = {
			0x00,
			0xD5, 0x80, /* Clock div ratio */
			0xA8, 0x1F, /* Multiplex 31 (128x32) */
			0xD3, 0x00, /* Display offset 0 */
			0x40,       /* Start line 0 */
			0x8D, 0x14, /* Enable internal charge pump */
			0x33,       /* Pump voltage 9V */
			0xA1,       /* Segment remap */
			0xC8,       /* COM scan flipped */
			0xDA, 0x02, /* COM pins sequential */
			0xD9, 0x22, /* Precharge period */
			0xDB, 0x20, /* VCOM deselect */
			0xA4,       /* Resume RAM content */
			0xA6,       /* Normal (non-inverted) */
			0x81, contrast,
			turn_on ? 0xAF : 0xAE,
		};

		return i2c_write(oled_i2c_dev, init_cmds, sizeof(init_cmds), OLED_I2C_ADDR);
	}

	uint8_t wake_cmds[] = {
		0x00,
		0x8D, 0x14,
		0xA4,
		0x81, contrast,
		turn_on ? 0xAF : 0xAE,
	};

	return i2c_write(oled_i2c_dev, wake_cmds, sizeof(wake_cmds), OLED_I2C_ADDR);
}

static int oled_flush_fb(uint8_t contrast)
{
	if (display_dev) {
		int ret = display_write(display_dev, 0, 0, &frame_desc, frame_buf);

		if (ret == 0) {
			return 0;
		}
		LOG_WRN("display_write failed (%d), syncing SSD1306", ret);
		ssd1306_i2c_sync(contrast, true, true);
		return display_write(display_dev, 0, 0, &frame_desc, frame_buf);
	}

	if (!oled_i2c_dev || !device_is_ready(oled_i2c_dev)) {
		return -ENODEV;
	}

	uint8_t win_cmds[] = {
		0x00,
		0x20, 0x00,
		0x21, 0x00, DISP_W - 1,
		0x22, 0x00, DISP_PAGES - 1,
	};
	int ret = i2c_write(oled_i2c_dev, win_cmds, sizeof(win_cmds), OLED_I2C_ADDR);

	if (ret != 0) {
		return ret;
	}

	static uint8_t pkt[1 + DISP_BUF_SZ];

	pkt[0] = 0x40;
	memcpy(&pkt[1], frame_buf, DISP_BUF_SZ);
	return i2c_write(oled_i2c_dev, pkt, sizeof(pkt), OLED_I2C_ADDR);
}

static void oled_apply_power(uint8_t contrast, bool on)
{
	if (display_dev) {
		if (on) {
			display_blanking_off(display_dev);
		} else {
			display_blanking_on(display_dev);
		}
	}
	if (on) {
		ssd1306_i2c_sync(contrast, false, true);
	} else if (!display_dev) {
		ssd1306_i2c_sync(contrast, false, false);
	}
}

static void oled_apply_contrast(uint8_t contrast)
{
	if (display_dev) {
		display_set_contrast(display_dev, contrast);
	} else {
		uint8_t cmd[] = { 0x00, 0x81, contrast };

		if (oled_i2c_dev && device_is_ready(oled_i2c_dev)) {
			(void)i2c_write(oled_i2c_dev, cmd, sizeof(cmd), OLED_I2C_ADDR);
		}
	}
}

static void update_display_power(int64_t now_ms)
{
	uint32_t full_s, dim_s;
	uint8_t full_bright, dim_bright;

	settings_get_display_cfg(&full_s, &dim_s, &full_bright, &dim_bright);

	enum disp_pwr_state target_state = DISP_PWR_FULL;
	uint8_t target_contrast = full_bright;

	if (full_s > 0 || dim_s > 0) {
		int64_t idle_ms = now_ms - last_activity_ms;
		int64_t full_ms = (int64_t)full_s * 1000;
		int64_t off_ms = (int64_t)(full_s + dim_s) * 1000;

		if (idle_ms >= off_ms) {
			target_state = DISP_PWR_OFF;
		} else if (idle_ms >= full_ms && dim_s > 0) {
			target_state = DISP_PWR_DIM;
			target_contrast = dim_bright;
		}
	}

	if (target_state != pwr_state || cfg_dirty) {
		if (target_state == DISP_PWR_OFF) {
			if (pwr_state != DISP_PWR_OFF) {
				LOG_INF("OLED auto sleep (OFF)");
				oled_apply_power(applied_contrast, false);
			}
		} else {
			if (pwr_state == DISP_PWR_OFF) {
				LOG_INF("OLED wake");
				oled_apply_power(target_contrast, true);
			}
			if (applied_contrast != target_contrast || cfg_dirty) {
				applied_contrast = target_contrast;
				oled_apply_contrast(target_contrast);
			}
		}
		pwr_state = target_state;
		cfg_dirty = false;
	}
}

static void display_thread_fn(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		if (!display_ready) {
			k_sleep(K_MSEC(500));
			continue;
		}

		struct wifi_info winfo;

		wifi_manager_get_info(&winfo);

		bool animating = false;
		int64_t now = k_uptime_get();

		k_mutex_lock(&ui_lock, K_FOREVER);
		update_display_power(now);
		bool is_off = (pwr_state == DISP_PWR_OFF);

		if (!is_off) {
			if (!boot_sync_done && now >= 1200) {
				ssd1306_i2c_sync(applied_contrast, true, true);
				boot_sync_done = true;
			}
			animating |= anim_step(&main_y, now);
			animating |= anim_step(&wifi_y, now);

			memset(frame_buf, 0, sizeof(frame_buf));
			draw_main_page(main_y.current, now, &winfo);
			draw_wifi_page(wifi_y.current, now, &winfo);
		}
		k_mutex_unlock(&ui_lock);

		if (!is_off) {
			if (oled_flush_fb(applied_contrast) != 0) {
				ssd1306_i2c_sync(applied_contrast, true, true);
			}
		}

		int wait_ms = RENDER_IDLE_MS;

		if (is_off) {
			wait_ms = 250;
		} else if (animating) {
			wait_ms = RENDER_ANIM_MS;
		} else if (winfo.state == WIFI_STATE_CONNECTING) {
			wait_ms = RENDER_BLINK_MS;
		}

		k_sem_take(&ui_wake_sem, K_MSEC(wait_ms));
	}
}

K_THREAD_DEFINE(display_thread_id, 2048, display_thread_fn,
		NULL, NULL, NULL, 7, 0, 100);

/* ── 公共 API ──────────────────────────────────────────────────────── */

int display_manager_init(void)
{
	ARG_UNUSED(wifi_icon_cols);

	oled_i2c_dev = DEVICE_DT_GET(OLED_I2C_BUS);
	display_dev = DEVICE_DT_GET(DISPLAY_NODE);
	if (!device_is_ready(display_dev)) {
		LOG_WRN("Zephyr ssd1306 device not ready, using direct I2C fallback");
		display_dev = NULL;
	}

	uint8_t full_bright = 255;

	settings_get_display_cfg(NULL, NULL, &full_bright, NULL);
	applied_contrast = full_bright;
	last_activity_ms = k_uptime_get();
	pwr_state = DISP_PWR_FULL;
	cfg_dirty = false;
	boot_sync_done = false;

	if (display_dev) {
		/* 使用正常显示模式（黑底亮字），避免反显白底导致烧屏 */
		if (display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO01) != 0) {
			LOG_WRN("Failed to set PIXEL_FORMAT_MONO01");
		}
		display_set_contrast(display_dev, full_bright);
		display_blanking_off(display_dev);
	}

	if (ssd1306_i2c_sync(full_bright, true, true) != 0 && !display_dev) {
		LOG_ERR("Display I2C init failed");
		return -ENODEV;
	}

	memset(frame_buf, 0, sizeof(frame_buf));
	(void)oled_flush_fb(full_bright);
	display_ready = true;
	k_sem_give(&ui_wake_sem);

	LOG_INF("Display manager initialized (SSD1306 128x32 normal mode)");
	return 0;
}

void display_manager_toggle_page(void)
{
	k_mutex_lock(&ui_lock, K_FOREVER);
	last_activity_ms = k_uptime_get();
	on_wifi_page = !on_wifi_page;
	if (on_wifi_page) {
		anim_set_target(&main_y, -DISP_H, ANIM_DURATION_MS);
		anim_set_target(&wifi_y, 0, ANIM_DURATION_MS);
	} else {
		anim_set_target(&main_y, 0, ANIM_DURATION_MS);
		anim_set_target(&wifi_y, DISP_H, ANIM_DURATION_MS);
	}
	k_mutex_unlock(&ui_lock);
	k_sem_give(&ui_wake_sem);
}

bool display_manager_is_wifi_page(void)
{
	bool res;

	k_mutex_lock(&ui_lock, K_FOREVER);
	res = on_wifi_page;
	k_mutex_unlock(&ui_lock);
	return res;
}

void display_manager_request_redraw(void)
{
	k_sem_give(&ui_wake_sem);
}

bool display_manager_wake(void)
{
	bool was_off;

	k_mutex_lock(&ui_lock, K_FOREVER);
	was_off = (pwr_state == DISP_PWR_OFF);
	last_activity_ms = k_uptime_get();
	cfg_dirty = true;
	k_mutex_unlock(&ui_lock);
	k_sem_give(&ui_wake_sem);
	return was_off;
}

void display_manager_reload_cfg(void)
{
	k_mutex_lock(&ui_lock, K_FOREVER);
	last_activity_ms = k_uptime_get();
	cfg_dirty = true;
	k_mutex_unlock(&ui_lock);
	k_sem_give(&ui_wake_sem);
}
