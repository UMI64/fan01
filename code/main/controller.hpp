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

public:
    controller();
    ~controller();
    void run();
};