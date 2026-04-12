#pragma once

#include <webgpu/webgpu.h>

namespace pwgpu
{

    struct ErrorScope
    {
        WGPUErrorFilter filter = WGPUErrorFilter_Validation;
        WGPUPopErrorScopeCallbackInfo callbackInfo{};
        bool hasCapturedError = false;
        WGPUErrorType capturedErrorType = WGPUErrorType_NoError;
        std::string capturedErrorMessage;
    };

} // namespace pwgpu
