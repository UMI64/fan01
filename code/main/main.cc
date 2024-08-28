#include "controller.hpp"
extern "C"
{
    void app_main(void)
    {
        controller controller_obj;
        controller_obj.run();
    }
}