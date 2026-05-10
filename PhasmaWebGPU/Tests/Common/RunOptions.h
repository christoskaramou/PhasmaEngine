#pragma once

#include <SDL.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace pwgpu::test
{
    struct RunOptions
    {
        int displayIndex = 0;
        uint64_t exitAfterFrames = 0;
        double exitAfterSeconds = 0.0;
        std::string error;

        bool Succeeded() const { return error.empty(); }
    };

    inline const char *GetOptionValue(const char *argument, const char *prefix)
    {
        if (!argument || !prefix)
            return nullptr;

        const size_t prefixLength = std::strlen(prefix);
        return std::strncmp(argument, prefix, prefixLength) == 0 ? argument + prefixLength : nullptr;
    }

    inline bool TryParseDisplayIndex(const char *value, int &displayIndex)
    {
        if (!value || *value == '\0')
            return false;

        char *end = nullptr;
        const long parsed = std::strtol(value, &end, 10);
        if (*end != '\0' || parsed < 0 || parsed > std::numeric_limits<int>::max())
            return false;

        displayIndex = static_cast<int>(parsed);
        return true;
    }

    inline RunOptions ParseRunOptions(int argc, char *argv[])
    {
        RunOptions options{};

        for (int i = 1; i < argc; ++i)
        {
            if (const char *value = GetOptionValue(argv[i], "--display="))
            {
                if (!TryParseDisplayIndex(value, options.displayIndex))
                    options.error = std::string("Invalid display index: ") + value;
            }
            else if (const char *value = GetOptionValue(argv[i], "--screen="))
            {
                if (!TryParseDisplayIndex(value, options.displayIndex))
                    options.error = std::string("Invalid display index: ") + value;
            }
            else if (std::strcmp(argv[i], "--display") == 0 || std::strcmp(argv[i], "--screen") == 0)
            {
                if (i + 1 >= argc || !TryParseDisplayIndex(argv[++i], options.displayIndex))
                    options.error = "Invalid display index";
            }
            else if (const char *value = GetOptionValue(argv[i], "--exit-after-frames="))
            {
                options.exitAfterFrames = static_cast<uint64_t>(std::strtoull(value, nullptr, 10));
            }
            else if (const char *value = GetOptionValue(argv[i], "--exit-after-seconds="))
            {
                options.exitAfterSeconds = std::strtod(value, nullptr);
            }

            if (!options.Succeeded())
                return options;
        }

        return options;
    }

    inline bool ValidateDisplayIndex(int displayIndex, std::string &error)
    {
        const int displayCount = SDL_GetNumVideoDisplays();
        if (displayCount <= 0)
        {
            error = std::string("SDL reports no video displays: ") + SDL_GetError();
            return false;
        }

        if (displayIndex >= displayCount)
        {
            error = "Invalid --display " + std::to_string(displayIndex) + "; SDL reports " +
                    std::to_string(displayCount) + " display(s)";
            return false;
        }

        return true;
    }
} // namespace pwgpu::test
