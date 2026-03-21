#pragma once

#include "PhasmaAgent/Agent.h"

#include <memory>
#include <string>
#include <vector>

namespace pagent
{
    enum class EmbeddingProviderKind
    {
        Google,
        OpenAI,
        Ollama,
        Voyage,
    };

    std::shared_ptr<IEmbeddingProvider> CreateEmbeddingProvider(EmbeddingProviderKind provider, const std::string &model);
    std::vector<std::string> GetKnownEmbeddingModels(EmbeddingProviderKind provider);
    std::string GetDefaultEmbeddingModelName(EmbeddingProviderKind provider);
} // namespace pagent
