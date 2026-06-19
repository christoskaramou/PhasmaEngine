#pragma once

#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <bitset>
#include <cassert>
#include <cctype>
#include <chrono>
#include <codecvt>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <execution>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <regex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "SDL.h"
#if defined(PE_WIN32)
#include <share.h>
#elif defined(PE_LINUX) || defined(PE_ANDROID)
#if defined(PE_LINUX)
#include <cxxabi.h>
#include <sys/sysinfo.h>
#endif
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(PE_LINUX)
// Undefine X11 macros that conflict with third-party libraries (httplib, etc.)
#ifdef Complex
#undef Complex
#endif
#ifdef Success
#undef Success
#endif
#endif
#endif

#include "Base/Log.h"
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
#include "Base/Profiler.h"
#include "Base/FileSystem.h"
#include "Base/FileWatcher.h"
#include "Base/EventSystem.h"
#include "Base/Resource.h"
#include "Base/ResourceManager.h"

#include "ECS/Component.h"
#include "ECS/System.h"
#include "ECS/Entity.h"
#include "ECS/Context.h"

#include "API/Debug.h"

#define STBI_MSC_SECURE_CRT
