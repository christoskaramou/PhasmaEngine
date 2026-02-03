#include "Properties.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "MeshWidget.h"
#include "Scene/Model.h"
#include "Scene/SelectionManager.h"
#include "TransformWidget.h"

namespace pe
{
    void Properties::Update()
    {
        if (!m_open)
            return;

        // Use the helper we moved to Helpers.h
        static bool propertiesSized = false;
        if (!propertiesSized)
        {
            ui::SetInitialWindowSizeFraction(1.0f / 7.0f, 0.35f, ImGuiCond_Always);
            propertiesSized = true;
        }
        else
        {
            ui::SetInitialWindowSizeFraction(1.0f / 7.0f, 0.35f, ImGuiCond_Appearing);
        }

        ImGui::Begin("Properties", &m_open);

        auto &selection = SelectionManager::Instance();
        if (selection.HasSelection())
        {
            if (selection.GetSelectionType() == SelectionType::Node)
            {
                auto *transformWidget = m_gui->GetWidget<TransformWidget>();
                if (transformWidget)
                {
                    transformWidget->DrawEmbed(selection.GetSelectedNodeInfo());
                }
            }
            else if (selection.GetSelectionType() == SelectionType::Mesh)
            {
                auto *meshWidget = m_gui->GetWidget<MeshWidget>();
                if (meshWidget)
                {
                    Model *model = selection.GetSelectedModel();
                    int nodeIndex = selection.GetSelectedNodeIndex();
                    int meshIndex = model->GetNodeMesh(nodeIndex);
                    if (meshIndex >= 0)
                    {
                        auto &meshInfos = model->GetMeshInfos();
                        if (meshIndex < static_cast<int>(meshInfos.size()))
                        {
                            meshWidget->DrawEmbed(&meshInfos[meshIndex], model);
                        }
                    }
                }
            }
        }
        else
        {
            ImGui::TextDisabled("No object selected");
        }

        ImGui::End();
    }
} // namespace pe
