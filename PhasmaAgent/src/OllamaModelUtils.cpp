#include "PhasmaAgent/OllamaModelUtils.h"

#include "OllamaProcess.h"

namespace pagent
{
    ModelPullCancelToken PullModel(const std::string &model,
                                   ModelPullProgressCallback progressCb,
                                   ModelPullCompleteCallback completeCb)
    {
        return OllamaProcess::Pull(model, std::move(progressCb), std::move(completeCb));
    }

    void CancelModelPull(const ModelPullCancelToken &token)
    {
        OllamaProcess::CancelPull(token);
    }

    void UnloadModel(const std::string &model,
                     const std::string &base_url)
    {
        OllamaProcess::UnloadModel(model, base_url);
    }
} // namespace pagent
