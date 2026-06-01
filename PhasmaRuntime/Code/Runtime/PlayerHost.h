#pragma once

namespace pe
{
    class IRuntimeUiBackend;

    struct PlayerHostDesc
    {
        std::function<std::unique_ptr<IRuntimeUiBackend>()> runtimeUiBackendFactory;
        bool fileWatchersEnabled = false;
    };

    int RunPlayerHost(int argc, char *argv[], PlayerHostDesc desc);
} // namespace pe
