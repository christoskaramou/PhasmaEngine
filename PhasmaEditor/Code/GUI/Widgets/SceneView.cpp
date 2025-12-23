#include "SceneView.h"
#include "GUI/GUI.h"
#include "GUI/GUIState.h"
#include "Systems/RendererSystem.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_internal.h"

namespace pe
{
    void SceneView::Update()
    {
        if (GUIState::s_sceneViewFloating)
        {
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        }
        else if (GUIState::s_sceneViewRedockQueued && m_gui->GetDockspaceId() != 0)
        {
            ImGui::DockBuilderDockWindow("Scene View", m_gui->GetDockspaceId());
            GUIState::s_sceneViewRedockQueued = false;
        }

        ImGuiWindowFlags sceneViewFlags = 0;
        if (GUIState::s_sceneViewFloating)
            sceneViewFlags |= ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Scene View", nullptr, sceneViewFlags);
        ImGui::PopStyleVar();

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Image *displayRT = renderer->GetDisplayRT();

        auto recreateSceneTexture = [renderer, displayRT]()
        {
            if (!displayRT)
                return;

            Image::Destroy(GUIState::s_sceneViewImage);
            GUIState::s_sceneViewImage = renderer->CreateFSSampledImage(false);

            if (GUIState::s_viewportTextureId)
            {
                ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)GUIState::s_viewportTextureId);
                GUIState::s_viewportTextureId = nullptr;
            }
        };

        if (!GUIState::s_sceneViewImage || !displayRT ||
            GUIState::s_sceneViewImage->GetWidth() != displayRT->GetWidth() ||
            GUIState::s_sceneViewImage->GetHeight() != displayRT->GetHeight())
        {
            recreateSceneTexture();
        }

        Image *sceneTexture = GUIState::s_sceneViewImage ? GUIState::s_sceneViewImage : displayRT;

        // Ensure we have a valid render target with sampler and SRV
        if (!sceneTexture || !sceneTexture->HasSRV() || !sceneTexture->GetSampler())
        {
            ImGui::TextDisabled("Initializing viewport...");
            ImGui::End();
            return;
        }

        // Create descriptor set for the render target if not already created
        if (!GUIState::s_viewportTextureId)
        {
            VkSampler sampler = sceneTexture->GetSampler()->ApiHandle();
            VkImageView imageView = sceneTexture->GetSRV();

            if (sampler && imageView)
            {
                GUIState::s_viewportTextureId = (void *)ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }

        if (GUIState::s_viewportTextureId)
        {
            // Get available content region
            ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();

            if (viewportPanelSize.x > 0 && viewportPanelSize.y > 0)
            {
                // Calculate aspect-preserving size
                float targetAspect = (float)sceneTexture->GetWidth() / (float)sceneTexture->GetHeight();
                float panelAspect = viewportPanelSize.x / viewportPanelSize.y;

                ImVec2 imageSize;
                if (panelAspect > targetAspect)
                {
                    // Panel is wider, fit to height
                    imageSize.y = viewportPanelSize.y;
                    imageSize.x = imageSize.y * targetAspect;
                }
                else
                {
                    // Panel is taller, fit to width
                    imageSize.x = viewportPanelSize.x;
                    imageSize.y = imageSize.x / targetAspect;
                }

                // Center the image
                ImVec2 cursorPos = ImGui::GetCursorPos();
                cursorPos.x += (viewportPanelSize.x - imageSize.x) * 0.5f;
                cursorPos.y += (viewportPanelSize.y - imageSize.y) * 0.5f;
                ImGui::SetCursorPos(cursorPos);

                ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(GUIState::s_viewportTextureId)), imageSize);
            }
        }
        else
        {
            ImGui::TextDisabled("Rendering...");
        }

        ImGui::End();
    }
} // namespace pe
