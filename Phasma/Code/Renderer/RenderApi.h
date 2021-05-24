#pragma once

#include "Vulkan/Vulkan.h"
#ifdef WIN32
#include "Dx12/Dx12.h"
#endif

namespace pe
{
	enum class RenderApi
	{
		Vulkan,
#ifdef WIN32
		Dx12
#endif
	};
	
	inline RenderApi g_Api = RenderApi::Vulkan;

#define PE_VULKAN bool(g_Api == RenderApi::Vulkan)
#ifdef WIN32
#define PE_DX12 bool(g_Api == RenderApi::Dx12)
#endif
}