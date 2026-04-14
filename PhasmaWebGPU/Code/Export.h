#pragma once

#if defined(PWGPU_SHARED_LIBRARY)
#if defined(_WIN32)
#if defined(PWGPU_BUILDING)
#define PWGPU_API __declspec(dllexport)
#else
#define PWGPU_API __declspec(dllimport)
#endif
#else
#if defined(PWGPU_BUILDING)
#define PWGPU_API __attribute__((visibility("default")))
#else
#define PWGPU_API
#endif
#endif
#else
#define PWGPU_API
#endif
