#include "ui_base.hpp"
ui_base::ui_base(u8g2_t *u8g2) : u8g2(u8g2)
{
    set_flag(this, visible); // 默认可见
    set_focus(this);
}
ui_base::ui_base(u8g2_t *u8g2, int x, int y) : u8g2(u8g2)
{
    relative_x = x;
    relative_y = y;
    absolute_x = x;
    absolute_y = y;
    set_flag(this, visible); // 默认可见
    set_focus(this);
}
ui_base::ui_base(u8g2_t *u8g2, int x, int y, int width, int height) : u8g2(u8g2), width(width), height(height)
{
    relative_x = x;
    relative_y = y;
    absolute_x = x;
    absolute_y = y;
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
int ui_base::render(ui_base *parent)
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
void ui_base::forward_render(ui_base *parent, ui_base *self)
{
    if (parent)
    {
        self->absolute_x = parent->absolute_x + self->relative_x;
        self->absolute_y = parent->absolute_y + self->relative_y;
    }
    self->render(parent);
    for (auto child : self->childs)
    {
        if (read_flag(child, visible, hidden | visible))
            forward_render(self, child);
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
void ui_base::set_postion(uint32_t x, uint32_t y)
{
    this->relative_x = x;
    this->relative_y = y;
}
void ui_base::set_size(uint32_t width, uint32_t height)
{
    this->width = width;
    this->height = height;
}
