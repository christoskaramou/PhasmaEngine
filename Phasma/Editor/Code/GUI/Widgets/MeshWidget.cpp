#include "MeshWidget.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "FileSelector.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "Scene/Material.h"
#include "Scene/MaterialAsset.h"
#include "Scene/OpenPBRMaterial.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneNode.h"
#include "Scene/SelectionManager.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"

namespace pe
{
    MeshWidget::MeshWidget() : Widget("Mesh")
    {
    }

    MeshWidget::~MeshWidget()
    {
        for (auto &pair : m_textureDescriptors)
        {
            if (m_gui)
                m_gui->ReleaseImageTexture(pair.second);
        }
    }

    void MeshWidget::Update()
    {
    }

    void MeshWidget::DrawEmbed(Mesh *mesh, NodeId *node)
    {
        if (!mesh || !node)
            return;

        const SceneNodeHandle editNode = GetActiveScene()->MakeHandle(node);
        if (m_openpbrEditNode != editNode)
        {
            m_openpbrEditNode = editNode;
            m_openpbrEdits.clear();
        }

        ImGui::Text("Mesh Data & Offsets");
        if (ImGui::BeginTable("MeshOffsets", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableNextRow();
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Data Size");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%zu", sizeof(NodeGpuData));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Vertex Offset");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", mesh->vertexOffset);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Vertices Count");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", mesh->vertexCount);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Index Offset");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", mesh->indexOffset);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Indices Count");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", mesh->indexCount);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Positions Offset");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", mesh->positionsOffset);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("AABB Vertex Offset");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%zu", mesh->aabbVertexOffset);

            ImGui::EndTable();
        }

        const bool boundsOpen = ImGui::CollapsingHeader("Bounds");
        ui::ItemTooltip("Show mesh bounds and debug AABB display settings.");
        if (boundsOpen)
        {
            ImGui::LabelText("Local AABB", "Min: (%.2f, %.2f, %.2f)\nMax: (%.2f, %.2f, %.2f)",
                             mesh->boundingBox.min.x, mesh->boundingBox.min.y, mesh->boundingBox.min.z,
                             mesh->boundingBox.max.x, mesh->boundingBox.max.y, mesh->boundingBox.max.z);

            // AABB Color (debug)
            float aabbColor[4];
            aabbColor[0] = ((mesh->aabbColor >> 24) & 0xFF) / 255.0f;
            aabbColor[1] = ((mesh->aabbColor >> 16) & 0xFF) / 255.0f;
            aabbColor[2] = ((mesh->aabbColor >> 8) & 0xFF) / 255.0f;
            aabbColor[3] = ((mesh->aabbColor >> 0) & 0xFF) / 255.0f;

            if (ImGui::ColorEdit4(ui::LabelAbove("AABB Color"), aabbColor))
            {
                uint32_t r = static_cast<uint32_t>(aabbColor[0] * 255.0f + 0.5f);
                uint32_t g = static_cast<uint32_t>(aabbColor[1] * 255.0f + 0.5f);
                uint32_t b = static_cast<uint32_t>(aabbColor[2] * 255.0f + 0.5f);
                uint32_t a = static_cast<uint32_t>(aabbColor[3] * 255.0f + 0.5f);
                mesh->aabbColor = (r << 24) | (g << 16) | (b << 8) | a;
                PropagateMeshChange(node);
                m_gui->NotifyChange();
            }
            ui::ItemTooltip("Color used when drawing this mesh's debug AABB.");
        }

        {
            const bool lodOpen = ImGui::CollapsingHeader("LOD");
            ui::ItemTooltip("Per-mesh level-of-detail. The GPU cull pass swaps this mesh to a simpler index "
                            "set by camera distance. Needs the scene Mesh LOD master switch enabled.");
            if (lodOpen)
            {
                if (mesh->lodCount <= 1)
                {
                    ImGui::TextDisabled("No LODs - mesh has too few indices (%u) to simplify.", mesh->indexCount);
                    ui::ItemTooltip("LODs are generated at load only for meshes with >= 256 indices; meshopt "
                                    "cannot meaningfully simplify smaller meshes.");
                }
                else
                {
                    ImGui::Text("Levels: %u (generated at load)", mesh->lodCount);
                    bool lodDirty = ImGui::Checkbox("Enable", &mesh->lodEnabled);
                    ui::ItemTooltip("Let this mesh use LOD. Off = always full detail regardless of distance.");
                    if (mesh->lodEnabled)
                    {
                        int shift = static_cast<int>(mesh->lodShift);
                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::DragInt(ui::LabelAbove("Shift LOD"), &shift, 0.1f, 0, static_cast<int>(mesh->lodCount) - 1))
                        {
                            mesh->lodShift =
                                static_cast<uint32_t>(std::clamp(shift, 0, static_cast<int>(mesh->lodCount) - 1));
                            lodDirty = true;
                        }
                        ui::ItemTooltip("LOD level offset added to the distance pick (0 = automatic, 1+ forces that "
                                        "many levels coarser, clamped to the last level).");
                        ImGui::SetNextItemWidth(120.0f);
                        lodDirty |= ImGui::DragFloat(ui::LabelAbove("Distance Bias"), &mesh->lodBias, 0.01f, 0.05f, 20.0f);
                        ui::ItemTooltip("Per-mesh multiplier on camera distance (>1 = drop detail sooner, "
                                        "<1 = keep detail longer).");
                    }
                    if (lodDirty)
                    {
                        Scene &sc = *GetActiveScene();
                        sc.SetGeometryDirty(); // rebuild Mesh_Constants so the change reaches the GPU
                        sc.MarkNodeDirty(node);
                        m_gui->NotifyChange();
                    }
                }
            }
        }

        DrawMaterialInfo(mesh, node);
    }

    void MeshWidget::DrawMaterialInfo(Mesh *mesh, NodeId *node)
    {
        const bool materialOpen = ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen);
        ui::ItemTooltip("Edit the mesh material, shader parameters, and texture bindings.");
        if (materialOpen)
        {
            if (mesh->material && DrawOpenPBRMaterial(*mesh))
                return;

            // If material has a PassInfoAsset, use the reflection-driven editor
            if (mesh->material && mesh->material->passInfoAsset)
            {
                if (m_materialEditor.Draw(mesh, node, *GetActiveScene()))
                {
                    PropagateMeshChange(node);
                    GetActiveScene()->UpdateDirtyMaterials();
                    m_gui->NotifyChange();
                }
                DrawTextureInfo(mesh, node);
                return;
            }

            // Show material name if available
            Material *mat = mesh->material;
            if (mat && !mat->name.empty())
            {
                ImGui::TextDisabled("Material: %s", mat->name.c_str());
            }

            // Shared / Instanced toggle
            if (mat)
            {
                bool isInstanced = mesh->materialInstance != nullptr;
                if (ImGui::Checkbox("Instanced", &isInstanced))
                {
                    RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                    if (renderer)
                    {
                        renderer->WaitAllFramesCommands();
                        Scene &scene = renderer->GetScene();
                        if (isInstanced)
                            scene.CreateMaterialInstance(*mesh);
                        else
                            scene.DestroyMaterialInstance(*mesh);
                        scene.UpdateTextures();
                        m_gui->NotifyChange();
                    }
                }
                ui::ItemTooltip("Use per-mesh material overrides instead of editing the shared base material.");
                ImGui::SameLine();
                if (isInstanced)
                    ImGui::TextDisabled("(per-mesh overrides)");
                else
                    ImGui::TextDisabled("(shared)");
            }

            // Drag .mat file onto this button or anywhere in the section to apply it
            static char s_matSaveName[128] = "";
            static bool s_openSavePopup = false;

            if (ImGui::Button("Save Material..."))
            {
                std::snprintf(s_matSaveName, sizeof(s_matSaveName), "material");
                s_openSavePopup = true;
            }
            const bool saveMaterialHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    std::filesystem::path matPath((const char *)p->Data);
                    if (matPath.extension() == ".mat")
                    {
                        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                        if (renderer)
                        {
                            renderer->WaitAllFramesCommands();
                            if (mat && MaterialIO::LoadMaterial(*mat, matPath))
                            {
                                PropagateMeshChange(node);
                                renderer->GetScene().UpdateTextures();
                                m_gui->NotifyChange();
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (saveMaterialHovered)
                ui::TooltipText("Save the current material settings as a .mat asset, or drop a .mat here to apply it.");

            if (s_openSavePopup)
            {
                ImGui::OpenPopup("##SaveMaterial");
                s_openSavePopup = false;
            }

            if (ImGui::BeginPopup("##SaveMaterial"))
            {
                ImGui::Text("Save to Assets/Materials/");
                ImGui::SetNextItemWidth(200.f);
                ImGui::InputText("##matname", s_matSaveName, sizeof(s_matSaveName));
                ui::ItemTooltip("Filename for the material asset under Assets/Materials.");
                ImGui::SameLine();
                ImGui::Text(".mat");

                const bool saveClicked = ImGui::Button("Save");
                ui::ItemTooltip("Write the material asset to disk.");
                if (saveClicked && s_matSaveName[0] != '\0')
                {
                    std::filesystem::path savePath =
                        std::filesystem::path(Path::Assets) / "Materials" / (std::string(s_matSaveName) + ".mat");
                    if (mat)
                        MaterialIO::SaveMaterial(*mat, savePath);
                    else
                        MaterialIO::Save(*mesh, savePath);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    ImGui::CloseCurrentPopup();
                ui::ItemTooltip("Close without saving a material asset.");

                ImGui::EndPopup();
            }

            ImGui::Separator();

            if (ImGui::BeginTable("MaterialTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 120.f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                bool changed = false;
                MaterialInstance *inst = mesh->materialInstance;

                // Read current values directly — no Material copy (avoids shared_ptr refcount issues)
                RenderType curRenderType = inst ? inst->GetRenderType() : (mat ? mat->renderType : mesh->renderType);
                float curAlphaCutoff = inst ? inst->GetAlphaCutoff() : (mat ? mat->alphaCutoff : 0.5f);
                vec4 curBaseColor = inst ? inst->GetBaseColorFactor() : (mat ? mat->baseColorFactor : vec4(1.f));
                float curMetallic = inst ? inst->GetMetallic() : (mat ? mat->metallic : 0.f);
                float curRoughness = inst ? inst->GetRoughness() : (mat ? mat->roughness : 1.f);

                // Render Type
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Render Type");
                ImGui::TableSetColumnIndex(1);

                const char *renderTypeItems[] = {"Opaque", "AlphaCut", "AlphaBlend", "Transmission", "Lines"};
                int currentRenderType = static_cast<int>(curRenderType) - 1;
                if (currentRenderType >= 0 && currentRenderType < IM_ARRAYSIZE(renderTypeItems))
                {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::Combo("##RenderType", &currentRenderType, renderTypeItems, IM_ARRAYSIZE(renderTypeItems)))
                    {
                        RenderType newRT = static_cast<RenderType>(currentRenderType + 1);
                        if (inst)
                            inst->SetRenderType(newRT);
                        else if (mat)
                            mat->renderType = newRT;
                        mesh->renderType = newRT;
                        changed = true;
                    }
                    ui::ItemTooltip("Select the material render path: opaque, cutout, blended, transmission, or lines.");
                }
                else
                {
                    ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "Unknown (%d)", static_cast<int>(curRenderType));
                }

                // Alpha Cutoff
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Alpha Cutoff");
                ImGui::TableSetColumnIndex(1);

                ImGui::SetNextItemWidth(-FLT_MIN);
                float alphaCutoff = curAlphaCutoff;
                if (ImGui::SliderFloat("##AlphaCutoff", &alphaCutoff, 0.0f, 1.0f, "%.3f"))
                {
                    if (inst)
                        inst->SetAlphaCutoff(alphaCutoff);
                    else if (mat)
                        mat->alphaCutoff = alphaCutoff;
                    changed = true;
                }
                ui::ItemTooltip("Alpha threshold used by AlphaCut materials.");

                // Base Color Factor
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Base Color");
                ImGui::TableSetColumnIndex(1);

                vec4 baseColorFactor = curBaseColor;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::ColorEdit4("##BaseColor", &baseColorFactor.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf))
                {
                    if (inst)
                        inst->SetBaseColorFactor(baseColorFactor);
                    else if (mat)
                        mat->baseColorFactor = baseColorFactor;
                    changed = true;
                }
                ui::ItemTooltip("Base color multiplier and alpha for this material.");

                // Metallic
                float metallic = curMetallic;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Metallic");
                ImGui::TableSetColumnIndex(1);

                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderFloat("##Metallic", &metallic, 0.0f, 1.0f, "%.3f"))
                {
                    if (inst)
                        inst->SetMetallic(metallic);
                    else if (mat)
                        mat->metallic = metallic;
                    changed = true;
                }
                ui::ItemTooltip("Metalness factor used by the PBR shader.");

                // Roughness
                float roughness = curRoughness;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Roughness");
                ImGui::TableSetColumnIndex(1);

                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderFloat("##Roughness", &roughness, 0.0f, 1.0f, "%.3f"))
                {
                    if (inst)
                        inst->SetRoughness(roughness);
                    else if (mat)
                        mat->roughness = roughness;
                    changed = true;
                }
                ui::ItemTooltip("Surface roughness used by the PBR shader.");

                ImGui::EndTable();

                if (changed)
                {
                    if (mat && !inst)
                        mat->dirty = true;
                    PropagateMeshChange(node);
                    RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                    if (renderer)
                    {
                        if (inst)
                            renderer->GetScene().UpdateTextures(); // instance needs full rebuild for its own GPU entry
                        else
                            renderer->GetScene().UpdateDirtyMaterials();
                    }
                    m_gui->NotifyChange();
                }
            }

            DrawTextureInfo(mesh, node);
        }
    }

    static bool DrawOpenPBRValue(const OpenPBRInput &input, MaterialParamValue &value)
    {
        if (input.type == "bool")
        {
            bool enabled = std::get<uint32_t>(value) != 0u;
            if (!ImGui::Checkbox("##value", &enabled))
                return false;

            value = enabled ? 1u : 0u;
            return true;
        }

        if (input.type == "int")
            return ImGui::InputScalar("##value", ImGuiDataType_S32, &std::get<int32_t>(value));

        if (input.type == "uint")
            return ImGui::InputScalar("##value", ImGuiDataType_U32, &std::get<uint32_t>(value));

        if (input.type == "float")
            return ImGui::DragFloat("##value", &std::get<float>(value), 0.01f);

        if (input.type == "float2")
            return ImGui::DragFloat2("##value", &std::get<vec2>(value).x, 0.01f);

        if (input.type == "float3")
            return ImGui::DragFloat3("##value", &std::get<vec3>(value).x, 0.01f);

        if (input.type == "float4")
            return ImGui::DragFloat4("##value", &std::get<vec4>(value).x, 0.01f);

        return false;
    }

    bool MeshWidget::DrawOpenPBRMaterial(Mesh &mesh)
    {
        auto [it, inserted] = m_openpbrEdits.try_emplace(&mesh);
        OpenPBREditState &state = it->second;
        const auto source = mesh.materialInstance ? mesh.materialInstance->GetOpenPBRMaterialSource() : nullptr;
        if (inserted || source != state.source)
        {
            state.source = source;
            state.error.clear();
            const bool assignedDocument = source && source != GetOpenPBRDefaultSource();
            std::snprintf(state.document, sizeof(state.document), "%s", assignedDocument ? source->path.c_str() : "");
            std::snprintf(state.material, sizeof(state.material), "%s",
                          assignedDocument ? source->material.c_str() : "Material");
        }

        bool enabled = mesh.materialInstance && mesh.materialInstance->GetOpenPBR();
        if (ImGui::Checkbox("OpenPBR", &enabled))
        {
            if (auto *renderer = GetGlobalSystem<RendererSystem>())
                renderer->WaitAllFramesCommands();

            state.error.clear();
            if (SetOpenPBREnabled(*GetActiveScene(), mesh, enabled, state.error))
                m_gui->NotifyChange();
        }
        ui::ItemTooltip("Use the assigned MaterialX graph. Turning this off preserves its source and input overrides.");

        DrawOpenPBRAssignment(mesh, state);
        if (!state.error.empty())
            ImGui::TextWrapped("%s", state.error.c_str());

        if (!mesh.materialInstance || !mesh.materialInstance->GetOpenPBR())
            return false;

        const OpenPBRMaterialSource &activeSource = GetOpenPBRSource(*mesh.materialInstance);
        const bool defaultSource = &activeSource == GetOpenPBRDefaultSource().get();
        ImGui::TextWrapped("Document: %s", defaultSource ? "Built-in OpenPBR" : activeSource.path.c_str());
        ImGui::TextWrapped("Material: %s", activeSource.material.c_str());
        if (!MeshUsesOpenPBR(mesh))
            ImGui::TextWrapped("OpenPBR shading requires a standard opaque or alpha-cut material.");

        DrawOpenPBRInputs(*mesh.materialInstance, state.error);
        return true;
    }

    void MeshWidget::DrawOpenPBRAssignment(Mesh &mesh, OpenPBREditState &state)
    {
        ImGui::InputText(ui::LabelAbove("MaterialX document"), state.document, sizeof(state.document));
        if (ImGui::Button("Browse...##MaterialX"))
        {
            if (auto *selector = m_gui->GetWidget<FileSelector>())
            {
                auto selectDocument = [this, target = &mesh, node = m_openpbrEditNode](const std::string &path)
                {
                    if (m_openpbrEditNode != node)
                        return true;

                    const auto it = m_openpbrEdits.find(target);
                    if (it != m_openpbrEdits.end())
                        std::snprintf(it->second.document, sizeof(it->second.document), "%s", path.c_str());

                    return true;
                };
                selector->OpenSelection(selectDocument, {".mtlx"});
            }
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const auto *payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                const std::filesystem::path path(static_cast<const char *>(payload->Data));
                if (path.extension() == ".mtlx")
                {
                    std::snprintf(state.document, sizeof(state.document), "%s",
                                  static_cast<const char *>(payload->Data));
                }
            }
            ImGui::EndDragDropTarget();
        }
        ui::ItemTooltip("Choose or drop a .mtlx document, enter its material name, then assign it.");

        ImGui::InputText(ui::LabelAbove("MaterialX material"), state.material, sizeof(state.material));
        if (!ImGui::Button("Assign MaterialX"))
            return;

        if (auto *renderer = GetGlobalSystem<RendererSystem>())
            renderer->WaitAllFramesCommands();

        state.error.clear();
        const auto source = LoadOpenPBRMaterial(state.document, state.material, state.error);
        if (source && AssignOpenPBRMaterial(*GetActiveScene(), mesh, source, state.error))
            m_gui->NotifyChange();
    }

    void MeshWidget::DrawOpenPBRInputs(MaterialInstance &instance, std::string &error)
    {
        const OpenPBRMaterialSource &source = GetOpenPBRSource(instance);
        if (!ImGui::BeginTable("MaterialXInputs", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            return;

        ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        std::string node;
        for (const OpenPBRInput &input : source.module->layout.inputs)
        {
            if (node != input.node)
            {
                node = input.node;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", node.c_str());
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextWrapped("%s", input.identity.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", input.type.c_str());
            ImGui::TableSetColumnIndex(2);
            if (input.type == "SamplerTexture2D")
            {
                ImGui::TextWrapped("%s", source.textures.at(input.identity).file.c_str());
                continue;
            }

            ImGui::PushID(input.identity.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            MaterialParamValue value = ResolveOpenPBRInput(instance, input);
            if (DrawOpenPBRValue(input, value))
            {
                error.clear();
                if (SetOpenPBRInput(*GetActiveScene(), instance, input.identity, value, error))
                    m_gui->NotifyChange();
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    void MeshWidget::DrawTextureInfo(Mesh *mesh, NodeId *node)
    {
        Material *mat = mesh->material;
        MaterialInstance *inst = mesh->materialInstance;
        {
            if (ImGui::BeginTable("TextureTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 140.f);
                ImGui::TableSetupColumn("Texture", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                // Texture Mask
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Mask");
                    ImGui::TableSetColumnIndex(1);

                    const char *labels[] = {"BC", "MR", "N", "O", "E"};
                    const char *fullNames[] = {"Base Color", "Metallic Roughness", "Normal", "Occlusion", "Emissive"};
                    bool changed = false;

                    for (int i = 0; i < 5; i++)
                    {
                        uint32_t mask = inst ? inst->GetTextureMask() : (mat ? mat->textureMask : 0u);
                        bool active = (mask & (1 << i)) != 0;
                        bool wasActive = active;
                        if (wasActive)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.55f, 0.85f, 0.8f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.30f, 0.60f, 0.90f, 0.9f));
                        }
                        if (ImGui::Selectable(labels[i], &active, ImGuiSelectableFlags_None, ImVec2(24, 0)))
                        {
                            uint32_t newMask = mask;
                            if (active)
                                newMask |= (1 << i);
                            else
                                newMask &= ~(1 << i);

                            if (inst)
                                inst->SetTextureMask(newMask);
                            else if (mat)
                                mat->textureMask = newMask;
                            changed = true;
                        }
                        if (wasActive)
                            ImGui::PopStyleColor(2);
                        ui::ItemTooltip(fullNames[i]);

                        ImGui::SameLine();
                    }
                    ImGui::Text(" (0x%X)", inst ? inst->GetTextureMask() : (mat ? mat->textureMask : 0u));

                    if (changed)
                    {
                        PropagateMeshChange(node);

                        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                        if (renderer)
                            renderer->GetScene().UpdateTextures();
                        m_gui->NotifyChange();
                    }
                }

                const char *textureNames[] = {"Base Color", "Metallic Roughness", "Normal", "Occlusion", "Emissive"};

                for (int i = 0; i < 5; i++)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%s", textureNames[i]);

                    ImGui::TableSetColumnIndex(1);
                    Image *img = nullptr;
                    if (inst)
                        img = inst->GetTexture(i);
                    else if (mat)
                        img = mat->textures[i].get();
                    std::string id = "##tex" + std::to_string(i);

                    // Thumbnail / Button
                    bool clicked = false;
                    float size = 32.f;
                    if (img)
                    {
                        void *desc = GetDescriptor(img);
                        if (desc)
                        {
                            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
                            if (ImGui::ImageButton(id.c_str(), (ImTextureID)desc, ImVec2(size, size)))
                                clicked = true;
                            ImGui::PopStyleVar();

                            if (ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                ImGui::TextUnformatted("Click to choose a replacement texture.");
                                ImGui::Separator();
                                ImGui::Text("%s", img->GetName().c_str());
                                ImGui::Text("Resolution: %ux%u", img->GetWidth(), img->GetHeight());
                                ImGui::Text("Format: %s", ::PeFormatName(img->GetFormat()));
                                ImGui::Text("Mips: %u", img->GetMipLevels());
                                float aspect = img->GetWidth_f() / img->GetHeight_f();
                                ImGui::Image((ImTextureID)desc, ImVec2(256 * aspect, 256));
                                ImGui::EndTooltip();
                            }
                        }
                    }
                    else
                    {
                        if (ImGui::Button(("Select..." + id).c_str(), ImVec2(-FLT_MIN, 0.f)))
                            clicked = true;
                        ui::ItemTooltip("Select a texture asset for this material slot.");
                    }

                    ImGui::SameLine();
                    if (img)
                    {
                        if (ImGui::Button(("Clear" + id).c_str()))
                        {
                            if (inst)
                            {
                                inst->SetTexture(static_cast<TextureType>(i), ResourceHandle<Image>());
                                uint32_t newMask = inst->GetTextureMask() & ~(1 << i);
                                inst->SetTextureMask(newMask);
                            }
                            else if (mat)
                            {
                                mat->textures[i] = ResourceHandle<Image>();
                                mat->textureMask &= ~(1 << i);
                            }
                            PropagateMeshChange(node);

                            RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                            if (renderer)
                                renderer->GetScene().UpdateTextures();
                            m_gui->NotifyChange();
                        }
                        ui::ItemTooltip("Clear this texture slot and disable its texture mask bit.");
                    }
                    else
                    {
                        ImGui::TextDisabled("None");
                    }

                    if (clicked)
                    {
                        if (auto *fs = m_gui->GetWidget<FileSelector>())
                        {
                            fs->OpenSelection([this, mesh, node, i](const std::string &path)
                                              {
                                Queue *queue = RHII.GetMainQueue();
                                CommandBuffer *cmd = queue->AcquireCommandBuffer();
                                cmd->Begin();

                                // Load texture via ResourceManager
                                std::error_code ec;
                                std::filesystem::path normalized = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
                                if (ec) normalized = path;
                                std::string normalizedStr(reinterpret_cast<const char *>(normalized.u8string().c_str()));

                                ResourceHandle<Image> newImg = ResourceManager::Get().Find<Image>(normalizedStr);
                                if (!newImg)
                                {
                                    Image *rawImg = Image::LoadRGBA8(cmd, normalizedStr);
                                    if (rawImg)
                                    {
                                        std::shared_ptr<Image> sharedImage(rawImg, [](Image *img) { Image::Destroy(img); });
                                        ResourceManager::Get().Register<Image>(normalizedStr, sharedImage);
                                        newImg = ResourceHandle<Image>(sharedImage);
                                    }
                                }

                                cmd->End();
                                queue->Submit(1, &cmd, nullptr, nullptr);
                                cmd->Wait();
                                queue->ReturnCommandBuffer(cmd);

                                if (newImg)
                                {
                                    if (mesh->materialInstance)
                                    {
                                        mesh->materialInstance->SetTexture(static_cast<TextureType>(i), newImg);
                                        uint32_t newMask = mesh->materialInstance->GetTextureMask() | (1 << i);
                                        mesh->materialInstance->SetTextureMask(newMask);
                                    }
                                    else if (mesh->material)
                                    {
                                        mesh->material->textures[i] = newImg;
                                        mesh->material->textureMask |= (1 << i);
                                    }
                                    PropagateMeshChange(node);

                                    RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                                    if (renderer)
                                        renderer->GetScene().UpdateTextures();
                                    m_gui->NotifyChange();
                                } return true; });
                        }
                    }
                }
                ImGui::EndTable();
            }
        }
    }

    void *MeshWidget::GetDescriptor(Image *image)
    {
        if (!m_gui)
            return nullptr;

        if (m_textureDescriptors.find(image) == m_textureDescriptors.end())
        {
            if (!image->HasSRV())
            {
                image->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
            }

            if (!image->GetSRV())
                return nullptr;

            if (!image->GetSampler())
            {
                SamplerDesc info = Sampler::CreateInfoInit();
                image->SetSampler(Sampler::Create(info));
            }

            if (!image->GetSampler())
                return nullptr;

            m_textureDescriptors[image] = m_gui->RegisterImageTexture(image);
        }

        return m_textureDescriptors[image];
    }

    void MeshWidget::PropagateMeshChange(NodeId *node)
    {
        Scene &scene = *GetActiveScene();
        scene.MarkNodeDirty(node);
    }
} // namespace pe
