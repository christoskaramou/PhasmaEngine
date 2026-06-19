#pragma once

#if defined(PE_WIN32)
#if defined(PE_EDITOR_MODULE_EXPORTS)
#define PE_EDITOR_MODULE_API __declspec(dllexport)
#else
#define PE_EDITOR_MODULE_API __declspec(dllimport)
#endif
#elif defined(PE_LINUX)
#if defined(PE_EDITOR_MODULE_EXPORTS)
#define PE_EDITOR_MODULE_API __attribute__((visibility("default")))
#else
#define PE_EDITOR_MODULE_API
#endif
#else
#define PE_EDITOR_MODULE_API
#endif
