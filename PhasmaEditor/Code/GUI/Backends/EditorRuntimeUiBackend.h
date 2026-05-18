#pragma once

#include "UI/RuntimeUi.h"

namespace pe
{
    std::unique_ptr<IRuntimeUiBackend> CreateEditorRuntimeUiBackend();
} // namespace pe
