#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace pagent
{
    using ModelPullProgressCallback = std::function<void(const std::string &status)>;
    using ModelPullCompleteCallback = std::function<void(bool success)>;
    using ModelPullCancelToken = std::shared_ptr<std::atomic<bool>>;

    ModelPullCancelToken PullModel(const std::string &model,
                                   ModelPullProgressCallback progressCb,
                                   ModelPullCompleteCallback completeCb);
    void CancelModelPull(const ModelPullCancelToken &token);

    void UnloadModel(const std::string &model,
                     const std::string &base_url = "");
} // namespace pagent
