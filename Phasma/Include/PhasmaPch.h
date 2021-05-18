#pragma once

#define NOMINMAX
#include "SDL/SDL.h"
#include "SDL/SDL_syswm.h"
#include "SDL/SDL_vulkan.h"
#include <vulkan/vulkan.hpp>
#include "vma/vk_mem_alloc.h"
#ifdef WIN32
    #include <DX12/d3d12.h>
    #ifdef _DEBUG
        #include <DX12/d3d12sdklayers.h>
    #endif
#endif