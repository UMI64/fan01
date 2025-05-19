#include "ui_button.hpp"
#include <string.h>
#include <string>

ui_button::ui_button(u8g2_t *u8g2, uint32_t x, uint32_t y, char const *ascii_str) : ui_base(u8g2, x, y)
{
    if (ascii_str != NULL)
        strncpy(ascii_buffer, ascii_str, sizeof(ascii_buffer) - 1);
    front = u8g2_font_6x10_mf;
}
void ui_button::set_ascii_str(char const *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(ascii_buffer, sizeof(ascii_buffer), fmt, args);
    va_end(args);
}
void ui_button::set_font(const uint8_t *font)
{
    front = u8g2_font_6x10_mf;
}

int ui_button::render()
{
    u8g2_SetFontMode(u8g2, 1);
    u8g2_SetFont(u8g2, front);
    u8g2_DrawStr(u8g2, absolute_postion_x, absolute_postion_y, ascii_buffer);
    return 0;
}
