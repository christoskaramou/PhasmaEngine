#include "Animation/AnimationReferenceFrames.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace pe::AnimationReferenceFrames
{
    namespace
    {
        constexpr int kFormatVersion = 1;
        constexpr std::string_view kFrameToken = "{frame}";

        bool Fail(std::string &error, std::string message)
        {
            error = std::move(message);
            return false;
        }

        std::filesystem::path AbsolutePath(const std::filesystem::path &base,
                                           const std::filesystem::path &path,
                                           std::error_code &error)
        {
            const std::filesystem::path combined = path.is_absolute() ? path : base / path;
            if (combined.empty())
                return std::filesystem::current_path(error).lexically_normal();
            return std::filesystem::absolute(combined, error).lexically_normal();
        }

        bool IsImage(const std::filesystem::path &path)
        {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
                           { return static_cast<char>(std::tolower(character)); });
            return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
        }

        unsigned char Lower(unsigned char character)
        {
            return character >= 'A' && character <= 'Z' ? static_cast<unsigned char>(character + ('a' - 'A')) : character;
        }

        bool NaturalStringLess(std::string_view left, std::string_view right)
        {
            std::size_t l = 0, r = 0;
            while (l < left.size() && r < right.size())
            {
                const bool leftDigit = std::isdigit(static_cast<unsigned char>(left[l])) != 0;
                const bool rightDigit = std::isdigit(static_cast<unsigned char>(right[r])) != 0;
                if (leftDigit && rightDigit)
                {
                    const std::size_t leftRun = l, rightRun = r;
                    while (l < left.size() && left[l] == '0')
                        ++l;
                    while (r < right.size() && right[r] == '0')
                        ++r;
                    const std::size_t leftSignificant = l, rightSignificant = r;
                    while (l < left.size() && std::isdigit(static_cast<unsigned char>(left[l])))
                        ++l;
                    while (r < right.size() && std::isdigit(static_cast<unsigned char>(right[r])))
                        ++r;

                    const std::size_t leftDigits = l - leftSignificant, rightDigits = r - rightSignificant;
                    if (leftDigits != rightDigits)
                        return leftDigits < rightDigits;
                    const int numberOrder = left.substr(leftSignificant, leftDigits).compare(right.substr(rightSignificant, rightDigits));
                    if (numberOrder != 0)
                        return numberOrder < 0;
                    const std::size_t leftRunLength = l - leftRun, rightRunLength = r - rightRun;
                    if (leftRunLength != rightRunLength)
                        return leftRunLength < rightRunLength;
                    continue;
                }

                const unsigned char a = Lower(static_cast<unsigned char>(left[l]));
                const unsigned char b = Lower(static_cast<unsigned char>(right[r]));
                if (a != b)
                    return a < b;
                ++l;
                ++r;
            }
            if (left.size() != right.size())
                return left.size() < right.size();
            return left < right;
        }

        bool ReadNumber(const nlohmann::json &root, const char *key, double &value, std::string &error, bool required)
        {
            const auto it = root.find(key);
            if (it == root.end())
                return !required || Fail(error, std::string("missing number '") + key + "'");
            if (!it->is_number())
                return Fail(error, std::string("'") + key + "' must be a finite number");
            value = it->get<double>();
            return std::isfinite(value) || Fail(error, std::string("'") + key + "' must be a finite number");
        }

        bool ReadBool(const nlohmann::json &root, const char *key, bool &value, std::string &error)
        {
            const auto it = root.find(key);
            if (it == root.end())
                return true;
            if (!it->is_boolean())
                return Fail(error, std::string("'") + key + "' must be a boolean");
            value = it->get<bool>();
            return true;
        }

        bool ReadInt(const nlohmann::json &object, const char *key, int &value, std::string &error)
        {
            const auto it = object.find(key);
            if (it == object.end())
                return Fail(error, std::string("missing integer '") + key + "'");

            if (it->is_number_unsigned())
            {
                const std::uint64_t parsed = it->get<std::uint64_t>();
                if (parsed <= static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
                {
                    value = static_cast<int>(parsed);
                    return true;
                }
            }
            else if (it->is_number_integer())
            {
                const std::int64_t parsed = it->get<std::int64_t>();
                if (parsed >= std::numeric_limits<int>::min() && parsed <= std::numeric_limits<int>::max())
                {
                    value = static_cast<int>(parsed);
                    return true;
                }
            }
            return Fail(error, std::string("'") + key + "' must be an integer in the 32-bit range");
        }

        bool ValidateConfig(const Config &config, std::string &error)
        {
            if (config.source != Source::Files && config.source != Source::Directory && config.source != Source::Pattern)
                return Fail(error, "reference frame source is invalid");
            if (config.playback != Playback::Clamp && config.playback != Playback::Loop)
                return Fail(error, "reference frame playback is invalid");
            if (!std::isfinite(config.sourceFps) || config.sourceFps <= 0.0)
                return Fail(error, "source_fps must be a positive finite number");
            if (!std::isfinite(config.timelineOffsetSeconds))
                return Fail(error, "timeline_offset_seconds must be finite");
            if (!std::isfinite(config.opacity) || config.opacity < 0.f || config.opacity > 1.f)
                return Fail(error, "opacity must be finite and between 0 and 1");
            if (!std::isfinite(config.scale) || config.scale <= 0.f)
                return Fail(error, "scale must be a positive finite number");
            if (!std::isfinite(config.offset.x) || !std::isfinite(config.offset.y))
                return Fail(error, "offset must contain two finite numbers");

            if (config.source == Source::Files)
            {
                if (config.files.empty())
                    return Fail(error, "Reference file list must not be empty");
                if (config.files.size() > kSequenceFrameLimit)
                    return Fail(error, "Reference file list exceeds the 100000-frame limit");
                for (std::size_t i = 0; i < config.files.size(); ++i)
                    if (config.files[i].empty())
                        return Fail(error, "Reference file path " + std::to_string(i) + " must not be empty");
            }
            else if (config.source == Source::Directory)
            {
                if (config.directory.empty())
                    return Fail(error, "Reference frame directory must not be empty");
            }
            else
            {
                const std::size_t token = config.pattern.find(kFrameToken);
                if (token == std::string::npos || config.pattern.find(kFrameToken, token + kFrameToken.size()) != std::string::npos)
                    return Fail(error, "Reference frame pattern must contain exactly one '{frame}' token");
                if (config.patternEnd < config.patternStart)
                    return Fail(error, "Reference frame pattern end must be greater than or equal to start");
                const std::int64_t frameCount = static_cast<std::int64_t>(config.patternEnd) - config.patternStart + 1;
                if (frameCount > static_cast<std::int64_t>(kSequenceFrameLimit))
                    return Fail(error, "Reference frame pattern exceeds the 100000-frame limit");
            }
            return true;
        }

        bool AppendFrame(const std::filesystem::path &path, Sequence &sequence, std::string &error)
        {
            if (!IsImage(path))
                return Fail(error, "Reference frame is not a PNG/JPG image: " + path.generic_string());
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec) || ec)
                return Fail(error, "Reference frame file is missing or unreadable: " + path.generic_string());
            sequence.frames.push_back(path);
            return true;
        }
    } // namespace

    bool LoadConfig(const std::filesystem::path &jsonPath, Config &config, std::string &error)
    {
        error.clear();
        std::error_code ec;
        const std::filesystem::path absoluteJson = AbsolutePath({}, jsonPath, ec);
        if (ec)
            return Fail(error, "Could not resolve reference config path: " + ec.message());

        std::ifstream input(absoluteJson, std::ios::binary);
        if (!input)
            return Fail(error, "Could not open reference config: " + absoluteJson.generic_string());
        const nlohmann::json root = nlohmann::json::parse(input, nullptr, false);
        if (root.is_discarded() || !root.is_object())
            return Fail(error, "Reference config must be a valid JSON object");
        if (!root.contains("version") || !root["version"].is_number_integer() || root["version"] != kFormatVersion)
            return Fail(error, "Unsupported or missing reference config version (expected 1)");

        Config loaded;
        loaded.baseDirectory = absoluteJson.parent_path();
        if (!ReadNumber(root, "source_fps", loaded.sourceFps, error, true) ||
            !ReadNumber(root, "timeline_offset_seconds", loaded.timelineOffsetSeconds, error, false))
            return false;

        double opacity = loaded.opacity, scale = loaded.scale;
        if (!ReadNumber(root, "opacity", opacity, error, false) || !ReadNumber(root, "scale", scale, error, false))
            return false;
        if (opacity < -std::numeric_limits<float>::max() || opacity > std::numeric_limits<float>::max() ||
            scale < -std::numeric_limits<float>::max() || scale > std::numeric_limits<float>::max())
            return Fail(error, "opacity and scale must fit in finite 32-bit floats");
        loaded.opacity = static_cast<float>(opacity);
        loaded.scale = static_cast<float>(scale);

        if (!ReadBool(root, "flip_x", loaded.flipX, error) || !ReadBool(root, "flip_y", loaded.flipY, error))
            return false;
        if (const auto playback = root.find("playback"); playback != root.end())
        {
            if (!playback->is_string())
                return Fail(error, "'playback' must be 'clamp' or 'loop'");
            const std::string &value = playback->get_ref<const std::string &>();
            if (value == "clamp")
                loaded.playback = Playback::Clamp;
            else if (value == "loop")
                loaded.playback = Playback::Loop;
            else
                return Fail(error, "'playback' must be 'clamp' or 'loop'");
        }
        if (const auto offset = root.find("offset"); offset != root.end())
        {
            if (!offset->is_array() || offset->size() != 2 || !(*offset)[0].is_number() || !(*offset)[1].is_number())
                return Fail(error, "'offset' must be an array of two finite numbers");
            const double x = (*offset)[0].get<double>(), y = (*offset)[1].get<double>();
            if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > std::numeric_limits<float>::max() ||
                std::abs(y) > std::numeric_limits<float>::max())
                return Fail(error, "'offset' must be an array of two finite numbers");
            loaded.offset = vec2(static_cast<float>(x), static_cast<float>(y));
        }

        const bool hasFiles = root.contains("files"), hasDirectory = root.contains("directory"), hasPattern = root.contains("pattern");
        if (static_cast<int>(hasFiles) + static_cast<int>(hasDirectory) + static_cast<int>(hasPattern) != 1)
            return Fail(error, "Reference config must contain exactly one of 'files', 'directory', or 'pattern'");

        if (hasFiles)
        {
            loaded.source = Source::Files;
            if (!root["files"].is_array() || root["files"].empty())
                return Fail(error, "'files' must be a non-empty array of paths");
            if (root["files"].size() > kSequenceFrameLimit)
                return Fail(error, "'files' exceeds the 100000-frame limit");
            for (const nlohmann::json &file : root["files"])
            {
                if (!file.is_string() || file.get_ref<const std::string &>().empty())
                    return Fail(error, "'files' must contain only non-empty path strings");
                loaded.files.emplace_back(file.get_ref<const std::string &>());
            }
        }
        else if (hasDirectory)
        {
            loaded.source = Source::Directory;
            if (!root["directory"].is_string() || root["directory"].get_ref<const std::string &>().empty())
                return Fail(error, "'directory' must be a non-empty path string");
            loaded.directory = root["directory"].get_ref<const std::string &>();
        }
        else
        {
            loaded.source = Source::Pattern;
            const nlohmann::json &pattern = root["pattern"];
            if (!pattern.is_object() || !pattern.contains("path") || !pattern["path"].is_string() ||
                pattern["path"].get_ref<const std::string &>().empty())
                return Fail(error, "'pattern' must contain a non-empty string 'path'");
            loaded.pattern = pattern["path"].get_ref<const std::string &>();
            if (!ReadInt(pattern, "start", loaded.patternStart, error) || !ReadInt(pattern, "end", loaded.patternEnd, error))
                return false;
        }

        if (!ValidateConfig(loaded, error))
            return false;
        config = std::move(loaded);
        return true;
    }

    bool BuildSequence(const Config &config, Sequence &sequence, std::string &error)
    {
        error.clear();
        if (!ValidateConfig(config, error))
            return false;

        Sequence built;
        built.config = config;
        std::error_code ec;
        const std::filesystem::path base = AbsolutePath({}, config.baseDirectory, ec);
        if (ec)
            return Fail(error, "Could not resolve reference base directory: " + ec.message());
        built.config.baseDirectory = base;

        if (config.source == Source::Files)
        {
            for (std::size_t i = 0; i < config.files.size(); ++i)
            {
                const std::filesystem::path path = AbsolutePath(base, config.files[i], ec);
                if (ec)
                    return Fail(error, "Could not resolve reference frame path: " + ec.message());
                if (!AppendFrame(path, built, error))
                    return false;
            }
        }
        else if (config.source == Source::Directory)
        {
            const std::filesystem::path directory = AbsolutePath(base, config.directory, ec);
            if (ec || !std::filesystem::is_directory(directory, ec) || ec)
                return Fail(error, "Reference frame directory is missing or unreadable: " + directory.generic_string());

            std::vector<std::filesystem::path> files;
            std::filesystem::directory_iterator it(directory, ec), end;
            if (ec)
                return Fail(error, "Could not scan reference frame directory: " + ec.message());
            std::size_t scanned = 0;
            for (; it != end; it.increment(ec))
            {
                if (ec)
                    return Fail(error, "Could not scan reference frame directory: " + ec.message());
                if (++scanned > kDirectoryScanLimit)
                    return Fail(error, "Reference frame directory exceeds the 4096-entry scan limit: " +
                                           directory.generic_string());
                if (it->is_regular_file(ec) && !ec && IsImage(it->path()))
                    files.push_back(it->path().lexically_normal());
                else if (ec)
                    return Fail(error, "Could not inspect reference frame directory entry: " + ec.message());
            }
            if (ec)
                return Fail(error, "Could not scan reference frame directory: " + ec.message());
            std::sort(files.begin(), files.end(), [](const std::filesystem::path &left, const std::filesystem::path &right)
                      {
                          const std::string a = left.filename().generic_string(), b = right.filename().generic_string();
                          return NaturalStringLess(a, b); });
            if (files.empty())
                return Fail(error, "Reference frame directory contains no PNG/JPG images: " + directory.generic_string());
            for (std::size_t i = 0; i < files.size(); ++i)
                if (!AppendFrame(files[i], built, error))
                    return false;
        }
        else
        {
            const std::size_t token = config.pattern.find(kFrameToken);
            const std::int64_t frameCount = static_cast<std::int64_t>(config.patternEnd) - config.patternStart + 1;

            built.frames.reserve(static_cast<std::size_t>(frameCount));
            for (int frame = config.patternStart;; ++frame)
            {
                std::string pathText = config.pattern;
                pathText.replace(token, kFrameToken.size(), std::to_string(frame));
                const std::filesystem::path path = AbsolutePath(base, pathText, ec);
                if (ec)
                    return Fail(error, "Could not resolve reference frame pattern path: " + ec.message());
                if (!AppendFrame(path, built, error))
                    return false;
                if (frame == config.patternEnd)
                    break;
            }
        }

        sequence = std::move(built);
        return true;
    }

    Lookup Resolve(const Sequence &sequence, double timelineTimeSeconds)
    {
        Lookup result;
        if (!std::isfinite(timelineTimeSeconds))
        {
            result.error = "Timeline time must be finite";
            return result;
        }
        if (sequence.frames.empty())
        {
            result.error = "Reference frame sequence is empty";
            return result;
        }
        if ((sequence.config.playback != Playback::Clamp && sequence.config.playback != Playback::Loop) ||
            !std::isfinite(sequence.config.sourceFps) || sequence.config.sourceFps <= 0.0 ||
            !std::isfinite(sequence.config.timelineOffsetSeconds))
        {
            result.error = "Reference frame sequence timing or playback is invalid";
            return result;
        }

        const double framePosition = (timelineTimeSeconds - sequence.config.timelineOffsetSeconds) * sequence.config.sourceFps;
        if (!std::isfinite(framePosition))
        {
            result.error = "Timeline time is outside the supported numeric range";
            return result;
        }

        const double frame = std::floor(framePosition);
        const double count = static_cast<double>(sequence.frames.size());
        if (sequence.config.playback == Playback::Loop)
        {
            double wrapped = std::fmod(frame, count);
            if (wrapped < 0.0)
                wrapped += count;
            result.sequenceIndex = static_cast<std::size_t>(wrapped);
            result.wrapped = frame < 0.0 || frame >= count;
        }
        else if (frame < 0.0)
        {
            result.sequenceIndex = 0;
            result.clamped = true;
        }
        else if (frame >= count)
        {
            result.sequenceIndex = sequence.frames.size() - 1;
            result.clamped = true;
        }
        else
        {
            result.sequenceIndex = static_cast<std::size_t>(frame);
        }

        result.path = sequence.frames[result.sequenceIndex];
        return result;
    }
} // namespace pe::AnimationReferenceFrames
