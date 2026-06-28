#include "SpriteEditor.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "GUI/GUI.h"
#include "GUI/GUIState.h"
#include "GUI/Helpers.h"
#include "GUI/Widgets/FileSelector.h"
#include "imgui/imgui.h"
#include <nlohmann/json.hpp>

namespace pe
{
    namespace
    {
        constexpr int kNoFrameLimit = 0;

        int SafeInt(const nlohmann::json &value, int fallback)
        {
            return value.is_number_integer() ? value.get<int>() : fallback;
        }

        float SafeFloat(const nlohmann::json &value, float fallback)
        {
            return value.is_number() ? value.get<float>() : fallback;
        }

        bool SafeBool(const nlohmann::json &value, bool fallback)
        {
            return value.is_boolean() ? value.get<bool>() : fallback;
        }

        ImTextureID TextureId(void *textureId)
        {
            return (ImTextureID)(intptr_t)textureId;
        }

        ImVec2 FitImageSize(float imageW, float imageH, const ImVec2 &available, float zoom = 1.0f)
        {
            if (imageW <= 0.0f || imageH <= 0.0f)
                return ImVec2(0.0f, 0.0f);

            const float scaleX = available.x > 0.0f ? available.x / imageW : 1.0f;
            const float scaleY = available.y > 0.0f ? available.y / imageH : 1.0f;
            const float scale = std::max(0.05f, std::min(scaleX, scaleY)) * std::max(0.1f, zoom);
            return ImVec2(std::max(1.0f, imageW * scale), std::max(1.0f, imageH * scale));
        }

        void DrawCross(ImDrawList *drawList, const ImVec2 &p, ImU32 color, float radius)
        {
            drawList->AddLine(ImVec2(p.x - radius, p.y), ImVec2(p.x + radius, p.y), color, 1.5f);
            drawList->AddLine(ImVec2(p.x, p.y - radius), ImVec2(p.x, p.y + radius), color, 1.5f);
        }

        template <size_t N>
        void CopyTextToBuffer(std::array<char, N> &buffer, const std::string &value)
        {
            buffer.fill('\0');
            const size_t count = std::min(value.size(), N - 1);
            std::copy_n(value.data(), count, buffer.data());
        }

        std::filesystem::path U8Path(const std::string &value)
        {
            return std::filesystem::path(reinterpret_cast<const char8_t *>(value.c_str()));
        }

        std::filesystem::path PayloadPath(const ImGuiPayload *payload)
        {
            if (!payload || !payload->Data || payload->DataSize <= 1)
                return {};
            return U8Path(std::string(static_cast<const char *>(payload->Data), static_cast<size_t>(payload->DataSize - 1)));
        }
    } // namespace

    SpriteEditor::SpriteEditor() : Widget("Sprite Editor")
    {
        m_open = false;
        CopyToBuffer(m_metadataPath, "Sprites/sprite_sheet.sprite.json");
        m_clips.push_back({"default", 0, 0, 10.0f, true});
    }

    SpriteEditor::~SpriteEditor()
    {
        ReleaseImage();
    }

    void SpriteEditor::Update()
    {
        if (!m_open)
            return;

        UpdatePlayback();

        ui::SetInitialWindowSizeFraction(0.6f, 0.7f);
        if (!ImGui::Begin("Sprite Editor", &m_open))
        {
            ImGui::End();
            return;
        }

        DrawSourcePanel();
        ImGui::Separator();

        const float sideWidth = std::min(420.0f, std::max(300.0f, ImGui::GetContentRegionAvail().x * 0.36f));
        ImGui::BeginChild("##sprite_editor_side", ImVec2(sideWidth, 0), true);
        DrawFramePanel();
        ImGui::Separator();
        DrawClipPanel();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##sprite_editor_preview", ImVec2(0, 0), true);
        DrawAnimationPreview();
        ImGui::Separator();
        DrawAtlasPanel();
        ImGui::EndChild();

        ImGui::End();
    }

    void SpriteEditor::DrawSourcePanel()
    {
        ImGui::TextUnformatted("Sheet Image");
        ImGui::SameLine();
        const float sourceWidth = std::max(220.0f, ImGui::GetContentRegionAvail().x * 0.42f);
        ImGui::SetNextItemWidth(sourceWidth);
        const std::string imageLabel = BufferString(m_imagePath).empty() ? "<select image or sheet asset>" : BufferString(m_imagePath);
        if (ImGui::Button((imageLabel + "##sprite_image_picker").c_str(), ImVec2(sourceWidth, 0.0f)))
            OpenSourceDialog();
        ui::ItemTooltip("Choose a source image or .sheet.json asset.");
        HandleSourceDropTarget();

        ImGui::SameLine();
        if (ImGui::Button("Browse"))
            OpenSourceDialog();
        ui::ItemTooltip("Select the sprite sheet image from a dialog.");

        const bool canUsePreview = (GUIState::s_assetPreview.type == AssetPreviewType::Image ||
                                    GUIState::s_assetPreview.type == AssetPreviewType::SpriteSheet) &&
                                   !GUIState::s_assetPreview.fullPath.empty();
        ImGui::SameLine();
        if (!canUsePreview)
            ImGui::BeginDisabled();
        if (ImGui::Button("Use Selected") && canUsePreview)
            LoadSourceAsset(U8Path(GUIState::s_assetPreview.fullPath));
        ui::ItemTooltip("Use the current image or sprite sheet asset selected from the File Browser.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        if (!canUsePreview)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("New Sheet Asset"))
            OpenCreateSheetDialog();
        ui::ItemTooltip("Create a .sheet.json asset from multiple image files.");

        if (!m_sheetImages.empty())
        {
            ImGui::TextUnformatted("Sheet Asset");
            ImGui::SameLine();
            ImGui::TextDisabled("%s", BufferString(m_sheetPath).empty() ? "<unsaved>" : BufferString(m_sheetPath).c_str());

            std::vector<const char *> labels;
            labels.reserve(m_sheetImages.size());
            for (const auto &image : m_sheetImages)
                labels.push_back(image.label.c_str());

            ImGui::TextUnformatted("Image");
            ImGui::SameLine();
            const ImGuiStyle &style = ImGui::GetStyle();
            const float addImagesWidth = ImGui::CalcTextSize("Add Images").x + style.FramePadding.x * 2.0f;
            const float saveSheetWidth = ImGui::CalcTextSize("Save Sheet").x + style.FramePadding.x * 2.0f;
            const float sheetControlWidth = addImagesWidth + saveSheetWidth + style.ItemSpacing.x * 2.0f;
            ImGui::SetNextItemWidth(std::max(180.0f, ImGui::GetContentRegionAvail().x - sheetControlWidth));
            int selectedImage = std::clamp(m_selectedSheetImage, 0, static_cast<int>(m_sheetImages.size()) - 1);
            if (ImGui::Combo("##sprite_sheet_image", &selectedImage, labels.data(), static_cast<int>(labels.size())))
                SelectSheetImage(selectedImage);

            ImGui::SameLine();
            if (ImGui::SmallButton("Add Images"))
                OpenAddSheetImagesDialog();
            ui::ItemTooltip("Append images to this sheet asset.");

            ImGui::SameLine();
            if (ImGui::SmallButton("Save Sheet"))
                SaveSheetAsset();
            ui::ItemTooltip("Save this sheet image list.");

            DrawSheetImagesEditor();
        }

        ImGui::TextUnformatted("Metadata");
        ImGui::SameLine();
        const ImGuiStyle &style = ImGui::GetStyle();
        const float loadJsonWidth = ImGui::CalcTextSize("Load JSON").x + style.FramePadding.x * 2.0f;
        const float saveJsonWidth = ImGui::CalcTextSize("Save JSON").x + style.FramePadding.x * 2.0f;
        const float metadataControlsWidth = loadJsonWidth + saveJsonWidth + style.ItemSpacing.x * 2.0f;
        ImGui::SetNextItemWidth(std::max(220.0f, ImGui::GetContentRegionAvail().x - metadataControlsWidth));
        ImGui::InputText("##sprite_metadata_path", m_metadataPath.data(), m_metadataPath.size());

        ImGui::SameLine();
        if (ImGui::Button("Load JSON"))
            LoadMetadataFromBuffer();
        ui::ItemTooltip("Load sprite metadata from an asset-relative or absolute JSON path.");

        ImGui::SameLine();
        if (ImGui::Button("Save JSON"))
            SaveMetadata();
        ui::ItemTooltip("Save the current sprite frames and clips as JSON.");

        if (m_image)
        {
            ImGui::TextDisabled("Image: %ux%u", m_image->GetWidth(), m_image->GetHeight());
            ImGui::SameLine();
        }
        if (!m_status.empty())
            ImGui::TextWrapped("%s", m_status.c_str());
    }

    void SpriteEditor::DrawSheetImagesEditor()
    {
        if (m_sheetImages.empty())
            return;

        if (!ImGui::BeginTable("##sprite_sheet_images", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
            return;

        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 84.0f);
        ImGui::TableHeadersRow();

        int removeIndex = -1;
        int moveFrom = -1;
        int moveTo = -1;
        for (int i = 0; i < static_cast<int>(m_sheetImages.size()); ++i)
        {
            SheetImage &image = m_sheetImages[i];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetFrameHeight());
            ImGui::TableNextColumn();
            if (ImGui::Selectable(std::to_string(i).c_str(), i == m_selectedSheetImage, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0.0f, ImGui::GetFrameHeight())))
                SelectSheetImage(i);

            ImGui::TableNextColumn();
            std::array<char, 256> label{};
            CopyTextToBuffer(label, image.label);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText(("##sheet_label_" + std::to_string(i)).c_str(), label.data(), label.size()))
                image.label = label.data();

            ImGui::TableNextColumn();
            const std::string relativePath = MakeAssetRelative(image.path);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", relativePath.c_str());
            ui::ItemTooltip(relativePath.c_str());

            ImGui::TableNextColumn();
            constexpr float kSheetButtonWidth = 22.0f;
            if (i <= 0)
                ImGui::BeginDisabled();
            if (ImGui::Button(("^##sheet_up_" + std::to_string(i)).c_str(), ImVec2(kSheetButtonWidth, 0.0f)) && i > 0)
            {
                moveFrom = i;
                moveTo = i - 1;
            }
            ui::ItemTooltip("Move up.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
            if (i <= 0)
                ImGui::EndDisabled();

            ImGui::SameLine(0.0f, 2.0f);
            if (i + 1 >= static_cast<int>(m_sheetImages.size()))
                ImGui::BeginDisabled();
            if (ImGui::Button(("v##sheet_down_" + std::to_string(i)).c_str(), ImVec2(kSheetButtonWidth, 0.0f)) && i + 1 < static_cast<int>(m_sheetImages.size()))
            {
                moveFrom = i;
                moveTo = i + 1;
            }
            ui::ItemTooltip("Move down.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
            if (i + 1 >= static_cast<int>(m_sheetImages.size()))
                ImGui::EndDisabled();

            ImGui::SameLine(0.0f, 2.0f);
            if (ImGui::Button(("X##sheet_remove_" + std::to_string(i)).c_str(), ImVec2(kSheetButtonWidth, 0.0f)))
                removeIndex = i;
            ui::ItemTooltip("Remove image from sheet.");
        }
        ImGui::EndTable();

        if (moveFrom >= 0 && moveTo >= 0)
        {
            std::swap(m_sheetImages[moveFrom], m_sheetImages[moveTo]);
            if (m_selectedSheetImage == moveFrom)
                m_selectedSheetImage = moveTo;
            else if (m_selectedSheetImage == moveTo)
                m_selectedSheetImage = moveFrom;
        }

        if (removeIndex >= 0)
        {
            m_sheetImages.erase(m_sheetImages.begin() + removeIndex);
            if (m_sheetImages.empty())
            {
                m_selectedSheetImage = -1;
                m_status = "Sheet image removed.";
            }
            else
            {
                SelectSheetImage(std::clamp(m_selectedSheetImage, 0, static_cast<int>(m_sheetImages.size()) - 1));
                m_status = "Sheet image removed.";
            }
        }
    }

    void SpriteEditor::DrawFramePanel()
    {
        ImGui::TextUnformatted("Grid");
        ImGui::SetNextItemWidth(86.0f);
        ImGui::DragInt("Frame W", &m_gridFrameWidth, 1.0f, 1, 8192);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(86.0f);
        ImGui::DragInt("Frame H", &m_gridFrameHeight, 1.0f, 1, 8192);

        ImGui::SetNextItemWidth(86.0f);
        ImGui::DragInt("Margin X", &m_gridMarginX, 1.0f, 0, 8192);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(86.0f);
        ImGui::DragInt("Margin Y", &m_gridMarginY, 1.0f, 0, 8192);

        ImGui::SetNextItemWidth(86.0f);
        ImGui::DragInt("Spacing X", &m_gridSpacingX, 1.0f, 0, 8192);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(86.0f);
        ImGui::DragInt("Spacing Y", &m_gridSpacingY, 1.0f, 0, 8192);

        ImGui::SetNextItemWidth(86.0f);
        ImGui::DragInt("Max Frames", &m_gridMaxFrames, 1.0f, 0, 10000);
        ui::ItemTooltip("0 means generate all frames that fit the sheet.");

        if (ImGui::Button("Generate Grid Frames"))
            GenerateGridFrames();
        ui::ItemTooltip("Replace the frame list with grid slices from the loaded sheet.");

        ImGui::SameLine();
        if (ImGui::Button("Add Frame"))
        {
            SpriteFrame frame;
            frame.name = "frame_" + std::to_string(m_frames.size());
            if (m_image)
            {
                frame.w = std::min(m_gridFrameWidth, static_cast<int>(m_image->GetWidth()));
                frame.h = std::min(m_gridFrameHeight, static_cast<int>(m_image->GetHeight()));
                frame.hitboxW = frame.w;
                frame.hitboxH = frame.h;
            }
            m_frames.push_back(frame);
            m_selectedFrame = static_cast<int>(m_frames.size()) - 1;
            ClampSelection();
        }

        ImGui::Spacing();
        ImGui::Text("Frames: %d", static_cast<int>(m_frames.size()));

        if (ImGui::BeginChild("##sprite_frame_list", ImVec2(0, 160.0f), true))
        {
            if (ImGui::BeginTable("##sprite_frames_table", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Rect", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Sec", ImGuiTableColumnFlags_WidthFixed, 52.0f);
                ImGui::TableHeadersRow();

                for (int i = 0; i < static_cast<int>(m_frames.size()); ++i)
                {
                    auto &frame = m_frames[i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", i);
                    ImGui::TableNextColumn();
                    const bool selected = i == m_selectedFrame;
                    if (ImGui::Selectable(frame.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                    {
                        m_selectedFrame = i;
                        m_previewFrame = i;
                        m_playing = false;
                    }
                    ImGui::TableNextColumn();
                    ImGui::Text("%d,%d %dx%d", frame.x, frame.y, frame.w, frame.h);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.2f", frame.duration);
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();

        ClampSelection();
        if (m_selectedFrame < 0 || m_selectedFrame >= static_cast<int>(m_frames.size()))
        {
            ImGui::TextDisabled("Select or generate a frame to edit metadata.");
            return;
        }

        SpriteFrame &frame = m_frames[m_selectedFrame];
        ImGui::SeparatorText("Selected Frame");

        std::array<char, 128> name{};
        CopyTextToBuffer(name, frame.name);
        if (ImGui::InputText("Name", name.data(), name.size()))
            frame.name = name.data();

        int rect[4] = {frame.x, frame.y, frame.w, frame.h};
        if (ImGui::DragInt4("Rect", rect, 1.0f, 0, 8192))
        {
            frame.x = rect[0];
            frame.y = rect[1];
            frame.w = std::max(1, rect[2]);
            frame.h = std::max(1, rect[3]);
            ClampFrame(frame);
        }

        float pivot[2] = {frame.pivotX, frame.pivotY};
        if (ImGui::DragFloat2("Pivot", pivot, 0.01f, 0.0f, 1.0f, "%.2f"))
        {
            frame.pivotX = std::clamp(pivot[0], 0.0f, 1.0f);
            frame.pivotY = std::clamp(pivot[1], 0.0f, 1.0f);
        }
        ImGui::DragFloat("Duration", &frame.duration, 0.01f, 0.01f, 10.0f, "%.3f s");

        ImGui::Checkbox("Hitbox", &frame.hitboxEnabled);
        int hitbox[4] = {frame.hitboxX, frame.hitboxY, frame.hitboxW, frame.hitboxH};
        if (!frame.hitboxEnabled)
            ImGui::BeginDisabled();
        if (ImGui::DragInt4("Hitbox Rect", hitbox, 1.0f, -8192, 8192))
        {
            frame.hitboxX = hitbox[0];
            frame.hitboxY = hitbox[1];
            frame.hitboxW = std::max(1, hitbox[2]);
            frame.hitboxH = std::max(1, hitbox[3]);
        }
        if (!frame.hitboxEnabled)
            ImGui::EndDisabled();

        if (ImGui::Button("Duplicate"))
        {
            SpriteFrame copy = frame;
            copy.name += "_copy";
            m_frames.insert(m_frames.begin() + m_selectedFrame + 1, copy);
            ++m_selectedFrame;
            ClampSelection();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
        {
            m_frames.erase(m_frames.begin() + m_selectedFrame);
            m_selectedFrame = std::min(m_selectedFrame, static_cast<int>(m_frames.size()) - 1);
            ClampSelection();
        }
    }

    void SpriteEditor::DrawClipPanel()
    {
        ImGui::SeparatorText("Clips");
        if (m_clips.empty())
            m_clips.push_back({"default", 0, std::max(0, static_cast<int>(m_frames.size()) - 1), 10.0f, true});

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##sprite_clip_combo", m_clips[m_selectedClip].name.c_str()))
        {
            for (int i = 0; i < static_cast<int>(m_clips.size()); ++i)
            {
                const bool selected = i == m_selectedClip;
                if (ImGui::Selectable(m_clips[i].name.c_str(), selected))
                {
                    m_selectedClip = i;
                    m_playAccumulator = 0.0f;
                    m_previewFrame = m_clips[i].start;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        SpriteClip &clip = m_clips[m_selectedClip];
        std::array<char, 128> name{};
        CopyTextToBuffer(name, clip.name);
        if (ImGui::InputText("Clip Name", name.data(), name.size()))
            clip.name = name.data();

        const int maxFrame = std::max(0, static_cast<int>(m_frames.size()) - 1);
        ImGui::DragInt("Start", &clip.start, 1.0f, 0, maxFrame);
        ImGui::DragInt("End", &clip.end, 1.0f, 0, maxFrame);
        if (clip.start > clip.end)
            std::swap(clip.start, clip.end);
        ImGui::DragFloat("FPS", &clip.fps, 0.1f, 0.1f, 120.0f, "%.1f");
        ImGui::Checkbox("Loop", &clip.loop);

        if (ImGui::Button(m_playing ? "Pause" : "Play"))
        {
            m_playing = !m_playing;
            if (m_previewFrame < clip.start || m_previewFrame > clip.end)
                m_previewFrame = clip.start;
        }
        ImGui::SameLine();
        if (ImGui::Button("Step"))
        {
            m_playing = false;
            m_previewFrame = m_previewFrame < clip.start || m_previewFrame > clip.end ? clip.start : m_previewFrame + 1;
            if (m_previewFrame > clip.end)
                m_previewFrame = clip.loop ? clip.start : clip.end;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Clip"))
        {
            const int start = std::max(0, m_selectedFrame);
            m_clips.push_back({"clip_" + std::to_string(m_clips.size()), start, start, 10.0f, true});
            m_selectedClip = static_cast<int>(m_clips.size()) - 1;
            m_previewFrame = start;
        }
        ImGui::SameLine();
        if (m_clips.size() <= 1)
            ImGui::BeginDisabled();
        if (ImGui::Button("Delete Clip") && m_clips.size() > 1)
        {
            m_clips.erase(m_clips.begin() + m_selectedClip);
            m_selectedClip = std::min(m_selectedClip, static_cast<int>(m_clips.size()) - 1);
        }
        if (m_clips.size() <= 1)
            ImGui::EndDisabled();
    }

    void SpriteEditor::DrawAtlasPanel()
    {
        ImGui::TextUnformatted("Atlas");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("Zoom", &m_atlasZoom, 0.25f, 8.0f, "%.2fx");

        if (!m_image || !m_textureId)
        {
            ImGui::TextDisabled("Load a sprite sheet to inspect frames.");
            return;
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.y = std::max(180.0f, avail.y);
        ImVec2 drawSize = FitImageSize(m_image->GetWidth_f(), m_image->GetHeight_f(), avail, m_atlasZoom);
        ImVec2 imageMin = ImGui::GetCursorScreenPos();
        ImGui::Image(TextureId(m_textureId), drawSize);
        ImVec2 imageMax(imageMin.x + drawSize.x, imageMin.y + drawSize.y);

        const bool hovered = ImGui::IsItemHovered();
        const float sx = drawSize.x / std::max(1.0f, m_image->GetWidth_f());
        const float sy = drawSize.y / std::max(1.0f, m_image->GetHeight_f());
        ImDrawList *drawList = ImGui::GetWindowDrawList();

        for (int i = 0; i < static_cast<int>(m_frames.size()); ++i)
        {
            const SpriteFrame &frame = m_frames[i];
            const bool selected = i == m_selectedFrame || i == m_previewFrame;
            const ImU32 color = selected ? IM_COL32(80, 210, 255, 255) : IM_COL32(255, 255, 255, 90);
            ImVec2 p0(imageMin.x + frame.x * sx, imageMin.y + frame.y * sy);
            ImVec2 p1(imageMin.x + (frame.x + frame.w) * sx, imageMin.y + (frame.y + frame.h) * sy);
            drawList->AddRect(p0, p1, color, 0.0f, 0, selected ? 2.0f : 1.0f);

            ImVec2 pivot(p0.x + frame.pivotX * frame.w * sx, p0.y + frame.pivotY * frame.h * sy);
            DrawCross(drawList, pivot, IM_COL32(255, 210, 40, selected ? 255 : 120), selected ? 5.0f : 3.0f);

            if (frame.hitboxEnabled)
            {
                ImVec2 h0(p0.x + frame.hitboxX * sx, p0.y + frame.hitboxY * sy);
                ImVec2 h1(h0.x + frame.hitboxW * sx, h0.y + frame.hitboxH * sy);
                drawList->AddRect(h0, h1, IM_COL32(255, 120, 50, selected ? 255 : 130), 0.0f, 0, 1.5f);
            }
        }

        drawList->AddRect(imageMin, imageMax, IM_COL32(255, 255, 255, 160));

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float px = (mouse.x - imageMin.x) / sx;
            const float py = (mouse.y - imageMin.y) / sy;
            for (int i = 0; i < static_cast<int>(m_frames.size()); ++i)
            {
                const SpriteFrame &frame = m_frames[i];
                if (px >= frame.x && px <= frame.x + frame.w && py >= frame.y && py <= frame.y + frame.h)
                {
                    m_selectedFrame = i;
                    m_previewFrame = i;
                    m_playing = false;
                    break;
                }
            }
        }
    }

    void SpriteEditor::DrawAnimationPreview()
    {
        ImGui::TextUnformatted("Preview");
        if (!m_image || !m_textureId || m_frames.empty())
        {
            ImGui::TextDisabled("No frames generated yet.");
            return;
        }

        ClampSelection();
        const int frameIndex = std::clamp(m_previewFrame >= 0 ? m_previewFrame : m_selectedFrame, 0, static_cast<int>(m_frames.size()) - 1);
        const SpriteFrame &frame = m_frames[frameIndex];
        const float imageW = std::max(1.0f, m_image->GetWidth_f());
        const float imageH = std::max(1.0f, m_image->GetHeight_f());
        const ImVec2 uv0(frame.x / imageW, frame.y / imageH);
        const ImVec2 uv1((frame.x + frame.w) / imageW, (frame.y + frame.h) / imageH);

        const float previewHeight = 180.0f;
        const float aspect = frame.h > 0 ? static_cast<float>(frame.w) / static_cast<float>(frame.h) : 1.0f;
        ImVec2 previewSize(std::min(ImGui::GetContentRegionAvail().x, previewHeight * aspect), previewHeight);
        if (previewSize.x <= 0.0f)
            previewSize.x = previewHeight * aspect;

        ImVec2 imageMin = ImGui::GetCursorScreenPos();
        ImGui::Image(TextureId(m_textureId), previewSize, uv0, uv1);
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        ImVec2 imageMax(imageMin.x + previewSize.x, imageMin.y + previewSize.y);
        drawList->AddRect(imageMin, imageMax, IM_COL32(255, 255, 255, 140));

        const ImVec2 pivot(imageMin.x + frame.pivotX * previewSize.x, imageMin.y + frame.pivotY * previewSize.y);
        DrawCross(drawList, pivot, IM_COL32(255, 210, 40, 255), 7.0f);
        if (frame.hitboxEnabled && frame.w > 0 && frame.h > 0)
        {
            const float sx = previewSize.x / frame.w;
            const float sy = previewSize.y / frame.h;
            ImVec2 h0(imageMin.x + frame.hitboxX * sx, imageMin.y + frame.hitboxY * sy);
            ImVec2 h1(h0.x + frame.hitboxW * sx, h0.y + frame.hitboxH * sy);
            drawList->AddRect(h0, h1, IM_COL32(255, 120, 50, 255), 0.0f, 0, 2.0f);
        }

        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("Frame %d", frameIndex);
        ImGui::Text("%s", frame.name.c_str());
        if (m_selectedClip >= 0 && m_selectedClip < static_cast<int>(m_clips.size()))
        {
            const SpriteClip &clip = m_clips[m_selectedClip];
            ImGui::Text("Clip: %s", clip.name.c_str());
            ImGui::Text("%d-%d at %.1f fps", clip.start, clip.end, clip.fps);
        }
        ImGui::EndGroup();
    }

    void SpriteEditor::OpenSourceDialog()
    {
        auto *selector = m_gui ? m_gui->GetWidget<FileSelector>() : nullptr;
        if (!selector)
        {
            m_status = "File selector is not available.";
            return;
        }

        selector->OpenSelection([this](const std::string &path) -> bool
                                { return LoadSourceAsset(U8Path(path)); },
                                SourcePickerExtensions(),
                                Path::Assets,
                                {},
                                {},
                                "Use");
    }

    void SpriteEditor::OpenCreateSheetDialog()
    {
        auto *selector = m_gui ? m_gui->GetWidget<FileSelector>() : nullptr;
        if (!selector)
        {
            m_status = "File selector is not available.";
            return;
        }

        std::vector<std::string> imageExtensions = SourcePickerExtensions();
        imageExtensions.erase(std::remove(imageExtensions.begin(), imageExtensions.end(), ".json"), imageExtensions.end());
        selector->OpenMultiSelection([this](const std::vector<std::string> &paths)
                                     {
                                         std::vector<std::filesystem::path> images;
                                         images.reserve(paths.size());
                                         for (const std::string &path : paths)
                                             images.push_back(U8Path(path));
                                         OpenSaveSheetDialog(std::move(images)); },
                                     imageExtensions,
                                     Path::Assets);
    }

    void SpriteEditor::OpenAddSheetImagesDialog()
    {
        auto *selector = m_gui ? m_gui->GetWidget<FileSelector>() : nullptr;
        if (!selector)
        {
            m_status = "File selector is not available.";
            return;
        }

        std::vector<std::string> imageExtensions = SourcePickerExtensions();
        imageExtensions.erase(std::remove(imageExtensions.begin(), imageExtensions.end(), ".json"), imageExtensions.end());
        selector->OpenMultiSelection([this](const std::vector<std::string> &paths)
                                     {
                                         int added = 0;
                                         for (const std::string &pathString : paths)
                                         {
                                             const std::filesystem::path path = U8Path(pathString).lexically_normal();
                                             if (!HasImageExtension(path))
                                                 continue;

                                             const auto duplicate = std::find_if(m_sheetImages.begin(), m_sheetImages.end(),
                                                                                [&](const SheetImage &image)
                                                                                { return image.path == path; });
                                             if (duplicate != m_sheetImages.end())
                                                 continue;

                                             SheetImage image;
                                             image.path = path;
                                             image.label = MakeAssetRelative(path);
                                             m_sheetImages.push_back(std::move(image));
                                             ++added;
                                         }

                                         if (m_selectedSheetImage < 0 && !m_sheetImages.empty())
                                             SelectSheetImage(0);
                                         m_status = added > 0 ? "Added " + std::to_string(added) + " sheet images." : "No new sheet images added."; },
                                     imageExtensions,
                                     Path::Assets);
    }

    void SpriteEditor::OpenSaveSheetDialog(std::vector<std::filesystem::path> images)
    {
        if (images.empty())
        {
            m_status = "Choose at least one image for a sheet asset.";
            return;
        }

        auto *selector = m_gui ? m_gui->GetWidget<FileSelector>() : nullptr;
        if (!selector)
        {
            m_status = "File selector is not available.";
            return;
        }

        std::filesystem::path defaultPath = std::filesystem::path(Path::Assets) / "Sprites";
        std::error_code ec;
        std::filesystem::create_directories(defaultPath, ec);

        std::string defaultName = "sprite_sheet.sheet.json";
        if (!images.front().stem().empty())
            defaultName = images.front().stem().generic_string() + ".sheet.json";

        selector->OpenSelection([this, images = std::move(images)](const std::string &pathString) -> bool
                                {
                                    std::filesystem::path path = U8Path(pathString);
                                    if (!IsSheetAssetFile(path))
                                    {
                                        if (ToLower(path.extension().string()) == ".json")
                                            path.replace_filename(path.stem().generic_string() + ".sheet.json");
                                        else
                                            path += ".sheet.json";
                                    }

                                    std::vector<SheetImage> sheetImages;
                                    sheetImages.reserve(images.size());
                                    for (const auto &imagePath : images)
                                        sheetImages.push_back({imagePath.lexically_normal(), MakeAssetRelative(imagePath)});

                                    if (!SaveSheetAssetAs(path, sheetImages))
                                        return false;

                                    LoadSheetAsset(path, 0);
                                    return true; },
                                {".json"},
                                defaultPath.string(),
                                {},
                                defaultName,
                                "Save");
    }

    bool SpriteEditor::HandleSourceDropTarget()
    {
        if (!ImGui::BeginDragDropTarget())
            return false;

        bool accepted = false;
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            accepted = LoadSourceAsset(PayloadPath(payload));
        }
        else if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
        {
            accepted = LoadSourceAsset(PayloadPath(payload));
        }
        ImGui::EndDragDropTarget();
        return accepted;
    }

    bool SpriteEditor::LoadSourceAsset(const std::filesystem::path &path)
    {
        if (HasImageExtension(path))
        {
            m_loadedSheetPath.clear();
            m_sheetImages.clear();
            m_selectedSheetImage = -1;
            SetSheetPath({});
            LoadImage(path);
            return true;
        }

        if (IsSheetAssetFile(path))
            return LoadSheetAsset(path, 0);

        m_status = "Select an image file or .sheet.json asset.";
        return false;
    }

    void SpriteEditor::LoadImageFromBuffer()
    {
        LoadImage(ResolveAssetPath(m_imagePath.data()));
    }

    void SpriteEditor::LoadImage(const std::filesystem::path &path)
    {
        if (path.empty())
        {
            m_status = "No sprite sheet path set.";
            return;
        }
        if (!std::filesystem::exists(path))
        {
            m_status = "Sprite sheet not found: " + PathToUtf8(path);
            return;
        }
        if (!HasImageExtension(path))
        {
            m_status = "Sprite sheet must be an image file.";
            return;
        }

        Queue *queue = RHII.GetMainQueue();
        if (!queue)
        {
            m_status = "Main queue not available.";
            return;
        }

        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        Image *image = Image::LoadRGBA8(cmd, PathToUtf8(path));
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();

        if (!image)
        {
            m_status = "Failed to load sprite sheet: " + PathToUtf8(path);
            return;
        }

        ReleaseImage();
        m_image = image;
        m_textureId = m_gui ? m_gui->RegisterImageTexture(m_image) : nullptr;
        m_loadedImagePath = path;
        SetImagePath(path);
        SuggestMetadataPath();

        if (m_gridFrameWidth <= 0)
            m_gridFrameWidth = static_cast<int>(m_image->GetWidth());
        if (m_gridFrameHeight <= 0)
            m_gridFrameHeight = static_cast<int>(m_image->GetHeight());

        m_status = "Loaded " + MakeAssetRelative(path);
    }

    bool SpriteEditor::LoadSheetAsset(const std::filesystem::path &path, int preferredImageIndex)
    {
        if (path.empty() || !std::filesystem::exists(path))
        {
            m_status = "Sheet asset not found: " + PathToUtf8(path);
            return false;
        }
        if (!IsSheetAssetFile(path))
        {
            m_status = "Sheet asset must use .sheet.json.";
            return false;
        }

        std::ifstream in(path, std::ios::binary);
        nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
        if (!root.is_object())
        {
            m_status = "Sheet asset JSON is not an object.";
            return false;
        }
        if (!root.contains("images") || !root["images"].is_array())
        {
            m_status = "Sheet asset has no images array.";
            return false;
        }

        std::vector<SheetImage> images;
        const std::filesystem::path sheetDir = path.parent_path();
        for (const auto &item : root["images"])
        {
            std::string imagePath;
            std::string label;
            if (item.is_string())
            {
                imagePath = item.get<std::string>();
            }
            else if (item.is_object() && item.contains("path") && item["path"].is_string())
            {
                imagePath = item["path"].get<std::string>();
                label = item.value("label", "");
            }

            if (imagePath.empty())
                continue;

            SheetImage image;
            image.path = ResolveSheetEntryPath(imagePath, sheetDir);
            if (!HasImageExtension(image.path))
                continue;
            image.label = label.empty() ? MakeAssetRelative(image.path) : label;
            images.push_back(std::move(image));
        }

        if (images.empty())
        {
            m_status = "Sheet asset has no valid image paths.";
            return false;
        }

        m_loadedSheetPath = path.lexically_normal();
        m_sheetImages = std::move(images);
        SetSheetPath(m_loadedSheetPath);
        preferredImageIndex = std::clamp(preferredImageIndex, 0, static_cast<int>(m_sheetImages.size()) - 1);
        if (!SelectSheetImage(preferredImageIndex))
            return false;

        m_status = "Loaded sheet asset " + MakeAssetRelative(path);
        return true;
    }

    void SpriteEditor::SaveSheetAsset()
    {
        if (m_sheetImages.empty())
        {
            m_status = "No sheet images to save.";
            return;
        }

        if (m_loadedSheetPath.empty())
        {
            std::vector<std::filesystem::path> images;
            images.reserve(m_sheetImages.size());
            for (const SheetImage &image : m_sheetImages)
                images.push_back(image.path);
            OpenSaveSheetDialog(std::move(images));
            return;
        }

        SaveSheetAssetAs(m_loadedSheetPath, m_sheetImages);
    }

    bool SpriteEditor::SaveSheetAssetAs(const std::filesystem::path &path, const std::vector<SheetImage> &images)
    {
        if (images.empty())
        {
            m_status = "No sheet images to save.";
            return false;
        }

        std::filesystem::path savePath = path;
        if (!IsSheetAssetFile(savePath))
        {
            if (ToLower(savePath.extension().string()) == ".json")
                savePath.replace_filename(savePath.stem().generic_string() + ".sheet.json");
            else
                savePath += ".sheet.json";
        }

        nlohmann::json root;
        root["schema"] = "phasma.sprite_sheet.v1";
        root["images"] = nlohmann::json::array();
        for (const SheetImage &image : images)
        {
            nlohmann::json item;
            item["path"] = MakeAssetRelative(image.path);
            if (!image.label.empty() && image.label != MakeAssetRelative(image.path))
                item["label"] = image.label;
            root["images"].push_back(std::move(item));
        }

        try
        {
            if (!savePath.parent_path().empty())
                std::filesystem::create_directories(savePath.parent_path());
            std::ofstream out(savePath, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                m_status = "Sheet save failed: " + PathToUtf8(savePath);
                return false;
            }
            out << root.dump(2) << "\n";
            if (!out)
            {
                m_status = "Sheet write failed: " + PathToUtf8(savePath);
                return false;
            }
        }
        catch (const std::exception &e)
        {
            m_status = std::string("Sheet save failed: ") + e.what();
            return false;
        }

        m_loadedSheetPath = savePath.lexically_normal();
        SetSheetPath(m_loadedSheetPath);
        m_status = "Saved sheet asset " + MakeAssetRelative(savePath);
        return true;
    }

    bool SpriteEditor::SelectSheetImage(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_sheetImages.size()))
        {
            m_status = "Sheet image index out of range.";
            return false;
        }

        m_selectedSheetImage = index;
        LoadImage(m_sheetImages[index].path);
        return m_loadedImagePath == m_sheetImages[index].path.lexically_normal();
    }

    void SpriteEditor::SetSheetPath(const std::filesystem::path &path)
    {
        CopyToBuffer(m_sheetPath, MakeAssetRelative(path));
    }

    void SpriteEditor::ReleaseImage()
    {
        if (m_textureId && m_gui)
            m_gui->ReleaseImageTexture(m_textureId);
        m_textureId = nullptr;

        Image::Destroy(m_image);
        m_loadedImagePath.clear();
    }

    void SpriteEditor::GenerateGridFrames()
    {
        if (!m_image)
        {
            m_status = "Load a sprite sheet before generating frames.";
            return;
        }

        m_gridFrameWidth = std::max(1, m_gridFrameWidth);
        m_gridFrameHeight = std::max(1, m_gridFrameHeight);
        m_gridMarginX = std::max(0, m_gridMarginX);
        m_gridMarginY = std::max(0, m_gridMarginY);
        m_gridSpacingX = std::max(0, m_gridSpacingX);
        m_gridSpacingY = std::max(0, m_gridSpacingY);

        m_frames.clear();
        const int imageW = static_cast<int>(m_image->GetWidth());
        const int imageH = static_cast<int>(m_image->GetHeight());
        int index = 0;

        for (int y = m_gridMarginY; y + m_gridFrameHeight <= imageH; y += m_gridFrameHeight + m_gridSpacingY)
        {
            for (int x = m_gridMarginX; x + m_gridFrameWidth <= imageW; x += m_gridFrameWidth + m_gridSpacingX)
            {
                if (m_gridMaxFrames != kNoFrameLimit && index >= m_gridMaxFrames)
                    break;

                SpriteFrame frame;
                frame.name = "frame_" + std::to_string(index);
                frame.x = x;
                frame.y = y;
                frame.w = m_gridFrameWidth;
                frame.h = m_gridFrameHeight;
                frame.hitboxW = frame.w;
                frame.hitboxH = frame.h;
                m_frames.push_back(frame);
                ++index;
            }
            if (m_gridMaxFrames != kNoFrameLimit && index >= m_gridMaxFrames)
                break;
        }

        if (m_frames.empty())
        {
            m_selectedFrame = -1;
            m_previewFrame = -1;
            m_status = "No frames fit the current grid.";
            return;
        }

        m_selectedFrame = 0;
        m_previewFrame = 0;
        if (m_clips.empty())
            m_clips.push_back({"default", 0, static_cast<int>(m_frames.size()) - 1, 10.0f, true});
        else
        {
            m_clips[0].start = 0;
            m_clips[0].end = static_cast<int>(m_frames.size()) - 1;
        }
        ClampSelection();
        m_status = "Generated " + std::to_string(m_frames.size()) + " frames.";
    }

    void SpriteEditor::ClampSelection()
    {
        if (m_frames.empty())
        {
            m_selectedFrame = -1;
            m_previewFrame = -1;
        }
        else
        {
            m_selectedFrame = std::clamp(m_selectedFrame, 0, static_cast<int>(m_frames.size()) - 1);
            m_previewFrame = std::clamp(m_previewFrame < 0 ? m_selectedFrame : m_previewFrame, 0, static_cast<int>(m_frames.size()) - 1);
        }

        if (m_clips.empty())
            m_clips.push_back({"default", 0, std::max(0, static_cast<int>(m_frames.size()) - 1), 10.0f, true});
        m_selectedClip = std::clamp(m_selectedClip, 0, static_cast<int>(m_clips.size()) - 1);

        const int maxFrame = std::max(0, static_cast<int>(m_frames.size()) - 1);
        for (auto &clip : m_clips)
        {
            clip.start = std::clamp(clip.start, 0, maxFrame);
            clip.end = std::clamp(clip.end, 0, maxFrame);
            if (clip.start > clip.end)
                std::swap(clip.start, clip.end);
            clip.fps = std::max(0.1f, clip.fps);
        }
    }

    void SpriteEditor::ClampFrame(SpriteFrame &frame) const
    {
        if (!m_image)
            return;

        const int imageW = static_cast<int>(m_image->GetWidth());
        const int imageH = static_cast<int>(m_image->GetHeight());
        frame.w = std::clamp(frame.w, 1, std::max(1, imageW));
        frame.h = std::clamp(frame.h, 1, std::max(1, imageH));
        frame.x = std::clamp(frame.x, 0, std::max(0, imageW - frame.w));
        frame.y = std::clamp(frame.y, 0, std::max(0, imageH - frame.h));
    }

    void SpriteEditor::UpdatePlayback()
    {
        ClampSelection();
        if (!m_playing || m_frames.empty() || m_selectedClip < 0 || m_selectedClip >= static_cast<int>(m_clips.size()))
            return;

        const SpriteClip &clip = m_clips[m_selectedClip];
        if (clip.end < clip.start)
            return;

        const float step = 1.0f / std::max(0.1f, clip.fps);
        m_playAccumulator += ImGui::GetIO().DeltaTime;
        while (m_playAccumulator >= step)
        {
            m_playAccumulator -= step;
            if (m_previewFrame < clip.start || m_previewFrame > clip.end)
                m_previewFrame = clip.start;
            else
                ++m_previewFrame;

            if (m_previewFrame > clip.end)
            {
                if (clip.loop)
                    m_previewFrame = clip.start;
                else
                {
                    m_previewFrame = clip.end;
                    m_playing = false;
                    break;
                }
            }
        }
    }

    void SpriteEditor::SaveMetadata()
    {
        const std::filesystem::path path = ResolveAssetPath(m_metadataPath.data());
        if (path.empty())
        {
            m_status = "No metadata path set.";
            return;
        }

        nlohmann::json root;
        root["schema"] = "phasma.sprite_editor.v1";
        root["image"] = m_loadedImagePath.empty() ? BufferString(m_imagePath) : MakeAssetRelative(m_loadedImagePath);
        if (!m_loadedSheetPath.empty())
        {
            root["sheet"] = MakeAssetRelative(m_loadedSheetPath);
            root["image_index"] = m_selectedSheetImage;
        }
        if (m_image)
            root["image_size"] = {{"width", m_image->GetWidth()}, {"height", m_image->GetHeight()}};
        root["grid"] = {
            {"frame_width", m_gridFrameWidth},
            {"frame_height", m_gridFrameHeight},
            {"margin_x", m_gridMarginX},
            {"margin_y", m_gridMarginY},
            {"spacing_x", m_gridSpacingX},
            {"spacing_y", m_gridSpacingY},
            {"max_frames", m_gridMaxFrames},
        };

        root["frames"] = nlohmann::json::array();
        for (const auto &frame : m_frames)
        {
            nlohmann::json item = {
                {"name", frame.name},
                {"rect", {frame.x, frame.y, frame.w, frame.h}},
                {"pivot", {frame.pivotX, frame.pivotY}},
                {"duration", frame.duration},
            };
            if (frame.hitboxEnabled)
            {
                item["hitbox"] = {
                    {"enabled", true},
                    {"rect", {frame.hitboxX, frame.hitboxY, frame.hitboxW, frame.hitboxH}},
                };
            }
            root["frames"].push_back(std::move(item));
        }

        root["clips"] = nlohmann::json::array();
        for (const auto &clip : m_clips)
        {
            root["clips"].push_back({
                {"name", clip.name},
                {"start", clip.start},
                {"end", clip.end},
                {"fps", clip.fps},
                {"loop", clip.loop},
            });
        }

        try
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << root.dump(2) << "\n";
            m_status = "Saved " + MakeAssetRelative(path);
        }
        catch (const std::exception &e)
        {
            m_status = std::string("Save failed: ") + e.what();
        }
    }

    void SpriteEditor::LoadMetadataFromBuffer()
    {
        LoadMetadata(ResolveAssetPath(m_metadataPath.data()));
    }

    void SpriteEditor::LoadMetadata(const std::filesystem::path &path)
    {
        if (path.empty() || !std::filesystem::exists(path))
        {
            m_status = "Metadata file not found: " + PathToUtf8(path);
            return;
        }

        std::ifstream in(path, std::ios::binary);
        nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
        if (!root.is_object())
        {
            m_status = "Metadata JSON is not an object.";
            return;
        }

        SetMetadataPath(path);

        if (root.contains("grid") && root["grid"].is_object())
        {
            const auto &grid = root["grid"];
            m_gridFrameWidth = SafeInt(grid.value("frame_width", nlohmann::json()), m_gridFrameWidth);
            m_gridFrameHeight = SafeInt(grid.value("frame_height", nlohmann::json()), m_gridFrameHeight);
            m_gridMarginX = SafeInt(grid.value("margin_x", nlohmann::json()), m_gridMarginX);
            m_gridMarginY = SafeInt(grid.value("margin_y", nlohmann::json()), m_gridMarginY);
            m_gridSpacingX = SafeInt(grid.value("spacing_x", nlohmann::json()), m_gridSpacingX);
            m_gridSpacingY = SafeInt(grid.value("spacing_y", nlohmann::json()), m_gridSpacingY);
            m_gridMaxFrames = SafeInt(grid.value("max_frames", nlohmann::json()), m_gridMaxFrames);
        }

        bool loadedImageFromSheet = false;
        if (root.contains("sheet") && root["sheet"].is_string())
        {
            const int imageIndex = SafeInt(root.value("image_index", nlohmann::json()), 0);
            loadedImageFromSheet = LoadSheetAsset(ResolveSheetEntryPath(root["sheet"].get<std::string>(), path.parent_path()), imageIndex);
        }

        if (root.contains("image") && root["image"].is_string())
        {
            const std::filesystem::path imagePath = ResolveSheetEntryPath(root["image"].get<std::string>(), path.parent_path());
            bool selectedSheetImage = false;
            if (loadedImageFromSheet)
            {
                for (int i = 0; i < static_cast<int>(m_sheetImages.size()); ++i)
                {
                    if (m_sheetImages[i].path == imagePath.lexically_normal())
                    {
                        selectedSheetImage = SelectSheetImage(i);
                        break;
                    }
                }
            }

            if (!selectedSheetImage)
            {
                SetImagePath(imagePath);
                LoadImageFromBuffer();
            }
        }

        m_frames.clear();
        if (root.contains("frames") && root["frames"].is_array())
        {
            for (const auto &item : root["frames"])
            {
                if (!item.is_object())
                    continue;

                SpriteFrame frame;
                frame.name = item.value("name", "frame_" + std::to_string(m_frames.size()));
                if (item.contains("rect") && item["rect"].is_array() && item["rect"].size() >= 4)
                {
                    frame.x = SafeInt(item["rect"][0], frame.x);
                    frame.y = SafeInt(item["rect"][1], frame.y);
                    frame.w = std::max(1, SafeInt(item["rect"][2], frame.w));
                    frame.h = std::max(1, SafeInt(item["rect"][3], frame.h));
                }
                if (item.contains("pivot") && item["pivot"].is_array() && item["pivot"].size() >= 2)
                {
                    frame.pivotX = std::clamp(SafeFloat(item["pivot"][0], frame.pivotX), 0.0f, 1.0f);
                    frame.pivotY = std::clamp(SafeFloat(item["pivot"][1], frame.pivotY), 0.0f, 1.0f);
                }
                frame.duration = std::max(0.01f, SafeFloat(item.value("duration", nlohmann::json()), frame.duration));

                if (item.contains("hitbox") && item["hitbox"].is_object())
                {
                    const auto &hitbox = item["hitbox"];
                    frame.hitboxEnabled = SafeBool(hitbox.value("enabled", nlohmann::json()), false);
                    if (hitbox.contains("rect") && hitbox["rect"].is_array() && hitbox["rect"].size() >= 4)
                    {
                        frame.hitboxX = SafeInt(hitbox["rect"][0], frame.hitboxX);
                        frame.hitboxY = SafeInt(hitbox["rect"][1], frame.hitboxY);
                        frame.hitboxW = std::max(1, SafeInt(hitbox["rect"][2], frame.hitboxW));
                        frame.hitboxH = std::max(1, SafeInt(hitbox["rect"][3], frame.hitboxH));
                    }
                }

                ClampFrame(frame);
                m_frames.push_back(frame);
            }
        }

        m_clips.clear();
        if (root.contains("clips") && root["clips"].is_array())
        {
            for (const auto &item : root["clips"])
            {
                if (!item.is_object())
                    continue;

                SpriteClip clip;
                clip.name = item.value("name", "clip_" + std::to_string(m_clips.size()));
                clip.start = SafeInt(item.value("start", nlohmann::json()), clip.start);
                clip.end = SafeInt(item.value("end", nlohmann::json()), clip.end);
                clip.fps = std::max(0.1f, SafeFloat(item.value("fps", nlohmann::json()), clip.fps));
                clip.loop = SafeBool(item.value("loop", nlohmann::json()), clip.loop);
                m_clips.push_back(clip);
            }
        }

        m_selectedFrame = m_frames.empty() ? -1 : 0;
        m_selectedClip = 0;
        m_previewFrame = m_selectedFrame;
        ClampSelection();
        m_status = "Loaded " + MakeAssetRelative(path);
    }

    void SpriteEditor::SetImagePath(const std::filesystem::path &path)
    {
        CopyToBuffer(m_imagePath, MakeAssetRelative(path));
    }

    void SpriteEditor::SetMetadataPath(const std::filesystem::path &path)
    {
        CopyToBuffer(m_metadataPath, MakeAssetRelative(path));
    }

    void SpriteEditor::SuggestMetadataPath()
    {
        const std::string current = BufferString(m_metadataPath);
        if (!current.empty() && current != "Sprites/sprite_sheet.sprite.json")
            return;

        std::filesystem::path rel = m_loadedImagePath.empty() ? ResolveAssetPath(m_imagePath.data()) : m_loadedImagePath;
        if (rel.empty())
            return;

        std::filesystem::path suggested = rel;
        suggested.replace_extension(".sprite.json");
        SetMetadataPath(suggested);
    }

    std::filesystem::path SpriteEditor::ResolveAssetPath(const char *path) const
    {
        if (!path || path[0] == '\0')
            return {};

        std::filesystem::path p(path);
        if (p.is_absolute())
            return p.lexically_normal();

        std::filesystem::path direct = p.lexically_normal();
        if (std::filesystem::exists(direct))
            return direct;

        std::filesystem::path assets = std::filesystem::path(Path::Assets) / p;
        return assets.lexically_normal();
    }

    std::filesystem::path SpriteEditor::ResolveSheetEntryPath(const std::string &path, const std::filesystem::path &sheetDir) const
    {
        if (path.empty())
            return {};

        std::filesystem::path p = U8Path(path);
        if (p.is_absolute())
            return p.lexically_normal();

        if (!sheetDir.empty())
        {
            std::filesystem::path relativeToSheet = (sheetDir / p).lexically_normal();
            if (std::filesystem::exists(relativeToSheet))
                return relativeToSheet;
        }

        return ResolveAssetPath(path.c_str()).lexically_normal();
    }

    std::string SpriteEditor::MakeAssetRelative(const std::filesystem::path &path) const
    {
        if (path.empty())
            return {};

        std::error_code ec;
        std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
        if (ec)
            normalized = path.lexically_normal();

        ec.clear();
        std::filesystem::path assets = std::filesystem::weakly_canonical(Path::Assets, ec);
        if (ec)
            assets = std::filesystem::path(Path::Assets).lexically_normal();

        ec.clear();
        std::filesystem::path rel = std::filesystem::relative(normalized, assets, ec);
        if (!ec && !rel.empty() && rel.begin() != rel.end() && *rel.begin() != "..")
            return rel.generic_string();

        return PathToUtf8(path);
    }

    void SpriteEditor::CopyToBuffer(std::array<char, 512> &buffer, const std::string &value)
    {
        CopyTextToBuffer(buffer, value);
    }

    std::string SpriteEditor::BufferString(const std::array<char, 512> &buffer)
    {
        return std::string(buffer.data());
    }

    bool SpriteEditor::HasImageExtension(const std::filesystem::path &path)
    {
        std::string ext = ToLower(path.extension().string());
        return ext == ".bmp" || ext == ".gif" || ext == ".hdr" || ext == ".jpeg" ||
               ext == ".jpg" || ext == ".pic" || ext == ".png" || ext == ".psd" ||
               ext == ".tga";
    }

    bool SpriteEditor::IsSheetAssetFile(const std::filesystem::path &path)
    {
        return ToLower(path.filename().string()).ends_with(".sheet.json");
    }

    std::vector<std::string> SpriteEditor::SourcePickerExtensions()
    {
        return {".bmp", ".gif", ".hdr", ".jpeg", ".jpg", ".pic", ".png", ".psd", ".tga", ".json"};
    }

    std::string SpriteEditor::PathToUtf8(const std::filesystem::path &path)
    {
        auto u8 = path.u8string();
        return std::string(reinterpret_cast<const char *>(u8.c_str()));
    }
} // namespace pe
