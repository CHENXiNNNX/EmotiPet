#include "app/app.hpp"

extern "C" void app_main(void)
{
    app::App app;
    app.setup();
    app.run();
}
