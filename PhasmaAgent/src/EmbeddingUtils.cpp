#include "PhasmaAgent/EmbeddingUtils.h"
#include "PhasmaAgent/GoogleEmbedding.h"
#include "PhasmaAgent/OllamaEmbedding.h"
#include "PhasmaAgent/OpenAIEmbedding.h"
#include "PhasmaAgent/ProviderUtils.h"

#include <algorithm>

namespace pagent
{
    std::shared_ptr<IEmbeddingProvider> CreateEmbeddingProvider(EmbeddingProviderKind provider, const std::string &model)
    {
        if (model.empty())
            return nullptr;

        const std::string geminiKey = GetEnvOrEmpty("PAGENT_GEMINI_API_KEY");
        const std::string openaiKey = GetEnvOrEmpty("PAGENT_OPENAI_API_KEY");

        switch (provider)
        {
        case EmbeddingProviderKind::Google:
            if (!geminiKey.empty())
                return std::make_shared<GoogleEmbedding>(geminiKey, model, 3072);
            break;

        case EmbeddingProviderKind::OpenAI:
        {
            if (openaiKey.empty())
                break;
            int dims = 1536;
            if (model.find("large") != std::string::npos)
                dims = 3072;
            return std::make_shared<OpenAIEmbedding>(openaiKey, model, dims);
        }

        case EmbeddingProviderKind::Ollama:
            return std::make_shared<OllamaEmbedding>(model);

        case EmbeddingProviderKind::Voyage:
        {
            const std::string voyageKey = GetEnvOrEmpty("PAGENT_VOYAGE_API_KEY");
            if (voyageKey.empty())
                break;
            return std::make_shared<OpenAIEmbedding>(voyageKey, model, 1024, "api.voyageai.com", "/v1/embeddings");
        }
        }

        return nullptr;
    }

    std::vector<std::string> GetKnownEmbeddingModels(EmbeddingProviderKind provider)
    {
        switch (provider)
        {
        case EmbeddingProviderKind::Google:
            return {"gemini-embedding-2-preview", "gemini-embedding-001"};
        case EmbeddingProviderKind::OpenAI:
            return {"text-embedding-3-small", "text-embedding-3-large"};
        case EmbeddingProviderKind::Voyage:
            return {"voyage-code-3", "voyage-3"};
        case EmbeddingProviderKind::Ollama:
        default:
            return {};
        }
    }

    std::string GetDefaultEmbeddingModelName(const EmbeddingProviderKind provider)
    {
        switch (provider)
        {
        case EmbeddingProviderKind::Google:
            return "gemini-embedding-2-preview";
        case EmbeddingProviderKind::OpenAI:
            return "text-embedding-3-small";
        case EmbeddingProviderKind::Voyage:
            return "voyage-code-3";
        case EmbeddingProviderKind::Ollama:
        default:
            return "qwen3-embedding:0.6b";
        }
    }

} // namespace pagent
