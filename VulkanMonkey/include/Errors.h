#ifndef ERRORS_H
#define ERRORS_H

#include <iostream>

#ifdef _DEBUG
#define check(x)																							\
{																											\
	vk::Result res = (x);																					\
	if (res != vk::Result::eSuccess)																		\
	{																										\
		std::cout << vk::to_string(res) << "\" in " << __FILE__ << " at line " << __LINE__ << std::endl;	\
		system("PAUSE");																					\
		exit(-1);																							\
	}																										\
}																											\

#else
#define check(x) x
#endif // _DEBUG
#endif // ERRORS_H
