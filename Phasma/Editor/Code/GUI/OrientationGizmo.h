#pragma once

#include "Camera/Camera.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

namespace pe::ui
{
    struct OrientationGizmoResult
    {
        bool hovered = false;
        bool active = false;
        bool clicked = false;
        vec3 direction = vec3(0.f); // the world direction the camera should look along after a click
    };

    // The active-camera orientation overlay in the top-right corner of a viewport image: three projected arrows
    // (Right = red +X, Front = blue +Z, Up = green +Y). Hover / click detection is manual around the shafts and
    // heads, so the empty corner stays transparent and never acts like a panel. Shared by the editor Viewport and
    // PhasmaAnimator; the caller applies the click (the editor re-orients the camera in place, the animator its
    // orbit).
    inline OrientationGizmoResult DrawOrientationGizmo(Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize)
    {
        OrientationGizmoResult result;
        if (!camera || imageSize.x < 96.0f || imageSize.y < 96.0f)
            return result;

        const float edge = glm::clamp(glm::min(imageSize.x, imageSize.y) * 0.16f, 88.0f, 128.0f);
        const float pad = 12.0f;
        const ImVec2 pos(imageMin.x + imageSize.x - edge - pad, imageMin.y + pad);
        const ImVec2 size(edge, edge);
        const ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        const float radius = size.x * 0.38f;
        const float hitRadius = 16.0f;

        struct OrientationAxis
        {
            const char *name = "";
            const char *letter = "";
            glm::vec3 direction = glm::vec3(0.0f);
            ImU32 color = 0;
            ImVec2 end = ImVec2(0.0f, 0.0f);
            ImVec2 dir2 = ImVec2(0.0f, 0.0f);
            float projectedLength = 0.0f;
            float depth = 0.0f;
            bool hovered = false;
        };

        OrientationAxis axes[3] = {
            {"Right", "X", glm::vec3(1.0f, 0.0f, 0.0f), IM_COL32(235, 74, 74, 255)},
            {"Front", "Z", glm::vec3(0.0f, 0.0f, 1.0f), IM_COL32(74, 137, 255, 255)},
            {"Up", "Y", glm::vec3(0.0f, 1.0f, 0.0f), IM_COL32(74, 210, 116, 255)},
        };

        const glm::vec3 cameraRight = glm::normalize(camera->GetRight());
        const glm::vec3 cameraUp = glm::normalize(camera->GetUp());
        const glm::vec3 cameraFront = glm::normalize(camera->GetFront());

        for (OrientationAxis &axis : axes)
        {
            const glm::vec3 worldDir = glm::normalize(axis.direction);
            const glm::vec2 projected(glm::dot(worldDir, cameraRight), glm::dot(worldDir, cameraUp));
            axis.depth = glm::dot(worldDir, cameraFront);
            axis.projectedLength = glm::length(projected);

            if (axis.projectedLength > 1e-4f)
            {
                const glm::vec2 dir = projected / axis.projectedLength;
                const float drawLength = radius * glm::clamp(axis.projectedLength, 0.25f, 1.0f);
                axis.dir2 = ImVec2(dir.x, dir.y);
                axis.end = ImVec2(center.x + dir.x * drawLength, center.y + dir.y * drawLength);
            }
            else
            {
                axis.end = center;
            }
        }

        const ImVec2 mouse = ImGui::GetMousePos();
        const ImRect gizmoRect(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        const bool canInteract =
            ImGui::IsWindowHovered() && gizmoRect.Contains(mouse) && !ImGui::IsMouseDown(ImGuiMouseButton_Right);

        auto distanceToSegment = [](const ImVec2 &p, const ImVec2 &a, const ImVec2 &b)
        {
            const ImVec2 ab(b.x - a.x, b.y - a.y);
            const ImVec2 ap(p.x - a.x, p.y - a.y);
            const float abLenSq = ab.x * ab.x + ab.y * ab.y;
            float t = 0.0f;
            if (abLenSq > 1e-6f)
                t = glm::clamp((ap.x * ab.x + ap.y * ab.y) / abLenSq, 0.0f, 1.0f);

            const ImVec2 closest(a.x + ab.x * t, a.y + ab.y * t);
            const float dx = p.x - closest.x;
            const float dy = p.y - closest.y;
            return std::sqrt(dx * dx + dy * dy);
        };

        int hoveredIndex = -1;
        float bestDistance = hitRadius;
        if (canInteract)
        {
            for (int i = 0; i < 3; ++i)
            {
                const float endpointDx = mouse.x - axes[i].end.x;
                const float endpointDy = mouse.y - axes[i].end.y;
                float dist = std::sqrt(endpointDx * endpointDx + endpointDy * endpointDy);
                if (axes[i].projectedLength > 0.12f)
                    dist = std::min(dist, distanceToSegment(mouse, center, axes[i].end));

                if (dist < bestDistance)
                {
                    bestDistance = dist;
                    hoveredIndex = i;
                }
            }
        }

        if (hoveredIndex >= 0)
        {
            axes[hoveredIndex].hovered = true;
            result.hovered = true;
            result.active = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                result.clicked = true;
                result.direction = axes[hoveredIndex].direction;
            }
            ImGui::SetTooltip("Snap the camera to the %s view.", axes[hoveredIndex].name);
        }

        int drawOrder[3] = {0, 1, 2};
        std::sort(drawOrder, drawOrder + 3, [&](int a, int b)
                  { return axes[a].depth > axes[b].depth; });

        auto withAlpha = [](ImU32 color, int alpha)
        { return (color & IM_COL32(255, 255, 255, 0)) | IM_COL32(0, 0, 0, alpha); };

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        ImFont *font = ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize();
        drawList->AddCircleFilled(center, 4.0f, IM_COL32(22, 24, 28, 150));
        drawList->AddCircle(center, 4.0f, IM_COL32(255, 255, 255, 120), 12, 1.0f);

        for (int orderIndex : drawOrder)
        {
            const OrientationAxis &axis = axes[orderIndex];
            const int alpha = axis.hovered ? 255 : (axis.depth > 0.0f ? 170 : 230);
            const ImU32 color = withAlpha(axis.color, alpha);
            const float thickness = axis.hovered ? 4.0f : 3.0f;
            ImVec2 labelPos = axis.end;

            if (axis.projectedLength > 0.12f)
            {
                const float headLength = axis.hovered ? 13.0f : 11.0f;
                const float headWidth = axis.hovered ? 8.0f : 7.0f;
                const ImVec2 shaftEnd(axis.end.x - axis.dir2.x * headLength * 0.75f,
                                      axis.end.y - axis.dir2.y * headLength * 0.75f);
                const ImVec2 perp(-axis.dir2.y, axis.dir2.x);
                const ImVec2 left(axis.end.x - axis.dir2.x * headLength + perp.x * headWidth,
                                  axis.end.y - axis.dir2.y * headLength + perp.y * headWidth);
                const ImVec2 right(axis.end.x - axis.dir2.x * headLength - perp.x * headWidth,
                                   axis.end.y - axis.dir2.y * headLength - perp.y * headWidth);

                drawList->AddLine(center, shaftEnd, color, thickness);
                drawList->AddTriangleFilled(axis.end, left, right, color);
                labelPos = ImVec2(axis.end.x + axis.dir2.x * 9.0f, axis.end.y + axis.dir2.y * 9.0f);
            }
            else
            {
                const float dotRadius = axis.hovered ? 8.5f : 7.0f;
                drawList->AddCircleFilled(axis.end, dotRadius, color, 18);
                drawList->AddCircle(axis.end, dotRadius, IM_COL32(255, 255, 255, axis.hovered ? 210 : 130), 18, 1.0f);
                labelPos = ImVec2(axis.end.x + 10.0f, axis.end.y - 10.0f);
            }

            const ImVec2 labelSize = ImGui::CalcTextSize(axis.letter);
            labelPos.x = glm::clamp(labelPos.x - labelSize.x * 0.5f, imageMin.x + 2.0f,
                                    imageMin.x + imageSize.x - labelSize.x - 2.0f);
            labelPos.y = glm::clamp(labelPos.y - labelSize.y * 0.5f, imageMin.y + 2.0f,
                                    imageMin.y + imageSize.y - labelSize.y - 2.0f);
            drawList->AddText(font, fontSize, ImVec2(labelPos.x + 1.0f, labelPos.y + 1.0f), IM_COL32(0, 0, 0, alpha),
                              axis.letter);
            drawList->AddText(font, fontSize, labelPos, color, axis.letter);
        }
        return result;
    }
} // namespace pe::ui
