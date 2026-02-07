#include "MeshWidget.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "FileSelector.h"
#include "GUI/GUI.h"
#include "Scene/Model.h"
#include "Scene/SelectionManager.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_vulkan.h"

namespace pe
{
    MeshWidget::MeshWidget() : Widget("Mesh")
    {
    }

    MeshWidget::~MeshWidget()
    {
        for (auto &pair : m_textureDescriptors)
        {
            if (pair.second)
                ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)pair.second);
        }
    }

    void MeshWidget::Update()
    {
    }

    void MeshWidget::DrawEmbed(MeshInfo *mesh, Model *model)
    {
        if (!mesh)
            return;

        ImGui::Text("Mesh Data & Offsets");
        if (ImGui::BeginTable("MeshOffsets", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableNextRow();
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Data Size");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%zu", mesh->dataSize);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Vertex Offset");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", mesh->vertexOffset);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Vertices Count");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", mesh->verticesCount);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Index Offset");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", mesh->indexOffset);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Indices Count");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", mesh->indicesCount);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Indirect Index");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", mesh->indirectIndex);

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

        if (ImGui::CollapsingHeader("Bounds"))
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
                PropagateMeshChange(mesh, model);
            }
        }

        DrawMaterialInfo(mesh, model);
        DrawTextureInfo(mesh, model);
    }

    void MeshWidget::DrawMaterialInfo(MeshInfo *mesh, Model *model)
    {
        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginTable("MaterialTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 120.f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                bool changed = false;

                // Render Type
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Render Type");
                ImGui::TableSetColumnIndex(1);

                const char *renderTypeItems[] = {"Opaque", "AlphaCut", "AlphaBlend", "Transmission"};
                int currentRenderType = static_cast<int>(mesh->renderType) - 1;
                if (currentRenderType >= 0 && currentRenderType < 4)
                {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::Combo("##RenderType", &currentRenderType, renderTypeItems, IM_ARRAYSIZE(renderTypeItems)))
                    {
                        mesh->renderType = static_cast<RenderType>(currentRenderType + 1);
                        changed = true;
                    }
                }
                else
                {
                    ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "Unknown (%d)", static_cast<int>(mesh->renderType));
                }

                // Alpha Cutoff
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Alpha Cutoff");
                ImGui::TableSetColumnIndex(1);

                ImGui::SetNextItemWidth(-FLT_MIN);
                float alphaCutoff = mesh->materialFactors[0][2].z;
                if (ImGui::SliderFloat("##AlphaCutoff", &alphaCutoff, 0.0f, 1.0f, "%.3f"))
                {
                    mesh->materialFactors[0][2].z = alphaCutoff;
                    changed = true;
                }

                // Base Color Factor
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Base Color");
                ImGui::TableSetColumnIndex(1);

                vec4 baseColorFactor = mesh->materialFactors[0][0];
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::ColorEdit4("##BaseColor", &baseColorFactor.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf))
                {
                    mesh->materialFactors[0][0] = baseColorFactor;
                    changed = true;
                }

                // Metallic
                float metallic = mesh->materialFactors[0][2].x;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Metallic");
                ImGui::TableSetColumnIndex(1);

                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderFloat("##Metallic", &metallic, 0.0f, 1.0f, "%.3f"))
                {
                    mesh->materialFactors[0][2].x = metallic;
                    changed = true;
                }

                // Roughness
                float roughness = mesh->materialFactors[0][2].y;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Roughness");
                ImGui::TableSetColumnIndex(1);

                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderFloat("##Roughness", &roughness, 0.0f, 1.0f, "%.3f"))
                {
                    mesh->materialFactors[0][2].y = roughness;
                    changed = true;
                }

                ImGui::EndTable();

                if (changed)
                {
                    PropagateMeshChange(mesh, model);
                    RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                    if (renderer)
                        renderer->GetScene().UpdateTextures();
                }
            }
        }
    }

    void MeshWidget::DrawTextureInfo(MeshInfo *mesh, Model *model)
    {
        if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
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
                        bool active = (mesh->textureMask & (1 << i)) != 0;
                        if (ImGui::Selectable(labels[i], &active, ImGuiSelectableFlags_None, ImVec2(24, 0)))
                        {
                            if (active)
                                mesh->textureMask |= (1 << i);
                            else
                                mesh->textureMask &= ~(1 << i);
                            changed = true;
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", fullNames[i]);

                        ImGui::SameLine();
                    }
                    ImGui::Text(" (0x%X)", mesh->textureMask);

                    if (changed)
                    {
                        model->MarkDirty(0);
                        PropagateMeshChange(mesh, model);

                        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                        if (renderer)
                            renderer->GetScene().UpdateTextures();
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
                    Image *img = mesh->images[i];
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
                                ImGui::Text("%s", img->GetName().c_str());
                                ImGui::Text("Resolution: %ux%u", img->GetWidth(), img->GetHeight());
                                ImGui::Text("Format: %s", vk::to_string(img->GetFormat()).c_str());
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
                    }

                    ImGui::SameLine();
                    if (img)
                    {
                        if (ImGui::Button(("Clear" + id).c_str()))
                        {
                            mesh->images[i] = nullptr;
                            mesh->textureMask &= ~(1 << i);
                            model->MarkDirty(0);
                            PropagateMeshChange(mesh, model);

                            RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                            if (renderer)
                                renderer->GetScene().UpdateTextures();
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("None");
                    }

                    if (clicked)
                    {
                        if (auto *fs = m_gui->GetWidget<FileSelector>())
                        {
                            fs->OpenSelection([this, mesh, model, i](const std::string &path)
                                              {
                                Queue *queue = RHII.GetMainQueue();
                                CommandBuffer *cmd = queue->AcquireCommandBuffer();
                                cmd->Begin();
                                Image *newImg = model->LoadTexture(cmd, path);
                                cmd->End();
                                queue->Submit(1, &cmd, nullptr, nullptr);
                                cmd->Wait();
                                queue->ReturnCommandBuffer(cmd);

                                if (newImg)
                                {
                                    mesh->images[i] = newImg;
                                    mesh->textureMask |= (1 << i);
                                    model->MarkDirty(0);
                                    PropagateMeshChange(mesh, model);
                                    
                                    RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                                    if (renderer)
                                        renderer->GetScene().UpdateTextures();
                                } }, {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".psd", ".gif", ".hdr", ".pic", ".ppm", ".pgm"});
                        }
                    }
                }
                ImGui::EndTable();
            }
        }
    }

    void *MeshWidget::GetDescriptor(Image *image)
    {
        if (m_textureDescriptors.find(image) == m_textureDescriptors.end())
        {
            if (!image->HasSRV())
            {
                image->CreateSRV(vk::ImageViewType::e2D);
            }

            if (!image->GetSampler())
            {
                vk::SamplerCreateInfo info = Sampler::CreateInfoInit();
                image->SetSampler(Sampler::Create(info));
            }

            m_textureDescriptors[image] = (void *)ImGui_ImplVulkan_AddTexture(
                image->GetSampler()->ApiHandle(),
                image->GetSRV()->ApiHandle(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        return m_textureDescriptors[image];
    }
    void MeshWidget::PropagateMeshChange(MeshInfo *mesh, Model *model)
    {
        const auto &meshInfos = model->GetMeshInfos();
        int meshIndex = -1;

        for (int i = 0; i < static_cast<int>(meshInfos.size()); i++)
        {
            if (&meshInfos[i] == mesh)
            {
                meshIndex = i;
                break;
            }
        }

        if (meshIndex < 0)
            return;

        int nodeCount = model->GetNodeCount();
        for (int i = 0; i < nodeCount; i++)
        {
            if (model->GetNodeMesh(i) == meshIndex)
            {
                model->MarkDirty(i);
            }
        }
    }
} // namespace pe
