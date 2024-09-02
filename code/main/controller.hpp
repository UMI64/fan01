// #include "leida_wifi.hpp"
// #include "leida_web.hpp"
#include "board.hpp"
#include "helper.hpp"
class controller
{
private:
    board *board_obj;
    thread_helper *main_thread;
    void main_task(void *param);
    void keyboard_callback(keyboard::keys key, keyboard::actions action);
public:
    controller();
    ~controller();
    void run();
};