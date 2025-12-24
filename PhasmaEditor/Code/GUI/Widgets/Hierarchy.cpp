#include "Hierarchy.h"
#include "Scene/Model.h"
#include "Scene/Scene.h"
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

        // Set initial size if needed (though docking usually handles it)
        // ui::SetInitialWindowSizeFraction(0.2f, 0.5f);

        ImGui::Begin("Hierarchy", &m_open);

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Scene &scene = renderer->GetScene();
        auto &models = scene.GetModels();

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
                if (m_selectedId == id)
                    modelFlags |= ImGuiTreeNodeFlags_Selected;

                bool modelOpen = ImGui::TreeNodeEx((void *)(intptr_t)id, modelFlags, "%s", name.c_str());
                if (ImGui::IsItemClicked())
                {
                    m_selectedId = id;
                    m_selectedNodeIndex = -1;
                    // TODO: Do something with it here
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
                        // Using a simple hash approach for void* pointer
                        uintptr_t uniqueId = (id << 16) ^ nodeIndex;

                        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
                        if (isLeaf)
                            nodeFlags |= ImGuiTreeNodeFlags_Leaf;

                        // Highlight if selected
                        if (m_selectedId == id && m_selectedNodeIndex == nodeIndex)
                            nodeFlags |= ImGuiTreeNodeFlags_Selected;

                        bool nodeOpen = ImGui::TreeNodeEx((void *)uniqueId, nodeFlags, "%s", node.name.c_str());

                        if (ImGui::IsItemClicked())
                        {
                            m_selectedId = id;
                            m_selectedNodeIndex = nodeIndex;
                            // TODO: Do something with it here
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
