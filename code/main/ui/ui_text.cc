#include "ui_text.hpp"
#include <string.h>
#include <string>

ui_text::ui_text(u8g2_t *u8g2, uint32_t x, uint32_t y, char const *ascii_str) : ui_base(u8g2, x, y)
{
    front_horizontal_align = left_align;
    front_vertical_align = up_align;
    if (ascii_str != NULL)
        strncpy(ascii_buffer, ascii_str, sizeof(ascii_buffer) - 1);
    front = u8g2_font_6x10_mf;
}
void ui_text::set_ascii_str(char const *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(ascii_buffer, sizeof(ascii_buffer), fmt, args);
    va_end(args);
}
void ui_text::set_font(const uint8_t *font)
{
    front = u8g2_font_6x10_mf;
}

int ui_text::render()
{
    int text_x = absolute_postion_x;
    int text_y = absolute_postion_y + u8g2_GetMaxCharHeight(u8g2);
    if (front_vertical_align == center_align && absolute_height > u8g2_GetMaxCharHeight(u8g2))
        text_y += (absolute_height - u8g2_GetMaxCharHeight(u8g2)) / 2;
    else if (front_vertical_align == down_align && absolute_height > u8g2_GetMaxCharHeight(u8g2))
        text_y += absolute_height - u8g2_GetMaxCharHeight(u8g2);
    u8g2_SetFontMode(u8g2, 1);
    u8g2_SetFont(u8g2, front);
    u8g2_DrawStr(u8g2, text_x, text_y, ascii_buffer);
    return 0;
}
