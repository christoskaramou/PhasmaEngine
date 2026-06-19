#pragma once

#include "Phasma/MCP/Tool.h"
#include "Phasma/MCP/Codebase/BM25Index.h"

namespace pe
{
    class GUI;
    class EditorToolRuntime;

    struct EditorToolCatalogContext
    {
        std::string projectRoot;
        GUI *gui = nullptr;
        EditorToolRuntime *runtime = nullptr;
        std::shared_ptr<pmcp::BM25Index> codebaseBM25;
        std::function<void(const std::string &message)> onFeatureRequest;
    };

    std::filesystem::path GetEditorRepoRootFromAssets();
    std::vector<pmcp::ToolDefinition> BuildEditorToolDefinitions(const EditorToolCatalogContext &context);
} // namespace pe
