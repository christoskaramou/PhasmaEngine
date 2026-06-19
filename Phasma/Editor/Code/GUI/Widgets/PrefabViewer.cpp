#include "PrefabViewer.h"
#include "FileBrowser.h"
#include "FileSelector.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "GUI/IconsFontAwesome.h"
#include "GUI/UndoRedo.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneHost.h"
#include "Scene/SceneNode.h"
#include "Scene/SelectionManager.h"
#include "imgui/ImGuizmo.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <unordered_set>

namespace pe
{
    namespace
    {
        constexpr const char *kPrefabPayload = "PREFAB_VIEWER_NODE";

        std::string PathToString(const std::filesystem::path &path)
        {
            auto u8 = path.u8string();
            return std::string(reinterpret_cast<const char *>(u8.c_str()));
        }

        void MakeIdentity(float matrix[16])
        {
            for (int i = 0; i < 16; ++i)
                matrix[i] = 0.0f;
            matrix[0] = 1.0f;
            matrix[5] = 1.0f;
            matrix[10] = 1.0f;
            matrix[15] = 1.0f;
        }

        bool ReadMatrix(const nlohmann::json &nodeJson, float matrix[16])
        {
            MakeIdentity(matrix);
            auto it = nodeJson.find("local_matrix");
            if (it == nodeJson.end() || !it->is_array() || it->size() != 16)
                return false;

            for (int i = 0; i < 16; ++i)
            {
                if (!(*it)[i].is_number())
                    return false;
                matrix[i] = (*it)[i].get<float>();
            }
            return true;
        }

        void WriteMatrix(nlohmann::json &nodeJson, const float matrix[16])
        {
            nlohmann::json arr = nlohmann::json::array();
            for (int i = 0; i < 16; ++i)
                arr.push_back(matrix[i]);
            nodeJson["local_matrix"] = std::move(arr);
        }

        nlohmann::json MakeIdentityMatrixJson()
        {
            float matrix[16];
            MakeIdentity(matrix);
            nlohmann::json arr = nlohmann::json::array();
            for (float value : matrix)
                arr.push_back(value);
            return arr;
        }

        nlohmann::json MakeVec3Json(float x, float y, float z)
        {
            return nlohmann::json::array({x, y, z});
        }

        nlohmann::json MakeVec4Json(float x, float y, float z, float w)
        {
            return nlohmann::json::array({x, y, z, w});
        }

        nlohmann::json MakeDefaultMaterialFactors()
        {
            return nlohmann::json::array({MakeIdentityMatrixJson(), MakeIdentityMatrixJson()});
        }

        void RoundJsonFloats(nlohmann::json &value)
        {
            if (value.is_number_float())
            {
                double number = value.get<double>();
                if (std::isfinite(number))
                    value = std::round(number * 1000000.0) / 1000000.0;
                return;
            }

            if (value.is_array())
            {
                for (auto &item : value)
                    RoundJsonFloats(item);
                return;
            }

            if (value.is_object())
            {
                for (auto &item : value.items())
                    RoundJsonFloats(item.value());
            }
        }

        bool HasJsonField(const nlohmann::json &nodeJson, const char *name)
        {
            auto it = nodeJson.find(name);
            return it != nodeJson.end() && !it->is_null();
        }

        std::string ReadStringField(const nlohmann::json &nodeJson, const char *name, const char *fallback)
        {
            auto it = nodeJson.find(name);
            return it != nodeJson.end() && it->is_string() ? it->get<std::string>() : fallback;
        }

        int ReadIntField(const nlohmann::json &nodeJson, const char *name, int fallback)
        {
            auto it = nodeJson.find(name);
            return it != nodeJson.end() && it->is_number_integer() ? it->get<int>() : fallback;
        }

        bool ReadBoolField(const nlohmann::json &nodeJson, const char *name, bool fallback)
        {
            auto it = nodeJson.find(name);
            return it != nodeJson.end() && it->is_boolean() ? it->get<bool>() : fallback;
        }

        bool IsPortableRelativePath(const std::filesystem::path &path)
        {
            if (path.empty() || path.is_absolute())
                return false;

            auto it = path.begin();
            return it == path.end() || *it != "..";
        }

        const std::vector<std::pair<std::string, std::string>> &PrimitiveChoices()
        {
            static const std::vector<std::pair<std::string, std::string>> choices = {
                {"cube", "Cube"},
                {"sphere", "Sphere"},
                {"uv_sphere", "UV Sphere"},
                {"ico_sphere", "Ico Sphere"},
                {"plane", "Plane"},
                {"grid", "Grid"},
                {"cylinder", "Cylinder"},
                {"cone", "Cone"},
                {"pyramid", "Pyramid"},
                {"quad", "Quad"},
                {"circle", "Circle"},
                {"torus", "Torus"},
                {"skinned_strip_2d", "Skinned Strip 2D"},
            };
            return choices;
        }

        const char *NodeKindLabel(const nlohmann::json &nodeJson)
        {
            if (HasJsonField(nodeJson, "prefab"))
                return "Nested Prefab";
            if (HasJsonField(nodeJson, "mesh") || HasJsonField(nodeJson, "mesh_refs"))
                return "Mesh";
            if (HasJsonField(nodeJson, "camera"))
                return "Camera";
            if (HasJsonField(nodeJson, "light"))
                return "Light";
            if (HasJsonField(nodeJson, "script"))
                return "Script";
            if (HasJsonField(nodeJson, "skybox"))
                return "Skybox";
            if (HasJsonField(nodeJson, "physics") || HasJsonField(nodeJson, "physics2d"))
                return "Physics";
            if (HasJsonField(nodeJson, "audio"))
                return "Audio";
            return "Node";
        }
    } // namespace

    PrefabViewer::PrefabViewer() : Widget("Prefab Viewer")
    {
        m_open = false;
    }

    bool PrefabViewer::OpenPrefab(const std::filesystem::path &path)
    {
        m_open = true;
        return LoadFromDisk(path);
    }

    bool PrefabViewer::LoadFromDisk(const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            m_status = "Failed to open " + path.string();
            PE_WARN("[PrefabViewer] Failed to open prefab: %s", path.string().c_str());
            return false;
        }

        nlohmann::json document = nlohmann::json::parse(file, nullptr, false);
        if (document.is_discarded() || !document.is_object() || !document.contains("nodes") || !document["nodes"].is_array())
        {
            m_status = "Invalid prefab file";
            PE_WARN("[PrefabViewer] Invalid prefab file: %s", path.string().c_str());
            return false;
        }

        m_prefabPath = path;
        m_document = std::move(document);
        if (!m_document.contains("asset_type"))
            m_document["asset_type"] = "prefab";
        if (!m_document.contains("version"))
            m_document["version"] = 1;
        if (!m_document.contains("root"))
            m_document["root"] = 0;

        RebuildNodeViews();
        m_selectedIndex = m_rootIndex >= 0 ? m_rootIndex : (m_nodes.empty() ? -1 : 0);
        m_dirty = false;
        m_status = "Loaded " + m_prefabPath.filename().string();
        return true;
    }

    bool PrefabViewer::SaveToDisk()
    {
        if (m_prefabPath.empty() || !m_document.is_object() || !m_document.contains("nodes"))
            return false;

        std::error_code ec;
        if (!m_prefabPath.parent_path().empty())
            std::filesystem::create_directories(m_prefabPath.parent_path(), ec);
        if (ec)
        {
            m_status = "Failed to create prefab folder";
            PE_WARN("[PrefabViewer] Failed to create prefab folder '%s': %s",
                    m_prefabPath.parent_path().string().c_str(), ec.message().c_str());
            return false;
        }

        std::ofstream file(m_prefabPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            m_status = "Failed to save prefab";
            PE_WARN("[PrefabViewer] Failed to save prefab: %s", m_prefabPath.string().c_str());
            return false;
        }

        nlohmann::json documentToWrite = m_document;
        RoundJsonFloats(documentToWrite);
        file << std::setw(2) << documentToWrite << '\n';
        if (!file.good())
        {
            m_status = "Failed to write prefab";
            return false;
        }

        m_dirty = false;
        m_status = "Saved " + m_prefabPath.filename().string();
        if (auto *browser = m_gui ? m_gui->GetWidget<FileBrowser>() : nullptr)
            browser->RefreshCache();
        if (m_gui)
            m_gui->NotifyChange();
        return true;
    }

    void PrefabViewer::OpenFileDialog()
    {
        if (!m_gui)
            return;

        auto *selector = m_gui->GetWidget<FileSelector>();
        if (!selector)
            return;

        selector->OpenSelection([this](const std::string &path) -> bool
                                {
                                    OpenPrefab(path);
                                    return true; },
                                {".peprefab"},
                                Path::Assets);
    }

    void PrefabViewer::ReloadFromDisk()
    {
        if (!m_prefabPath.empty())
            LoadFromDisk(m_prefabPath);
    }

    void PrefabViewer::RebuildNodeViews()
    {
        m_nodes.clear();
        m_rootIndex = 0;

        if (!m_document.contains("nodes") || !m_document["nodes"].is_array())
            return;

        auto &nodesJson = m_document["nodes"];
        m_nodes.resize(nodesJson.size());

        if (m_document.contains("root") && m_document["root"].is_number_integer())
            m_rootIndex = m_document["root"].get<int>();
        if (m_rootIndex < 0 || m_rootIndex >= static_cast<int>(m_nodes.size()))
            m_rootIndex = m_nodes.empty() ? -1 : 0;
        if (m_rootIndex >= 0)
            m_document["root"] = m_rootIndex;

        for (int i = 0; i < static_cast<int>(nodesJson.size()); ++i)
        {
            auto &nodeJson = nodesJson[i];
            if (!nodeJson.is_object())
                nodeJson = nlohmann::json::object();

            PrefabNodeView &view = m_nodes[i];
            view.name = ReadStringField(nodeJson, "name", "Prefab Node");
            if (view.name.empty())
                view.name = "Prefab Node";
            view.parent = ReadIntField(nodeJson, "parent", -1);
            if (view.parent < 0 || view.parent >= static_cast<int>(nodesJson.size()) || view.parent == i)
                view.parent = -1;
            if (i == m_rootIndex)
                view.parent = -1;
            view.enabled = ReadBoolField(nodeJson, "enabled", true);

            nodeJson["name"] = view.name;
            nodeJson["parent"] = view.parent;
            if (view.enabled)
                nodeJson.erase("enabled");
            else
                nodeJson["enabled"] = false;

            float matrix[16];
            if (!ReadMatrix(nodeJson, matrix))
                WriteMatrix(nodeJson, matrix);
        }

        for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
        {
            int parent = m_nodes[i].parent;
            if (parent >= 0 && parent < static_cast<int>(m_nodes.size()) && !IsDescendantOf(parent, i))
                m_nodes[parent].children.push_back(i);
            else
            {
                m_nodes[i].parent = -1;
                nodesJson[i]["parent"] = -1;
            }
        }
    }

    void PrefabViewer::MarkDirty()
    {
        m_dirty = true;
        if (m_gui)
            m_gui->NotifyChange();
    }

    void PrefabViewer::Update()
    {
        if (!m_open)
            return;

        ImGui::SetNextWindowSize(ImVec2(860.f, 560.f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Prefab Viewer", &m_open))
        {
            ImGui::End();
            return;
        }

        DrawToolbar();
        ImGui::Separator();

        if (m_prefabPath.empty() || !m_document.is_object())
        {
            DrawEmptyState();
            ImGui::End();
            return;
        }

        if (!m_status.empty())
            ImGui::TextDisabled("%s", m_status.c_str());

        ImGui::BeginChild("##prefab_viewer_tree", ImVec2(ImGui::GetContentRegionAvail().x * 0.38f, 0), true);
        DrawTreePanel();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##prefab_viewer_inspector", ImVec2(0, 0), true);
        DrawInspectorPanel();
        ImGui::EndChild();

        ImGui::End();
    }

    void PrefabViewer::DrawToolbar()
    {
        if (ImGui::Button("Open..."))
            OpenFileDialog();
        ui::ItemTooltip("Open a .peprefab asset.");

        ImGui::SameLine();
        const bool canSave = !m_prefabPath.empty() && m_document.is_object();
        if (!canSave)
            ImGui::BeginDisabled();
        if (ImGui::Button("Save"))
            SaveToDisk();
        ui::ItemTooltip("Save this prefab asset.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        if (!canSave)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (!canSave)
            ImGui::BeginDisabled();
        if (ImGui::Button("Reload"))
            ReloadFromDisk();
        ui::ItemTooltip("Reload this prefab from disk.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        if (!canSave)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (!canSave)
            ImGui::BeginDisabled();
        if (ImGui::Button("Instantiate"))
            InstantiateIntoScene();
        ui::ItemTooltip("Instantiate this prefab into the active scene.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        if (!canSave)
            ImGui::EndDisabled();

        if (m_dirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.f, 0.75f, 0.25f, 1.f), "modified");
        }
    }

    void PrefabViewer::DrawEmptyState()
    {
        ImGui::TextDisabled("Open a .peprefab asset to edit its internal hierarchy.");
        ImGui::Spacing();
        if (ImGui::Button("Open Prefab..."))
            OpenFileDialog();
        ui::ItemTooltip("Open a .peprefab asset.");

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                const char *pathStr = static_cast<const char *>(payload->Data);
                std::filesystem::path path(pathStr);
                if (FileBrowser::IsPrefabFile(path))
                    OpenPrefab(path);
            }
            ImGui::EndDragDropTarget();
        }
    }

    void PrefabViewer::DrawTreePanel()
    {
        if (!m_prefabPath.empty())
        {
            ImGui::TextWrapped("%s", PathToString(m_prefabPath).c_str());
            ImGui::Separator();
        }

        if (m_nodes.empty())
        {
            ImGui::TextDisabled("This prefab has no nodes.");
            if (ImGui::Button("+ Add Root"))
                ImGui::OpenPopup("AddPrefabRoot##tree");
            if (ImGui::BeginPopup("AddPrefabRoot##tree"))
            {
                DrawAddMenu(-1);
                ImGui::EndPopup();
            }
            return;
        }

        if (ImGui::Button("+ Add"))
            ImGui::OpenPopup("AddPrefabItem##tree");
        ui::ItemTooltip("Add content inside the prefab asset.");
        if (ImGui::BeginPopup("AddPrefabItem##tree"))
        {
            DrawAddMenu(m_selectedIndex >= 0 ? m_selectedIndex : m_rootIndex);
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        const bool canRemove = m_selectedIndex >= 0 && m_selectedIndex != m_rootIndex;
        if (!canRemove)
            ImGui::BeginDisabled();
        if (ImGui::Button("Remove"))
            RemoveSelectedNode();
        ui::ItemTooltip("Remove the selected node and its children from the prefab asset.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        if (!canRemove)
            ImGui::EndDisabled();

        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
            if (m_nodes[i].parent < 0)
                DrawNodeTree(i);
    }

    void PrefabViewer::DrawNodeTree(int nodeIndex)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
            return;

        PrefabNodeView &node = m_nodes[nodeIndex];
        ImGui::PushID(nodeIndex);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (node.children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (m_selectedIndex == nodeIndex)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (nodeIndex == m_rootIndex)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;

        const nlohmann::json &nodeJson = m_document["nodes"][nodeIndex];
        const char *icon = HasJsonField(nodeJson, "prefab") ? ICON_FA_CUBES : (node.children.empty() ? ICON_FA_FOLDER : ICON_FA_SITEMAP);
        std::string label = std::string(icon) + "  " + node.name;
        bool open = ImGui::TreeNodeEx("##prefab_node", flags, "%s", label.c_str());
        if (!node.enabled)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("disabled");
        }
        if (nodeIndex == m_rootIndex)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("root");
        }
        ui::ItemTooltip("Select this prefab item. Drag it onto another item to reparent it inside the prefab asset.");

        if (ImGui::IsItemClicked())
        {
            m_selectedIndex = nodeIndex;
            snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", node.name.c_str());
        }

        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload(kPrefabPayload, &nodeIndex, sizeof(nodeIndex));
            ImGui::Text("%s", node.name.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kPrefabPayload))
            {
                int sourceIndex = *static_cast<const int *>(payload->Data);
                ReparentNode(sourceIndex, nodeIndex);
            }
            ImGui::EndDragDropTarget();
        }

        if (open)
        {
            for (int child : node.children)
                DrawNodeTree(child);
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    void PrefabViewer::DrawInspectorPanel()
    {
        if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_nodes.size()))
        {
            ImGui::TextDisabled("Select a prefab item to edit it.");
            return;
        }

        PrefabNodeView &view = m_nodes[m_selectedIndex];
        nlohmann::json &nodeJson = m_document["nodes"][m_selectedIndex];

        ImGui::Text("%s", NodeKindLabel(nodeJson));
        ImGui::SameLine();
        if (m_selectedIndex == m_rootIndex)
            ImGui::TextDisabled("Root");

        ImGui::Separator();

        if (m_renameBuffer[0] == '\0')
            snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", view.name.c_str());

        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::InputText("Name", m_renameBuffer, sizeof(m_renameBuffer)))
        {
            view.name = m_renameBuffer[0] ? m_renameBuffer : "Prefab Node";
            nodeJson["name"] = view.name;
            MarkDirty();
        }
        ui::ItemTooltip("Rename this prefab item.");

        bool enabled = view.enabled;
        if (ImGui::Checkbox("Enabled", &enabled))
        {
            view.enabled = enabled;
            if (enabled)
                nodeJson.erase("enabled");
            else
                nodeJson["enabled"] = false;
            MarkDirty();
        }
        ui::ItemTooltip("Toggle this item when the prefab is instantiated.");

        ImGui::Separator();
        DrawTransformEditor(m_selectedIndex);

        ImGui::Separator();
        if (ImGui::Button("+ Add"))
            ImGui::OpenPopup("AddPrefabItem##inspector");
        ui::ItemTooltip("Add content under or onto this prefab item.");
        if (ImGui::BeginPopup("AddPrefabItem##inspector"))
        {
            DrawAddMenu(m_selectedIndex);
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        const bool canRemove = m_selectedIndex != m_rootIndex;
        if (!canRemove)
            ImGui::BeginDisabled();
        if (ImGui::Button("Remove Item"))
            RemoveSelectedNode();
        ui::ItemTooltip("Remove this item and its children from the prefab asset.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        if (!canRemove)
            ImGui::EndDisabled();

        ImGui::Separator();
        DrawMeshEditor(m_selectedIndex);
        DrawComponentEditors(m_selectedIndex);
        DrawJsonSummary(m_selectedIndex);
    }

    void PrefabViewer::DrawTransformEditor(int nodeIndex)
    {
        nlohmann::json &nodeJson = m_document["nodes"][nodeIndex];
        float matrix[16];
        ReadMatrix(nodeJson, matrix);

        float t[3], r[3], s[3];
        ImGuizmo::DecomposeMatrixToComponents(matrix, t, r, s);

        bool changed = false;
        ImGui::TextDisabled("Local Transform");

        ImGui::SetNextItemWidth(-1.f);
        changed |= ImGui::DragFloat3("Position", t, 0.1f, 0.0f, 0.0f, "%.2f");
        ui::ItemTooltip("Edit local position stored in the prefab asset.");

        ImGui::SetNextItemWidth(-1.f);
        changed |= ImGui::DragFloat3("Rotation", r, 0.1f, 0.0f, 0.0f, "%.2f");
        ui::ItemTooltip("Edit local Euler rotation in degrees.");

        ImGui::SetNextItemWidth(-1.f);
        changed |= ImGui::DragFloat3("Scale", s, 0.01f, 0.001f, 0.0f, "%.3f");
        ui::ItemTooltip("Edit local scale.");

        if (changed)
        {
            for (int i = 0; i < 3; ++i)
                s[i] = std::max(s[i], 0.001f);
            ImGuizmo::RecomposeMatrixFromComponents(t, r, s, matrix);
            WriteMatrix(nodeJson, matrix);
            MarkDirty();
        }
    }

    void PrefabViewer::DrawMeshEditor(int nodeIndex)
    {
        nlohmann::json &nodeJson = m_document["nodes"][nodeIndex];
        if (!ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        bool hasMesh = HasJsonField(nodeJson, "mesh") || HasJsonField(nodeJson, "mesh_refs");
        if (hasMesh)
        {
            if (nodeJson.contains("mesh") && nodeJson["mesh"].is_number_integer())
                ImGui::Text("Mesh ref: %d", nodeJson["mesh"].get<int>());
            if (nodeJson.contains("mesh_refs") && nodeJson["mesh_refs"].is_array())
            {
                ImGui::Text("Mesh refs:");
                for (const auto &ref : nodeJson["mesh_refs"])
                    if (ref.is_number_integer())
                        ImGui::BulletText("%d", ref.get<int>());
            }

            if (ImGui::Button("Remove Mesh"))
                RemoveComponent(nodeIndex, Component_Mesh);
            ui::ItemTooltip("Detach mesh references from this prefab item.");
            ImGui::SameLine();
        }
        else
        {
            ImGui::TextDisabled("No mesh attached.");
        }

        if (ImGui::Button(hasMesh ? "Attach More..." : "Attach Mesh..."))
            ImGui::OpenPopup("AttachMesh##prefab");
        ui::ItemTooltip("Attach generated or cooked mesh geometry to this prefab item.");
        if (ImGui::BeginPopup("AttachMesh##prefab"))
        {
            DrawAttachMeshMenu(nodeIndex);
            ImGui::EndPopup();
        }
    }

    void PrefabViewer::DrawComponentEditors(int nodeIndex)
    {
        nlohmann::json &nodeJson = m_document["nodes"][nodeIndex];
        if (!ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (ImGui::Button("+ Add Component"))
            ImGui::OpenPopup("AddComponent##prefab");
        ui::ItemTooltip("Add a component payload to this prefab item.");
        if (ImGui::BeginPopup("AddComponent##prefab"))
        {
            DrawComponentAddMenu(nodeIndex);
            ImGui::EndPopup();
        }

        uint32_t flags = GetNodeFlags(nodeJson);

        if (flags & Component_Camera)
        {
            ImGui::SeparatorText("Camera");
            nlohmann::json &camera = nodeJson["camera"];
            if (!camera.is_object())
                camera = nlohmann::json::object();

            std::string projection = ReadStringField(camera, "projection", "perspective");
            int projectionIndex = projection == "orthographic" ? 1 : 0;
            const char *projectionItems[] = {"Perspective", "Orthographic"};
            if (ImGui::Combo("Projection", &projectionIndex, projectionItems, IM_ARRAYSIZE(projectionItems)))
            {
                camera["projection"] = projectionIndex == 1 ? "orthographic" : "perspective";
                MarkDirty();
            }

            float fovx = camera.value("fovx", 60.0f);
            if (ImGui::DragFloat("FOV X", &fovx, 0.1f, 1.0f, 179.0f, "%.1f"))
            {
                camera["fovx"] = fovx;
                MarkDirty();
            }
            float ortho = camera.value("orthographic_size", 10.0f);
            if (ImGui::DragFloat("Orthographic Size", &ortho, 0.1f, 0.01f, 10000.0f, "%.2f"))
            {
                camera["orthographic_size"] = ortho;
                MarkDirty();
            }
            float nearPlane = camera.value("near_plane", 0.1f);
            if (ImGui::DragFloat("Near", &nearPlane, 0.01f, 0.001f, 1000.0f, "%.3f"))
            {
                camera["near_plane"] = nearPlane;
                MarkDirty();
            }
            float farPlane = camera.value("far_plane", 1000.0f);
            if (ImGui::DragFloat("Far", &farPlane, 1.0f, 0.01f, 100000.0f, "%.1f"))
            {
                camera["far_plane"] = farPlane;
                MarkDirty();
            }
            if (ImGui::Button("Remove Camera"))
                RemoveComponent(nodeIndex, Component_Camera);
        }

        if (flags & Component_Light)
        {
            ImGui::SeparatorText("Light");
            nlohmann::json &light = nodeJson["light"];
            if (!light.is_object())
                light = nlohmann::json::object();

            std::string type = ReadStringField(light, "type", "point");
            int typeIndex = type == "directional" ? 0 : (type == "spot" ? 2 : (type == "area" ? 3 : 1));
            const char *lightItems[] = {"Directional", "Point", "Spot", "Area"};
            if (ImGui::Combo("Type", &typeIndex, lightItems, IM_ARRAYSIZE(lightItems)))
            {
                light["type"] = typeIndex == 0 ? "directional" : (typeIndex == 2 ? "spot" : (typeIndex == 3 ? "area" : "point"));
                MarkDirty();
            }

            float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            if (light.contains("color") && light["color"].is_array() && light["color"].size() >= 4)
            {
                for (int i = 0; i < 4; ++i)
                    if (light["color"][i].is_number())
                        color[i] = light["color"][i].get<float>();
            }
            if (ImGui::ColorEdit4("Color", color))
            {
                light["color"] = MakeVec4Json(color[0], color[1], color[2], color[3]);
                MarkDirty();
            }

            float range = light.value(typeIndex == 1 ? "radius" : "range", 10.0f);
            if (typeIndex != 0 && ImGui::DragFloat(typeIndex == 1 ? "Radius" : "Range", &range, 0.1f, 0.0f, 10000.0f, "%.2f"))
            {
                light[typeIndex == 1 ? "radius" : "range"] = range;
                MarkDirty();
            }
            if (ImGui::Button("Remove Light"))
                RemoveComponent(nodeIndex, Component_Light);
        }

        if (flags & Component_Script)
        {
            ImGui::SeparatorText("Script");
            std::string scriptPath = ReadStringField(nodeJson, "script", "");
            ImGui::TextWrapped("%s", scriptPath.empty() ? "<none>" : scriptPath.c_str());
            if (ImGui::Button("Browse Script..."))
                OpenScriptDialog(nodeIndex);
            ImGui::SameLine();
            if (ImGui::Button("Remove Script"))
                RemoveComponent(nodeIndex, Component_Script);
        }

        if (flags & Component_Physics)
        {
            ImGui::SeparatorText("Physics");
            nlohmann::json &physics = nodeJson["physics"];
            if (!physics.is_object())
                AddComponent(nodeIndex, Component_Physics);
            int bodyType = physics.value("body_type", 1);
            const char *bodyItems[] = {"Static", "Dynamic", "Kinematic"};
            if (ImGui::Combo("Body Type", &bodyType, bodyItems, IM_ARRAYSIZE(bodyItems)))
            {
                physics["body_type"] = bodyType;
                MarkDirty();
            }
            int shapeType = physics.value("shape_type", 0);
            const char *shapeItems[] = {"Box", "Sphere", "Capsule", "Convex Hull"};
            if (ImGui::Combo("Shape", &shapeType, shapeItems, IM_ARRAYSIZE(shapeItems)))
            {
                physics["shape_type"] = shapeType;
                MarkDirty();
            }
            float mass = physics.value("mass", 1.0f);
            if (ImGui::DragFloat("Mass", &mass, 0.05f, 0.0f, 100000.0f, "%.2f"))
            {
                physics["mass"] = mass;
                MarkDirty();
            }
            bool autoFit = physics.value("auto_fit", true);
            if (ImGui::Checkbox("Auto Fit Shape", &autoFit))
            {
                physics["auto_fit"] = autoFit;
                MarkDirty();
            }
            bool trigger = physics.value("is_trigger", false);
            if (ImGui::Checkbox("Trigger", &trigger))
            {
                physics["is_trigger"] = trigger;
                MarkDirty();
            }
            if (ImGui::Button("Remove Physics"))
                RemoveComponent(nodeIndex, Component_Physics);
        }

        if (flags & Component_Physics2D)
        {
            ImGui::SeparatorText("Physics2D");
            nlohmann::json &physics = nodeJson["physics2d"];
            if (!physics.is_object())
                AddComponent(nodeIndex, Component_Physics2D);
            int bodyType = physics.value("body_type", 1);
            const char *bodyItems[] = {"Static", "Dynamic", "Kinematic"};
            if (ImGui::Combo("2D Body Type", &bodyType, bodyItems, IM_ARRAYSIZE(bodyItems)))
            {
                physics["body_type"] = bodyType;
                MarkDirty();
            }
            int shapeType = physics.value("shape_type", 0);
            const char *shapeItems[] = {"Box", "Circle", "Capsule"};
            if (ImGui::Combo("2D Shape", &shapeType, shapeItems, IM_ARRAYSIZE(shapeItems)))
            {
                physics["shape_type"] = shapeType;
                MarkDirty();
            }
            float width = physics.value("width", 1.0f);
            float height = physics.value("height", 1.0f);
            if (ImGui::DragFloat("Width", &width, 0.05f, 0.001f, 10000.0f, "%.2f"))
            {
                physics["width"] = width;
                MarkDirty();
            }
            if (ImGui::DragFloat("Height", &height, 0.05f, 0.001f, 10000.0f, "%.2f"))
            {
                physics["height"] = height;
                MarkDirty();
            }
            bool sensor = physics.value("is_sensor", false);
            if (ImGui::Checkbox("Sensor", &sensor))
            {
                physics["is_sensor"] = sensor;
                MarkDirty();
            }
            if (ImGui::Button("Remove Physics2D"))
                RemoveComponent(nodeIndex, Component_Physics2D);
        }

        if (flags & Component_Audio)
        {
            ImGui::SeparatorText("Audio");
            nlohmann::json &audio = nodeJson["audio"];
            if (!audio.is_object())
                AddComponent(nodeIndex, Component_Audio);
            float volume = audio.value("volume", 1.0f);
            if (ImGui::DragFloat("Volume", &volume, 0.01f, 0.0f, 10.0f, "%.2f"))
            {
                audio["volume"] = volume;
                MarkDirty();
            }
            bool loop = audio.value("loop", false);
            if (ImGui::Checkbox("Loop", &loop))
            {
                audio["loop"] = loop;
                MarkDirty();
            }
            bool spatial = audio.value("spatial", true);
            if (ImGui::Checkbox("Spatial", &spatial))
            {
                audio["spatial"] = spatial;
                MarkDirty();
            }
            if (ImGui::Button("Remove Audio"))
                RemoveComponent(nodeIndex, Component_Audio);
        }

        if (flags & Component_Skybox)
        {
            ImGui::SeparatorText("Skybox");
            nlohmann::json &skybox = nodeJson["skybox"];
            if (!skybox.is_object())
                AddComponent(nodeIndex, Component_Skybox);
            std::string path = ReadStringField(skybox, "path", "");
            ImGui::TextWrapped("%s", path.empty() ? "<empty>" : path.c_str());
            if (ImGui::Button("Remove Skybox"))
                RemoveComponent(nodeIndex, Component_Skybox);
        }

        if (flags & Component_RuntimeUi)
        {
            ImGui::SeparatorText("Runtime UI");
            ImGui::TextDisabled("Runtime UI tag component");
            if (ImGui::Button("Remove Runtime UI"))
                RemoveComponent(nodeIndex, Component_RuntimeUi);
        }
    }

    void PrefabViewer::DrawAddMenu(int parentIndex)
    {
        if (ImGui::MenuItem("Empty Item"))
            AddChild(parentIndex);
        ui::ItemTooltip("Add an empty prefab item.");

        if (ImGui::BeginMenu("Primitive Mesh Child"))
        {
            for (const auto &[type, label] : PrimitiveChoices())
            {
                if (ImGui::MenuItem(label.c_str()))
                    AddPrimitiveMeshChild(parentIndex, type, label);
            }
            ImGui::EndMenu();
        }
        ui::ItemTooltip("Add a child with generated primitive mesh geometry.");

        if (ImGui::MenuItem("Cooked Mesh Child..."))
            OpenCookedMeshDialog(true, parentIndex);
        ui::ItemTooltip("Add a child backed by a cooked .pemesh asset.");

        ImGui::Separator();

        const bool canAttach = parentIndex >= 0 && parentIndex < static_cast<int>(m_nodes.size());
        if (!canAttach)
            ImGui::BeginDisabled();
        if (ImGui::BeginMenu("Attach Primitive Mesh"))
        {
            for (const auto &[type, label] : PrimitiveChoices())
            {
                if (ImGui::MenuItem(label.c_str()))
                    AttachPrimitiveMesh(parentIndex, type);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Attach Cooked Mesh..."))
            OpenCookedMeshDialog(false, parentIndex);
        if (ImGui::BeginMenu("Add Component"))
        {
            DrawComponentAddMenu(parentIndex);
            ImGui::EndMenu();
        }
        if (!canAttach)
            ImGui::EndDisabled();
    }

    void PrefabViewer::DrawAttachMeshMenu(int nodeIndex)
    {
        if (ImGui::BeginMenu("Primitive"))
        {
            for (const auto &[type, label] : PrimitiveChoices())
            {
                if (ImGui::MenuItem(label.c_str()))
                    AttachPrimitiveMesh(nodeIndex, type);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Cooked .pemesh..."))
            OpenCookedMeshDialog(false, nodeIndex);
    }

    void PrefabViewer::DrawComponentAddMenu(int nodeIndex)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
            return;

        nlohmann::json &nodeJson = m_document["nodes"][nodeIndex];
        const uint32_t flags = GetNodeFlags(nodeJson);

        if (ImGui::MenuItem("Camera", nullptr, false, (flags & Component_Camera) == 0))
            AddComponent(nodeIndex, Component_Camera);
        if (ImGui::BeginMenu("Light", (flags & Component_Light) == 0))
        {
            if (ImGui::MenuItem("Directional"))
                AddComponent(nodeIndex, Component_Light, "directional");
            if (ImGui::MenuItem("Point"))
                AddComponent(nodeIndex, Component_Light, "point");
            if (ImGui::MenuItem("Spot"))
                AddComponent(nodeIndex, Component_Light, "spot");
            if (ImGui::MenuItem("Area"))
                AddComponent(nodeIndex, Component_Light, "area");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Lua Script...", nullptr, false, (flags & Component_Script) == 0))
            OpenScriptDialog(nodeIndex);
        if (ImGui::MenuItem("Physics Body", nullptr, false, (flags & Component_Physics) == 0))
            AddComponent(nodeIndex, Component_Physics);
        if (ImGui::MenuItem("Physics2D Body", nullptr, false, (flags & Component_Physics2D) == 0))
            AddComponent(nodeIndex, Component_Physics2D);
        if (ImGui::MenuItem("Audio Source", nullptr, false, (flags & Component_Audio) == 0))
            AddComponent(nodeIndex, Component_Audio);
        if (ImGui::MenuItem("Skybox", nullptr, false, (flags & Component_Skybox) == 0))
            AddComponent(nodeIndex, Component_Skybox);
        if (ImGui::MenuItem("Runtime UI", nullptr, false, (flags & Component_RuntimeUi) == 0))
            AddComponent(nodeIndex, Component_RuntimeUi);
    }

    void PrefabViewer::DrawJsonSummary(int nodeIndex)
    {
        const nlohmann::json &nodeJson = m_document["nodes"][nodeIndex];
        if (!ImGui::CollapsingHeader("Stored Components"))
            return;

        const uint32_t flags = GetNodeFlags(nodeJson);
        if (HasJsonField(nodeJson, "mesh") || HasJsonField(nodeJson, "mesh_refs"))
            ImGui::BulletText("Mesh");
        if (HasJsonField(nodeJson, "camera"))
            ImGui::BulletText("Camera");
        if (HasJsonField(nodeJson, "light"))
            ImGui::BulletText("Light");
        if (HasJsonField(nodeJson, "script"))
        {
            if (nodeJson["script"].is_string())
                ImGui::BulletText("Script: %s", nodeJson["script"].get<std::string>().c_str());
            else
                ImGui::BulletText("Script");
        }
        if (HasJsonField(nodeJson, "prefab"))
        {
            if (nodeJson["prefab"].is_string())
                ImGui::BulletText("Nested prefab: %s", nodeJson["prefab"].get<std::string>().c_str());
            else
                ImGui::BulletText("Nested prefab");
        }
        if (HasJsonField(nodeJson, "physics") || HasJsonField(nodeJson, "physics2d"))
            ImGui::BulletText("Physics");
        if (HasJsonField(nodeJson, "audio"))
            ImGui::BulletText("Audio");
        if (HasJsonField(nodeJson, "skybox"))
            ImGui::BulletText("Skybox");
        if (HasJsonField(nodeJson, "skinned_strip_2d"))
            ImGui::BulletText("Skinned Strip 2D");
        if (flags & Component_RuntimeUi)
            ImGui::BulletText("Runtime UI");
        ImGui::TextDisabled("Component data is preserved when saving.");
    }

    void PrefabViewer::AddChild(int parentIndex)
    {
        AddNode(parentIndex, MakeUniqueChildName(parentIndex, "Prefab Item"));
    }

    int PrefabViewer::AddNode(int parentIndex, const std::string &name)
    {
        if (!m_document.contains("nodes") || !m_document["nodes"].is_array())
            m_document["nodes"] = nlohmann::json::array();

        if (parentIndex < -1 || parentIndex >= static_cast<int>(m_nodes.size()))
            parentIndex = m_rootIndex;

        nlohmann::json nodeJson = nlohmann::json::object();
        nodeJson["name"] = name.empty() ? MakeUniqueChildName(parentIndex, "Prefab Item") : name;
        nodeJson["parent"] = parentIndex;
        float matrix[16];
        MakeIdentity(matrix);
        WriteMatrix(nodeJson, matrix);

        m_document["nodes"].push_back(std::move(nodeJson));
        RebuildNodeViews();
        m_selectedIndex = static_cast<int>(m_nodes.size()) - 1;
        snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", m_nodes[m_selectedIndex].name.c_str());
        MarkDirty();
        return m_selectedIndex;
    }

    void PrefabViewer::AddPrimitiveMeshChild(int parentIndex, const std::string &primitiveType, const std::string &name)
    {
        int nodeIndex = AddNode(parentIndex, MakeUniqueChildName(parentIndex, name));
        AttachPrimitiveMesh(nodeIndex, primitiveType);
    }

    void PrefabViewer::AttachPrimitiveMesh(int nodeIndex, const std::string &primitiveType)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
            return;

        int sourceIndex = AddPrimitiveSource(primitiveType);
        int meshIndex = AddMeshForSource(sourceIndex);
        AttachMeshRef(nodeIndex, meshIndex);
    }

    void PrefabViewer::OpenCookedMeshDialog(bool asChild, int targetIndex)
    {
        if (!m_gui)
            return;

        auto *selector = m_gui->GetWidget<FileSelector>();
        if (!selector)
            return;

        selector->OpenSelection([this, asChild, targetIndex](const std::string &path) -> bool
                                {
                                    int nodeIndex = targetIndex;
                                    if (asChild)
                                    {
                                        std::filesystem::path meshPath(path);
                                        nodeIndex = AddNode(targetIndex, MakeUniqueChildName(targetIndex, meshPath.stem().string()));
                                    }

                                    AttachCookedMesh(nodeIndex, path);
                                    return true; },
                                {".pemesh"},
                                Path::Assets);
    }

    void PrefabViewer::AttachCookedMesh(int nodeIndex, const std::filesystem::path &path)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
            return;

        int sourceIndex = AddCookedMeshSource(path);
        int meshIndex = AddMeshForSource(sourceIndex);
        AttachMeshRef(nodeIndex, meshIndex);
    }

    void PrefabViewer::OpenScriptDialog(int nodeIndex)
    {
        if (!m_gui || nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
            return;

        auto *selector = m_gui->GetWidget<FileSelector>();
        if (!selector)
            return;

        selector->OpenSelection([this, nodeIndex](const std::string &path) -> bool
                                {
                                    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
                                        return true;

                                    nlohmann::json &nodeJson = m_document["nodes"][nodeIndex];
                                    nodeJson["script"] = SerializePrefabRelativePath(path);
                                    SetNodeFlags(nodeJson, GetNodeFlags(nodeJson) | Component_Script);
                                    MarkDirty();
                                    return true; },
                                {".lua"},
                                Path::Assets);
    }

    void PrefabViewer::RemoveSelectedNode()
    {
        if (m_selectedIndex < 0 || m_selectedIndex == m_rootIndex || m_selectedIndex >= static_cast<int>(m_nodes.size()))
            return;

        std::vector<bool> remove(m_nodes.size(), false);
        auto mark = [&](auto &&self, int nodeIndex) -> void
        {
            remove[nodeIndex] = true;
            for (int child : m_nodes[nodeIndex].children)
                self(self, child);
        };
        mark(mark, m_selectedIndex);

        std::vector<int> oldToNew(m_nodes.size(), -1);
        nlohmann::json newNodes = nlohmann::json::array();
        for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
        {
            if (remove[i])
                continue;
            oldToNew[i] = static_cast<int>(newNodes.size());
            newNodes.push_back(m_document["nodes"][i]);
        }

        for (auto &nodeJson : newNodes)
        {
            int oldParent = ReadIntField(nodeJson, "parent", -1);
            nodeJson["parent"] = (oldParent >= 0 && oldParent < static_cast<int>(oldToNew.size())) ? oldToNew[oldParent] : -1;
        }

        int newRoot = (m_rootIndex >= 0 && m_rootIndex < static_cast<int>(oldToNew.size())) ? oldToNew[m_rootIndex] : 0;
        m_document["nodes"] = std::move(newNodes);
        m_document["root"] = std::max(newRoot, 0);
        RebuildNodeViews();
        m_selectedIndex = m_rootIndex;
        if (m_selectedIndex >= 0)
            snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", m_nodes[m_selectedIndex].name.c_str());
        MarkDirty();
    }

    void PrefabViewer::ReparentNode(int nodeIndex, int newParent)
    {
        if (!CanReparent(nodeIndex, newParent))
            return;

        m_document["nodes"][nodeIndex]["parent"] = newParent;
        m_selectedIndex = nodeIndex;
        RebuildNodeViews();
        snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", m_nodes[m_selectedIndex].name.c_str());
        MarkDirty();
    }

    bool PrefabViewer::CanReparent(int nodeIndex, int newParent) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
            return false;
        if (nodeIndex == m_rootIndex)
            return false;
        if (newParent < 0 || newParent >= static_cast<int>(m_nodes.size()))
            return false;
        if (nodeIndex == newParent)
            return false;
        return !IsDescendantOf(newParent, nodeIndex);
    }

    bool PrefabViewer::IsDescendantOf(int nodeIndex, int possibleAncestor) const
    {
        if (nodeIndex < 0 || possibleAncestor < 0)
            return false;

        int current = nodeIndex;
        while (current >= 0 && current < static_cast<int>(m_nodes.size()))
        {
            if (current == possibleAncestor)
                return true;
            current = m_nodes[current].parent;
        }
        return false;
    }

    std::string PrefabViewer::MakeUniqueChildName(int parentIndex, const std::string &baseName) const
    {
        std::unordered_set<std::string> names;
        for (const PrefabNodeView &node : m_nodes)
            if (node.parent == parentIndex)
                names.insert(node.name);

        if (!names.count(baseName))
            return baseName;

        for (int i = 1; i < 10000; ++i)
        {
            std::string candidate = baseName + " " + std::to_string(i);
            if (!names.count(candidate))
                return candidate;
        }
        return baseName;
    }

    void PrefabViewer::InstantiateIntoScene()
    {
        Scene *scene = GetActiveScene();
        if (!scene || m_prefabPath.empty())
            return;

        UndoRedo::Instance().RecordSnapshot(*scene, "Instantiated Prefab");
        SyncSceneBeforeMutation();
        SceneNodeHandle handle = scene->InstantiatePrefab(m_prefabPath);
        if (handle.nodeId && scene->IsNodeAlive(handle.nodeId))
        {
            SelectionManager::Instance().Select(handle.nodeId, SelectionType::Node);
            if (m_gui)
                m_gui->NotifyChange();
        }
    }

    int PrefabViewer::AddPrimitiveSource(const std::string &primitiveType)
    {
        if (!m_document.contains("sources") || !m_document["sources"].is_array())
            m_document["sources"] = nlohmann::json::array();

        nlohmann::json source = nlohmann::json::object();
        source["primitive_type"] = primitiveType;

        if (primitiveType == "plane")
            source["primitive_params"] = nlohmann::json::array({10.0f, 10.0f});
        else if (primitiveType == "grid")
            source["primitive_params"] = nlohmann::json::array({10.0f, 10.0f, 10.0f});
        else if (primitiveType == "cylinder" || primitiveType == "cone")
            source["primitive_params"] = nlohmann::json::array({1.0f, 2.0f});
        else if (primitiveType == "pyramid" || primitiveType == "quad")
            source["primitive_params"] = nlohmann::json::array({1.0f, 1.0f});
        else if (primitiveType == "uv_sphere")
            source["primitive_params"] = nlohmann::json::array({1.0f, 32.0f, 32.0f});
        else if (primitiveType == "ico_sphere")
            source["primitive_params"] = nlohmann::json::array({1.0f, 2.0f});
        else if (primitiveType == "circle")
            source["primitive_params"] = nlohmann::json::array({1.0f, 64.0f});
        else if (primitiveType == "torus")
            source["primitive_params"] = nlohmann::json::array({1.0f, 0.25f, 64.0f, 16.0f});
        else if (primitiveType == "skinned_strip_2d")
            source["primitive_params"] = nlohmann::json::array({4.0f, 1.0f, 32.0f, 24.0f});
        else
            source["primitive_params"] = nlohmann::json::array({1.0f});

        m_document["sources"].push_back(std::move(source));
        return static_cast<int>(m_document["sources"].size()) - 1;
    }

    int PrefabViewer::AddCookedMeshSource(const std::filesystem::path &path)
    {
        if (!m_document.contains("sources") || !m_document["sources"].is_array())
            m_document["sources"] = nlohmann::json::array();

        nlohmann::json source = nlohmann::json::object();
        source["path"] = SerializePrefabRelativePath(path);
        m_document["sources"].push_back(std::move(source));
        return static_cast<int>(m_document["sources"].size()) - 1;
    }

    int PrefabViewer::AddMeshForSource(int sourceIndex, int sourceMeshIndex)
    {
        if (!m_document.contains("meshes") || !m_document["meshes"].is_array())
            m_document["meshes"] = nlohmann::json::array();

        nlohmann::json mesh = nlohmann::json::object();
        mesh["source"] = sourceIndex;
        mesh["source_mesh"] = sourceMeshIndex;
        mesh["render_type"] = 0;
        mesh["texture_mask"] = 0;
        mesh["double_sided"] = false;
        mesh["material_factors"] = MakeDefaultMaterialFactors();
        mesh["textures"] = nlohmann::json::object();

        m_document["meshes"].push_back(std::move(mesh));
        return static_cast<int>(m_document["meshes"].size()) - 1;
    }

    void PrefabViewer::AttachMeshRef(int nodeIndex, int meshIndex)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()) || meshIndex < 0)
            return;

        nlohmann::json &nodeJson = m_document["nodes"][nodeIndex];
        if (nodeJson.contains("mesh_refs") && nodeJson["mesh_refs"].is_array())
        {
            nodeJson["mesh_refs"].push_back(meshIndex);
            nodeJson.erase("mesh");
        }
        else if (nodeJson.contains("mesh") && nodeJson["mesh"].is_number_integer())
        {
            int oldMesh = nodeJson["mesh"].get<int>();
            nodeJson.erase("mesh");
            nodeJson["mesh_refs"] = nlohmann::json::array({oldMesh, meshIndex});
        }
        else
        {
            nodeJson["mesh"] = meshIndex;
        }

        SetNodeFlags(nodeJson, GetNodeFlags(nodeJson) | Component_Mesh);
        MarkDirty();
    }

    void PrefabViewer::AddComponent(int nodeIndex, uint32_t componentFlag, const std::string &componentKind)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
            return;

        nlohmann::json &nodeJson = m_document["nodes"][nodeIndex];
        uint32_t flags = GetNodeFlags(nodeJson) | componentFlag;
        SetNodeFlags(nodeJson, flags);

        switch (componentFlag)
        {
        case Component_Camera:
            nodeJson["camera"] = {
                {"projection", "perspective"},
                {"fovx", 60.0f},
                {"orthographic_size", 10.0f},
                {"near_plane", 0.1f},
                {"far_plane", 1000.0f},
                {"speed", 1.0f},
                {"euler", MakeVec3Json(0.0f, 0.0f, 0.0f)},
            };
            break;
        case Component_Light:
        {
            std::string type = componentKind.empty() ? "point" : componentKind;
            nlohmann::json light = {
                {"type", type},
                {"color", MakeVec4Json(1.0f, 1.0f, 1.0f, 1.0f)},
            };
            if (type == "point")
                light["radius"] = 10.0f;
            else if (type == "spot")
            {
                light["range"] = 10.0f;
                light["params"] = MakeVec4Json(15.0f, 5.0f, 0.0f, 0.0f);
            }
            else if (type == "area")
            {
                light["range"] = 10.0f;
                light["size"] = MakeVec4Json(2.0f, 2.0f, 0.0f, 0.0f);
            }
            nodeJson["light"] = std::move(light);
            break;
        }
        case Component_Physics:
            nodeJson["physics"] = {
                {"body_type", 1},
                {"shape_type", 0},
                {"mass", 1.0f},
                {"friction", 0.5f},
                {"restitution", 0.3f},
                {"auto_fit", true},
                {"is_trigger", false},
                {"box_half_extents", MakeVec3Json(0.5f, 0.5f, 0.5f)},
                {"sphere_radius", 0.5f},
                {"capsule_half_height", 0.5f},
                {"capsule_radius", 0.25f},
            };
            break;
        case Component_Physics2D:
            nodeJson["physics2d"] = {
                {"body_type", 1},
                {"shape_type", 0},
                {"width", 1.0f},
                {"height", 1.0f},
                {"radius", 0.5f},
                {"capsule_height", 1.0f},
                {"capsule_radius", 0.25f},
                {"density", 1.0f},
                {"friction", 0.5f},
                {"restitution", 0.0f},
                {"linear_damping", 0.0f},
                {"angular_damping", 0.0f},
                {"gravity_scale", 1.0f},
                {"category_bits", 1},
                {"mask_bits", std::numeric_limits<uint64_t>::max()},
                {"group_index", 0},
                {"fixed_rotation", false},
                {"is_sensor", false},
                {"sync_node", true},
                {"bullet", false},
                {"enable_sleep", true},
            };
            break;
        case Component_Audio:
            nodeJson["audio"] = {
                {"file", ""},
                {"volume", 1.0f},
                {"pitch", 1.0f},
                {"min_distance", 1.0f},
                {"max_distance", 50.0f},
                {"loop", false},
                {"spatial", true},
                {"autoplay", false},
            };
            break;
        case Component_Skybox:
            nodeJson["skybox"] = {{"path", ""}};
            break;
        case Component_RuntimeUi:
            break;
        default:
            break;
        }

        MarkDirty();
    }

    void PrefabViewer::RemoveComponent(int nodeIndex, uint32_t componentFlag)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
            return;

        nlohmann::json &nodeJson = m_document["nodes"][nodeIndex];
        uint32_t flags = GetNodeFlags(nodeJson) & ~componentFlag;
        SetNodeFlags(nodeJson, flags);

        switch (componentFlag)
        {
        case Component_Mesh:
            nodeJson.erase("mesh");
            nodeJson.erase("mesh_refs");
            break;
        case Component_Camera:
            nodeJson.erase("camera");
            break;
        case Component_Light:
            nodeJson.erase("light");
            break;
        case Component_Physics:
            nodeJson.erase("physics");
            break;
        case Component_Physics2D:
            nodeJson.erase("physics2d");
            break;
        case Component_Script:
            nodeJson.erase("script");
            break;
        case Component_Audio:
            nodeJson.erase("audio");
            break;
        case Component_Skybox:
            nodeJson.erase("skybox");
            break;
        default:
            break;
        }

        MarkDirty();
    }

    uint32_t PrefabViewer::GetNodeFlags(const nlohmann::json &nodeJson) const
    {
        uint32_t flags = 0;
        auto it = nodeJson.find("component_flags");
        if (it != nodeJson.end() && it->is_number_unsigned())
            flags = it->get<uint32_t>();
        else if (it != nodeJson.end() && it->is_number_integer() && it->get<int>() >= 0)
            flags = static_cast<uint32_t>(it->get<int>());

        if (HasJsonField(nodeJson, "mesh") || HasJsonField(nodeJson, "mesh_refs"))
            flags |= Component_Mesh;
        if (HasJsonField(nodeJson, "script"))
            flags |= Component_Script;
        return flags;
    }

    void PrefabViewer::SetNodeFlags(nlohmann::json &nodeJson, uint32_t flags)
    {
        if (flags)
            nodeJson["component_flags"] = flags;
        else
            nodeJson.erase("component_flags");
    }

    std::string PrefabViewer::SerializePrefabRelativePath(const std::filesystem::path &path) const
    {
        if (m_prefabPath.empty())
            return path.generic_string();

        std::error_code ec;
        const std::filesystem::path relative = std::filesystem::relative(path, m_prefabPath.parent_path(), ec);
        if (!ec && IsPortableRelativePath(relative))
            return relative.generic_string();

        return path.generic_string();
    }
} // namespace pe
