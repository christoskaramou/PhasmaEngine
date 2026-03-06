#include "Particles.h"
#include "API/Image.h"
#include "Camera/Camera.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "GUI/Widget.h"
#include "GUI/Widgets/FileSelector.h"
#include "Particles/ParticleManager.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_vulkan.h"

namespace pe
{
    void Particles::DrawEmitterControls(ParticleManager *pm, size_t i)
    {
        auto &emitters = pm->GetEmitters();
        if (i >= emitters.size())
            return;

        ParticleEmitter &emitter = emitters[i];
        bool changed = false;

        // Particle Count (Effective Reset)
        int count = static_cast<int>(emitter.count);
        if (ImGui::DragInt("Particle Count", &count, 1.0f, 0, 100000))
        {
            if (count < 0)
                count = 0;
            emitter.count = static_cast<uint32_t>(count);
            changed = true;
        }

        // Position
        if (ImGui::DragFloat3("Position", &emitter.position.x, 0.01f))
            changed = true;
        if (ImGui::DragFloat3("Velocity", &emitter.velocity.x, 0.01f))
            changed = true;
        if (ImGui::DragFloat3("Gravity", &emitter.gravity.x, 0.01f))
            changed = true;

        ImGui::Separator();

        if (ImGui::ColorEdit4("Color Start", &emitter.colorStart.x))
            changed = true;
        if (ImGui::ColorEdit4("Color End", &emitter.colorEnd.x))
            changed = true;

        ImGui::Separator();

        // Size & Life
        float size[2] = {emitter.sizeLife.x, emitter.sizeLife.y};
        if (ImGui::DragFloat2("Size (Start/End)", size, 0.001f, 0.0f, 100.0f))
        {
            emitter.sizeLife.x = size[0];
            emitter.sizeLife.y = size[1];
            changed = true;
        }

        float life[2] = {emitter.sizeLife.z, emitter.sizeLife.w};
        if (ImGui::DragFloat2("Life (Min/Max)", life, 0.001f, 0.0f, 100.0f))
        {
            emitter.sizeLife.z = life[0];
            emitter.sizeLife.w = life[1];
            changed = true;
        }

        ImGui::Separator();

        // Physics
        if (ImGui::DragFloat("Spawn Rate", &emitter.physics.x, 0.01f, 0.0f, 10000.0f))
            changed = true;
        if (ImGui::DragFloat("Spawn Radius", &emitter.physics.y, 0.01f, 0.0f, 100.0f))
            changed = true;
        if (ImGui::DragFloat("Noise Strength", &emitter.physics.z, 0.01f, 0.0f, 10.0f))
            changed = true;
        if (ImGui::DragFloat("Drag", &emitter.physics.w, 0.01f, 0.0f, 5.0f))
            changed = true;

        ImGui::Separator();

        // Orientation
        const char *orientations[] = {"Billboard", "Horizontal", "Vertical", "Velocity"};
        int currentOrientation = static_cast<int>(emitter.orientation);
        if (ImGui::Combo("Orientation", &currentOrientation, orientations, IM_ARRAYSIZE(orientations)))
        {
            emitter.orientation = static_cast<uint32_t>(currentOrientation);
            changed = true;
        }

        // Animation
        if (ImGui::DragFloat2("Anim Rows/Cols", &emitter.animation.x, 1.0f, 1.0f, 64.0f, "%.0f"))
        {
            if (emitter.animation.x < 1.0f)
                emitter.animation.x = 1.0f;
            if (emitter.animation.y < 1.0f)
                emitter.animation.y = 1.0f;
            changed = true;
        }
        if (ImGui::DragFloat("Anim Speed", &emitter.animation.z, 0.1f, 0.0f, 10.0f))
            changed = true;

        bool interpolate = emitter.animation.w > 0.5f;
        if (ImGui::Checkbox("Interpolate Frames", &interpolate))
        {
            emitter.animation.w = interpolate ? 1.0f : 0.0f;
            changed = true;
        }

        ImGui::Separator();

        // Texture Selection & Loading
        const auto &texNames = pm->GetTextureNames();
        int currentItem = static_cast<int>(emitter.textureIndex);
        if (texNames.empty())
            currentItem = -1;
        else if (currentItem >= static_cast<int>(texNames.size()))
            currentItem = 0;

        // Clickable Image Preview / Load Button
        void *textureID = nullptr;
        if (currentItem >= 0 && currentItem < static_cast<int>(pm->GetTextures().size()))
        {
            Image *img = pm->GetTextures()[currentItem];
            if (img && img->GetSRV() && img->GetSampler())
            {
                void *imgPtr = (void *)img;
                if (m_textureCache.find(imgPtr) == m_textureCache.end())
                {
                    m_textureCache[imgPtr] = (void *)ImGui_ImplVulkan_AddTexture(
                        img->GetSampler()->ApiHandle(),
                        img->GetSRV()->ApiHandle(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
                textureID = m_textureCache[imgPtr];
            }
        }

        float imgSize = 32.0f;
        bool clicked = false;
        if (textureID)
        {
            ImGui::Image((ImTextureID)textureID, ImVec2(imgSize, imgSize), ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 0.5f));
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Image((ImTextureID)textureID, ImVec2(256, 256));
                ImGui::EndTooltip();
            }
            if (ImGui::IsItemClicked())
                clicked = true;
        }
        else
        {
            if (ImGui::Button("Load", ImVec2(imgSize, imgSize)))
                clicked = true;
        }
        ImGui::SameLine();

        // Load Logic
        if (clicked)
        {
            auto *fs = m_gui->GetWidget<FileSelector>();
            if (fs)
            {
                uint32_t emitterIdx = static_cast<uint32_t>(i);
                fs->OpenSelection([emitterIdx, pm](const std::string &path)
                                  {
                        uint32_t newTexIndex = pm->LoadTexture(path);
                        if (emitterIdx < pm->GetEmitters().size())
                        {
                            pm->GetEmitters()[emitterIdx].textureIndex = newTexIndex;
                            pm->UpdateEmitterBuffer();
                        } },
                                  {".png", ".jpg", ".jpeg", ".tga"});
            }
        }

        // Combo Box
        if (!texNames.empty())
        {
            if (ImGui::Combo("Texture", &currentItem, [](void *data, int idx, const char **out_text)
                             {
                    auto& vec = *static_cast<const std::vector<std::string>*>(data);
                    *out_text = vec[idx].c_str();
                    return true; }, (void *)&texNames, static_cast<int>(texNames.size())))
            {
                emitter.textureIndex = static_cast<uint32_t>(currentItem);
                changed = true;
            }
        }
        else
        {
            ImGui::TextDisabled("No Textures Loaded");
        }

        if (changed)
            pm->UpdateEmitterBuffer();
    }

    void Particles::DrawEmbed(ParticleManager *pm, int emitterIndex)
    {
        if (!pm || emitterIndex < 0 || emitterIndex >= static_cast<int>(pm->GetEmitters().size()))
            return;

        auto &names = pm->GetEmitterNames();
        if (emitterIndex < static_cast<int>(names.size()))
            ImGui::Text("Emitter: %s", names[emitterIndex].c_str());

        DrawEmitterControls(pm, static_cast<size_t>(emitterIndex));
    }

    void Particles::Update()
    {
        if (!m_open)
            return;

        static bool particlesSized = false;
        if (!particlesSized)
        {
            ui::SetInitialWindowSizeFraction(1.0f / 6.0f, 0.4f, ImGuiCond_Always);
            particlesSized = true;
        }
        else
        {
            ui::SetInitialWindowSizeFraction(1.0f / 6.0f, 0.4f, ImGuiCond_Appearing);
        }

        ImGui::Begin("Particles", &m_open);

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        if (!renderer)
        {
            ImGui::TextDisabled("Renderer system not available");
            ImGui::End();
            return;
        }

        Scene &scene = renderer->GetScene();
        ParticleManager *particleManager = scene.GetParticleManager();
        if (!particleManager)
        {
            ImGui::TextDisabled("Particle manager not available");
            ImGui::End();
            return;
        }

        // Display particle count
        uint32_t particleCount = particleManager->GetParticleCount();
        ImGui::Text("Particle Count: %u", particleCount);
        ImGui::Separator();

        // Emitters section
        auto &emitters = particleManager->GetEmitters();
        auto &names = particleManager->GetEmitterNames();
        ImGui::Text("Emitters: %zu", emitters.size());

        if (ImGui::Button("Add Emitter"))
        {
            Camera *camera = scene.GetActiveCamera();
            vec3 spawnPos = camera ? (camera->GetPosition() + camera->GetFront() * 5.0f) : vec3(0.0f);

            ParticleEmitter newEmitter;
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
            particleManager->UpdateEmitterBuffer();
        }

        ImGui::Separator();

        // List of emitters
        if (ImGui::BeginChild("##emitters_list", ImVec2(0, 0), true))
        {
            for (size_t i = 0; i < emitters.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));

                std::string label = (i < names.size()) ? names[i] : ("Emitter " + std::to_string(i));
                bool emitterOpen = ImGui::TreeNodeEx(
                    label.c_str(),
                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);

                // Delete button
                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem("Delete Emitter"))
                    {
                        emitters.erase(emitters.begin() + i);
                        if (i < names.size())
                            names.erase(names.begin() + i);
                        particleManager->UpdateEmitterBuffer();
                        ImGui::EndPopup();
                        ImGui::PopID();
                        if (emitterOpen)
                            ImGui::TreePop();
                        break;
                    }
                    ImGui::EndPopup();
                }

                if (emitterOpen)
                {
                    DrawEmitterControls(particleManager, i);
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }

            if (emitters.empty())
            {
                ImGui::TextDisabled("No emitters. Click 'Add Emitter' to create one.");
            }
        }
        ImGui::EndChild();

        ImGui::End();
    }
} // namespace pe
