#include "TerrainBrush.h"
#include "Camera/Camera.h"
#include "ECS/Context.h" // GetGlobalSystem<TerrainSystem>
#include "GUI/GUI.h"
#include "GUI/GUIState.h"
#include "GUI/Helpers.h"
#include "MapPainter.h" // scatter strokes route through the painter (one owner of the map math)
#include "Scene/NodeComponents.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Terrain/TerrainSystem.h"

namespace pe
{
    namespace
    {
        // Local copies of SceneView.cpp's file-static helpers (reverse-Z unproject + projector) —
        // ponytail: 40 duplicated lines beat exporting SceneView internals; share a header if a
        // third user appears.
        bool BuildViewportRay(Camera *camera, const ImVec2 &mouse, const ImVec2 &imageMin,
                              const ImVec2 &imageSize, vec3 &rayOrigin, vec3 &rayDir)
        {
            if (!camera || imageSize.x <= 0.0f || imageSize.y <= 0.0f)
                return false;
            const float ndcX = ((mouse.x - imageMin.x) / imageSize.x) * 2.0f - 1.0f;
            const float ndcY = ((mouse.y - imageMin.y) / imageSize.y) * 2.0f - 1.0f;
            const mat4 invViewProj = camera->GetInvViewProjection();
            vec4 nearPoint = invViewProj * vec4(ndcX, ndcY, 1.0f, 1.0f); // reverse-Z: near = 1
            if (std::abs(nearPoint.w) < 1e-6f)
                return false;
            nearPoint /= nearPoint.w;
            vec4 farPoint = invViewProj * vec4(ndcX, ndcY, 0.0f, 1.0f);
            rayOrigin = vec3(nearPoint);
            if (std::abs(farPoint.w) < 1e-6f)
                rayDir = normalize(vec3(farPoint));
            else
            {
                farPoint /= farPoint.w;
                rayDir = normalize(vec3(farPoint) - rayOrigin);
            }
            return std::isfinite(rayOrigin.x) && std::isfinite(rayDir.x);
        }

        bool ProjectToViewport(const vec3 &world, const mat4 &viewProj, const ImVec2 &imageMin,
                               const ImVec2 &imageSize, ImVec2 &screen)
        {
            const vec4 clip = viewProj * vec4(world, 1.0f);
            if (clip.w <= 0.0f)
                return false;
            const vec2 ndc = vec2(clip) / clip.w;
            screen.x = (ndc.x * 0.5f + 0.5f) * imageSize.x + imageMin.x;
            screen.y = (ndc.y * 0.5f + 0.5f) * imageSize.y + imageMin.y;
            return std::isfinite(screen.x) && std::isfinite(screen.y);
        }

        terrain::TerrainWorld *LiveWorld()
        {
            auto *ts = GetGlobalSystem<terrain::TerrainSystem>();
            return ts ? ts->World() : nullptr;
        }
    } // namespace

    TerrainBrush::TerrainBrush() : Widget("Terrain Brush")
    {
        m_open = false;
    }

    bool TerrainBrush::Active() const
    {
        return m_open && m_active && !GUIState::s_playMode && LiveWorld() != nullptr;
    }

    void TerrainBrush::Update()
    {
        if (!m_open)
            return;
        ImGui::Begin(m_name.c_str(), &m_open);

        Scene *scene = GetActiveScene();
        NodeId *tn = scene ? scene->GetTerrainNode() : nullptr;
        NodeTerrainTag *tag = tn ? scene->GetTerrainForNode(tn) : nullptr;
        if (!tag || !LiveWorld())
        {
            ImGui::TextDisabled("Add a Terrain node (Hierarchy > Add) to sculpt in the viewport.");
            ImGui::End();
            return;
        }

        ImGui::Checkbox("Active", &m_active);
        ui::ItemTooltip("While on, the brush owns the left mouse button over terrain in the Scene "
                        "view (object picking is suspended there).");
        static const char *kModes[5] = {"Raise", "Dig", "Smooth", "Flatten", "Scatter"};
        ImGui::SetNextItemWidth(120.0f);
        ImGui::Combo("Mode", &m_mode, kModes, 5);
        ui::ItemTooltip("Raise/Dig: CSG sphere at the hit point (Shift inverts; the hit is 3D, so "
                        "digging into a cliff undercuts it). Smooth: pull toward the local average. "
                        "Flatten: pull toward the height under the stroke start. Scatter: plant the "
                        "selected Scatter Mesh (Ctrl erases). All strokes persist and serialize.");
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("Radius (m)", &m_radius, 0.1f, 0.5f, 64.0f, "%.1f");
        if (m_mode == static_cast<int>(Mode::Smooth) || m_mode == static_cast<int>(Mode::Flatten))
        {
            ImGui::SetNextItemWidth(120.0f);
            ImGui::SliderFloat("Strength", &m_strength, 0.05f, 1.0f, "%.2f");
            ui::ItemTooltip("Level weight per stamp — low values converge gently over a held stroke.");
        }
        if (m_mode == static_cast<int>(Mode::Scatter))
        {
            const int kinds = static_cast<int>(tag->scatterMeshes.size());
            if (kinds == 0)
            {
                ImGui::TextDisabled("Add Scatter Meshes on the Terrain node first.");
            }
            else
            {
                std::vector<std::string> labels;
                for (int i = 0; i < kinds; ++i)
                    labels.push_back(std::to_string(i + 1) + ": " + tag->scatterMeshes[i]);
                labels.emplace_back("Erase");
                std::vector<const char *> items;
                for (const std::string &s : labels)
                    items.push_back(s.c_str());
                m_scatterKind = std::clamp(m_scatterKind, 0, kinds);
                ImGui::SetNextItemWidth(180.0f);
                ImGui::Combo("Scatter Mesh", &m_scatterKind, items.data(), static_cast<int>(items.size()));
            }
        }
        if (!m_hint.empty())
            ImGui::TextDisabled("%s", m_hint.c_str());
        ImGui::End();
    }

    bool TerrainBrush::HandleViewport(Camera *camera, const ImVec2 &mouse, const ImVec2 &imageMin,
                                      const ImVec2 &imageSize, bool imageHovered)
    {
        terrain::TerrainWorld *world = LiveWorld();
        if (!world || !camera)
            return false;
        const bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (!mouseDown)
        {
            m_haveFlattenY = false;
            m_haveLastStamp = false;
        }
        if (!imageHovered)
            return false;

        vec3 rayO(0.0f), rayD(0.0f), hit(0.0f), n(0.0f);
        if (!BuildViewportRay(camera, mouse, imageMin, imageSize, rayO, rayD) ||
            !world->Raycast(rayO, rayD, 4000.0f, hit, n))
            return false; // over sky: leave the mouse to picking

        // Ring decal in the tangent plane of the hit normal (+ half-radius inner ring + centre dot).
        const mat4 viewProj = camera->GetViewProjection();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        const vec3 up = std::abs(n.y) > 0.95f ? vec3(1.0f, 0.0f, 0.0f) : vec3(0.0f, 1.0f, 0.0f);
        const vec3 t1 = normalize(cross(n, up));
        const vec3 t2 = cross(n, t1);
        const ImU32 color = m_mode == static_cast<int>(Mode::Dig)       ? IM_COL32(255, 120, 60, 230)
                            : m_mode == static_cast<int>(Mode::Scatter) ? IM_COL32(90, 220, 90, 230)
                                                                        : IM_COL32(255, 210, 40, 230);
        for (int ring = 0; ring < 2; ++ring)
        {
            const float r = m_radius * (ring == 0 ? 1.0f : 0.5f);
            ImVec2 pts[33];
            int count = 0;
            for (int i = 0; i <= 32; ++i)
            {
                const float a = static_cast<float>(i) * (6.2831853f / 32.0f);
                const vec3 p = hit + (t1 * std::cos(a) + t2 * std::sin(a)) * r;
                ImVec2 s;
                if (!ProjectToViewport(p, viewProj, imageMin, imageSize, s))
                    break;
                pts[count++] = s;
            }
            if (count == 33)
                draw->AddPolyline(pts, 33, color, 0, ring == 0 ? 2.0f : 1.0f);
        }
        ImVec2 centre;
        if (ProjectToViewport(hit, viewProj, imageMin, imageSize, centre))
            draw->AddCircleFilled(centre, 3.0f, color);

        if (!mouseDown)
            return true; // hovering terrain with the tool armed still owns the click

        // Stamp with spacing so held strokes lay a bead instead of flooding the op list.
        if (m_haveLastStamp && glm::length(hit - m_lastStamp) < m_radius * 0.45f)
            return true;
        m_haveLastStamp = true;
        m_lastStamp = hit;
        m_hint.clear();

        const bool shift = ImGui::GetIO().KeyShift;
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        switch (static_cast<Mode>(m_mode))
        {
        case Mode::Raise:
        case Mode::Dig:
        {
            const bool dig = (m_mode == static_cast<int>(Mode::Dig)) != shift;
            world->QueueSculpt(hit, m_radius, dig ? -1.0f : 1.0f);
            break;
        }
        case Mode::Smooth:
        {
            // Level toward the average true surface around the hit (5 down-ray taps).
            float avg = 0.0f;
            int count = 0;
            for (int i = 0; i < 5; ++i)
            {
                const float sx = hit.x + (i == 1 ? m_radius * 0.5f : i == 2 ? -m_radius * 0.5f
                                                                            : 0.0f);
                const float sz = hit.z + (i == 3 ? m_radius * 0.5f : i == 4 ? -m_radius * 0.5f
                                                                            : 0.0f);
                vec3 sp(0.0f), sn(0.0f);
                if (world->Raycast(vec3(sx, hit.y + m_radius * 2.0f + 5.0f, sz), vec3(0.0f, -1.0f, 0.0f),
                                   m_radius * 4.0f + 50.0f, sp, sn))
                {
                    avg += sp.y;
                    ++count;
                }
            }
            if (count > 0)
                world->QueueLevel(hit, m_radius, avg / count, m_strength);
            break;
        }
        case Mode::Flatten:
        {
            if (!m_haveFlattenY)
            {
                m_haveFlattenY = true;
                m_flattenY = hit.y; // the stroke start owns the target plane
            }
            world->QueueLevel(vec3(hit.x, m_flattenY, hit.z), m_radius, m_flattenY,
                              std::min(1.0f, m_strength * 2.0f));
            break;
        }
        case Mode::Scatter:
        {
            MapPainter *painter = m_gui ? m_gui->GetWidget<MapPainter>() : nullptr;
            Scene *scene = GetActiveScene();
            NodeId *tn = scene ? scene->GetTerrainNode() : nullptr;
            NodeTerrainTag *tag = tn ? scene->GetTerrainForNode(tn) : nullptr;
            const int kinds = tag ? static_cast<int>(tag->scatterMeshes.size()) : 0;
            const int kind = (ctrl || m_scatterKind >= kinds) ? 0 : m_scatterKind + 1;
            if (!painter || kinds == 0 ||
                !painter->ScatterStrokeWorld(hit.x, hit.z, m_radius, kind))
                m_hint = kinds == 0 ? "Add Scatter Meshes on the Terrain node first."
                                    : "No scatter map here - set/create one in Map Painter's Scatter "
                                      "layer (it must cover this spot).";
            break;
        }
        }
        return true;
    }
} // namespace pe
