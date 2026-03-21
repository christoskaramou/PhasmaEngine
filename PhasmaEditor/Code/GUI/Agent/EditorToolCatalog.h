#pragma once

#include "PhasmaAgent/Agent.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pe
{
    class GUI;
    class EditorToolRuntime;

    struct EditorToolCatalogContext
    {
        std::string projectRoot;
        GUI *gui = nullptr;
        EditorToolRuntime *runtime = nullptr;
        std::shared_ptr<pagent::VectorStore> codebaseStore;
        std::shared_ptr<pagent::BM25Index> codebaseBM25;
        std::shared_ptr<pagent::IEmbeddingProvider> embeddingProvider;
        std::function<void(const std::string &message)> onFeatureRequest;
    };

    std::filesystem::path GetEditorRepoRootFromAssets();
    std::vector<pagent::ToolDefinition> BuildEditorToolDefinitions(const EditorToolCatalogContext &context);
} // namespace pe
