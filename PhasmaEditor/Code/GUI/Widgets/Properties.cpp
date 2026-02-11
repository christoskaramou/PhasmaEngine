#include "Properties.h"
#include "CameraWidget.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "LightWidget.h"
#include "MeshWidget.h"
#include "Scene/Model.h"
#include "Scene/Scene.h"
#include "Scene/SelectionManager.h"
#include "Systems/LightSystem.h"
#include "Systems/RendererSystem.h"
#include "TransformWidget.h"

namespace pe
{
    void Properties::Update()
    {
        if (!m_open)
            return;

        // Keep size stable: first time always, after that only on appearing
        static bool sizedOnce = false;
        ui::SetInitialWindowSizeFraction(1.0f / 7.0f, 0.35f, sizedOnce ? ImGuiCond_Appearing : ImGuiCond_Always);
        sizedOnce = true;

        if (!ImGui::Begin("Properties", &m_open))
        {
            ImGui::End();
            return;
        }

        auto &sel = SelectionManager::Instance();
        if (!sel.HasSelection())
        {
            ImGui::TextDisabled("No object selected");
            ImGui::End();
            return;
        }

        auto drawNode = [&]()
        {
            if (auto *w = m_gui->GetWidget<TransformWidget>())
                w->DrawEmbed(sel.GetSelectedNodeInfo());
        };

        auto drawMesh = [&]()
        {
            auto *w = m_gui->GetWidget<MeshWidget>();
            if (!w)
                return;

            Model *model = sel.GetSelectedModel();
            if (!model)
                return;

            const int nodeIndex = sel.GetSelectedNodeIndex();
            const int meshIndex = model->GetNodeMesh(nodeIndex);
            if (meshIndex < 0)
                return;

            auto &meshInfos = model->GetMeshInfos();
            if (meshIndex >= (int)meshInfos.size())
                return;

            w->DrawEmbed(&meshInfos[meshIndex], model);
        };

        auto drawCamera = [&]()
        {
            auto *w = m_gui->GetWidget<CameraWidget>();
            if (!w)
                return;

            const int index = sel.GetSelectedNodeIndex();
            auto &cameras = GetGlobalSystem<RendererSystem>()->GetScene().GetCameras();
            if (index < 0 || index >= (int)cameras.size())
                return;

            w->DrawEmbed(cameras[index]);
        };

        auto drawLight = [&]()
        {
            auto *w = m_gui->GetWidget<LightWidget>();
            if (!w)
                return;

            LightSystem *ls = GetGlobalSystem<LightSystem>();
            if (!ls)
                return;

            w->DrawEmbed(ls, sel.GetSelectedLightType(), sel.GetSelectedLightIndex());
        };

        switch (sel.GetSelectionType())
        {
        case SelectionType::Node:
            drawNode();
            break;
        case SelectionType::Mesh:
            drawMesh();
            break;
        case SelectionType::Camera:
            drawCamera();
            break;
        case SelectionType::Light:
            drawLight();
            break;
        default:
            break;
        }

        ImGui::End();
    }
} // namespace pe
