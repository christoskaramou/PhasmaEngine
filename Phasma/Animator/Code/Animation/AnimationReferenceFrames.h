#pragma once

#include "Base/Math.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace pe::AnimationReferenceFrames
{
    inline constexpr std::size_t kDirectoryScanLimit = 4096;
    inline constexpr std::size_t kSequenceFrameLimit = 100000;

    enum class Source
    {
        Files,
        Directory,
        Pattern
    };

    enum class Playback
    {
        Clamp,
        Loop
    };

    struct Config
    {
        std::filesystem::path baseDirectory;
        Source source = Source::Files;
        std::vector<std::filesystem::path> files;
        std::filesystem::path directory;
        std::string pattern; // Exactly one {frame}; decimal frame numbers are inserted without padding.
        int patternStart = 0;
        int patternEnd = -1; // Inclusive.

        double sourceFps = 24.0;
        double timelineOffsetSeconds = 0.0;
        Playback playback = Playback::Clamp;
        float opacity = 1.f;
        float scale = 1.f;
        vec2 offset = vec2(0.f);
        bool flipX = false;
        bool flipY = false;
    };

    struct Sequence
    {
        Config config;
        std::vector<std::filesystem::path> frames; // Absolute, normalized, and ordered.
    };

    struct Lookup
    {
        std::filesystem::path path;
        std::size_t sequenceIndex = 0;
        bool clamped = false;
        bool wrapped = false;
        std::string error;

        explicit operator bool() const { return error.empty() && !path.empty(); }
    };

    // Version 1 JSON fields: source_fps, timeline_offset_seconds, playback (clamp/loop),
    // opacity, scale, offset [x,y], flip_x, flip_y, and exactly one source:
    // files ["..."], directory "...", or pattern {"path":"...{frame}...","start":0,"end":10}.
    bool LoadConfig(const std::filesystem::path &jsonPath, Config &config, std::string &error);

    // Resolves sources relative to baseDirectory, validates every selected image, and snapshots directory order.
    bool BuildSequence(const Config &config, Sequence &sequence, std::string &error);

    // Frame zero begins at timelineOffsetSeconds. Exact frame boundaries use floor; loop also wraps negative time.
    Lookup Resolve(const Sequence &sequence, double timelineTimeSeconds);

    // ponytail: pre-extracted PNG/JPG frames only; no bundled video decoder or external process execution.
    // Add a decoder only if image-sequence I/O proves inadequate.
} // namespace pe::AnimationReferenceFrames
