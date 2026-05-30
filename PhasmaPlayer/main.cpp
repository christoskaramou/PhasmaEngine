#include "UI/Backends/ImGuiRuntimeUiBackend.h"
#include "Runtime/PlayerHost.h"

int main(int argc, char *argv[])
{
    pe::PlayerHostDesc desc{};
    desc.fileWatchersEnabled = true;
    desc.runtimeUiBackendFactory = pe::CreateImGuiRuntimeUiBackend;

    // Desktop mesh-cook tooling: PHASMA_COOK_MODEL=<source model> PHASMA_COOK_OUT=<.pemesh> imports
    // via Assimp and writes a cooked mesh, then exits (mirrors the offline shader bake).
    if (const char *cookIn = std::getenv("PHASMA_COOK_MODEL"))
    {
        desc.cookModelPath = cookIn;
        if (const char *cookOut = std::getenv("PHASMA_COOK_OUT"))
            desc.cookOutputPath = cookOut;
    }

    return pe::RunPlayerHost(argc, argv, std::move(desc));
}
