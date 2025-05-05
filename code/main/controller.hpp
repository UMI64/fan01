#include "board.hpp"
#include "ui.hpp"
class controller
{
private:
    board *board_obj;
    ui *ui_obj;
    thread_helper *main_thread;
    void main_task(void *param);
public:
    controller();
    ~controller();
    void run();
};