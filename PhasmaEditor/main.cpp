#include "Code/App/App.h"
#include "Base/Log.h"

int main(int argc, char *argv[])
{
    pe::Log::Init();
    pe::App app;
    app.Run();

    return 0;
}
