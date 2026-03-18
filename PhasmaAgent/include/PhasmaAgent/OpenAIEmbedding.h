#pragma once

#include "PhasmaAgent/Agent.h"

namespace pagent
{
    class OpenAIEmbedding : public IEmbeddingProvider
    {
    public:
        explicit OpenAIEmbedding(std::string api_key,
                                 std::string model = "text-embedding-3-small",
                                 int dims = 1536,
                                 std::string host = "api.openai.com",
                                 std::string path = "/v1/embeddings");

        std::vector<float> Embed(const std::string &text) override;
        int Dimensions() const override { return m_dims; }

    private:
        std::string m_apiKey;
        std::string m_model;
        int m_dims;
        std::string m_host;
        std::string m_path;
    };
} // namespace pagent
