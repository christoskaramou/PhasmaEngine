#pragma once

#include "PhasmaAgent/Agent.h"

namespace pagent
{
    class OllamaEmbedding : public IEmbeddingProvider
    {
    public:
        explicit OllamaEmbedding(std::string model = "nomic-embed-text",
                                 int dims = 768,
                                 std::string base_url = "http://localhost:11434");

        std::vector<float> Embed(const std::string &text) override;
        std::vector<std::vector<float>> EmbedBatch(const std::vector<std::string> &texts) override;
        int Dimensions() const override { return m_dims; }
        int RecommendedConcurrency() const override { return 1; }

    private:
        std::string m_model;
        int m_dims;
        std::string m_host; // "host:port" — no http:// prefix
    };
} // namespace pagent
