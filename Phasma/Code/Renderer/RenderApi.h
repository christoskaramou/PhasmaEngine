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
	
// Note:
// If DYNAMIC_RENDER_API is defined, all checks for RenderApi will be done dynamically
// Else the RenderApi will be picked at compile time
// Using DYNAMIC_RENDER_API will have some overhead but it will allow RenderApi to be changed at runtime

//                  DYNAMIC RENDERING API
// ------------------------------------------------------------//
//#define DYNAMIC_RENDER_API
// ------------------------------------------------------------//

//                  DEFAULT RENDERING API
// ------------------------------------------------------------//
	constexpr RenderApi defaultRenderApi = RenderApi::Vulkan;
// ------------------------------------------------------------//

#ifdef DYNAMIC_RENDER_API
	#define DYNAMIC_CONSTEXPR
	
	inline RenderApi g_Api = defaultRenderApi;
	
	#define PE_VULKAN bool(g_Api == RenderApi::Vulkan)
	#ifdef WIN32
		#define PE_DX12 bool(g_Api == RenderApi::Dx12)
	#endif
#else
	#define DYNAMIC_CONSTEXPR constexpr
	
	constexpr RenderApi g_Api = defaultRenderApi;
	
	constexpr bool PE_VULKAN = g_Api == RenderApi::Vulkan;
	#ifdef WIN32
		constexpr bool PE_DX12 = g_Api == RenderApi::Dx12;
	#endif
#endif
}