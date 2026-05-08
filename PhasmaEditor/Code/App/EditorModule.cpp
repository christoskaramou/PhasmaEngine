#include "App/EditorModule.h"
#include "App/App.h"
#include "Base/Path.h"
#include "GUI/GUI.h"
#include "imgui/imgui.h"

namespace
{
    pe::App *g_app = nullptr;
}

extern "C"
{
    PE_EDITOR_MODULE_API bool TickEditorModule()
    {
        pe::Path::Init();
        if (!g_app)
            g_app = new pe::App();
        return g_app->Frame();
    }

    PE_EDITOR_MODULE_API void RenderReloadFrameEditorModule()
    {
        if (g_app)
            g_app->RenderReloadFrame();
    }

    PE_EDITOR_MODULE_API void DestroyEditorModule()
    {
        delete g_app;
        g_app = nullptr;
    }

    PE_EDITOR_MODULE_API void *GetImGuiContextEditorModule()
    {
        ImGuiContext *ctx = ImGui::GetCurrentContext();
        if (!ctx)
            return nullptr;

        ImGuiIO &io = ImGui::GetIO();
        if (io.IniFilename)
            ImGui::SaveIniSettingsToDisk(io.IniFilename);
        if (g_app)
            g_app->ReleaseImGuiContext();

        return static_cast<void *>(ctx);
    }

    PE_EDITOR_MODULE_API void InitEditorModuleWithContext(void *imguiCtx)
    {
        pe::Path::Init();
        ImGuiContext *ctx = static_cast<ImGuiContext *>(imguiCtx);
        ImGui::SetCurrentContext(ctx);
        pe::GUI::SetHotReloadContext(ctx);
        if (!g_app)
            g_app = new pe::App();
    }
}
