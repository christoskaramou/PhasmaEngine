#pragma once

// Builds the Navigation sources outside the engine (a game's headless check, the native nav tests): force-include this
// for what the engine's precompiled header gives them. The engine itself never includes it.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace pe
{
    using vec2 = glm::vec2;
    using vec3 = glm::vec3;
} // namespace pe

// One line each, as the engine's log.
#ifndef PE_WARN
#define PE_WARN(...) (std::fprintf(stderr, __VA_ARGS__), std::fputc('\n', stderr))
#endif
#ifndef PE_ERROR
#define PE_ERROR(...) (std::fprintf(stderr, __VA_ARGS__), std::fputc('\n', stderr))
#endif
