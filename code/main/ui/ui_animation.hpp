#pragma once
#include "helper.hpp"
class ui_animation
{
private:
public:
    ui_animation();
    ~ui_animation();
};
class ui_animation_bezier : public ui_animation
{
private:
public:
    ui_animation_bezier();
    ~ui_animation_bezier();
    bool add_controller_point(float x, float y, float len, float degrees);
};
