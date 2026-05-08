#pragma once
#include "Base/PhasmaExport.h"

namespace pe
{
    // PE_API resource tracker. Lives entirely in PhasmaCore.dll
    class PE_API PeTracker
    {
    public:
        static void Track(const std::type_info &type, void *ptr);
        static void Untrack(const std::type_info &type, void *ptr);
        static std::vector<void *> GetHandles(const std::type_info &type);
    };
} // namespace pe
