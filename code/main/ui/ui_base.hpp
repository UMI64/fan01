#pragma once
#include "u8g2.h"
#include "helper.hpp"
#include <list>
#include <functional>
class ui_base
{
public:
    typedef std::function<bool(uint32_t, uint32_t, bool, bool)> key_event_cb_t;
    typedef enum
    {
        none_align,
        left_align,
        right_align,
        up_align,
        down_align,
        center_align,
    } ui_align_type;
    typedef enum
    {
        pix_length,
        percentage_length,
    } ui_length_type;
    typedef struct
    {
        union
        {
            uint32_t pix;
            uint32_t percentage;
        };
        ui_length_type type;
    } ui_length_t;
    typedef struct
    {
        int pix;
        ui_align_type align;
    } ui_postion_t;

private:
    typedef enum
    {
        hidden = ONEBIT(0),
        visible = ONEBIT(1),
    } flags_e;
    uint32_t flags = 0;

protected:
    std::list<ui_base *> childs;
    ui_base *parent = nullptr;
    ui_base *focus = nullptr;
    int absolute_postion_x, absolute_postion_y;
    int absolute_width, absolute_height;

public:
    u8g2_t *u8g2;
    ui_postion_t postion_x, postion_y;
    ui_length_t width, height;
    key_event_cb_t key_event_cb = nullptr;
    ui_base(u8g2_t *u8g2);
    ui_base(u8g2_t *u8g2, int x, int y);
    ui_base(u8g2_t *u8g2, int x, int y, int width, int height);
    ui_base *append_component(ui_base *ui_component);
    ui_base *remove_component(ui_base *ui_component);
    virtual int render();

    void set_focus(ui_base *ui_base);
    void set_keyevent_cb(key_event_cb_t key_event_cb);
    void set_postion(uint32_t x, uint32_t y, ui_align_type align_x, ui_align_type align_y);
    void set_postion(uint32_t x, uint32_t y);
    void set_postion(ui_align_type align_x, ui_align_type align_y);
    void set_size(uint32_t width, uint32_t height, ui_length_type width_type, ui_length_type height_type);

public:
    // 读取flag值，如果flag位上的flag不等于flag，则返回false
    static bool read_flag(ui_base *ui_base, uint32_t flag);
    // 读取mask位上的flag值，如果mask位上的flag不等于flag，则返回false
    static bool read_flag(ui_base *ui_base, uint32_t flag, uint32_t mask);
    static bool set_flag(ui_base *ui_base, uint32_t flag);
    static bool unset_flag(ui_base *ui_base, uint32_t flag);
    static void forward_render(ui_base *ui_base);
    static void forward_keyevent(ui_base *ui_base, uint32_t key, uint32_t key_continue_ms, bool press, bool change);
    static void back_forward_keyevent(ui_base *ui_base, uint32_t key, uint32_t key_continue_ms, bool press, bool change);
};