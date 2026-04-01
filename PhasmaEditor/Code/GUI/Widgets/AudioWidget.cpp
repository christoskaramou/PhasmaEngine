#ifdef PE_AUDIO

#include "AudioWidget.h"
#include "FileSelector.h"
#include "imgui/imgui.h"
#include "Audio/AudioTypes.h"
#include "GUI/GUI.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Systems/AudioSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void AudioWidget::DrawEmbed(NodeId *node, Scene *scene)
    {
        auto *as = GetGlobalSystem<AudioSystem>();
        if (!as)
            return;

        AudioSourceDesc *desc = as->GetSourceDesc(node);
        if (!desc)
            return;

        // File path display + browse button
        std::string displayPath = desc->filePath.empty() ? "(none)" : desc->filePath;
        // Truncate to filename for display
        auto pos = displayPath.find_last_of("/\\");
        if (pos != std::string::npos)
            displayPath = displayPath.substr(pos + 1);

        ImGui::Text("File: %s", displayPath.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Browse##audio"))
        {
            NodeId *capturedNode = node;
            if (auto *fs = m_gui->GetWidget<FileSelector>())
            {
                fs->OpenSelection([capturedNode](const std::string &path) -> bool
                                  {
                    auto *audioSys = GetGlobalSystem<AudioSystem>();
                    if (audioSys)
                    {
                        if (auto *d = audioSys->GetSourceDesc(capturedNode))
                            d->filePath = path;
                    }
                    return true; },
                                  {".wav", ".mp3", ".flac", ".ogg"});
            }
        }

        ImGui::DragFloat("Volume", &desc->volume, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Pitch", &desc->pitch, 0.01f, 0.1f, 3.0f);
        ImGui::Checkbox("Loop", &desc->loop);
        ImGui::Checkbox("Spatial", &desc->spatial);
        ImGui::Checkbox("Autoplay", &desc->autoplay);

        if (desc->spatial)
        {
            ImGui::DragFloat("Min Distance", &desc->minDistance, 0.1f, 0.1f, 100.0f);
            ImGui::DragFloat("Max Distance", &desc->maxDistance, 1.0f, 1.0f, 1000.0f);
        }

        // Preview buttons
        ImGui::Spacing();
        if (ImGui::SmallButton("Preview"))
            as->PlaySource(node);
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop"))
            as->StopSource(node);

        ImGui::Spacing();
        if (ImGui::SmallButton("Remove Audio"))
            as->RemoveSource(node);
    }
} // namespace pe

#endif // PE_AUDIO
