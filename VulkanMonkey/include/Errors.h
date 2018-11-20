#pragma once
#include <iostream>
#include "Vulkan.h"

namespace vm {
#ifdef _DEBUG
#define VkCheck(x)																							\
{																											\
	vk::Result res = (x);																					\
	if (res != vk::Result::eSuccess)																		\
	{																										\
		std::cout << vk::to_string(res) << " in " << __FILE__ << " at line " << __LINE__ << std::endl;		\
		exit(-1);																							\
	}																										\
}																											\

#else
#define VkCheck(x) x
#endif // _DEBUG
}