#pragma once

#define NOMINMAX

#include <string>
#include <vector>
#include <memory>
#include <type_traits>
#include <typeinfo>
#include <typeindex>
#include <future>
#include <cassert>
#include <tuple>
#include <deque>
#include <any>
#include <mutex>
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

#include "SDL/SDL.h"
#include "SDL/SDL_syswm.h"
#include "SDL/SDL_vulkan.h"
#include <vulkan/vulkan.h>
#include "vma/vk_mem_alloc.h"
#ifdef WIN32
    #include <dxgi1_6.h>
    #include <d3d12.h>
    #include <wrl.h>
    #include <d3d12sdklayers.h>
#endif

#include "Core/Defines.h"
#include "Core/Enums.h"
#include "Core/Settings.h"
#include "Core/Hash.h"
#include "Core/Math.h"
#include "Core/Base.h"
#include "Core/Path.h"
#include "Core/Delegate.h"
#include "Core/SyncQueue.h"
#include "Core/Timer.h"
#include "Core/FileSystem.h"
#include "Core/EventSystem.h"
#include "Core/Externs.h"

#include "ECS/Component.h"
#include "ECS/System.h"
#include "ECS/Entity.h"
#include "ECS/Context.h"

#include "Renderer/Debug.h"

#define STBI_MSC_SECURE_CRT