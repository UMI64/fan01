#include "ui_base.hpp"
ui_base::ui_base(u8g2_t *u8g2) : u8g2(u8g2)
{
    this->postion_x.align = left_align;
    this->postion_y.align = up_align;
    this->width.percentage = 100;
    this->width.type = percentage_length;
    this->height.percentage = 100;
    this->height.type = percentage_length;
    set_flag(this, visible); // 默认可见
    set_focus(this);
}
ui_base::ui_base(u8g2_t *u8g2, int x, int y) : u8g2(u8g2)
{
    this->postion_x.pix = x;
    this->postion_x.align = none_align;
    this->postion_y.pix = x;
    this->postion_y.align = none_align;
    this->width.percentage = 100;
    this->width.type = percentage_length;
    this->height.percentage = 100;
    this->height.type = percentage_length;
    set_flag(this, visible); // 默认可见
    set_focus(this);
}
ui_base::ui_base(u8g2_t *u8g2, int x, int y, int width, int height) : u8g2(u8g2)
{
    this->postion_x.pix = x;
    this->postion_x.align = none_align;
    this->postion_y.pix = x;
    this->postion_y.align = none_align;
    this->width.pix = width;
    this->width.type = pix_length;
    this->height.pix = height;
    this->height.type = pix_length;
    set_flag(this, visible); // 默认可见
    set_focus(this);
}
ui_base *ui_base::append_component(ui_base *ui_component)
{
    childs.push_back(ui_component);
    return ui_component;
}
ui_base *ui_base::remove_component(ui_base *ui_component)
{
    ui_base *ret = nullptr;
    for (auto it = childs.begin(); it != childs.end(); ++it)
    {
        if (*it == ui_component)
        {
            ret = *it;
            childs.erase(it);
            break;
        }
    }
    return ret; // 返回被移除的组件
}
void ui_base::set_focus(ui_base *ui_base)
{
    this->focus = ui_base;
}
void ui_base::set_keyevent_cb(key_event_cb_t key_event_cb)
{
    this->key_event_cb = key_event_cb;
}
int ui_base::render()
{
    return 0;
}

bool ui_base::read_flag(ui_base *ui_base, uint32_t flag)
{
    return (ui_base->flags & flag) > 0;
}
bool ui_base::read_flag(ui_base *ui_base, uint32_t flag, uint32_t mask)
{
    return (ui_base->flags & mask) == flag;
}
bool ui_base::set_flag(ui_base *ui_base, uint32_t flag)
{
    uint32_t old = read_flag(ui_base, flag);
    ui_base->flags |= flag;
    return old;
}
bool ui_base::unset_flag(ui_base *ui_base, uint32_t flag)
{
    uint32_t old = read_flag(ui_base, flag);
    ui_base->flags &= ~flag;
    return old;
}
void ui_base::forward_render(ui_base *ui_base)
{
    { // 计算位置
        if (ui_base->parent)
        {
            int offset_x, offset_y;
            switch (ui_base->postion_x.align)
            {
            case left_align:
                offset_x = 0;
                break;
            case right_align:
                offset_x = 0;
                break;
            case center_align:
                offset_x = 0;
                break;
            default:
                offset_x = ui_base->postion_x.pix;
            }
            switch (ui_base->postion_y.align)
            {
            case up_align:
                offset_y = 0;
                break;
            case down_align:
                offset_y = 0;
                break;
            case center_align:
                offset_y = 0;
                break;
            default:
                offset_y = ui_base->postion_y.pix;
            }
            ui_base->absolute_postion_x = ui_base->parent->absolute_postion_x + offset_x;
            ui_base->absolute_postion_y = ui_base->parent->absolute_postion_y + offset_y;
        }
        else
        {
            ui_base->absolute_postion_x = ui_base->postion_x.pix;
            ui_base->absolute_postion_y = ui_base->postion_y.pix;
        }
    }
    { // 计算长宽
        if (ui_base->width.type == percentage_length)
            ui_base->absolute_width = ui_base->parent ? (ui_base->parent->absolute_width * ui_base->width.percentage / 100) : (0);
        else
            ui_base->absolute_width = ui_base->width.pix;
        if (ui_base->height.type == percentage_length)
            ui_base->absolute_height = ui_base->parent ? (ui_base->parent->absolute_height * ui_base->height.percentage / 100) : (0);
        else
            ui_base->absolute_height = ui_base->height.pix;
    }

    switch (ui_base->width.type)
    {
    case pix_length:
        ui_base->absolute_width = ui_base->width.pix;
        break;
    case percentage_length:
        ui_base->absolute_width = ui_base->width.pix;
        break;
    }

    ui_base->render();
    for (auto child : ui_base->childs)
    {
        if (read_flag(child, visible, hidden | visible))
            forward_render(child);
        else
            break;
    }
}
void ui_base::forward_keyevent(ui_base *ui_base, uint32_t key, uint32_t key_continue_ms, bool press, bool change)
{
    if (ui_base->focus == nullptr || (ui_base->focus && ui_base->focus == ui_base))
        back_forward_keyevent(ui_base, key, key_continue_ms, press, change);
    else
        forward_keyevent(ui_base->focus, key, key_continue_ms, press, change);
}
void ui_base::back_forward_keyevent(ui_base *ui_base, uint32_t key, uint32_t key_continue_ms, bool press, bool change)
{
    if (ui_base->key_event_cb)
    {
        if (ui_base->key_event_cb(key, key_continue_ms, press, change) == false)
            return;
    }
    if (ui_base->parent != nullptr)
        back_forward_keyevent(ui_base->parent, key, key_continue_ms, press, change);
}
void ui_base::set_postion(uint32_t x, uint32_t y, ui_align_type align_x, ui_align_type align_y)
{
    this->postion_x.pix = x;
    this->postion_x.align = align_x;
    this->postion_y.pix = y;
    this->postion_y.align = align_y;
}
void ui_base::set_postion(uint32_t x, uint32_t y)
{
    this->postion_x.pix = x;
    this->postion_x.align = none_align;
    this->postion_y.pix = y;
    this->postion_y.align = none_align;
}
void ui_base::set_postion(ui_align_type align_x, ui_align_type align_y)
{
    this->postion_x.align = align_x;
    this->postion_y.align = align_y;
}
void ui_base::set_size(uint32_t width, uint32_t height, ui_length_type width_type, ui_length_type height_type)
{
    this->width.type = width_type;
    if (this->width.type == percentage_length)
        this->width.percentage = width;
    else
        this->width.pix = width;
    this->height.type = height_type;
    if (this->height.type == percentage_length)
        this->height.percentage = height;
    else
        this->height.pix = height;
}
