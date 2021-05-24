/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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
		Dx12 // Not implemented yet
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