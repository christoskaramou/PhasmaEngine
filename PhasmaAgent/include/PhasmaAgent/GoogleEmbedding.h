#pragma once

#include "PhasmaAgent/Agent.h"

namespace pagent
{
    // Google embedding provider using Gemini Embedding 2 (gemini-embedding-2-preview).
    class GoogleEmbedding : public IEmbeddingProvider
    {
    public:
        explicit GoogleEmbedding(std::string api_key, std::string model = "gemini-embedding-2-preview", int dims = 3072);

        std::vector<float> Embed(const std::string &text) override;
        int Dimensions() const override { return m_dims; }

    private:
        std::string m_apiKey;
        std::string m_model;
        int m_dims;
    };
} // namespace pagent
