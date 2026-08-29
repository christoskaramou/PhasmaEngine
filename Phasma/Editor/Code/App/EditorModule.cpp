#include "App/EditorModule.h"
#include "App/App.h"

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
}
