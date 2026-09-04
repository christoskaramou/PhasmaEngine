#ifdef PE_AUDIO

#include "AudioWidget.h"
#include "FileSelector.h"
#include "imgui/imgui.h"
#include "Audio/AudioTypes.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Systems/AudioSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void AudioWidget::DrawEmbed(NodeId *node, Scene *scene, bool showAutoplay)
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
        ui::ItemTooltip("Choose the audio file this source will play.");

        ImGui::DragFloat("Volume", &desc->volume, 0.01f, 0.0f, 2.0f);
        ui::ItemTooltip("Scales playback loudness for this source.");
        ImGui::DragFloat("Pitch", &desc->pitch, 0.01f, 0.1f, 3.0f);
        ui::ItemTooltip("Changes playback pitch and speed.");
        ImGui::Checkbox("Loop", &desc->loop);
        ui::ItemTooltip("Restart playback automatically when the clip ends.");
        ImGui::Checkbox("Spatial", &desc->spatial);
        ui::ItemTooltip("Attenuate the sound by listener distance.");
        if (showAutoplay)
        {
            ImGui::Checkbox("Autoplay", &desc->autoplay);
            ui::ItemTooltip("Start this source automatically when play mode begins.");
        }

        if (desc->spatial)
        {
            ImGui::DragFloat("Min Distance", &desc->minDistance, 0.1f, 0.1f, 100.0f);
            ui::ItemTooltip("Distance where the sound is still at full volume.");
            ImGui::DragFloat("Max Distance", &desc->maxDistance, 1.0f, 1.0f, 1000.0f);
            ui::ItemTooltip("Distance where attenuation reaches its quietest point.");
        }

        // Preview buttons
        ImGui::Spacing();
        if (ImGui::SmallButton("Preview"))
            as->PlaySource(node);
        ui::ItemTooltip("Play this source immediately in the editor.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop"))
            as->StopSource(node);
        ui::ItemTooltip("Stop the editor preview for this source.");
    }
} // namespace pe

#endif // PE_AUDIO
