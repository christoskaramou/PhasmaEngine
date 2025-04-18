#pragma once

#include <string>
#include <vector>
#include <list>
#include <memory>
#include <type_traits>
#include <typeinfo>
#include <typeindex>
#include <future>
#include <queue>
#include <thread>
#include <condition_variable>
#include <stdexcept>
#include <cassert>
#include <tuple>
#include <deque>
#include <stack>
#include <any>
#include <mutex>
#include <shared_mutex>
#include <map>
#include <set>
#include <unordered_map>
#include <array>
#include <functional>
#include <utility>
#include <algorithm>
#include <execution>
#include <unordered_set>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <limits>
#include <chrono>
#include <random>
#include <regex>
#include <atomic>
#include <variant>
#include <bitset>
#include <codecvt>
#include <optional>

#include "SDL2/SDL.h"
#include "SDL2/SDL_syswm.h"
#include "SDL2/SDL_vulkan.h"
#include "vulkan/vulkan.h"
#include "vma/vk_mem_alloc.h"
#if defined(PE_WIN32)
#include <dxgi1_6.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#else
#include <dlfcn.h>
#endif

#include "Base/Defines.h"
#include "Base/Enums.h"
#include "Base/Settings.h"
#include "Base/Hash.h"
#include "Base/Math.h"
#include "Base/Base.h"
#include "Base/Path.h"
#include "Base/ThreadPool.h"
#include "Base/Delegate.h"
#include "Base/Timer.h"
#include "Base/FileSystem.h"
#include "Base/FileWatcher.h"
#include "Base/EventSystem.h"
#include "Base/Externs.h"

#include "ECS/Component.h"
#include "ECS/System.h"
#include "ECS/Entity.h"
#include "ECS/Context.h"

#include "API/Debug.h"

#define STBI_MSC_SECURE_CRT