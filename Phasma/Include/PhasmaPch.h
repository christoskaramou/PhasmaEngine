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

#define NOMINMAX

#include <string>
#include <vector>
#include <memory>
#include <type_traits>
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

#include "SDL/SDL.h"
#include "SDL/SDL_syswm.h"
#include "SDL/SDL_vulkan.h"
#include <vulkan/vulkan.h>
#include "vma/vk_mem_alloc.h"
#ifdef WIN32
    #include <DX12/d3d12.h>
    #ifdef _DEBUG
        #include <DX12/d3d12sdklayers.h>
    #endif
#endif

#include "Core/Settings.h"
#include "Core/Hash.h"
#include "Core/Math.h"
#include "Core/Base.h"
#include "Core/FileSystem.h"
#include "Core/Path.h"
#include "Core/Delegate.h"
#include "Core/SyncQueue.h"
#include "Core/Timer.h"

#include "ECS/ECSBase.h"
#include "ECS/Component.h"
#include "ECS/System.h"
#include "ECS/Entity.h"
#include "ECS/Context.h"

#include "Renderer/Debug.h"

#define STBI_MSC_SECURE_CRT