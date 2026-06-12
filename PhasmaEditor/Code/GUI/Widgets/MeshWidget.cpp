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

            if (ImGui::ColorEdit4("AABB Color", aabbColor))
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

        DrawMaterialInfo(mesh, node);
    }

    void MeshWidget::DrawMaterialInfo(Mesh *mesh, NodeId *node)
    {
        const bool materialOpen = ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen);
        ui::ItemTooltip("Edit the mesh material, shader parameters, and texture bindings.");
        if (materialOpen)
        {
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
