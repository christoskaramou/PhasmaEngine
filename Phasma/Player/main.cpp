#include "UI/Backends/ImGuiRuntimeUiBackend.h"
#include "Runtime/PlayerHost.h"

int main(int argc, char *argv[])
{
    pe::PlayerHostDesc desc{};
    desc.fileWatchersEnabled = true;
    desc.runtimeUiBackendFactory = pe::CreateImGuiRuntimeUiBackend;

    return pe::RunPlayerHost(argc, argv, std::move(desc));
}
