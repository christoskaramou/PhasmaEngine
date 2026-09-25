#pragma once
#include "ProjectNative.h"
#include <vector>

namespace phasma
{
    // This registry lives entirely inside the game module.
    inline std::vector<ScriptDesc> &RegisteredScripts()
    {
        static std::vector<ScriptDesc> scripts;
        return scripts;
    }

    template <class T>
    bool RegisterScript(const char *name, const char *source, ScriptKind kind = ScriptKind::Node)
    {
        auto desc = Script<T>(name, kind);
        desc.sourceFile = source;
        RegisteredScripts().push_back(desc);
        return true;
    }
} // namespace phasma

#define PHASMA_NODE_SCRIPT(Type)                                                      \
    namespace                                                                         \
    {                                                                                 \
        const bool registered_##Type = phasma::RegisterScript<Type>(#Type, __FILE__); \
    }
