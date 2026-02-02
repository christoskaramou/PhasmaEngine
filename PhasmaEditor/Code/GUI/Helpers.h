#pragma once

#include "imgui/imgui_internal.h"

// ========================= UI Utils =========================
namespace pe::ui
{
    // ---------- small helpers ----------
    inline ImU32 U32(const ImVec4 &c) { return ImGui::GetColorU32(c); }
    inline float Clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

    inline std::string FormatBytes(uint64_t b)
    {
        const double KB = 1024.0, MB = KB * KB, GB = KB * KB * KB;
        char buf[64];
        if (b >= (uint64_t)GB)
            snprintf(buf, sizeof(buf), "%.2f GB", b / GB);
        else if (b >= (uint64_t)MB)
            snprintf(buf, sizeof(buf), "%.2f MB", b / MB);
        else
            snprintf(buf, sizeof(buf), "%.0f KB", b / KB);
        return buf;
    }

    inline ImVec4 UsageColor(float frac)
    {
        // 0..1 → green→yellow→red
        frac = Clamp01(frac);
        ImVec4 g(0.20f, 0.80f, 0.25f, 0.95f);
        ImVec4 y(0.95f, 0.80f, 0.25f, 0.95f);
        ImVec4 r(0.85f, 0.25f, 0.25f, 0.95f);
        return (frac < 0.5f)
                   ? ImVec4(g.x + (y.x - g.x) * (frac / 0.5f), g.y + (y.y - g.y) * (frac / 0.5f), g.z + (y.z - g.z) * (frac / 0.5f), 0.95f)
                   : ImVec4(y.x + (r.x - y.x) * ((frac - 0.5f) / 0.5f), y.y + (r.y - y.y) * ((frac - 0.5f) / 0.5f), y.z + (r.z - y.z) * ((frac - 0.5f) / 0.5f), 0.95f);
    }

    inline ImVec4 Heat(float f) // 0..1 → green→yellow→red
    {
        f = Clamp01(f);
        ImVec4 g(0.20f, 0.80f, 0.25f, 1.0f), y(0.95f, 0.80f, 0.25f, 1.0f), r(0.85f, 0.25f, 0.25f, 1.0f);
        return (f < 0.5f)
                   ? ImVec4(g.x + (y.x - g.x) * (f / 0.5f), g.y + (y.y - g.y) * (f / 0.5f), g.z + (y.z - g.z) * (f / 0.5f), 1.0f)
                   : ImVec4(y.x + (r.x - y.x) * ((f - 0.5f) / 0.5f), y.y + (r.y - y.y) * ((f - 0.5f) / 0.5f), y.z + (r.z - y.z) * ((f - 0.5f) / 0.5f), 1.0f);
    }

    // ---------- bar chrome ----------
    inline void DrawBarBackground(const ImVec2 &p0, const ImVec2 &p1, ImDrawList *dl)
    {
        const ImU32 bg = U32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
        const ImU32 brdr = U32(ImVec4(1, 1, 1, 0.18f));
        const float r = 6.0f;

        // subtle shadow
        dl->AddRectFilled(ImVec2(p0.x, p0.y + 1), ImVec2(p1.x, p1.y + 2), IM_COL32(0, 0, 0, 45), r);
        // bg + border
        dl->AddRectFilled(p0, p1, bg, r);
        dl->AddRect(p0, p1, brdr, r);

        // ticks 25/50/75
        const float W = p1.x - p0.x, H = p1.y - p0.y;
        for (int i : {25, 50, 75})
        {
            float x = p0.x + (i / 100.f) * W;
            dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + H), IM_COL32(255, 255, 255, 30), 1.0f);
        }
        // top hairline
        dl->AddLine(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p1.x - 1, p0.y + 1), IM_COL32(255, 255, 255, 35), 1.0f);
    }

    // ---------- centered badge (glass pill) ----------
    inline void ComputeBadgeGeometry(const ImVec2 &p0, const ImVec2 &p1, const char *text,
                                     ImVec2 &b0, ImVec2 &b1, ImVec2 &tp,
                                     float padX = 6.f, float padY = 2.f)
    {
        ImVec2 ts = ImGui::CalcTextSize(text ? text : "");
        tp = ImVec2(p0.x + (p1.x - p0.x - ts.x) * 0.5f,
                    p0.y + (p1.y - p0.y - ts.y) * 0.5f);
        b0 = ImVec2(tp.x - padX, tp.y - padY);
        b1 = ImVec2(tp.x + ts.x + padX, tp.y + ts.y + padY);
    }

    // alpha scales badge visuals; 0..1 (0 = invisible, 1 = current look)
    inline int ScaledA(int base, float a)
    {
        a = std::clamp(a, 0.0f, 1.0f);
        return (int)std::round(base * a);
    }

    inline void DrawCenterBadgeBg(ImDrawList *dl, const ImVec2 &p0, const ImVec2 &p1,
                                  const char *text, float alpha = 0.45f)
    {
        if (!text || !text[0])
            return;
        ImVec2 b0, b1, tp;
        ComputeBadgeGeometry(p0, p1, text, b0, b1, tp);

        const float r = 8.0f;
        const ImU32 shadow = IM_COL32(0, 0, 0, ScaledA(45, alpha));
        const ImU32 bg = IM_COL32(20, 20, 26, ScaledA(180, alpha)); // main “glass”
        const ImU32 brdr = IM_COL32(255, 255, 255, ScaledA(35, alpha));
        const ImU32 shine = IM_COL32(255, 255, 255, ScaledA(40, alpha));

        dl->AddRectFilled(ImVec2(b0.x, b0.y + 1), ImVec2(b1.x, b1.y + 2), shadow, r);
        dl->AddRectFilled(b0, b1, bg, r);
        dl->AddRect(b0, b1, brdr, r);
        dl->AddLine(ImVec2(b0.x + 1, b0.y + 1), ImVec2(b1.x - 1, b0.y + 1), shine, 1.0f);
    }

    inline void DrawCenterBadgeText(ImDrawList *dl, const ImVec2 &p0, const ImVec2 &p1,
                                    const char *text, float alpha = 1.0f)
    {
        if (!text || !text[0])
            return;
        ImVec2 b0, b1, tp;
        ComputeBadgeGeometry(p0, p1, text, b0, b1, tp);

        ImVec4 txt = ImGui::GetStyle().Colors[ImGuiCol_Text];
        txt.w *= std::clamp(alpha, 0.0f, 1.0f); // optionally fade text too
        dl->AddText(tp, ImGui::GetColorU32(txt), text);
    }

    // convenience: bg then text, sharing alpha
    inline void DrawCenterBadge(ImDrawList *dl, const ImVec2 &p0, const ImVec2 &p1,
                                const char *text, float alpha = 0.45f)
    {
        DrawCenterBadgeBg(dl, p0, p1, text, alpha);
        DrawCenterBadgeText(dl, p0, p1, text, alpha);
    }

    // centered overlay text only (no background), with alpha fades the text
    inline void DrawCenterLabel(ImDrawList *dl, const ImVec2 &p0, const ImVec2 &p1,
                                const char *text, float alpha = 1.0f)
    {
        if (!text || !text[0])
            return;

        ImVec2 ts = ImGui::CalcTextSize(text);
        ImVec2 tp = ImVec2(
            p0.x + (p1.x - p0.x - ts.x) * 0.5f,
            p0.y + (p1.y - p0.y - ts.y) * 0.5f);

        ImVec4 col = ImGui::GetStyle().Colors[ImGuiCol_Text];
        col.w *= std::clamp(alpha, 0.0f, 1.0f);
        dl->AddText(tp, ImGui::GetColorU32(col), text);
    }

    // ---------- tiny relative bar (tables) ----------
    inline void TinyBar(float frac, float height = 12.f)
    {
        frac = Clamp01(frac);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + ImGui::GetContentRegionAvail().x, p0.y + height);
        ImDrawList *dl = ImGui::GetWindowDrawList();

        const float r = 4.0f;
        const ImU32 bg = U32(ImVec4(1, 1, 1, 0.08f));
        const ImU32 br = U32(ImVec4(1, 1, 1, 0.20f));

        dl->AddRectFilled(p0, p1, bg, r);
        dl->AddRect(p0, p1, br, r);

        ImVec2 f1 = ImVec2(p0.x + frac * (p1.x - p0.x), p1.y);
        dl->AddRectFilled(p0, f1, U32(Heat(frac)), r);

        ImGui::Dummy(ImVec2(0, height));
    }

    // ---------- pretty hierarchy cell (timings) ----------
    inline void DrawHierarchyCell(const char *name, int depth, float rel,
                                  float indent_step = 14.f, float bullet_r = 4.f)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + ImGui::GetContentRegionAvail().x,
                           p0.y + ImGui::GetTextLineHeightWithSpacing());

        const ImU32 guide = IM_COL32(255, 255, 255, 28);
        for (int d = 1; d <= depth; ++d)
        {
            float x = p0.x + d * indent_step;
            dl->AddLine(ImVec2(x, p0.y + 2), ImVec2(x, p1.y - 2), guide, 1.f);
        }

        float x0 = p0.x + depth * indent_step + indent_step * 0.5f;
        float cy = (p0.y + p1.y) * 0.5f;

        dl->AddCircleFilled(ImVec2(x0, cy), bullet_r, U32(Heat(rel)), 16);
        dl->AddCircle(ImVec2(x0, cy), bullet_r, IM_COL32(0, 0, 0, 120), 16, 1.f);

        ImVec2 tp = ImVec2(x0 + bullet_r + 6.f, p0.y);
        dl->AddText(tp, U32(ImGui::GetStyle().Colors[ImGuiCol_Text]), name);

        ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeightWithSpacing()));
    }

    // RAM bar: [System Used (others, grey)] + [App Used (gradient)] over System Total
    static void DrawRamBar(const char *label, uint64_t sysTotal, uint64_t appUsed, uint64_t sysUsed,
                           ImVec2 size = ImVec2(-FLT_MIN, 20.0f))
    {
        if (sysTotal == 0)
            sysTotal = 1;
        appUsed = std::min(appUsed, sysTotal);
        sysUsed = std::min(sysUsed, sysTotal);

        const uint64_t sysOther = (sysUsed > appUsed) ? (sysUsed - appUsed) : 0;
        const float fApp = (float)appUsed / (float)sysTotal;
        const float fOther = (float)sysOther / (float)sysTotal;

        ImGui::TextUnformatted(label);

        ImVec2 p0 = ImGui::GetCursorScreenPos();
        if (size.x <= 0)
            size.x = ImGui::GetContentRegionAvail().x;
        ImVec2 p1(p0.x + size.x, p0.y + size.y);
        ImDrawList *dl = ImGui::GetWindowDrawList();

        // frame
        const float R = 4.0f, inset = 2.0f, W = (p1.x - p0.x);
        dl->AddRectFilled(p0, p1, U32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]), R);
        dl->AddRect(p0, p1, U32(ImVec4(1, 1, 1, 0.25f)), R);

        // fills: others then app
        float xOther = p0.x + fOther * W;
        dl->AddRectFilled(ImVec2(p0.x, p0.y + inset), ImVec2(xOther, p1.y - inset), U32(ImVec4(0.60f, 0.60f, 0.60f, 0.90f)), R);
        float xApp1 = p0.x + (fOther + fApp) * W;
        dl->AddRectFilled(ImVec2(xOther, p0.y + inset), ImVec2(xApp1, p1.y - inset), U32(Heat(fApp)), R);

        // badge bg under, text over
        char overlay[128];
        int pctApp = (int)std::round(100.0 * (double)appUsed / (double)sysTotal);
        snprintf(overlay, sizeof(overlay), "%s / %s  (%d%%)", FormatBytes(appUsed).c_str(), FormatBytes(sysTotal).c_str(), pctApp);
        DrawCenterLabel(dl, p0, p1, overlay);

        ImGui::PushID(label);
        ImGui::InvisibleButton("##ram_bar", size);
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("%s", label);
            ImGui::Separator();
            ImGui::Text("App Used:            %s", FormatBytes(appUsed).c_str());
            ImGui::Text("System Used (other): %s", FormatBytes(sysOther).c_str());
            ImGui::Separator();
            ImGui::Text("Total RAM:           %s", FormatBytes(sysTotal).c_str());
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }

    // stacked version (others + app over budget) – same name
    inline void DrawGpuBar(const char *label,
                           uint64_t appUsed, uint64_t otherUsed, uint64_t totalBudget,
                           ImVec2 size = ImVec2(-FLT_MIN, 18.0f))
    {
        if (totalBudget == 0)
            totalBudget = 1;

        const double tot = (double)totalBudget;
        const float fOther = (float)std::min<uint64_t>(otherUsed, totalBudget) / (float)totalBudget;
        const float fApp = (float)std::min<uint64_t>(appUsed, (uint64_t)std::max<int64_t>((int64_t)totalBudget - (int64_t)otherUsed, 0)) / (float)totalBudget;

        const ImVec4 colOther = ImVec4(0.60f, 0.60f, 0.60f, 0.90f);
        const ImVec4 colApp = UsageColor((float)(appUsed / tot));

        ImGui::TextUnformatted(label);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        if (size.x <= 0)
            size.x = ImGui::GetContentRegionAvail().x;
        ImVec2 p1 = ImVec2(p0.x + size.x, p0.y + size.y);
        auto *dl = ImGui::GetWindowDrawList();

        DrawBarBackground(p0, p1, dl);

        const float inset = 2.0f, W = (p1.x - p0.x);
        float xOther = p0.x + fOther * W;
        dl->AddRectFilled(ImVec2(p0.x, p0.y + inset), ImVec2(xOther, p1.y - inset), ImGui::GetColorU32(colOther), 5.0f);
        float xApp1 = p0.x + (fOther + fApp) * W;
        dl->AddRectFilled(ImVec2(xOther, p0.y + inset), ImVec2(xApp1, p1.y - inset), ImGui::GetColorU32(colApp), 5.0f);

        // centered overlay: show APP vs budget
        char overlay[96];
        int pctApp = (int)std::round(100.0 * ((double)appUsed / tot));
        snprintf(overlay, sizeof(overlay), "%s / %s  (%d%%)",
                 FormatBytes(appUsed).c_str(), FormatBytes(totalBudget).c_str(), pctApp);
        DrawCenterLabel(dl, p0, p1, overlay);

        // tooltip
        ImGui::PushID(label);
        ImGui::InvisibleButton("##gpu_bar_stacked", size);
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("%s", label);
            ImGui::Separator();
            ImGui::Text("App Used:    %s", FormatBytes(appUsed).c_str());
            ImGui::Text("Other Used:  %s", FormatBytes(otherUsed).c_str());
            ImGui::Separator();
            ImGui::Text("Budget:      %s", FormatBytes(totalBudget).c_str());
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }

    // https://github.com/ocornut/imgui/issues/1901#issuecomment-444929973
    inline void LoadingIndicatorCircle(const char *label, const float indicator_radius,
                                       const ImVec4 &main_color, const ImVec4 &backdrop_color,
                                       const int circle_count, const float speed)
    {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return;

        ImGuiContext &g = *GImGui;
        const ImGuiID id = window->GetID(label);

        const ImVec2 pos = window->DC.CursorPos;
        const float circle_radius = indicator_radius / 10.f;
        const ImRect bb(pos, ImVec2(pos.x + indicator_radius * 2.0f, pos.y + indicator_radius * 2.0f));
        ImGui::ItemSize(bb, ImGui::GetStyle().FramePadding.y);
        if (!ImGui::ItemAdd(bb, id))
            return;

        const float t = (float)g.Time;
        const auto degree_offset = 2.0f * IM_PI / circle_count;
        for (int i = 0; i < circle_count; ++i)
        {
            const auto x = indicator_radius * std::sin(degree_offset * i) * 0.9f;
            const auto y = indicator_radius * std::cos(degree_offset * i) * 0.9f;
            const auto growth = std::max(0.0f, std::sin(t * speed - i * degree_offset));
            ImVec4 color;
            color.x = main_color.x * growth + backdrop_color.x * (1.0f - growth);
            color.y = main_color.y * growth + backdrop_color.y * (1.0f - growth);
            color.z = main_color.z * growth + backdrop_color.z * (1.0f - growth);
            color.w = 1.0f;
            window->DrawList->AddCircleFilled(ImVec2(pos.x + indicator_radius + x,
                                                     pos.y + indicator_radius - y),
                                              circle_radius + growth * circle_radius,
                                              ImGui::GetColorU32(color));
        }
    }

    // Classic PhasmaEngine/ImGui theme
    inline void ApplyClassicTheme()
    {
        ImGui::StyleColorsClassic();
        ImGuiStyle &s = ImGui::GetStyle();
        s.WindowRounding = 4.0f;
        s.ChildRounding = 4.0f;
        s.FrameRounding = 2.0f;
        s.GrabRounding = 2.0f;
        s.PopupRounding = 2.0f;
        s.ScrollbarRounding = 2.0f;
        s.TabRounding = 2.0f;
        s.FramePadding = ImVec2(8, 4);
        s.ItemSpacing = ImVec2(8, 6);
        s.WindowPadding = ImVec2(10, 8);
        s.SeparatorTextPadding = ImVec2(10, 4);
    }

    // ImGui Dark theme
    inline void ApplyDarkTheme()
    {
        ImGui::StyleColorsDark();
        ImGuiStyle &s = ImGui::GetStyle();
        s.WindowRounding = 4.0f;
        s.ChildRounding = 4.0f;
        s.FrameRounding = 2.0f;
        s.GrabRounding = 2.0f;
        s.PopupRounding = 2.0f;
        s.ScrollbarRounding = 2.0f;
        s.TabRounding = 2.0f;
        s.FramePadding = ImVec2(8, 4);
        s.ItemSpacing = ImVec2(8, 6);
        s.WindowPadding = ImVec2(10, 8);
        s.SeparatorTextPadding = ImVec2(10, 4);
    }

    // ImGui Light theme
    inline void ApplyLightTheme()
    {
        ImGui::StyleColorsLight();
        ImGuiStyle &s = ImGui::GetStyle();
        s.WindowRounding = 4.0f;
        s.ChildRounding = 4.0f;
        s.FrameRounding = 2.0f;
        s.GrabRounding = 2.0f;
        s.PopupRounding = 2.0f;
        s.ScrollbarRounding = 2.0f;
        s.TabRounding = 2.0f;
        s.FramePadding = ImVec2(8, 4);
        s.ItemSpacing = ImVec2(8, 6);
        s.WindowPadding = ImVec2(10, 8);
        s.SeparatorTextPadding = ImVec2(10, 4);
    }

    // Modern Dark theme (VS Code Dark Modern inspired)
    inline void ApplyModernTheme()
    {
        ImGui::StyleColorsDark();
        ImGuiStyle &s = ImGui::GetStyle();
        s.WindowRounding = 6.0f;
        s.ChildRounding = 6.0f;
        s.FrameRounding = 4.0f;
        s.GrabRounding = 4.0f;
        s.PopupRounding = 4.0f;
        s.ScrollbarRounding = 12.0f;
        s.TabRounding = 4.0f;
        s.FramePadding = ImVec2(10, 6);
        s.ItemSpacing = ImVec2(8, 6);
        s.WindowPadding = ImVec2(12, 12);
        s.SeparatorTextPadding = ImVec2(12, 4);
        s.WindowBorderSize = 1.0f;
        s.FrameBorderSize = 0.0f;

        auto &c = s.Colors;

        // Palette
        const ImVec4 bg_darkest = ImVec4(0.11f, 0.11f, 0.11f, 1.0f);
        const ImVec4 bg_dark = ImVec4(0.13f, 0.13f, 0.13f, 1.0f);
        const ImVec4 bg_mid = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
        const ImVec4 bg_light = ImVec4(0.23f, 0.23f, 0.23f, 1.0f);

        const ImVec4 accent = ImVec4(0.00f, 0.48f, 0.80f, 1.0f);
        const ImVec4 accent_hov = ImVec4(0.00f, 0.55f, 0.90f, 1.0f);
        const ImVec4 accent_act = ImVec4(0.00f, 0.60f, 1.00f, 1.0f);

        const ImVec4 text_main = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        const ImVec4 text_dim = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);

        // Backgrounds
        c[ImGuiCol_WindowBg] = bg_dark;
        c[ImGuiCol_ChildBg] = bg_darkest;
        c[ImGuiCol_PopupBg] = bg_darkest;

        // Frame
        c[ImGuiCol_FrameBg] = bg_light;
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);

        // Title
        c[ImGuiCol_TitleBg] = bg_darkest;
        c[ImGuiCol_TitleBgActive] = bg_darkest;
        c[ImGuiCol_TitleBgCollapsed] = bg_darkest;
        c[ImGuiCol_MenuBarBg] = bg_mid;

        // Scrollbar
        c[ImGuiCol_ScrollbarBg] = bg_darkest;
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 1.0f);
        c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);

        // Header
        c[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.30f);
        c[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.50f);
        c[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.80f);

        // Tabs
        c[ImGuiCol_Tab] = bg_mid;
        c[ImGuiCol_TabHovered] = bg_light;
        c[ImGuiCol_TabSelected] = bg_dark;
        c[ImGuiCol_TabSelectedOverline] = accent;
        c[ImGuiCol_TabDimmed] = bg_mid;
        c[ImGuiCol_TabDimmedSelected] = bg_dark;
        c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.40f, 0.40f, 0.40f, 1.0f);

        // Separators
        c[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);
        c[ImGuiCol_SeparatorHovered] = accent;
        c[ImGuiCol_SeparatorActive] = accent;

        // Button
        c[ImGuiCol_Button] = accent;
        c[ImGuiCol_ButtonHovered] = accent_hov;
        c[ImGuiCol_ButtonActive] = accent_act;

        // Text
        c[ImGuiCol_Text] = text_main;
        c[ImGuiCol_TextDisabled] = text_dim;
        c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);

        // Borders
        c[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
        c[ImGuiCol_NavHighlight] = accent;

        // Check/Slider
        c[ImGuiCol_CheckMark] = text_main;
        c[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    // Unity-like dark theme
    inline void ApplyUnityTheme()
    {
        ImGui::StyleColorsDark(); // Reset ALL colors to dark theme base first
        ImGuiStyle &s = ImGui::GetStyle();
        s.WindowRounding = 8.0f;
        s.ChildRounding = 8.0f;
        s.FrameRounding = 6.0f;
        s.GrabRounding = 6.0f;
        s.PopupRounding = 6.0f;
        s.ScrollbarRounding = 6.0f;
        s.TabRounding = 4.0f;
        s.FramePadding = ImVec2(10, 6);
        s.ItemSpacing = ImVec2(10, 8);
        s.WindowPadding = ImVec2(14, 10);
        s.SeparatorTextPadding = ImVec2(12, 6);

        auto &c = s.Colors;
        // Window backgrounds
        c[ImGuiCol_WindowBg] = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
        c[ImGuiCol_ChildBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.60f);
        c[ImGuiCol_PopupBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.95f);

        // Frame backgrounds
        c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);

        // Title bars
        c[ImGuiCol_TitleBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.0f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
        c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.16f, 0.16f, 0.16f, 0.75f);
        c[ImGuiCol_MenuBarBg] = ImVec4(0.19f, 0.19f, 0.19f, 1.0f);

        // Scrollbar
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.0f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
        c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);

        // Headers
        c[ImGuiCol_Header] = ImVec4(0.17f, 0.36f, 0.53f, 1.0f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.45f, 0.62f, 1.0f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.8f);

        // Separators
        c[ImGuiCol_Separator] = ImVec4(0.35f, 0.35f, 0.35f, 0.50f);
        c[ImGuiCol_SeparatorHovered] = ImVec4(0.45f, 0.45f, 0.45f, 0.78f);
        c[ImGuiCol_SeparatorActive] = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);

        // Tab colors - Unity style (blue accent)
        c[ImGuiCol_Tab] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
        c[ImGuiCol_TabHovered] = ImVec4(0.32f, 0.32f, 0.32f, 1.0f);
        c[ImGuiCol_TabSelected] = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);
        c[ImGuiCol_TabSelectedOverline] = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
        c[ImGuiCol_TabDimmed] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
        c[ImGuiCol_TabDimmedSelected] = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
        c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);

        // Docking
        c[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
        c[ImGuiCol_DockingEmptyBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);

        // Border
        c[ImGuiCol_Border] = ImVec4(0.35f, 0.35f, 0.35f, 0.50f);
        c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        // Text
        c[ImGuiCol_Text] = ImVec4(0.86f, 0.86f, 0.86f, 1.0f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);

        // Buttons
        c[ImGuiCol_Button] = ImVec4(0.26f, 0.26f, 0.26f, 1.0f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.40f, 0.40f, 0.40f, 1.0f);

        // Checkmarks, sliders - blue accent
        c[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
        c[ImGuiCol_SliderGrab] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);

        // Resize grip
        c[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
        c[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
        c[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);

        // Plots
        c[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.0f);
        c[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.43f, 0.35f, 1.0f);
        c[ImGuiCol_PlotHistogram] = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
        c[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.60f, 0.0f, 1.0f);

        // Tables
        c[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.19f, 1.0f);
        c[ImGuiCol_TableBorderStrong] = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
        c[ImGuiCol_TableBorderLight] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
        c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);

        // Text selected
        c[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);

        // Nav highlight
        c[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
    }

    // UnrealEngine-like dark theme
    inline void ApplyUnrealTheme()
    {
        ImGui::StyleColorsDark(); // Reset ALL colors to dark theme base first
        ImGuiStyle &s = ImGui::GetStyle();
        s.WindowRounding = 0.0f; // Unreal uses sharp corners
        s.ChildRounding = 0.0f;
        s.FrameRounding = 0.0f;
        s.GrabRounding = 0.0f;
        s.PopupRounding = 0.0f;
        s.ScrollbarRounding = 0.0f;
        s.TabRounding = 0.0f;
        s.FramePadding = ImVec2(6, 4);
        s.ItemSpacing = ImVec2(6, 4);
        s.WindowPadding = ImVec2(8, 8);
        s.SeparatorTextPadding = ImVec2(8, 4);
        s.WindowBorderSize = 1.0f;
        s.FrameBorderSize = 0.0f;

        auto &c = s.Colors;
        // Main backgrounds - dark blue-gray
        c[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.0f);
        c[ImGuiCol_ChildBg] = ImVec4(0.07f, 0.07f, 0.07f, 1.0f);
        c[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.98f);

        // Frame backgrounds
        c[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);

        // Title bars
        c[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.0f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.04f, 0.04f, 0.04f, 1.0f);
        c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.04f, 0.04f, 0.04f, 0.75f);
        c[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);

        // Scrollbar
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.0f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
        c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);

        // Headers - orange accent
        c[ImGuiCol_Header] = ImVec4(0.90f, 0.60f, 0.10f, 0.45f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.90f, 0.60f, 0.10f, 0.60f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.90f, 0.60f, 0.10f, 0.80f);

        // Separators
        c[ImGuiCol_Separator] = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
        c[ImGuiCol_SeparatorHovered] = ImVec4(0.90f, 0.60f, 0.10f, 0.78f);
        c[ImGuiCol_SeparatorActive] = ImVec4(0.90f, 0.60f, 0.10f, 1.0f);

        // Tabs - Unreal style (orange accent)
        c[ImGuiCol_Tab] = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
        c[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
        c[ImGuiCol_TabSelected] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
        c[ImGuiCol_TabSelectedOverline] = ImVec4(0.90f, 0.60f, 0.10f, 1.0f);
        c[ImGuiCol_TabDimmed] = ImVec4(0.06f, 0.06f, 0.06f, 1.0f);
        c[ImGuiCol_TabDimmedSelected] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
        c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.50f, 0.35f, 0.10f, 1.0f);

        // Docking
        c[ImGuiCol_DockingPreview] = ImVec4(0.90f, 0.60f, 0.10f, 0.70f);
        c[ImGuiCol_DockingEmptyBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.0f);

        // Borders
        c[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
        c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        // Text
        c[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.0f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);

        // Buttons
        c[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.90f, 0.60f, 0.10f, 0.80f);

        // Sliders and checkboxes - orange accent
        c[ImGuiCol_SliderGrab] = ImVec4(0.90f, 0.60f, 0.10f, 0.80f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.90f, 0.60f, 0.10f, 1.0f);
        c[ImGuiCol_CheckMark] = ImVec4(0.90f, 0.60f, 0.10f, 1.0f);

        // Resize grip
        c[ImGuiCol_ResizeGrip] = ImVec4(0.25f, 0.25f, 0.25f, 0.50f);
        c[ImGuiCol_ResizeGripHovered] = ImVec4(0.90f, 0.60f, 0.10f, 0.67f);
        c[ImGuiCol_ResizeGripActive] = ImVec4(0.90f, 0.60f, 0.10f, 0.95f);

        // Plots - orange accent
        c[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.0f);
        c[ImGuiCol_PlotLinesHovered] = ImVec4(0.90f, 0.60f, 0.10f, 1.0f);
        c[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.60f, 0.10f, 1.0f);
        c[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.70f, 0.20f, 1.0f);

        // Tables
        c[ImGuiCol_TableHeaderBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
        c[ImGuiCol_TableBorderStrong] = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
        c[ImGuiCol_TableBorderLight] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
        c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.04f);

        // Text selected
        c[ImGuiCol_TextSelectedBg] = ImVec4(0.90f, 0.60f, 0.10f, 0.35f);

        // Nav highlight
        c[ImGuiCol_NavHighlight] = ImVec4(0.90f, 0.60f, 0.10f, 1.0f);
    }

    inline void ShowCpuGpuSummary(float cpuTotal, float cpuUpdates, float cpuDraw, float gpuTotal)
    {
        ImGuiTableFlags flags =
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_NoSavedSettings;

        if (ImGui::BeginTable("##cpu_gpu_summary", 3, flags, ImVec2(-FLT_MIN, 0)))
        {
            ImGui::TableSetupColumn("metric", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("rel", ImGuiTableColumnFlags_WidthStretch, 0.45f);
            ImGui::TableHeadersRow();

            auto Row = [](const char *name, float ms, float norm, float indent = 0.0f)
            {
                float rel = (norm > 0.0f) ? (ms / norm) : 0.0f;

                ImGui::TableNextRow();

                // metric (with optional indent)
                ImGui::TableSetColumnIndex(0);
                if (indent > 0.0f)
                    ImGui::Indent(indent);
                ImGui::TextUnformatted(name);
                if (indent > 0.0f)
                    ImGui::Unindent(indent);

                // ms (heat colored vs. norm)
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(Heat(rel), "%.3f ms", ms);

                // tiny relative bar
                ImGui::TableSetColumnIndex(2);
                TinyBar(rel, 10.0f);
            };

            // CPU block (children colored vs CPU Total)
            Row("CPU Total", cpuTotal, cpuTotal);
            Row("CPU Updates", cpuUpdates, cpuTotal, 12.0f);
            Row("CPU Draw", cpuDraw, cpuTotal, 12.0f);

            // GPU block (self-normalized)
            Row("GPU Total", gpuTotal, gpuTotal);

            ImGui::EndTable();
        }
    }

    inline void SetTextColorTemp(float time, float maxTime)
    {
        const ImVec4 startColor = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); // White
        const ImVec4 endColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);   // Red

        time = std::clamp(time / maxTime, 0.0f, 1.0f);

        ImVec4 resultColor(
            startColor.x + time * (endColor.x - startColor.x), // Red
            startColor.y + time * (endColor.y - startColor.y), // Green
            startColor.z + time * (endColor.z - startColor.z), // Blue
            1.0f                                               // Alpha
        );

        ImGui::GetStyle().Colors[ImGuiCol_Text] = resultColor;
    }

    inline void ResetTextColor()
    {
        ImGui::GetStyle().Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    }

    inline void SetInitialWindowSizeFraction(float widthFraction, float heightFraction = -1.0f, ImGuiCond cond = ImGuiCond_FirstUseEver)
    {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImVec2 size = ImVec2(
            viewport->WorkSize.x * widthFraction,
            viewport->WorkSize.y * (heightFraction > 0.0f ? heightFraction : widthFraction));
        ImGui::SetNextWindowSize(size, cond);
    }
} // namespace pe::ui
