#pragma once
#include "ui_base.hpp"
class ui_button : public ui_base
{
private:
    char ascii_buffer[256] = {0};
    uint8_t const *front;
    int render();

public:
    ui_button(u8g2_t *u8g2, uint32_t x = 0, uint32_t y = 0, char const *ascii_str = nullptr);
    void set_ascii_str(char const *fmt, ...);
    void set_font(const uint8_t *font);
};
