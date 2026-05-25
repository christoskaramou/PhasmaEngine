#pragma once


namespace pe
{
    class IRuntimeUiBackend;

    struct PlayerHostDesc
    {
        std::function<std::unique_ptr<IRuntimeUiBackend>()> runtimeUiBackendFactory;
    };

    int RunPlayerHost(int argc, char *argv[]);
    int RunPlayerHost(int argc, char *argv[], PlayerHostDesc desc);
} // namespace pe
