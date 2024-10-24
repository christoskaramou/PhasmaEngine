#pragma once

namespace pe
{
    struct BarrierInfo;

    struct Dependency
    {
        void *resource = nullptr;
        BarrierInfo *barrier = nullptr;
    };

    class RenderGraph
    {
        using Func = std::function<void(CommandBuffer*)>;

    public:
        void AddPass(const Func &func, const std::vector<Dependency> &dependancies);
        void Execute();
        void ClearPasses();

    private:
        std::vector<std::pair<Func, std::vector<Dependency>>> m_passes;
    };
}
