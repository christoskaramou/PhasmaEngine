#include "MeshWidget.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "Scene/Model.h"
#include "Scene/SelectionManager.h"
#include "imgui/imgui.h"

namespace pe
{
    MeshWidget::MeshWidget() : Widget("Mesh")
    {
    }

    MeshWidget::~MeshWidget()
    {
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
        }

        DrawMaterialInfo(mesh, model);
        DrawTextureInfo(mesh, model);
    }

    void MeshWidget::DrawMaterialInfo(MeshInfo *mesh, Model *model)
    {
        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool changed = false;

            // Render Type
            const char *renderTypeItems[] = {"Opaque", "AlphaCut", "AlphaBlend", "Transmission"};
            int currentRenderType = static_cast<int>(mesh->renderType) - 1; // Enum starts at 1
            if (currentRenderType >= 0 && currentRenderType < 4)
            {
                if (ImGui::Combo("Render Type", &currentRenderType, renderTypeItems, IM_ARRAYSIZE(renderTypeItems)))
                {
                    mesh->renderType = static_cast<RenderType>(currentRenderType + 1);
                    changed = true;
                }
            }
            else
            {
                ImGui::Text("Render Type: Unknown (%d)", static_cast<int>(mesh->renderType));
            }

            // Flags
            bool cull = mesh->cull;
            if (ImGui::Checkbox("Cull Backfaces", &cull))
            {
                mesh->cull = cull;
                changed = true;
            }

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
                changed = true;
            }

            if (ImGui::DragFloat("Alpha Cutoff", &mesh->alphaCutoff, 0.01f, 0.0f, 1.0f))
                changed = true;

            ImGui::Separator();
            ImGui::Text("Factors");

            // Base Color Factor
            vec4 baseColorFactor = mesh->materialFactors[0][0];
            if (ImGui::ColorEdit4("Base Color", &baseColorFactor.x))
            {
                mesh->materialFactors[0][0] = baseColorFactor;
                changed = true;
            }

            // Metallic / Roughness (Packed in Factor 1, Col 0)
            float metallic = mesh->materialFactors[1][0].x;
            float roughness = mesh->materialFactors[1][0].y;

            if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f))
            {
                mesh->materialFactors[1][0].x = metallic;
                changed = true;
            }
            if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f))
            {
                mesh->materialFactors[1][0].y = roughness;
                changed = true;
            }

            if (changed)
                PropagateMeshChange(mesh, model);
        }
    }

    void MeshWidget::DrawTextureInfo(MeshInfo *mesh, Model *model)
    {
        if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Texture Mask: 0x%X", mesh->textureMask);

            const char *textureNames[] = {"Base Color", "Metallic Roughness", "Normal", "Occlusion", "Emissive"};

            for (int i = 0; i < 5; i++)
            {
                Image *img = mesh->images[i];
                if (img)
                {
                    ImGui::Text("%s: %s", textureNames[i], img->GetName().c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Mips: %u", img->GetMipLevels());
                }
                else
                {
                    ImGui::TextDisabled("%s: None", textureNames[i]);
                }
            }
        }
    }
    void MeshWidget::PropagateMeshChange(MeshInfo *mesh, Model *model)
    {
        // Find which nodes use this mesh and mark them dirty
        // This is a bit slow (linear search), but happens only on UI interaction
        const auto &meshInfos = model->GetMeshInfos();
        int meshIndex = -1;

        // Find mesh index
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
