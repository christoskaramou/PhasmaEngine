#pragma once

#include "UI/RuntimeUi.h"

namespace pe
{
    std::unique_ptr<IRuntimeUiBackend> CreateImGuiRuntimeUiBackend();
} // namespace pe
