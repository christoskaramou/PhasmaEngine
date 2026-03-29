#pragma once

#if defined(PE_WIN32)
#if defined(PE_PHASMACORE_STATIC)
#define PE_API
#elif defined(PE_PHASMACORE_EXPORTS)
#define PE_API __declspec(dllexport)
#else
#define PE_API __declspec(dllimport)
#endif
#elif defined(PE_LINUX)
#if defined(PE_PHASMACORE_EXPORTS)
#define PE_API __attribute__((visibility("default")))
#else
#define PE_API
#endif
#else
#define PE_API
#endif
