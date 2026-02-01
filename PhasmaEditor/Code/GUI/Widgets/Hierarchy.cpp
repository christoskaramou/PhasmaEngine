#include "Hierarchy.h"
#include "Scene/Model.h"
#include "Scene/Scene.h"
#include "Scene/SelectionManager.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"

namespace pe
{
    Hierarchy::Hierarchy() : Widget("Hierarchy")
    {
    }

    void Hierarchy::Update()
    {
        if (!m_open)
            return;

        ImGui::Begin("Hierarchy", &m_open);

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Scene &scene = renderer->GetScene();
        auto &models = scene.GetModels();
        auto &selection = SelectionManager::Instance();

        // Root node for the scene
        if (ImGui::TreeNodeEx("Root", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow))
        {
            for (auto model : models)
            {
                if (!model)
                    continue;

                size_t id = model->GetId();
                std::string name = model->GetLabel();
                if (name.empty())
                    name = "Model_" + std::to_string(id);

                ImGuiTreeNodeFlags modelFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
                if (selection.GetSelectedModel() == model)
                    modelFlags |= ImGuiTreeNodeFlags_Selected;

                bool modelOpen = ImGui::TreeNodeEx((void *)(intptr_t)id, modelFlags, "%s", name.c_str());
                if (ImGui::IsItemClicked())
                {
                    int nodeToSelect = -1;
                    for (int i = 0; i < model->GetNodeCount(); i++)
                    {
                        if (model->GetNodeMesh(i) >= 0)
                        {
                            nodeToSelect = i;
                            break;
                        }
                    }
                    if (nodeToSelect >= 0)
                        selection.Select(model, nodeToSelect);
                }

                if (modelOpen)
                {
                    const auto &nodes = model->GetNodeInfos();
                    int nodeCount = static_cast<int>(nodes.size());

                    // Build adjacency list for efficient traversal
                    std::vector<std::vector<int>> children(nodeCount);
                    std::vector<int> roots;
                    for (int i = 0; i < nodeCount; ++i)
                    {
                        if (nodes[i].parent >= 0 && nodes[i].parent < nodeCount)
                            children[nodes[i].parent].push_back(i);
                        else
                            roots.push_back(i);
                    }

                    // Recursive function to draw nodes
                    auto DrawNode = [&](auto &&self, int nodeIndex) -> void
                    {
                        const auto &node = nodes[nodeIndex];
                        bool isLeaf = children[nodeIndex].empty();

                        // Use a unique ID mixing model ID and node Index to avoid conflicts
                        uintptr_t uniqueId = (id << 16) ^ nodeIndex;

                        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
                        if (isLeaf)
                            nodeFlags |= ImGuiTreeNodeFlags_Leaf;

                        // Highlight if selected
                        if (selection.GetSelectedModel() == model && selection.GetSelectedNodeIndex() == nodeIndex)
                            nodeFlags |= ImGuiTreeNodeFlags_Selected;

                        bool nodeOpen = ImGui::TreeNodeEx((void *)uniqueId, nodeFlags, "%s", node.name.c_str());

                        if (ImGui::IsItemClicked())
                        {
                            selection.Select(model, nodeIndex);
                        }

                        if (nodeOpen)
                        {
                            for (int childIndex : children[nodeIndex])
                            {
                                self(self, childIndex);
                            }
                            ImGui::TreePop();
                        }
                    };

                    for (int rootIndex : roots)
                    {
                        DrawNode(DrawNode, rootIndex);
                    }

                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }

        ImGui::End();
    }
} // namespace pe
