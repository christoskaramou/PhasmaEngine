#include "Properties.h"
#include "CameraWidget.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "LightWidget.h"
#include "MeshWidget.h"
#include "Particles.h"
#include "Particles/ParticleManager.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
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

        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();

        auto drawNode = [&]()
        {
            if (auto *w = m_gui->GetWidget<TransformWidget>())
                w->DrawEmbed(sel.GetSelectedNode());
        };

        auto drawMesh = [&]()
        {
            auto *w = m_gui->GetWidget<MeshWidget>();
            if (!w)
                return;

            NodeId *node = sel.GetSelectedNode();
            if (!node)
                return;

            int meshIndex = scene.GetMeshRef(node);
            if (meshIndex < 0)
                return;

            Mesh &mesh = scene.GetMesh(meshIndex);
            w->DrawEmbed(&mesh, node);
        };

        auto drawCamera = [&]()
        {
            auto *w = m_gui->GetWidget<CameraWidget>();
            if (!w)
                return;

            const int index = sel.GetSelectedCameraIndex();
            auto &cameras = scene.GetCameras();
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

        auto drawEmitter = [&]()
        {
            auto *w = m_gui->GetWidget<Particles>();
            if (!w)
                return;

            RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
                return;

            ParticleManager *pm = renderer->GetScene().GetParticleManager();
            if (!pm)
                return;

            w->DrawEmbed(pm, sel.GetSelectedEmitterIndex());
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
        case SelectionType::Emitter:
            drawEmitter();
            break;
        default:
            break;
        }

        ImGui::End();
    }
} // namespace pe
