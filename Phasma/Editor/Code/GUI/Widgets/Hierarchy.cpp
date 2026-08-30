#include "Hierarchy.h"
#include "Camera/Camera.h"
#include "FileBrowser.h"
#include "FileSelector.h"
#include "GUI/GUI.h"
#include "GUI/GUIState.h"
#include "GUI/Helpers.h"
#include "GUI/HierarchyPayload.h"
#include "GUI/RuntimeUiAuthoring.h"
#include "GUI/SpriteAuthoring.h"
#include "GUI/IconsFontAwesome.h"
#include "GUI/UndoRedo.h"
#include "Particles/ParticleManager.h"
#include "Scene/ModelAsset.h"
#include "Scene/Primitives.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneHost.h"
#include "Scene/SceneNode.h"
#include "Scene/SelectionManager.h"
#include "Script/ScriptSystem.h"
#include "Script/Bindings/Lerp/Tween.h"
#include "ScriptEditor.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"

namespace pe
{
    // Unity-style colors
    namespace HierarchyStyle
    {
        // Unity dark theme colors
        const ImVec4 WindowBg = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
        const ImVec4 HeaderBg = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
        const ImVec4 HeaderHovered = ImVec4(0.17f, 0.36f, 0.53f, 1.0f);
        const ImVec4 HeaderActive = ImVec4(0.26f, 0.59f, 0.98f, 0.8f);
        const ImVec4 SelectionBg = ImVec4(0.17f, 0.36f, 0.53f, 1.0f);
        const ImVec4 SelectionBgUnfocused = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
        const ImVec4 TextNormal = ImVec4(0.86f, 0.86f, 0.86f, 1.0f);
        const ImVec4 TextDisabled = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);
        const ImVec4 TreeLineBg = ImVec4(0.35f, 0.35f, 0.35f, 0.5f);
    } // namespace HierarchyStyle

    namespace
    {
        // Built-in primitives for the Hierarchy "Add -> Mesh" submenus, sorted by display name. One table
        // drives all four add menus (toolbar Add, root right-click, node "Add", root "Add") so they stay
        // in sync. Each Create* is wrapped in a captureless lambda because several take defaulted params.
        struct AddMeshItem
        {
            const char *name;
            ModelAsset *(*make)();
            const char *tip;
        };
        constexpr AddMeshItem kAddMeshPrimitives[] = {
            {"Circle", []
             { return Primitives::CreateCircle(); }, "Circle primitive mesh."},
            {"Cone", []
             { return Primitives::CreateCone(); }, "Cone primitive mesh."},
            {"Cube", []
             { return Primitives::CreateCube(); }, "Cube primitive mesh."},
            {"Cylinder", []
             { return Primitives::CreateCylinder(); }, "Cylinder primitive mesh."},
            {"Grid", []
             { return Primitives::CreateGrid(); }, "Subdivided grid primitive mesh."},
            {"Ico Sphere", []
             { return Primitives::CreateIcoSphere(); }, "Ico-sphere primitive mesh."},
            {"Plane", []
             { return Primitives::CreatePlane(); }, "Flat plane primitive mesh."},
            {"Pyramid", []
             { return Primitives::CreatePyramid(); }, "Pyramid primitive mesh."},
            {"Quad", []
             { return Primitives::CreateQuad(); }, "Quad primitive mesh."},
            {"Skinned Strip 2D", []
             { return Primitives::CreateSkinnedStrip2D(); }, "GPU-skinned 2D strip primitive mesh."},
            {"Sphere", []
             { return Primitives::CreateSphere(); }, "Sphere primitive mesh."},
            {"Torus", []
             { return Primitives::CreateTorus(); }, "Torus primitive mesh."},
            {"UV Sphere", []
             { return Primitives::CreateUvSphere(); }, "UV sphere primitive mesh."},
        };
    } // namespace

    static bool ContainsSpriteMarker(const std::string &value)
    {
        return value.find("sprite") != std::string::npos || value.find("atlas") != std::string::npos;
    }

    static bool IsSpriteHierarchyNode(Scene &scene, NodeId *node, const std::string &nodeName, uint32_t componentFlags)
    {
        if (componentFlags & Component_Sprite)
            return true;

        if (ContainsSpriteMarker(ToLower(nodeName)))
            return true;

        if ((componentFlags & Component_Script) && ContainsSpriteMarker(ToLower(scene.GetNodeScriptPath(node))))
            return true;

        return scene.NodeUsesSkinnedStrip2D(node);
    }

    static bool DrawCenteredIconButton(const char *id, const char *icon, const ImVec2 &size)
    {
        const bool clicked = ImGui::InvisibleButton(id, size);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImGuiStyle &style = ImGui::GetStyle();

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImU32 buttonColor = ImGui::GetColorU32(active    ? ImGuiCol_ButtonActive
                                                     : hovered ? ImGuiCol_ButtonHovered
                                                               : ImGuiCol_Button);
        drawList->AddRectFilled(min, max, buttonColor, style.FrameRounding);

        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        const ImVec2 iconPos(min.x + (size.x - iconSize.x) * 0.5f,
                             min.y + (size.y - iconSize.y) * 0.5f - 1.0f);
        drawList->AddText(iconPos, ImGui::GetColorU32(ImGuiCol_Text), icon);
        return clicked;
    }

    Hierarchy::Hierarchy() : Widget("Hierarchy")
    {
    }

    Hierarchy::~Hierarchy()
    {
    }

    void Hierarchy::Init(GUI *gui)
    {
        Widget::Init(gui);
    }

    void Hierarchy::Update()
    {
        if (!m_open)
            return;

        bool useUnityStyle = GUIState::s_guiStyle == GUIStyle::Unity;
        int styleColorCount = 0;
        int styleVarCount = 0;

        // Apply Unity-style overrides only when Unity style is selected
        if (useUnityStyle)
        {
            ImGui::PushStyleColor(ImGuiCol_WindowBg, HierarchyStyle::WindowBg);
            ImGui::PushStyleColor(ImGuiCol_Header, HierarchyStyle::SelectionBg);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, HierarchyStyle::HeaderHovered);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, HierarchyStyle::HeaderActive);
            ImGui::PushStyleColor(ImGuiCol_Text, HierarchyStyle::TextNormal);
            styleColorCount = 5;

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 14.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
            styleVarCount = 3;
        }

        ImGui::Begin("Hierarchy", &m_open);
        GUIState::s_hierarchyFocused = ImGui::IsWindowFocused();

        Scene &scene = *GetActiveScene();
        auto &selection = SelectionManager::Instance();

        auto recordSnapshot = [&scene](const char *label)
        { UndoRedo::Instance().RecordSnapshot(scene, label); };
        auto recordUndo = [&recordSnapshot]()
        { recordSnapshot("Deleted Node"); };

        auto instantiatePrefab = [&](const std::filesystem::path &path, NodeId *parent)
        {
            recordSnapshot("Instantiated Prefab");
            SyncSceneBeforeMutation();
            SceneNodeHandle handle = scene.InstantiatePrefab(path, parent);
            if (handle.nodeId && scene.IsNodeAlive(handle.nodeId))
            {
                selection.Select(handle.nodeId, SelectionType::Node);
                ImGui::SetWindowFocus("Properties");
                if (m_gui)
                    m_gui->NotifyChange();
            }
        };

        auto createSkybox = [&scene, &selection, &recordSnapshot]()
        {
            if (NodeId *existing = scene.GetSkyboxNode())
            {
                selection.Select(existing, SelectionType::Node);
                return;
            }

            recordSnapshot("Added Skybox");
            NodeId *node = scene.CreateSkyboxNode();
            selection.Select(node, SelectionType::Node);
        };

        auto createTriggerZone = [&scene, &selection, &recordSnapshot]()
        {
            recordSnapshot("Added Trigger Zone");
            NodeId *node = scene.CreateTriggerZoneNode();
            selection.Select(node, SelectionType::Node);
        };

        auto createVoxelWorld = [&scene, &selection, &recordSnapshot]()
        {
            if (NodeId *existing = scene.GetVoxelWorldNode())
            {
                selection.Select(existing, SelectionType::Node);
                return;
            }
            recordSnapshot("Added Voxel World");
            NodeId *node = scene.CreateVoxelWorldNode();
            selection.Select(node, SelectionType::Node);
        };

        auto createTerrain = [&scene, &selection, &recordSnapshot]()
        {
            if (NodeId *existing = scene.GetTerrainNode())
            {
                selection.Select(existing, SelectionType::Node);
                return;
            }
            recordSnapshot("Added Terrain");
            NodeId *node = scene.CreateTerrainNode();
            selection.Select(node, SelectionType::Node);
        };

        auto createSceneSettings = [&scene, &selection, &recordSnapshot]()
        {
            if (NodeId *existing = scene.GetSceneSettingsNode())
            {
                selection.Select(existing, SelectionType::Node);
                return;
            }
            recordSnapshot("Added Scene Settings");
            scene.EnsureSceneSettingsNodeFromSettings(true);
            if (NodeId *node = scene.GetSceneSettingsNode())
                selection.Select(node, SelectionType::Node);
        };

        auto selectedSpriteAsset = []() -> std::filesystem::path
        {
            const AssetPreviewState &preview = GUIState::s_assetPreview;
            if ((preview.type == AssetPreviewType::Image || preview.type == AssetPreviewType::Sprite) && !preview.fullPath.empty())
                return std::filesystem::path(preview.fullPath);
            return {};
        };

        auto createSprite = [&](NodeId *parent, const std::filesystem::path &assetPath = std::filesystem::path())
        {
            recordSnapshot("Added Sprite");
            SyncSceneBeforeMutation();

            SpriteAuthoring::Options options;
            options.assetPath = assetPath;
            SpriteAuthoring::Result result = SpriteAuthoring::CreateNode(scene, options, parent);
            if (!result.error.empty())
            {
                PE_WARN("[Sprite] Failed to create sprite: %s", result.error.c_str());
                return;
            }

            selection.Select(result.node, SelectionType::Node);
            ImGui::SetWindowFocus("Properties");
            if (m_gui)
                m_gui->NotifyChange();
        };

        auto createSpriteFromSelection = [&](NodeId *parent)
        {
            createSprite(parent, selectedSpriteAsset());
        };

        auto createRuntimeUiElement = [&](NodeRuntimeUiWidgetType type, NodeId *parent)
        {
            recordSnapshot("Added UI Element");
            SyncSceneBeforeMutation();
            NodeId *node = RuntimeUiAuthoring::CreateNode(scene, type, vec2(48.0f, 48.0f), parent);
            scene.MarkNodeDirty(node);
            selection.Select(node, SelectionType::Node);
            ImGui::SetWindowFocus("Properties");
            if (m_gui)
                m_gui->NotifyChange();
        };

        auto acceptRuntimeUiDrop = [&](NodeId *parent) -> bool
        {
            const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(RuntimeUiAuthoring::kDragPayloadType);
            if (!payload || payload->DataSize != sizeof(RuntimeUiAuthoring::DragPayload))
                return false;

            const auto data = *static_cast<const RuntimeUiAuthoring::DragPayload *>(payload->Data);
            createRuntimeUiElement(data.type, parent);
            return true;
        };

        auto drawRuntimeUiCreateMenu = [&](NodeId *parent)
        {
            const bool menuOpen = ImGui::BeginMenu("UI");
            ui::ItemTooltip("Create a Runtime UI node.");
            if (!menuOpen)
                return;

            for (const RuntimeUiAuthoring::ElementTemplate &element : RuntimeUiAuthoring::Templates())
            {
                if (ImGui::MenuItem(element.name))
                    createRuntimeUiElement(element.type, parent);
                ui::ItemTooltip(element.tooltip);
            }
            ImGui::EndMenu();
        };

        // Single source of truth for the "Add" entity menu — drives the toolbar Add button, the root
        // right-click, the background right-click, and each node's "Add" submenu, so they can never drift.
        // parent==nullptr creates at the scene root; a non-null parent creates a child and enables the
        // node-only items (Lua Script, Mesh component) while hiding the one-per-scene singletons.
        auto drawAddEntityItems = [&](NodeId *parent)
        {
            const uint32_t componentFlags = parent ? scene.GetComponentFlags(parent) : 0u;

            if (ImGui::MenuItem("Camera"))
            {
                recordSnapshot("Added Camera");
                scene.AddCamera(parent);
            }
            ui::ItemTooltip(parent ? "Add a child camera node." : "Create a camera node.");

            if (ImGui::MenuItem("Empty Node"))
            {
                recordSnapshot("Added Node");
                NodeId *node = scene.CreateNode("Empty Node", parent);
                if (parent)
                    scene.MarkNodeDirty(node);
                selection.Select(node, SelectionType::Node);
            }
            ui::ItemTooltip(parent ? "Add an empty child transform node." : "Create an empty transform node.");

            const bool lightMenuOpen = ImGui::BeginMenu("Light");
            ui::ItemTooltip(parent ? "Add a child light node." : "Create a light node.");
            if (lightMenuOpen)
            {
                if (ImGui::MenuItem("Area Light"))
                {
                    recordSnapshot("Added Area Light");
                    scene.CreateAreaLight(parent);
                }
                ui::ItemTooltip("Create a rectangular area light.");
                if (ImGui::MenuItem("Directional Light"))
                {
                    recordSnapshot("Added Directional Light");
                    scene.CreateDirectionalLight(parent);
                }
                ui::ItemTooltip("Create a sun-like directional light.");
                if (ImGui::MenuItem("Point Light"))
                {
                    recordSnapshot("Added Point Light");
                    scene.CreatePointLight(parent);
                }
                ui::ItemTooltip("Create an omnidirectional point light.");
                if (ImGui::MenuItem("Spot Light"))
                {
                    recordSnapshot("Added Spot Light");
                    scene.CreateSpotLight(parent);
                }
                ui::ItemTooltip("Create a cone-shaped spot light.");
                ImGui::EndMenu();
            }

            // Lua Script — node-only (attaches a script component to the parent node).
            if (parent && !(componentFlags & Component_Script))
            {
                const bool scriptMenuOpen = ImGui::BeginMenu("Lua Script");
                ui::ItemTooltip("Attach a Lua script component to this node.");
                if (scriptMenuOpen)
                {
                    if (ImGui::MenuItem("Browse Existing..."))
                    {
                        if (auto *fs = m_gui->GetWidget<FileSelector>())
                        {
                            fs->OpenSelection([parent](const std::string &path) -> bool
                                              {
                                if (auto *r = GetGlobalSystem<RendererSystem>())
                                    r->GetScene().SetNodeScript(parent, path);
                                return true; },
                                              {".lua"});
                        }
                    }
                    ui::ItemTooltip("Choose an existing Lua script asset.");
                    if (ImGui::MenuItem("New Empty Script"))
                    {
                        if (auto *se = m_gui->GetWidget<ScriptEditor>())
                            se->OpenNewScript(parent);
                    }
                    ui::ItemTooltip("Create a new Lua script and attach it to this node.");
                    ImGui::EndMenu();
                }
            }

            // Mesh — root adds a standalone model; a child attaches a primitive component (node-only gating).
            if (!parent || !(componentFlags & Component_Mesh))
            {
                const bool meshMenuOpen = ImGui::BeginMenu("Mesh");
                ui::ItemTooltip(parent ? "Attach a primitive mesh component to this node."
                                       : "Create a built-in primitive mesh.");
                if (meshMenuOpen)
                {
                    for (const auto &p : kAddMeshPrimitives)
                    {
                        if (ImGui::MenuItem(p.name))
                        {
                            if (parent)
                            {
                                recordSnapshot("Added Mesh Component");
                                EventSystem::PushEvent(EventType::PrimitiveAttachedToNode,
                                                       Scene::PrimitiveAttachRequest{parent, p.make()});
                            }
                            else
                            {
                                recordSnapshot("Added Mesh");
                                EventSystem::PushEvent(EventType::ModelLoaded, p.make());
                            }
                        }
                        ui::ItemTooltip(p.tip);
                    }
                    ImGui::EndMenu();
                }
            }

            // Particle Emitter — spawns near the active camera (not parented); available in every scope.
            if (ImGui::MenuItem("Particle Emitter"))
            {
                if (ParticleManager *pm = scene.GetParticleManager())
                {
                    recordSnapshot("Added Particle Emitter");
                    auto &emitters = pm->GetEmitters();
                    auto &names = pm->GetEmitterNames();
                    Camera *camera = scene.GetActiveCamera();
                    vec3 spawnPos = camera ? (camera->GetPosition() + camera->GetFront() * 5.0f) : vec3(0.0f);

                    ParticleEmitter newEmitter{};
                    newEmitter.position = vec4(spawnPos, 1.0f);
                    newEmitter.velocity = vec4(0.0f, 5.0f, 0.0f, 0.0f);
                    newEmitter.colorStart = vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    newEmitter.colorEnd = vec4(0.0f, 0.0f, 0.0f, 0.0f);
                    newEmitter.sizeLife = vec4(0.05f, 0.15f, 1.0f, 2.0f);
                    newEmitter.physics = vec4(50.0f, 0.5f, 1.0f, 0.1f);
                    newEmitter.gravity = vec4(0.0f, -9.8f, 0.0f, 0.0f);
                    newEmitter.animation = vec4(1.0f, 1.0f, 1.0f, 0.0f);
                    newEmitter.textureIndex = 0;
                    newEmitter.count = 100;

                    emitters.push_back(newEmitter);
                    names.push_back("Emitter " + std::to_string(emitters.size() - 1));
                    pm->UpdateEmitterBuffer();
                    selection.SelectEmitter(static_cast<int>(emitters.size() - 1));
                }
            }
            ui::ItemTooltip("Create a particle emitter near the active camera.");

            // One-per-scene singletons — root scope only.
            if (!parent)
            {
                if (!scene.GetSceneSettingsNode() && ImGui::MenuItem("Scene Settings"))
                    createSceneSettings();
                if (!scene.GetSceneSettingsNode())
                    ui::ItemTooltip("Add the scene settings node (global render + default post-process). One per scene.");

                if (!scene.GetSkyboxNode() && ImGui::MenuItem("Skybox"))
                    createSkybox();
                if (!scene.GetSkyboxNode())
                    ui::ItemTooltip("Create the scene skybox node.");
            }

            if (ImGui::MenuItem("Sprite"))
                createSpriteFromSelection(parent);
            ui::ItemTooltip(parent ? "Add a child Component_Sprite node from the selected sprite asset when available."
                                   : "Create a Component_Sprite node from the selected image or sprite metadata when available.");

            if (!parent)
            {
                if (!scene.GetTerrainNode() && ImGui::MenuItem("Terrain"))
                    createTerrain();
                if (!scene.GetTerrainNode())
                    ui::ItemTooltip("Create the heightfield terrain node (surface from a heightmap or noise). One per scene.");

                if (ImGui::MenuItem("Trigger Zone"))
                    createTriggerZone();
                ui::ItemTooltip("Create a Trigger Zone (script on_enter/on_exit + post-process + audio sections).");
            }

            drawRuntimeUiCreateMenu(parent);

            if (!parent)
            {
                if (!scene.GetVoxelWorldNode() && ImGui::MenuItem("Voxel World"))
                    createVoxelWorld();
                if (!scene.GetVoxelWorldNode())
                    ui::ItemTooltip("Create the voxel world node — all voxel settings live on it. One per scene.");
            }
        };

        auto makePrefabFileName = [](std::string name)
        {
            if (name.empty())
                name = "Prefab";

            for (char &c : name)
            {
                switch (c)
                {
                case '<':
                case '>':
                case ':':
                case '"':
                case '/':
                case '\\':
                case '|':
                case '?':
                case '*':
                    c = '_';
                    break;
                default:
                    break;
                }
            }

            return name + ".peprefab";
        };

        auto openCreatePrefabDialog = [&](NodeId *root)
        {
            if (!root || !m_gui)
                return;

            auto *fs = m_gui->GetWidget<FileSelector>();
            if (!fs)
                return;

            std::filesystem::path defaultPath = std::filesystem::path(Path::Assets) / "Prefabs";
            std::error_code ec;
            std::filesystem::create_directories(defaultPath, ec);

            const SceneNodeHandle rootHandle = scene.MakeHandle(root);
            const std::string defaultName = makePrefabFileName(scene.GetNodeName(root));
            fs->OpenSelection([rootHandle, this](const std::string &path) -> bool
                              {
                                  Scene *activeScene = GetActiveScene();
                                  if (!activeScene || !rootHandle.IsValid(*activeScene))
                                      return true;

                                  std::filesystem::path prefabPath(path);
                                  if (prefabPath.extension() != ".peprefab")
                                      prefabPath += ".peprefab";

                                  UndoRedo::Instance().RecordSnapshot(*activeScene, "Created Prefab");
                                  const bool saved = activeScene->SavePrefab(rootHandle.nodeId, prefabPath);
                                  if (saved)
                                  {
                                      if (auto *browser = m_gui ? m_gui->GetWidget<FileBrowser>() : nullptr)
                                          browser->RefreshCache();
                                      if (m_gui)
                                          m_gui->NotifyChange();
                                  }
                                  return saved; },
                              {".peprefab"},
                              defaultPath.string(),
                              {},
                              defaultName,
                              "Save");
        };

        // Add Button
        float buttonWidth = ImGui::GetContentRegionAvail().x * 0.8f;
        float x = (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
        if (ImGui::Button("Add", ImVec2(buttonWidth, 0.f)))
            ImGui::OpenPopup("AddEntityPopup");
        ui::ItemTooltip("Open the scene-object creation menu.");

        // Scene name with new scene/settings buttons
        {
            std::string sceneName = scene.GetSceneName();
            if (scene.IsDirty())
                sceneName += " *";

            std::string label = std::string(ICON_FA_SITEMAP) + "  " + sceneName;
            const float btnSize = ImGui::GetFrameHeight();
            if (ImGui::BeginTable("##SceneHeader", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableSetupColumn("new", ImGuiTableColumnFlags_WidthFixed, btnSize);
                ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("settings", ImGuiTableColumnFlags_WidthFixed, btnSize);
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                if (ImGui::Button(ICON_FA_PLUS, ImVec2(btnSize, btnSize)))
                    m_gui->NewScene();
                ui::ItemTooltip("Create a new empty scene.");

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.7f, 0.8f, 1.0f));
                ImGui::TextUnformatted(label.c_str());
                ImGui::PopStyleColor();

                ImGui::TableNextColumn();
                if (DrawCenteredIconButton("##SceneSettings", ICON_FA_GEAR, ImVec2(btnSize, btnSize)))
                    createSceneSettings();
                ui::ItemTooltip("Open scene settings.");

                ImGui::EndTable();
            }
        }

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        if (ImGui::BeginPopup("AddEntityPopup"))
        {
            drawAddEntityItems(nullptr);
            ImGui::EndPopup();
        }

        static NodeId *s_renameNode = nullptr;
        static int s_renameEmitterIndex = -1;
        static char s_renameBuf[128] = "";
        static bool s_openRenamePopup = false;
        std::vector<NodeId *> nodesToDelete;

        // Check for selection change (auto-expand)
        NodeId *currentSelectedNode = selection.GetSelectedNode();
        if (m_lastSelectedNode && !scene.IsNodeAlive(m_lastSelectedNode))
            m_lastSelectedNode = nullptr;
        if (m_nodeToExpand && !scene.IsNodeAlive(m_nodeToExpand))
        {
            m_nodeToExpand = nullptr;
            m_nodesToExpand.clear();
            m_scrollToSelection = false;
        }
        if (currentSelectedNode != m_lastSelectedNode)
        {
            m_lastSelectedNode = currentSelectedNode;

            if (currentSelectedNode)
            {
                m_nodeToExpand = currentSelectedNode;
                m_nodesToExpand.clear();
                m_scrollToSelection = true;

                // Trace parents up to root
                NodeId *p = scene.GetParent(currentSelectedNode);
                while (p)
                {
                    m_nodesToExpand.insert(p);
                    p = scene.GetParent(p);
                }
            }
        }

        // Root Node - all scene objects live under here
        ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen |
                                       ImGuiTreeNodeFlags_SpanAvailWidth |
                                       ImGuiTreeNodeFlags_FramePadding |
                                       ImGuiTreeNodeFlags_OpenOnArrow;
        bool rootOpen = ImGui::TreeNodeEx("##RootNode", rootFlags, "%s  Root Node", ICON_FA_CUBE);
        const bool rootHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);

        if (ImGui::BeginPopupContextItem("RootNodeContext"))
        {
            drawAddEntityItems(nullptr);
            ImGui::EndPopup();
        }
        if (rootHovered)
            ui::TooltipText("Root of the scene hierarchy; right-click to create root-level objects.");

        if (ImGui::BeginDragDropTarget())
        {
            const bool acceptedRuntimeUi = acceptRuntimeUiDrop(nullptr);
            if (!acceptedRuntimeUi)
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    const char *pathStr = (const char *)payload->Data;
                    std::filesystem::path path(pathStr);
                    if (FileBrowser::IsPrefabFile(path))
                        instantiatePrefab(path, nullptr);
                    else if (SpriteAuthoring::IsSpriteAssetPath(path))
                        createSprite(nullptr, path);
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (rootOpen)
        {

            // Particle Emitters
            {
                ParticleManager *pm = scene.GetParticleManager();
                if (pm)
                {
                    auto &emitters = pm->GetEmitters();
                    auto &names = pm->GetEmitterNames();

                    // Ensure names vector matches emitters size
                    while (names.size() < emitters.size())
                        names.push_back("Emitter " + std::to_string(names.size()));

                    bool emittersOpen = false;
                    bool emittersHovered = false;
                    if (!emitters.empty())
                    {
                        emittersOpen = ImGui::TreeNodeEx("Particle Emitters", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding);
                        emittersHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
                    }
                    if (emittersOpen)
                    {
                        bool deleteAllEmitters = false;
                        if (ImGui::BeginPopupContextItem("EmittersContextMenu"))
                        {
                            if (ImGui::MenuItem("Delete All Emitters"))
                            {
                                recordUndo();
                                deleteAllEmitters = true;
                            }
                            ui::ItemTooltip("Remove every particle emitter from the scene.");
                            ImGui::EndPopup();
                        }
                        if (emittersHovered)
                            ui::TooltipText("Group containing all scene particle emitters.");

                        int emitterToDelete = -1;

                        if (!deleteAllEmitters)
                            for (int i = 0; i < static_cast<int>(emitters.size()); i++)
                            {
                                ImGuiTreeNodeFlags emitterFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                                                  ImGuiTreeNodeFlags_Leaf |
                                                                  ImGuiTreeNodeFlags_FramePadding;

                                if (selection.GetSelectionType() == SelectionType::Emitter &&
                                    selection.GetSelectedEmitterIndex() == i)
                                    emitterFlags |= ImGuiTreeNodeFlags_Selected;

                                std::string emitterName = (i < static_cast<int>(names.size())) ? names[i] : ("Emitter " + std::to_string(i));

                                bool emitterOpen = ImGui::TreeNodeEx((void *)(intptr_t)(i + 5000), emitterFlags, "%s  %s", ICON_FA_FIRE, emitterName.c_str());
                                const bool emitterHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);

                                if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
                                {
                                    selection.SelectEmitter(i);
                                    ImGui::SetWindowFocus("Properties");
                                }

                                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                                {
                                    Camera *camera = scene.GetActiveCamera();
                                    vec3 emitterPos = vec3(emitters[i].position);
                                    TweenCameraTo(emitterPos - camera->GetFront() * 5.0f, 0.3f);
                                }

                                if (ImGui::BeginPopupContextItem())
                                {
                                    if (ImGui::MenuItem("Focus"))
                                    {
                                        selection.SelectEmitter(i);
                                        Camera *camera = scene.GetActiveCamera();
                                        vec3 emitterPos = vec3(emitters[i].position);
                                        TweenCameraTo(emitterPos - camera->GetFront() * 5.0f, 0.3f);
                                        ImGui::SetWindowFocus("Properties");
                                    }
                                    ui::ItemTooltip("Move the active camera to frame this emitter.");
                                    if (ImGui::MenuItem("Rename"))
                                    {
                                        s_renameNode = nullptr;
                                        s_renameEmitterIndex = i;
                                        snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", emitterName.c_str());
                                        s_openRenamePopup = true;
                                    }
                                    ui::ItemTooltip("Rename this particle emitter.");
                                    if (ImGui::MenuItem("Delete"))
                                    {
                                        recordUndo();
                                        emitterToDelete = i;
                                    }
                                    ui::ItemTooltip("Delete this particle emitter.");
                                    ImGui::EndPopup();
                                }
                                if (emitterHovered)
                                    ui::TooltipText("Select this particle emitter; double-click to focus the camera.");

                                if (emitterOpen)
                                    ImGui::TreePop();
                            }

                        if (deleteAllEmitters)
                        {
                            emitters.clear();
                            names.clear();
                            pm->UpdateEmitterBuffer();
                            if (selection.GetSelectionType() == SelectionType::Emitter)
                                selection.ClearSelection();
                        }
                        else if (emitterToDelete >= 0)
                        {
                            emitters.erase(emitters.begin() + emitterToDelete);
                            if (emitterToDelete < static_cast<int>(names.size()))
                                names.erase(names.begin() + emitterToDelete);
                            pm->UpdateEmitterBuffer();
                            if (selection.GetSelectionType() == SelectionType::Emitter)
                                selection.ClearSelection();
                        }

                        ImGui::TreePop();
                    }
                    else if (emittersHovered)
                    {
                        ui::TooltipText("Group containing all scene particle emitters.");
                    }
                }
            }

            // --- Scene Nodes ---
            // Recursive draw node
            auto DrawNode = [&](auto &&self, NodeId *node) -> void
            {
                const std::string &nodeName = scene.GetNodeName(node);
                auto children = scene.GetChildren(node);
                bool hasChildren = !children.empty();

                // Auto-expand parents
                if (m_nodesToExpand.find(node) != m_nodesToExpand.end())
                {
                    ImGui::SetNextItemOpen(true);
                }

                bool isLeaf = !hasChildren;

                uintptr_t uniqueId = reinterpret_cast<uintptr_t>(node);

                // Choose icon based on component flags
                uint32_t nodeCompFlags = scene.GetComponentFlags(node);
                const bool spriteHierarchyNode = IsSpriteHierarchyNode(scene, node, nodeName, nodeCompFlags);
                const char *icon;
                if (nodeCompFlags & Component_Prefab)
                    icon = ICON_FA_CUBES;
                else if (nodeCompFlags & Component_Camera)
                    icon = ICON_FA_VIDEO;
                else if (nodeCompFlags & Component_Light)
                    icon = ICON_FA_LIGHTBULB;
                else if (nodeCompFlags & Component_Skybox)
                    icon = ICON_FA_SUN;
                else if (nodeCompFlags & Component_TriggerZone)
                    icon = ICON_FA_BELL;
                else if (nodeCompFlags & Component_SceneSettings)
                    icon = ICON_FA_GEAR;
                else if (nodeCompFlags & Component_VoxelWorld)
                    icon = ICON_FA_BORDER_ALL;
                else if (nodeCompFlags & Component_RuntimeUi)
                    icon = ICON_FA_WINDOW_MAXIMIZE;
                else if (spriteHierarchyNode)
                    icon = ICON_FA_IMAGE;
                else
                    icon = ICON_FA_VECTOR_SQUARE;

                std::string displayName = nodeName;
                if (nodeCompFlags & Component_Camera)
                {
                    Camera *thisCam = scene.GetCameraForNode(node);
                    if (thisCam && thisCam == scene.GetActiveCamera())
                        displayName += " (Main)";
                }

                // Look up script error for this node
                std::string scriptError;
                if (nodeCompFlags & Component_Script)
                {
                    if (auto *ss = GetGlobalSystem<ScriptSystem>())
                        if (auto *inst = ss->FindNodeInstance(node))
                            scriptError = inst->lastError;
                }

                std::string displayNodeName = std::string(icon) + "  " + displayName;
                if (!scriptError.empty())
                    displayNodeName += "  " ICON_FA_TRIANGLE_EXCLAMATION;
                const bool nodeEnabled = scene.IsNodeEnabled(node);
                const bool hierarchyEnabled = scene.IsNodeHierarchyEnabled(node);

                ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                               ImGuiTreeNodeFlags_OpenOnArrow |
                                               ImGuiTreeNodeFlags_FramePadding;
                if (isLeaf)
                    nodeFlags |= ImGuiTreeNodeFlags_Leaf;

                // Highlight if selected
                if (selection.GetSelectedNode() == node && selection.GetSelectionType() == SelectionType::Node)
                {
                    nodeFlags |= ImGuiTreeNodeFlags_Selected;
                    if (m_scrollToSelection && m_nodeToExpand == node)
                    {
                        m_scrollToSelection = false;
                        m_nodeToExpand = nullptr;
                        m_nodesToExpand.clear();
                    }
                }

                ImGui::PushID(node);
                bool enabledEdit = nodeEnabled;
                if (ImGui::Checkbox("##Enabled", &enabledEdit))
                {
                    recordSnapshot(enabledEdit ? "Enabled Node" : "Disabled Node");
                    scene.SetNodeEnabled(node, enabledEdit);
                }
                ui::ItemTooltip(enabledEdit ? "Disable this node and its subtree for rendering." : "Enable this node for rendering.");
                ImGui::SameLine(0.0f, 4.0f);

                if (!hierarchyEnabled)
                    ImGui::PushStyleColor(ImGuiCol_Text, HierarchyStyle::TextDisabled);
                bool nodeOpen = ImGui::TreeNodeEx((void *)uniqueId, nodeFlags, "%s", displayNodeName.c_str());
                const bool nodeHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
                if (!hierarchyEnabled)
                    ImGui::PopStyleColor();
                ImGui::PopID();

                // Drag & Drop Source — SourceAllowNullID lets the drag initiate
                // from the tree node label even when OpenOnArrow is set
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    HierarchyDragDropPayload payload;
                    payload.node = node;
                    ImGui::SetDragDropPayload("HIERARCHY_NODE", &payload, sizeof(HierarchyDragDropPayload));
                    ImGui::Text("%s", nodeName.c_str());
                    ImGui::EndDragDropSource();
                }

                if ((ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) && !ImGui::IsItemToggledOpen())
                    selection.Select(node, SelectionType::Node);

                // Focus Properties on release only — calling SetWindowFocus on press clears
                // g.ActiveId via FocusWindow→ClearActiveID, which breaks BeginDragDropSource.
                if (ImGui::IsItemHovered() &&
                    ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                    !ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
                    !ImGui::IsItemToggledOpen())
                    ImGui::SetWindowFocus("Properties");

                // Drag & Drop Target
                if (ImGui::BeginDragDropTarget())
                {
                    const bool acceptedRuntimeUi = acceptRuntimeUiDrop(node);
                    if (!acceptedRuntimeUi)
                    {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("HIERARCHY_NODE"))
                        {
                            HierarchyDragDropPayload data = *(const HierarchyDragDropPayload *)payload->Data;
                            if (scene.IsNodeAlive(data.node) && data.node != node && scene.GetParent(data.node) != node)
                            {
                                recordSnapshot("Reparented Node");
                                scene.ReparentNode(data.node, node);
                            }
                        }

                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                        {
                            const char *pathStr = (const char *)payload->Data;
                            std::filesystem::path path(pathStr);

                            std::string ext = path.extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                            // Only cooked .pemesh loads into the scene; source models are import-only (File > Import).
                            bool isModel = FileBrowser::IsCookedModelFile(path);

                            if (FileBrowser::IsPrefabFile(path))
                            {
                                instantiatePrefab(path, node);
                            }
                            else if (SpriteAuthoring::IsSpriteAssetPath(path))
                            {
                                createSprite(node, path);
                            }
                            else if (isModel && !GUIState::s_modelLoading)
                            {
                                auto loadTask = [path, node]()
                                {
                                    GUIState::s_modelLoading = true;
                                    try
                                    {
                                        if (ModelAsset *m = ModelAsset::Load(path))
                                            EventSystem::PushEvent(EventType::ModelLoadedForNode,
                                                                   Scene::ModelLoadForNodeRequest{node, m});
                                    }
                                    catch (const std::exception &e)
                                    {
                                        PE_WARN("[Scene] Failed to load model: %s", e.what());
                                    }
                                    GUIState::s_modelLoading = false;
                                };
                                ThreadPool::GUI.Enqueue(loadTask);
                            }

                            if (ext == ".lua")
                                scene.SetNodeScript(node, path.string());
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (!(nodeCompFlags & Component_Skybox) && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                {
                    Camera *camera = scene.GetActiveCamera();
                    const AABB &bounds = scene.GetWorldAABB(node);
                    vec3 center = (bounds.min + bounds.max) * 0.5f;
                    float dist = glm::length(bounds.max - bounds.min);
                    vec3 dir = camera->GetFront();
                    TweenCameraTo(center - dir * glm::max(dist, camera->GetNearPlane()), 0.3f);
                }

                if (ImGui::BeginPopupContextItem())
                {
                    if (!(nodeCompFlags & Component_Skybox) && ImGui::MenuItem("Focus"))
                    {
                        selection.Select(node, SelectionType::Node);
                        Camera *camera = scene.GetActiveCamera();
                        const AABB &bounds = scene.GetWorldAABB(node);
                        vec3 center = (bounds.min + bounds.max) * 0.5f;
                        float dist = glm::length(bounds.max - bounds.min);
                        vec3 dir = camera->GetFront();
                        TweenCameraTo(center - dir * glm::max(dist, camera->GetNearPlane()), 0.3f);
                        ImGui::SetWindowFocus("Properties");
                    }
                    if (!(nodeCompFlags & Component_Skybox))
                        ui::ItemTooltip("Move the active camera to frame this node.");
                    if (nodeCompFlags & Component_Camera)
                    {
                        Camera *thisCam = scene.GetCameraForNode(node);
                        if (thisCam && thisCam != scene.GetActiveCamera() && ImGui::MenuItem("Set as Active Camera"))
                            scene.SetActiveCamera(thisCam);
                        if (thisCam && thisCam != scene.GetActiveCamera())
                            ui::ItemTooltip("Use this camera for viewport rendering.");
                    }
                    if (ImGui::MenuItem("Rename"))
                    {
                        s_renameNode = node;
                        s_renameEmitterIndex = -1;
                        snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", nodeName.c_str());
                        s_openRenamePopup = true;
                    }
                    ui::ItemTooltip("Rename this node.");

                    if (ImGui::MenuItem("Create Prefab..."))
                        openCreatePrefabDialog(node);
                    ui::ItemTooltip("Save this node and all descendants as a .peprefab asset.");

                    const bool addMenuOpen = ImGui::BeginMenu("Add");
                    ui::ItemTooltip("Add a child object or component under this node.");
                    if (addMenuOpen)
                    {
                        drawAddEntityItems(node);
                        ImGui::EndMenu();
                    }

                    if (ImGui::MenuItem("Delete"))
                    {
                        bool canDelete = true;
                        if (nodeCompFlags & Component_Camera)
                        {
                            Camera *cam = scene.GetCameraForNode(node);
                            canDelete = cam && scene.GetCameras().size() > 1;
                        }
                        if (canDelete)
                        {
                            recordUndo();
                            nodesToDelete.push_back(node);
                        }
                    }
                    ui::ItemTooltip("Delete this node when allowed.");
                    ImGui::EndPopup();
                }
                if (nodeHovered)
                {
                    if (!scriptError.empty())
                    {
                        std::string tooltip = "Script error:\n" + scriptError;
                        ui::TooltipText(tooltip.c_str());
                    }
                    else if (nodeCompFlags & Component_RuntimeUi)
                    {
                        ui::TooltipText("Runtime UI scene node; double-click to frame it in the viewport.");
                    }
                    else if (spriteHierarchyNode)
                    {
                        ui::TooltipText("Sprite-related scene node; double-click to frame it in the viewport.");
                    }
                    else
                    {
                        ui::TooltipText("Select this scene node; double-click to frame it in the viewport.");
                    }
                }

                if (nodeOpen)
                {
                    for (NodeId *child : children)
                        self(self, child);

                    ImGui::TreePop();
                }
            };

            // Draw all root nodes (nodes without parents)
            uint32_t nodeCount = scene.GetNodeCount();
            for (uint32_t i = 0; i < nodeCount; i++)
            {
                NodeId *node = scene.GetNodeId(i);
                if (!scene.GetParent(node))
                    DrawNode(DrawNode, node);
            }

            // Fill remaining space with dummy to allow dropping in empty area
            ImVec2 available = ImGui::GetContentRegionAvail();
            if (available.y < 50.0f)
                available.y = 50.0f;
            ImGui::Dummy(available);

            // Window-wide Drop Target for loading models from FileBrowser
            if (ImGui::BeginDragDropTarget())
            {
                const bool acceptedRuntimeUi = acceptRuntimeUiDrop(nullptr);
                if (!acceptedRuntimeUi)
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    {
                        const char *pathStr = (const char *)payload->Data;
                        std::filesystem::path path(pathStr);

                        std::string ext = path.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                        // Only cooked .pemesh loads into the scene; source models are import-only (File > Import).
                        bool isModel = FileBrowser::IsCookedModelFile(path);

                        if (FileBrowser::IsPrefabFile(path))
                        {
                            instantiatePrefab(path, nullptr);
                        }
                        else if (SpriteAuthoring::IsSpriteAssetPath(path))
                        {
                            createSprite(nullptr, path);
                        }
                        else if (isModel && !GUIState::s_modelLoading)
                        {
                            auto loadTask = [path]()
                            {
                                GUIState::s_modelLoading = true;
                                try
                                {
                                    if (ModelAsset *m = ModelAsset::Load(path))
                                        EventSystem::PushEvent(EventType::ModelLoaded, m);
                                }
                                catch (const std::exception &e)
                                {
                                    PE_WARN("[Scene] Failed to load model: %s", e.what());
                                }
                                GUIState::s_modelLoading = false;
                            };
                            ThreadPool::GUI.Enqueue(loadTask);
                        }
                    }

                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("HIERARCHY_NODE"))
                    {
                        HierarchyDragDropPayload data = *(const HierarchyDragDropPayload *)payload->Data;
                        if (scene.IsNodeAlive(data.node) && scene.GetParent(data.node))
                        {
                            recordSnapshot("Reparented Node");
                            scene.ReparentNode(data.node, nullptr);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::TreePop(); // Root Node
        } // if (rootOpen)

        if (s_openRenamePopup)
        {
            ImGui::OpenPopup("Rename Entity");
            s_openRenamePopup = false;
        }

        if (ImGui::BeginPopupModal("Rename Entity", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", s_renameBuf, IM_ARRAYSIZE(s_renameBuf));
            ui::ItemTooltip("Edit the node or emitter display name.");
            if (ImGui::Button("OK", ui::DialogButtonSize("OK", 9.2f)))
            {
                if (s_renameEmitterIndex >= 0)
                {
                    ParticleManager *pm = scene.GetParticleManager();
                    if (pm)
                    {
                        auto &emNames = pm->GetEmitterNames();
                        if (s_renameEmitterIndex < static_cast<int>(emNames.size()))
                            emNames[s_renameEmitterIndex] = s_renameBuf;
                    }
                }
                else if (s_renameNode)
                {
                    if (scene.IsNodeAlive(s_renameNode))
                    {
                        scene.SetNodeName(s_renameNode, s_renameBuf);
                        // Sync camera/light names stored in their respective systems
                        Camera *cam = scene.GetCameraForNode(s_renameNode);
                        if (cam)
                            cam->SetName(s_renameBuf);
                        else
                        {
                            auto [lt, idx] = scene.GetLightForNode(s_renameNode);
                            if (idx >= 0)
                            {
                                if (lt == LightType::Directional)
                                    scene.GetDirectionalLights()[idx].name = s_renameBuf;
                                else if (lt == LightType::Point)
                                    scene.GetPointLights()[idx].name = s_renameBuf;
                                else if (lt == LightType::Spot)
                                    scene.GetSpotLights()[idx].name = s_renameBuf;
                                else if (lt == LightType::Area)
                                    scene.GetAreaLights()[idx].name = s_renameBuf;
                            }
                        }
                    }
                }
                s_renameNode = nullptr;
                s_renameEmitterIndex = -1;
                ImGui::CloseCurrentPopup();
            }
            ui::ItemTooltip("Apply the new name.");
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ui::DialogButtonSize("Cancel", 9.2f)))
            {
                s_renameNode = nullptr;
                s_renameEmitterIndex = -1;
                ImGui::CloseCurrentPopup();
            }
            ui::ItemTooltip("Close without renaming.");
            ImGui::EndPopup();
        }

        // Delete deferred nodes
        for (NodeId *node : nodesToDelete)
        {
            if (selection.GetSelectedNode() == node)
                selection.ClearSelection();
            scene.DeleteNode(node);
        }
        if (!nodesToDelete.empty())
            EventSystem::PushEvent(EventType::NodeRemoved);

        if (ImGui::BeginPopupContextWindow("HierarchyBgContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            const bool addMenuOpen = ImGui::BeginMenu("Add");
            ui::ItemTooltip("Create a root-level scene object.");
            if (addMenuOpen)
            {
                drawAddEntityItems(nullptr);
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        ImGui::End();

        if (styleVarCount > 0)
            ImGui::PopStyleVar(styleVarCount);
        if (styleColorCount > 0)
            ImGui::PopStyleColor(styleColorCount);
    }
} // namespace pe
