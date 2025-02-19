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
#else
#include <dlfcn.h>
#endif

#include "Core/Defines.h"
#include "Core/Enums.h"
#include "Core/Settings.h"
#include "Core/Hash.h"
#include "Core/Math.h"
#include "Core/Base.h"
#include "Core/Path.h"
#include "Core/ThreadPool.h"
#include "Core/Delegate.h"
#include "Core/Timer.h"
#include "Core/FileSystem.h"
#include "Core/FileWatcher.h"
#include "Core/EventSystem.h"
#include "Core/Externs.h"

#include "ECS/Component.h"
#include "ECS/System.h"
#include "ECS/Entity.h"
#include "ECS/Context.h"

#include "Renderer/Debug.h"

#define STBI_MSC_SECURE_CRT