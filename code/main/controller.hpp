#include "ui.hpp"
#include "devices/board.hpp"
#include "devices/fan.hpp"
#include "devices/husb238.hpp"
#include "devices/keyboard.hpp"
#include "devices/lcd.hpp"
class controller
{
private:
    ui *ui_obj;
    thread_helper *main_thread;
    void main_task(void *param);

public:
    board *board_obj;
    fan *fan_obj;
    husb238 *husb238_obj;
    keyboard *keyboard_obj;
    lcd *lcd_obj;
    controller();
    ~controller();
    void run();
};