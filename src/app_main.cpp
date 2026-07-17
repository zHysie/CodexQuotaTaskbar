#include "App.h"

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int)
{
    cqt::App app;
    return app.Run(instance);
}
