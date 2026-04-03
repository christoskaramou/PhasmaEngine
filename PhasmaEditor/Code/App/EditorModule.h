#pragma once
#include "App/EditorModuleExport.h"

extern "C"
{
    PE_EDITOR_MODULE_API bool TickEditorModule();
    PE_EDITOR_MODULE_API void DestroyEditorModule();
}
