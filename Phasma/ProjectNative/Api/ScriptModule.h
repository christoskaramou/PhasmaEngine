#pragma once
#include "ProjectNative.h"
#include <vector>

// ProjectNative.cmake defines this per source as its bare filename; other builds fall back to __FILE__.
#ifndef PHASMA_SOURCE_NAME
#define PHASMA_SOURCE_NAME __FILE__
#endif

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

#define PHASMA_NODE_SCRIPT(Type)                                                                \
    namespace                                                                                   \
    {                                                                                           \
        const bool registered_##Type = phasma::RegisterScript<Type>(#Type, PHASMA_SOURCE_NAME); \
    }

// A scene pipeline script (ScriptKind::Pipeline): a scene names it as pipeline_script "cpp:Type". Its constructor
// exposes values and adds passes; it needs a Update(double) the engine never calls.
#define PHASMA_PIPELINE_SCRIPT(Type)                                                                                          \
    namespace                                                                                                                 \
    {                                                                                                                         \
        const bool registered_##Type = phasma::RegisterScript<Type>(#Type, PHASMA_SOURCE_NAME, phasma::ScriptKind::Pipeline); \
    }
