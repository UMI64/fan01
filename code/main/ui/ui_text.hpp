#pragma once
#include "ui_base.hpp"
/*
 <prefix> '_' <name> '_' <purpose> <char set>
 prefix:基本上都是 u8g2；
 name:一般会挂钩上字符像素使用量，比如5X7
 purpose: t(transparent)\h(height)\m(monospace)\8(8x8pixe)
 char set: f(256)/r(regular)/u(uppercase)/n(numers)
*/

class ui_text : public ui_base
{
private:
    char ascii_buffer[256] = {0};
    uint8_t const *front;
    int render(ui_base *parent);
    ui_base::align front_horizontal_align;
    ui_base::align front_vertical_align;

public:
    ui_text(u8g2_t *u8g2, uint32_t x = 0, uint32_t y = 0, char const *ascii_str = nullptr);
    void set_ascii_str(char const *fmt, ...);
    void set_font(const uint8_t *font);
};