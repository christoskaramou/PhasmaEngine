#pragma once

namespace pe
{
    class IRuntimeUiBackend;

    struct PlayerHostDesc
    {
        std::function<std::unique_ptr<IRuntimeUiBackend>()> runtimeUiBackendFactory;
        bool fileWatchersEnabled = false;

        // Mesh cook (desktop tooling, requires Assimp): when cookModelPath is set, the host brings up
        // the RHI, imports that source model, writes a cooked ".pemesh" to cookOutputPath, and exits
        // before the frame loop. Mirrors the offline shader bake. Ignored by the packaged player.
        std::string cookModelPath;
        std::string cookOutputPath;
    };

    int RunPlayerHost(int argc, char *argv[], PlayerHostDesc desc);
} // namespace pe
