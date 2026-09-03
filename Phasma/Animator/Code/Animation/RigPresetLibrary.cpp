#include "Animation/RigPresetLibrary.h"

#include "Base/Base.h"
#include "Base/Path.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>

namespace pe
{
    namespace
    {
        constexpr int kRigPresetVersion = 1;

        bool ReadString(const nlohmann::json &object, const char *key, std::string &value, std::string &error,
                        bool required = true)
        {
            const auto it = object.find(key);
            if (it == object.end())
            {
                if (!required)
                    return true;
                error = std::string("missing string '") + key + "'";
                return false;
            }
            if (!it->is_string())
            {
                error = std::string("'") + key + "' must be a string";
                return false;
            }
            value = it->get<std::string>();
            if (required && value.empty())
            {
                error = std::string("'") + key + "' must not be empty";
                return false;
            }
            return true;
        }

        bool ReadVec3(const nlohmann::json &object, const char *key, vec3 &value, std::string &error)
        {
            const auto it = object.find(key);
            if (it == object.end() || !it->is_array() || it->size() != 3)
            {
                error = std::string("'") + key + "' must be an array of three finite numbers";
                return false;
            }

            float components[3]{};
            for (size_t i = 0; i < 3; ++i)
            {
                if (!(*it)[i].is_number())
                {
                    error = std::string("'") + key + "' must be an array of three finite numbers";
                    return false;
                }
                const double component = (*it)[i].get<double>();
                if (!std::isfinite(component) || std::abs(component) > std::numeric_limits<float>::max())
                {
                    error = std::string("'") + key + "' must be an array of three finite numbers";
                    return false;
                }
                components[i] = static_cast<float>(component);
            }
            value = vec3(components[0], components[1], components[2]);
            return true;
        }

        bool ReadRadius(const nlohmann::json &object, const char *key, float &value, std::string &error)
        {
            const auto it = object.find(key);
            if (it == object.end() || !it->is_number())
            {
                error = std::string("'") + key + "' must be a positive finite number";
                return false;
            }
            const double radius = it->get<double>();
            if (!std::isfinite(radius) || radius <= 0.0 || radius > std::numeric_limits<float>::max())
            {
                error = std::string("'") + key + "' must be a positive finite number";
                return false;
            }
            value = static_cast<float>(radius);
            return true;
        }

        bool ReadOptionalBool(const nlohmann::json &object, const char *key, bool &value, std::string &error)
        {
            const auto it = object.find(key);
            if (it == object.end())
                return true;
            if (!it->is_boolean())
            {
                error = std::string("'") + key + "' must be a boolean";
                return false;
            }
            value = it->get<bool>();
            return true;
        }

        bool ReadLimit(const nlohmann::json &object, float &swing, float &twist, std::string &error)
        {
            const auto limit = object.find("limit");
            if (limit == object.end())
                return true;
            if (!limit->is_object())
            {
                error = "'limit' must be an object";
                return false;
            }
            auto degrees = [&](const char *key, float &value)
            {
                const auto it = limit->find(key);
                if (it == limit->end() || !it->is_number())
                {
                    error = std::string("'limit.") + key + "' must be a finite number from 0 to 180";
                    return false;
                }
                const double number = it->get<double>();
                if (!std::isfinite(number) || number < 0.0 || number > 180.0)
                {
                    error = std::string("'limit.") + key + "' must be a finite number from 0 to 180";
                    return false;
                }
                value = static_cast<float>(number);
                return true;
            };
            return degrees("swing_degrees", swing) && degrees("twist_degrees", twist);
        }

        bool ReadShellPatterns(const nlohmann::json &object, std::vector<std::string> &patterns, std::string &error)
        {
            const auto it = object.find("shell_patterns");
            if (it == object.end())
                return true;
            if (!it->is_array())
            {
                error = "'shell_patterns' must be an array of non-empty strings";
                return false;
            }
            for (const nlohmann::json &pattern : *it)
            {
                if (!pattern.is_string() || pattern.get_ref<const std::string &>().empty())
                {
                    error = "'shell_patterns' must be an array of non-empty strings";
                    return false;
                }
                patterns.push_back(pattern.get<std::string>());
            }
            return true;
        }

        bool GlobMatches(std::string_view pattern, std::string_view text)
        {
            size_t p = 0, t = 0, star = std::string_view::npos, retry = 0;
            while (t < text.size())
            {
                if (p < pattern.size() &&
                    (pattern[p] == '?' || std::tolower(static_cast<unsigned char>(pattern[p])) ==
                                              std::tolower(static_cast<unsigned char>(text[t]))))
                {
                    ++p;
                    ++t;
                }
                else if (p < pattern.size() && pattern[p] == '*')
                {
                    star = p++;
                    retry = t;
                }
                else if (star != std::string_view::npos)
                {
                    p = star + 1;
                    t = ++retry;
                }
                else
                {
                    return false;
                }
            }
            while (p < pattern.size() && pattern[p] == '*')
                ++p;
            return p == pattern.size();
        }

        vec3 FitPoint(const vec3 &point, const AABB &bounds)
        {
            const vec3 size = bounds.GetSize();
            const vec3 center = bounds.GetCenter();
            return vec3(center.x + point.x * size.x * 0.5f, bounds.min.y + point.y * size.y,
                        center.z + point.z * size.z * 0.5f);
        }

        bool IsFinite(const vec3 &value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    } // namespace

    namespace
    {
        // X: -1..1 over the half-width (+X = the character's left, .L), Y: 0..1 over the height,
        // Z: -1..1 over the half-depth (+Z forward). Radii are fractions of the height. Bones named
        // *.L are mirrored to *.R automatically; "{s}" in a shell pattern expands to that side's letter.
        struct BuiltInBone
        {
            const char *name, *parent;
            vec3 head, tail;
            float headRadius, tailRadius;
            const char *patterns = ""; // comma separated shell globs, specific first
            bool spline = false;
        };

        // Y stations of the humanoid layout, shared by every preset that reuses its torso.
        const RigPresetLandmarks kBipedLandmarks = {0.f, 0.50f, 0.80f, 0.87f, 1.f, 0.20f, 0.34f};

        const BuiltInBone kHumanoid[] = {
            {"hips", "", {0.f, 0.50f, 0.f}, {0.f, 0.58f, 0.f}, 0.075f, 0.07f, "hips,pelvis,*hip*,*pelvis*"},
            {"spine", "hips", {0.f, 0.58f, 0.f}, {0.f, 0.68f, 0.f}, 0.07f, 0.075f, "spine,body,*spine*,*torso*,*body*"},
            {"chest", "spine", {0.f, 0.68f, 0.f}, {0.f, 0.80f, 0.f}, 0.08f, 0.085f, "chest,*chest*"},
            {"neck", "chest", {0.f, 0.80f, 0.f}, {0.f, 0.87f, 0.f}, 0.035f, 0.03f, "neck,*neck*"},
            {"head", "neck", {0.f, 0.87f, 0.f}, {0.f, 1.f, 0.f}, 0.06f, 0.06f, "head,*head*,*hood*,*helm*"},
            {"shoulder.L", "chest", {0.08f, 0.80f, 0.f}, {0.34f, 0.79f, 0.f}, 0.03f, 0.035f, "shoulder.{s},*shoulder*.{s},*clavicle*.{s}"},
            {"upper_arm.L", "shoulder.L", {0.34f, 0.79f, 0.f}, {0.62f, 0.60f, 0.f}, 0.04f, 0.035f, "upper_arm.{s},*upper*arm*.{s},*upperarm*.{s},*arm*.{s}"},
            {"forearm.L", "upper_arm.L", {0.62f, 0.60f, 0.f}, {0.86f, 0.42f, 0.f}, 0.033f, 0.028f, "forearm.{s},*forearm*.{s},*lower*arm*.{s}"},
            {"hand.L", "forearm.L", {0.86f, 0.42f, 0.f}, {0.98f, 0.32f, 0.f}, 0.028f, 0.02f, "hand.{s},*hand*.{s}"},
            {"thigh.L", "hips", {0.20f, 0.50f, 0.f}, {0.22f, 0.27f, 0.f}, 0.06f, 0.05f, "thigh.{s},*thigh*.{s},*upper*leg*.{s},*leg*.{s}"},
            {"shin.L", "thigh.L", {0.22f, 0.27f, 0.f}, {0.23f, 0.05f, -0.15f}, 0.045f, 0.035f, "shin.{s},*shin*.{s},*calf*.{s},*lower*leg*.{s}"},
            {"foot.L", "shin.L", {0.23f, 0.05f, -0.15f}, {0.23f, 0.01f, 0.6f}, 0.035f, 0.03f, "foot.{s},*foot*.{s},*boot*.{s}"},
        };

        const BuiltInBone kBipedTail[] = {
            {"hips", "", {0.f, 0.50f, 0.f}, {0.f, 0.58f, 0.f}, 0.075f, 0.07f, "hips,pelvis,*hip*,*pelvis*"},
            {"spine", "hips", {0.f, 0.58f, 0.f}, {0.f, 0.68f, 0.f}, 0.07f, 0.075f, "spine,body,*spine*,*torso*,*body*"},
            {"chest", "spine", {0.f, 0.68f, 0.f}, {0.f, 0.80f, 0.f}, 0.08f, 0.085f, "chest,*chest*"},
            {"neck", "chest", {0.f, 0.80f, 0.f}, {0.f, 0.87f, 0.f}, 0.035f, 0.03f, "neck,*neck*"},
            {"head", "neck", {0.f, 0.87f, 0.f}, {0.f, 1.f, 0.f}, 0.06f, 0.06f, "head,*head*,*hood*,*helm*"},
            {"shoulder.L", "chest", {0.08f, 0.80f, 0.f}, {0.34f, 0.79f, 0.f}, 0.03f, 0.035f, "shoulder.{s},*shoulder*.{s},*clavicle*.{s}"},
            {"upper_arm.L", "shoulder.L", {0.34f, 0.79f, 0.f}, {0.62f, 0.60f, 0.f}, 0.04f, 0.035f, "upper_arm.{s},*upper*arm*.{s},*upperarm*.{s},*arm*.{s}"},
            {"forearm.L", "upper_arm.L", {0.62f, 0.60f, 0.f}, {0.86f, 0.42f, 0.f}, 0.033f, 0.028f, "forearm.{s},*forearm*.{s},*lower*arm*.{s}"},
            {"hand.L", "forearm.L", {0.86f, 0.42f, 0.f}, {0.98f, 0.32f, 0.f}, 0.028f, 0.02f, "hand.{s},*hand*.{s}"},
            {"thigh.L", "hips", {0.20f, 0.50f, 0.f}, {0.22f, 0.27f, 0.f}, 0.06f, 0.05f, "thigh.{s},*thigh*.{s},*upper*leg*.{s},*leg*.{s}"},
            {"shin.L", "thigh.L", {0.22f, 0.27f, 0.f}, {0.23f, 0.05f, -0.15f}, 0.045f, 0.035f, "shin.{s},*shin*.{s},*calf*.{s},*lower*leg*.{s}"},
            {"foot.L", "shin.L", {0.23f, 0.05f, -0.15f}, {0.23f, 0.01f, 0.6f}, 0.035f, 0.03f, "foot.{s},*foot*.{s},*boot*.{s}"},
            {"tail.01", "hips", {0.f, 0.50f, -0.30f}, {0.f, 0.42f, -0.60f}, 0.045f, 0.04f, "tail,*tail*", true},
            {"tail.02", "tail.01", {0.f, 0.42f, -0.60f}, {0.f, 0.32f, -0.82f}, 0.04f, 0.03f, "", true},
            {"tail.03", "tail.02", {0.f, 0.32f, -0.82f}, {0.f, 0.22f, -0.98f}, 0.03f, 0.015f, "", true},
        };

        const BuiltInBone kQuadruped[] = {
            {"hips", "", {0.f, 0.62f, -0.50f}, {0.f, 0.65f, -0.15f}, 0.16f, 0.17f, "hips,pelvis,*hip*,*rear*body*,*body*"},
            {"spine", "hips", {0.f, 0.65f, -0.15f}, {0.f, 0.66f, 0.20f}, 0.17f, 0.17f, "spine,*spine*,*torso*"},
            {"chest", "spine", {0.f, 0.66f, 0.20f}, {0.f, 0.66f, 0.45f}, 0.17f, 0.15f, "chest,*chest*"},
            {"neck", "chest", {0.f, 0.66f, 0.45f}, {0.f, 0.80f, 0.68f}, 0.10f, 0.08f, "neck,*neck*"},
            {"head", "neck", {0.f, 0.80f, 0.68f}, {0.f, 0.82f, 0.98f}, 0.10f, 0.08f, "head,*head*"},
            {"tail.01", "hips", {0.f, 0.62f, -0.50f}, {0.f, 0.60f, -0.68f}, 0.06f, 0.05f, "tail,*tail*", true},
            {"tail.02", "tail.01", {0.f, 0.60f, -0.68f}, {0.f, 0.55f, -0.84f}, 0.05f, 0.04f, "", true},
            {"tail.03", "tail.02", {0.f, 0.55f, -0.84f}, {0.f, 0.48f, -0.98f}, 0.04f, 0.025f, "", true},
            {"thigh.L", "hips", {0.40f, 0.58f, -0.42f}, {0.46f, 0.32f, -0.36f}, 0.09f, 0.07f, "thigh.{s},*thigh*.{s},*rear*upper*.{s},*hind*upper*.{s}"},
            {"shin.L", "thigh.L", {0.46f, 0.32f, -0.36f}, {0.44f, 0.12f, -0.44f}, 0.06f, 0.045f, "shin.{s},*shin*.{s},*rear*lower*.{s},*hind*lower*.{s}"},
            {"hind_foot.L", "shin.L", {0.44f, 0.12f, -0.44f}, {0.44f, 0.01f, -0.30f}, 0.045f, 0.04f, "hind_foot.{s},*rear*foot*.{s},*hind*foot*.{s},*rear*paw*.{s},*hind*paw*.{s}"},
            {"upper_arm.L", "chest", {0.40f, 0.60f, 0.40f}, {0.44f, 0.34f, 0.38f}, 0.08f, 0.06f, "upper_arm.{s},*upper*arm*.{s},*front*upper*.{s},*fore*upper*.{s}"},
            {"forearm.L", "upper_arm.L", {0.44f, 0.34f, 0.38f}, {0.44f, 0.12f, 0.42f}, 0.055f, 0.045f, "forearm.{s},*forearm*.{s},*front*lower*.{s},*fore*lower*.{s}"},
            {"front_foot.L", "forearm.L", {0.44f, 0.12f, 0.42f}, {0.44f, 0.01f, 0.56f}, 0.045f, 0.04f, "front_foot.{s},*front*foot*.{s},*front*paw*.{s},*fore*paw*.{s}"},
        };

        // Quadruped body carrying a humanoid torso; +Z forward, the horse chest is where the rider's hips sit.
        const BuiltInBone kCentaur[] = {
            {"hips", "", {0.f, 0.52f, -0.55f}, {0.f, 0.54f, -0.25f}, 0.12f, 0.13f, "hips,*rear*body*,*horse*body*,*body*"},
            {"spine", "hips", {0.f, 0.54f, -0.25f}, {0.f, 0.55f, 0.05f}, 0.13f, 0.13f, "spine,*spine*,*barrel*"},
            {"chest", "spine", {0.f, 0.55f, 0.05f}, {0.f, 0.56f, 0.30f}, 0.13f, 0.12f, "chest,*chest*,*withers*"},
            {"torso", "chest", {0.f, 0.56f, 0.30f}, {0.f, 0.70f, 0.32f}, 0.09f, 0.085f, "torso,*torso*,*waist*"},
            {"upper_chest", "torso", {0.f, 0.70f, 0.32f}, {0.f, 0.82f, 0.32f}, 0.085f, 0.08f, "upper_chest,*upper*chest*"},
            {"neck", "upper_chest", {0.f, 0.82f, 0.32f}, {0.f, 0.88f, 0.32f}, 0.035f, 0.03f, "neck,*neck*"},
            {"head", "neck", {0.f, 0.88f, 0.32f}, {0.f, 1.f, 0.32f}, 0.06f, 0.06f, "head,*head*"},
            {"shoulder.L", "upper_chest", {0.06f, 0.82f, 0.32f}, {0.26f, 0.81f, 0.32f}, 0.03f, 0.035f, "shoulder.{s},*shoulder*.{s},*clavicle*.{s}"},
            {"upper_arm.L", "shoulder.L", {0.26f, 0.81f, 0.32f}, {0.48f, 0.64f, 0.32f}, 0.04f, 0.035f, "upper_arm.{s},*upper*arm*.{s},*arm*.{s}"},
            {"forearm.L", "upper_arm.L", {0.48f, 0.64f, 0.32f}, {0.66f, 0.48f, 0.34f}, 0.033f, 0.028f, "forearm.{s},*forearm*.{s}"},
            {"hand.L", "forearm.L", {0.66f, 0.48f, 0.34f}, {0.76f, 0.40f, 0.36f}, 0.028f, 0.02f, "hand.{s},*hand*.{s}"},
            {"tail.01", "hips", {0.f, 0.52f, -0.55f}, {0.f, 0.46f, -0.75f}, 0.05f, 0.04f, "tail,*tail*", true},
            {"tail.02", "tail.01", {0.f, 0.46f, -0.75f}, {0.f, 0.36f, -0.90f}, 0.04f, 0.03f, "", true},
            {"tail.03", "tail.02", {0.f, 0.36f, -0.90f}, {0.f, 0.24f, -0.98f}, 0.03f, 0.015f, "", true},
            {"thigh.L", "hips", {0.30f, 0.48f, -0.48f}, {0.34f, 0.28f, -0.42f}, 0.07f, 0.055f, "thigh.{s},*thigh*.{s},*rear*upper*.{s},*hind*upper*.{s}"},
            {"shin.L", "thigh.L", {0.34f, 0.28f, -0.42f}, {0.32f, 0.10f, -0.48f}, 0.05f, 0.04f, "shin.{s},*shin*.{s},*rear*lower*.{s},*hind*lower*.{s}"},
            {"hind_foot.L", "shin.L", {0.32f, 0.10f, -0.48f}, {0.32f, 0.01f, -0.36f}, 0.04f, 0.035f, "hind_foot.{s},*rear*foot*.{s},*hind*foot*.{s},*rear*hoof*.{s}"},
            {"front_thigh.L", "chest", {0.30f, 0.50f, 0.22f}, {0.32f, 0.28f, 0.20f}, 0.065f, 0.05f, "front_thigh.{s},*front*upper*.{s},*fore*upper*.{s}"},
            {"front_shin.L", "front_thigh.L", {0.32f, 0.28f, 0.20f}, {0.32f, 0.10f, 0.24f}, 0.045f, 0.04f, "front_shin.{s},*front*lower*.{s},*fore*lower*.{s}"},
            {"front_foot.L", "front_shin.L", {0.32f, 0.10f, 0.24f}, {0.32f, 0.01f, 0.36f}, 0.04f, 0.035f, "front_foot.{s},*front*foot*.{s},*front*hoof*.{s},*fore*hoof*.{s}"},
        };

        // Wings fold along the body; the knee points backwards and the toe reaches forward.
        const BuiltInBone kBird[] = {
            {"hips", "", {0.f, 0.48f, -0.25f}, {0.f, 0.55f, 0.f}, 0.12f, 0.14f, "hips,body,*body*,*torso*"},
            {"chest", "hips", {0.f, 0.55f, 0.f}, {0.f, 0.60f, 0.30f}, 0.14f, 0.12f, "chest,*chest*,*breast*"},
            {"neck.01", "chest", {0.f, 0.60f, 0.30f}, {0.f, 0.70f, 0.42f}, 0.06f, 0.05f, "neck,*neck*", true},
            {"neck.02", "neck.01", {0.f, 0.70f, 0.42f}, {0.f, 0.80f, 0.50f}, 0.05f, 0.045f, "", true},
            {"neck.03", "neck.02", {0.f, 0.80f, 0.50f}, {0.f, 0.88f, 0.55f}, 0.045f, 0.04f, "", true},
            {"head", "neck.03", {0.f, 0.88f, 0.55f}, {0.f, 0.94f, 0.75f}, 0.07f, 0.05f, "head,*head*"},
            {"beak", "head", {0.f, 0.90f, 0.75f}, {0.f, 0.86f, 0.98f}, 0.03f, 0.01f, "beak,*beak*,*bill*"},
            {"tail", "hips", {0.f, 0.48f, -0.25f}, {0.f, 0.44f, -0.98f}, 0.07f, 0.03f, "tail,*tail*"},
            {"wing_shoulder.L", "chest", {0.12f, 0.62f, 0.10f}, {0.35f, 0.66f, 0.05f}, 0.04f, 0.035f, "wing_shoulder.{s},*wing*shoulder*.{s},*wing*.{s}"},
            {"wing_humerus.L", "wing_shoulder.L", {0.35f, 0.66f, 0.05f}, {0.62f, 0.68f, -0.10f}, 0.035f, 0.03f, "wing_humerus.{s},*humerus*.{s},*wing*upper*.{s}"},
            {"wing_radius.L", "wing_humerus.L", {0.62f, 0.68f, -0.10f}, {0.86f, 0.66f, -0.30f}, 0.03f, 0.025f, "wing_radius.{s},*radius*.{s},*wing*lower*.{s}"},
            {"wing_hand.L", "wing_radius.L", {0.86f, 0.66f, -0.30f}, {0.98f, 0.62f, -0.50f}, 0.025f, 0.015f, "wing_hand.{s},*wing*hand*.{s},*wing*tip*.{s}"},
            {"thigh.L", "hips", {0.12f, 0.46f, -0.05f}, {0.16f, 0.30f, 0.08f}, 0.05f, 0.04f, "thigh.{s},*thigh*.{s},*leg*.{s}"},
            {"shin.L", "thigh.L", {0.16f, 0.30f, 0.08f}, {0.16f, 0.14f, -0.06f}, 0.035f, 0.025f, "shin.{s},*shin*.{s},*lower*leg*.{s}"},
            {"foot.L", "shin.L", {0.16f, 0.14f, -0.06f}, {0.16f, 0.02f, 0.02f}, 0.025f, 0.02f, "foot.{s},*foot*.{s},*tarsus*.{s}"},
            {"toe.L", "foot.L", {0.16f, 0.02f, 0.02f}, {0.16f, 0.f, 0.30f}, 0.02f, 0.015f, "toe.{s},*toe*.{s},*claw*.{s}"},
        };

        // Head plus one long spline; the same rig drives tentacles, ropes and vines.
        const BuiltInBone kSerpent[] = {
            {"head", "", {0.f, 0.60f, 0.78f}, {0.f, 0.62f, 0.98f}, 0.09f, 0.06f, "head,*head*"},
            {"spine.01", "head", {0.f, 0.60f, 0.78f}, {0.f, 0.58f, 0.58f}, 0.09f, 0.09f, "body,*body*,*spine*,*tail*", true},
            {"spine.02", "spine.01", {0.f, 0.58f, 0.58f}, {0.f, 0.52f, 0.38f}, 0.09f, 0.09f, "", true},
            {"spine.03", "spine.02", {0.f, 0.52f, 0.38f}, {0.f, 0.44f, 0.18f}, 0.09f, 0.09f, "", true},
            {"spine.04", "spine.03", {0.f, 0.44f, 0.18f}, {0.f, 0.36f, -0.02f}, 0.09f, 0.085f, "", true},
            {"spine.05", "spine.04", {0.f, 0.36f, -0.02f}, {0.f, 0.28f, -0.22f}, 0.085f, 0.08f, "", true},
            {"spine.06", "spine.05", {0.f, 0.28f, -0.22f}, {0.f, 0.20f, -0.42f}, 0.08f, 0.07f, "", true},
            {"spine.07", "spine.06", {0.f, 0.20f, -0.42f}, {0.f, 0.13f, -0.60f}, 0.07f, 0.06f, "", true},
            {"spine.08", "spine.07", {0.f, 0.13f, -0.60f}, {0.f, 0.08f, -0.76f}, 0.06f, 0.045f, "", true},
            {"spine.09", "spine.08", {0.f, 0.08f, -0.76f}, {0.f, 0.04f, -0.90f}, 0.045f, 0.03f, "", true},
            {"spine.10", "spine.09", {0.f, 0.04f, -0.90f}, {0.f, 0.02f, -0.98f}, 0.03f, 0.015f, "", true},
        };

        // Lateral body spline with fins as leaf bones; +Z is the nose.
        const BuiltInBone kFish[] = {
            {"head", "", {0.f, 0.50f, 0.55f}, {0.f, 0.50f, 0.98f}, 0.18f, 0.08f, "head,*head*"},
            {"spine.01", "head", {0.f, 0.50f, 0.55f}, {0.f, 0.50f, 0.25f}, 0.18f, 0.20f, "body,*body*,*spine*", true},
            {"spine.02", "spine.01", {0.f, 0.50f, 0.25f}, {0.f, 0.50f, -0.05f}, 0.20f, 0.18f, "", true},
            {"spine.03", "spine.02", {0.f, 0.50f, -0.05f}, {0.f, 0.50f, -0.35f}, 0.18f, 0.13f, "", true},
            {"spine.04", "spine.03", {0.f, 0.50f, -0.35f}, {0.f, 0.50f, -0.60f}, 0.13f, 0.08f, "", true},
            {"spine.05", "spine.04", {0.f, 0.50f, -0.60f}, {0.f, 0.50f, -0.78f}, 0.08f, 0.05f, "", true},
            {"tail_fin", "spine.05", {0.f, 0.50f, -0.78f}, {0.f, 0.50f, -0.98f}, 0.05f, 0.04f, "tail_fin,*tail*,*caudal*"},
            {"dorsal", "spine.02", {0.f, 0.68f, 0.05f}, {0.f, 0.96f, -0.15f}, 0.05f, 0.02f, "dorsal,*dorsal*,*fin*top*"},
            {"anal", "spine.03", {0.f, 0.32f, -0.20f}, {0.f, 0.05f, -0.35f}, 0.04f, 0.02f, "anal,*anal*,*fin*bottom*"},
            {"pectoral.L", "spine.01", {0.35f, 0.42f, 0.35f}, {0.95f, 0.30f, 0.10f}, 0.04f, 0.02f, "pectoral.{s},*pectoral*.{s},*fin*.{s}"},
        };

        // Legless caster: hover root, arms with a spell attachment, two robe trails.
        const BuiltInBone kFloating[] = {
            {"hover", "", {0.f, 0.10f, 0.f}, {0.f, 0.20f, 0.f}, 0.04f, 0.04f, ""},
            {"body", "hover", {0.f, 0.30f, 0.f}, {0.f, 0.62f, 0.f}, 0.17f, 0.14f, "body,*body*,*robe*,*torso*"},
            {"chest", "body", {0.f, 0.62f, 0.f}, {0.f, 0.76f, 0.f}, 0.14f, 0.12f, "chest,*chest*"},
            {"head", "chest", {0.f, 0.76f, 0.f}, {0.f, 0.97f, 0.f}, 0.13f, 0.11f, "head,*head*,*hood*"},
            {"upper_arm.L", "chest", {0.30f, 0.72f, 0.f}, {0.60f, 0.62f, 0.f}, 0.065f, 0.052f, "upper_arm.{s},*upper*arm*.{s},*arm*.{s}"},
            {"forearm.L", "upper_arm.L", {0.60f, 0.62f, 0.f}, {0.82f, 0.52f, 0.08f}, 0.052f, 0.043f, "forearm.{s},*forearm*.{s}"},
            {"hand.L", "forearm.L", {0.82f, 0.52f, 0.08f}, {0.96f, 0.50f, 0.16f}, 0.045f, 0.035f, "hand.{s},*hand*.{s}"},
            {"spell.L", "hand.L", {0.96f, 0.50f, 0.16f}, {1.05f, 0.50f, 0.22f}, 0.025f, 0.02f, "spell.{s},*spell*.{s},*orb*.{s}"},
            {"robe.L.01", "body", {0.15f, 0.38f, 0.f}, {0.22f, 0.25f, -0.08f}, 0.08f, 0.065f, "robe.{s},*robe*.{s},*cloth*.{s}", true},
            {"robe.L.02", "robe.L.01", {0.22f, 0.25f, -0.08f}, {0.28f, 0.12f, -0.18f}, 0.065f, 0.045f, "", true},
        };

        // Props: one hinge, a chest with a lid, a flag with a cloth spline, a pendulum.
        const BuiltInBone kHinge[] = {
            {"base", "", {0.f, 0.f, 0.f}, {0.f, 0.08f, 0.f}, 0.05f, 0.05f, "base,frame,*frame*,*base*"},
            {"hinge", "base", {-0.95f, 0.f, 0.f}, {-0.95f, 1.f, 0.f}, 0.06f, 0.06f, "door,lid,panel,*door*,*lid*,*panel*"},
        };
        const BuiltInBone kChest[] = {
            {"base", "", {0.f, 0.f, 0.f}, {0.f, 0.55f, 0.f}, 0.5f, 0.5f, "base,body,box,*base*,*body*,*box*"},
            {"lid", "base", {0.f, 0.55f, -0.95f}, {0.f, 0.60f, 0.95f}, 0.45f, 0.45f, "lid,top,*lid*,*top*"},
        };
        const BuiltInBone kFlag[] = {
            {"pole", "", {-0.9f, 0.f, 0.f}, {-0.9f, 1.f, 0.f}, 0.03f, 0.03f, "pole,*pole*,*staff*,*mast*"},
            {"cloth.01", "pole", {-0.9f, 0.9f, 0.f}, {-0.45f, 0.9f, 0.f}, 0.15f, 0.15f, "cloth,flag,*cloth*,*flag*,*banner*", true},
            {"cloth.02", "cloth.01", {-0.45f, 0.9f, 0.f}, {0.f, 0.9f, 0.f}, 0.15f, 0.15f, "", true},
            {"cloth.03", "cloth.02", {0.f, 0.9f, 0.f}, {0.45f, 0.9f, 0.f}, 0.15f, 0.15f, "", true},
            {"cloth.04", "cloth.03", {0.45f, 0.9f, 0.f}, {0.95f, 0.9f, 0.f}, 0.15f, 0.15f, "", true},
        };
        // Root plus a body that squashes and stretches for a bouncing pickup.
        const BuiltInBone kBounce[] = {
            {"root", "", {0.f, 0.f, 0.f}, {0.f, 0.1f, 0.f}, 0.05f, 0.05f, ""},
            {"body", "root", {0.f, 0.1f, 0.f}, {0.f, 1.f, 0.f}, 0.5f, 0.45f, "body,*body*,*gem*,*coin*,*chest*,*"},
        };
        const BuiltInBone kPendulum[] = {
            {"pivot", "", {0.f, 1.f, 0.f}, {0.f, 0.92f, 0.f}, 0.04f, 0.04f, "pivot,mount,*pivot*,*mount*,*hook*"},
            {"rod", "pivot", {0.f, 0.92f, 0.f}, {0.f, 0.20f, 0.f}, 0.03f, 0.03f, "rod,chain,rope,*rod*,*chain*,*rope*"},
            {"bob", "rod", {0.f, 0.20f, 0.f}, {0.f, 0.f, 0.f}, 0.12f, 0.12f, "bob,weight,*bob*,*weight*,*ball*"},
        };

        std::string ExpandSide(std::string_view pattern, char side)
        {
            std::string out;
            for (size_t i = 0; i < pattern.size(); ++i)
                if (pattern.compare(i, 3, "{s}") == 0)
                {
                    out += side;
                    i += 2;
                }
                else
                    out += pattern[i];
            return out;
        }

        void PushBone(RigPreset &preset, const BuiltInBone &b, bool mirror)
        {
            auto side = [&](std::string n)
            {
                const size_t dot = n.find(".L");
                if (mirror && dot != std::string::npos)
                    n[dot + 1] = 'R';
                return n;
            };
            RigPresetBone bone;
            bone.name = side(b.name);
            bone.parentName = side(b.parent);
            bone.head = mirror ? vec3(-b.head.x, b.head.y, b.head.z) : b.head;
            bone.tail = mirror ? vec3(-b.tail.x, b.tail.y, b.tail.z) : b.tail;
            bone.headRadius = b.headRadius;
            bone.tailRadius = b.tailRadius;
            bone.spline = b.spline;
            std::string_view patterns = b.patterns;
            while (!patterns.empty())
            {
                const size_t comma = patterns.find(',');
                bone.shellPatterns.push_back(ExpandSide(patterns.substr(0, comma), mirror ? 'r' : 'l'));
                patterns = comma == std::string_view::npos ? std::string_view{} : patterns.substr(comma + 1);
            }
            preset.bones.push_back(std::move(bone));
        }

        // Same rules LoadFile enforces on project files: a typo in a table must fail loudly, not rig quietly.
        void Validate(RigPreset &preset)
        {
            for (RigPresetBone &bone : preset.bones)
            {
                PE_ERROR_IF(glm::dot(bone.tail - bone.head, bone.tail - bone.head) <= 1e-12f,
                            "rig preset %s: bone %s has identical head and tail", preset.id.c_str(), bone.name.c_str());
                for (size_t i = 0; i < preset.bones.size(); ++i)
                    if (&preset.bones[i] != &bone)
                        PE_ERROR_IF(preset.bones[i].name == bone.name, "rig preset %s: duplicate bone %s", preset.id.c_str(),
                                    bone.name.c_str());
                if (bone.parentName.empty())
                    continue;
                for (size_t i = 0; i < preset.bones.size(); ++i)
                    if (preset.bones[i].name == bone.parentName)
                        bone.parent = static_cast<int>(i);
                PE_ERROR_IF(bone.parent < 0, "rig preset %s: bone %s has unknown parent %s", preset.id.c_str(), bone.name.c_str(),
                            bone.parentName.c_str());
            }
        }

        RigPreset MakeBuiltIn(const char *id, const char *name, const char *description, std::span<const BuiltInBone> bones,
                              const RigPresetLandmarks &landmarks = {}, bool puppetDefaults = false,
                              float leanBudgetDegrees = 15.f)
        {
            RigPreset preset;
            preset.id = id;
            preset.name = name;
            preset.description = description;
            preset.landmarks = landmarks;
            preset.leanBudgetDegrees = leanBudgetDegrees;
            for (const BuiltInBone &b : bones)
                PushBone(preset, b, false);
            for (const BuiltInBone &b : bones)
                if (std::string_view(b.name).find(".L") != std::string_view::npos)
                    PushBone(preset, b, true);
            Validate(preset);
            if (puppetDefaults)
            {
                for (RigPresetBone &bone : preset.bones)
                    if (bone.name.find("upper_arm.") == 0 || bone.name.find("forearm.") == 0 ||
                        bone.name.find("hand.") == 0 || bone.name.find("thigh.") == 0 ||
                        bone.name.find("shin.") == 0 || bone.name.find("foot.") == 0)
                        bone.swingLimitDegrees = 120.f, bone.twistLimitDegrees = 45.f;
                preset.pins.push_back("chest");
                preset.locks.push_back({"foot.L"});
                preset.locks.push_back({"foot.R"});
            }
            return preset;
        }

        // Cephalothorax, abdomen, mandibles and N leg pairs (coxa, femur, tibia, tarsus) fanned front to back.
        RigPreset MakeArthropod(const char *id, const char *name, const char *description, int pairs)
        {
            std::vector<BuiltInBone> bones = {
                {"thorax", "", {0.f, 0.55f, -0.15f}, {0.f, 0.56f, 0.40f}, 0.22f, 0.20f, "thorax,*thorax*,body,*body*"},
                {"abdomen", "thorax", {0.f, 0.55f, -0.15f}, {0.f, 0.50f, -0.95f}, 0.24f, 0.12f, "abdomen,*abdomen*,*tail*"},
                {"head", "thorax", {0.f, 0.56f, 0.40f}, {0.f, 0.50f, 0.75f}, 0.14f, 0.10f, "head,*head*"},
                {"mandible.L", "head", {0.12f, 0.46f, 0.72f}, {0.18f, 0.36f, 0.98f}, 0.03f, 0.015f, "mandible.{s},*mandible*.{s},*fang*.{s},*jaw*.{s}"},
            };
            std::vector<std::string> names; // BuiltInBone points at C strings; PushBone copies them before we return
            names.reserve(static_cast<size_t>(pairs) * 4);
            for (int i = 0; i < pairs; ++i)
            {
                const float t = pairs > 1 ? static_cast<float>(i) / static_cast<float>(pairs - 1) : 0.5f;
                const float z = 0.35f - 0.55f * t;   // attach point along the thorax
                const float fan = (0.5f - t) * 0.6f; // front legs reach forward, rear legs backward
                const std::string suffix = std::to_string(i + 1);
                const std::string &coxa = names.emplace_back("coxa." + suffix + ".L");
                const std::string &femur = names.emplace_back("femur." + suffix + ".L");
                const std::string &tibia = names.emplace_back("tibia." + suffix + ".L");
                const std::string &tarsus = names.emplace_back("tarsus." + suffix + ".L");
                bones.push_back({coxa.c_str(), "thorax", {0.20f, 0.52f, z}, {0.45f, 0.70f, z + fan * 0.5f}, 0.05f, 0.04f, ""});
                bones.push_back({femur.c_str(), coxa.c_str(), {0.45f, 0.70f, z + fan * 0.5f}, {0.75f, 0.80f, z + fan}, 0.04f, 0.03f, ""});
                bones.push_back({tibia.c_str(), femur.c_str(), {0.75f, 0.80f, z + fan}, {0.95f, 0.35f, z + fan * 1.5f}, 0.03f, 0.02f, ""});
                bones.push_back({tarsus.c_str(), tibia.c_str(), {0.95f, 0.35f, z + fan * 1.5f}, {0.98f, 0.f, z + fan * 1.7f}, 0.02f, 0.01f, ""});
            }
            return MakeBuiltIn(id, name, description, bones, {}, false, 0.f); // legs on every side: no trunk to lean
        }

        // Landmark measurement: Y slices of the point cloud, each split into X blobs at gaps, the blob nearest the
        // model's centre line standing in for the torso so held props and wide sleeves do not read as shoulders.
        struct Slice
        {
            int blobs = 0;
            float centralHalfWidth = 0.f;
            bool filled = false;
        };

        void SliceProfile(std::span<const vec3> points, const AABB &bounds, std::vector<Slice> &slices)
        {
            const vec3 size = bounds.GetSize();
            const float centreX = bounds.GetCenter().x;
            std::vector<std::vector<float>> xs(slices.size());
            for (const vec3 &p : points)
            {
                const int slice = std::clamp(static_cast<int>((p.y - bounds.min.y) / size.y * static_cast<float>(slices.size())), 0,
                                             static_cast<int>(slices.size()) - 1);
                xs[slice].push_back(p.x);
            }
            const float gap = std::max(size.x * 0.06f, 1e-4f);
            for (size_t s = 0; s < slices.size(); ++s)
            {
                std::vector<float> &x = xs[s];
                if (x.size() < 4)
                    continue;
                std::sort(x.begin(), x.end());
                float blobMin = x[0], blobMax = x[0], bestDist = std::numeric_limits<float>::max();
                int blobs = 1;
                auto close = [&]()
                {
                    const float dist = blobMin <= centreX && centreX <= blobMax ? 0.f : std::min(std::abs(blobMin - centreX), std::abs(blobMax - centreX));
                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        slices[s].centralHalfWidth = (blobMax - blobMin) * 0.5f;
                    }
                };
                for (size_t i = 1; i < x.size(); ++i)
                {
                    if (x[i] - blobMax > gap)
                    {
                        close();
                        blobMin = x[i];
                        ++blobs;
                    }
                    blobMax = x[i];
                }
                close();
                slices[s].blobs = blobs;
                slices[s].filled = true;
            }
        }
    } // namespace

    std::span<const RigPreset> RigPresetLibrary::BuiltIn()
    {
        static const std::vector<RigPreset> presets = {
            MakeBuiltIn("humanoid", "Humanoid", "Biped with spine, neck, shoulders, arms and legs; +Z forward.", kHumanoid,
                        kBipedLandmarks, true),
            MakeBuiltIn("biped_tail", "Biped + Tail", "Humanoid with a three-link spline tail; lizardfolk, demons, kobolds.",
                        kBipedTail, kBipedLandmarks, true),
            MakeBuiltIn("quadruped", "Quadruped", "Four legs, articulated neck and a spline tail; +Z forward.", kQuadruped, {},
                        false, 5.f),
            MakeBuiltIn("centaur", "Centaur", "Quadruped body carrying a humanoid torso, arms and head.", kCentaur, {}, false,
                        10.f),
            MakeBuiltIn("bird", "Bird", "Body, three-link neck, beak, tail, folded wings and backwards-knee legs with toes.", kBird,
                        {}, false, 8.f),
            MakeArthropod("insect", "Insect", "Thorax, abdomen, head, mandibles and three fanned leg pairs.", 3),
            MakeArthropod("spider", "Spider", "Thorax, abdomen, head, mandibles and four fanned leg pairs.", 4),
            MakeBuiltIn("serpent", "Serpent", "Head on a ten-link spline; also tentacles, ropes and vines.", kSerpent, {}, false,
                        0.f),
            MakeBuiltIn("fish", "Fish", "Lateral five-link body spline with tail, dorsal, anal and pectoral fins.", kFish, {},
                        false, 0.f),
            MakeBuiltIn("floating", "Floating Caster", "Legless caster: hover root, arms with spell attachments, two robe trails.",
                        kFloating, {}, false, 10.f),
            MakeBuiltIn("prop_hinge", "Prop: Hinge", "Fixed base and one door or lid panel hinged on the left edge.", kHinge, {},
                        false, 0.f),
            MakeBuiltIn("prop_chest", "Prop: Chest", "Box body with a lid hinged along the back edge.", kChest, {}, false, 0.f),
            MakeBuiltIn("prop_flag", "Prop: Flag", "Pole with a four-link cloth spline.", kFlag, {}, false, 0.f),
            MakeBuiltIn("prop_pendulum", "Prop: Pendulum", "Pivot, rod and bob.", kPendulum, {}, false, 0.f),
            MakeBuiltIn("prop_bounce", "Prop: Bounce", "Root and a squash-and-stretch body for a bouncing pickup.", kBounce, {},
                        false, 0.f),
        };
        return presets;
    }

    std::filesystem::path RigPresetLibrary::ProjectPresetDirectory()
    {
        Path::Init();
        return std::filesystem::path(Path::Assets) / "RigPresets";
    }

    std::vector<RigPreset> RigPresetLibrary::LoadProjectPresets(std::vector<std::string> *errors)
    {
        return LoadDirectory(ProjectPresetDirectory(), errors);
    }

    std::vector<RigPreset> RigPresetLibrary::LoadDirectory(const std::filesystem::path &directory,
                                                           std::vector<std::string> *errors)
    {
        std::vector<RigPreset> presets;
        std::error_code ec;
        if (!std::filesystem::exists(directory, ec))
            return presets;
        if (ec || !std::filesystem::is_directory(directory, ec))
        {
            if (errors)
                errors->push_back("Rig preset path is not a directory: " + directory.generic_string());
            return presets;
        }

        std::vector<std::filesystem::path> files;
        for (std::filesystem::recursive_directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied,
                                                              ec),
             end;
             it != end; it.increment(ec))
        {
            if (ec)
            {
                if (errors)
                    errors->push_back("Could not scan rig presets: " + ec.message());
                ec.clear();
                continue;
            }
            if (it->is_regular_file(ec) && !ec && ToLower(it->path().extension().string()) == ".json")
                files.push_back(it->path());
            ec.clear();
        }
        std::sort(files.begin(), files.end());

        std::unordered_set<std::string> ids;
        for (const std::filesystem::path &file : files)
        {
            RigPreset preset;
            std::string error;
            if (!LoadFile(file, preset, error))
            {
                if (errors)
                    errors->push_back(file.filename().generic_string() + ": " + error);
                continue;
            }
            if (!ids.insert(preset.id).second)
            {
                if (errors)
                    errors->push_back(file.filename().generic_string() + ": duplicate preset id '" + preset.id + "'");
                continue;
            }
            presets.push_back(std::move(preset));
        }
        return presets;
    }

    bool RigPresetLibrary::LoadFile(const std::filesystem::path &path, RigPreset &preset, std::string &error)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            error = "could not open file";
            return false;
        }

        const nlohmann::json root = nlohmann::json::parse(input, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            error = "invalid JSON object";
            return false;
        }
        if (!root.contains("version") || !root["version"].is_number_integer() ||
            root["version"] != kRigPresetVersion)
        {
            error = "unsupported or missing version (expected 1)";
            return false;
        }

        RigPreset loaded;
        loaded.sourcePath = path;
        if (!ReadString(root, "id", loaded.id, error) || !ReadString(root, "name", loaded.name, error) ||
            !ReadString(root, "description", loaded.description, error, false))
            return false;
        if (const auto lean = root.find("lean_budget_degrees"); lean != root.end())
        {
            if (!lean->is_number() || lean->get<float>() < 0.f || lean->get<float>() > 90.f)
            {
                error = "'lean_budget_degrees' must be a number between 0 and 90";
                return false;
            }
            loaded.leanBudgetDegrees = lean->get<float>();
        }

        const auto bonesIt = root.find("bones");
        if (bonesIt == root.end() || !bonesIt->is_array() || bonesIt->empty())
        {
            error = "'bones' must be a non-empty array";
            return false;
        }

        std::unordered_map<std::string, int> indices;
        for (size_t i = 0; i < bonesIt->size(); ++i)
        {
            const nlohmann::json &jsonBone = (*bonesIt)[i];
            if (!jsonBone.is_object())
            {
                error = "bone " + std::to_string(i) + " must be an object";
                return false;
            }

            RigPresetBone bone;
            std::string boneError;
            if (!ReadString(jsonBone, "name", bone.name, boneError) ||
                !ReadString(jsonBone, "parent", bone.parentName, boneError, false) ||
                !ReadVec3(jsonBone, "head", bone.head, boneError) || !ReadVec3(jsonBone, "tail", bone.tail, boneError) ||
                !ReadRadius(jsonBone, "radius_head", bone.headRadius, boneError) ||
                !ReadRadius(jsonBone, "radius_tail", bone.tailRadius, boneError) ||
                !ReadOptionalBool(jsonBone, "rigid", bone.rigid, boneError) ||
                !ReadOptionalBool(jsonBone, "spline", bone.spline, boneError) ||
                !ReadLimit(jsonBone, bone.swingLimitDegrees, bone.twistLimitDegrees, boneError) ||
                !ReadShellPatterns(jsonBone, bone.shellPatterns, boneError))
            {
                error = "bone " + std::to_string(i) + ": " + boneError;
                return false;
            }
            if (bone.rigid && bone.spline)
            {
                error = "bone '" + bone.name + "' cannot be both rigid and spline";
                return false;
            }
            const vec3 boneVector = bone.tail - bone.head;
            if (glm::dot(boneVector, boneVector) <= 1e-12f)
            {
                error = "bone '" + bone.name + "' has identical head and tail";
                return false;
            }
            if (!indices.emplace(bone.name, static_cast<int>(loaded.bones.size())).second)
            {
                error = "duplicate bone name '" + bone.name + "'";
                return false;
            }
            loaded.bones.push_back(std::move(bone));
        }

        for (RigPresetBone &bone : loaded.bones)
        {
            if (bone.parentName.empty())
                continue;
            const auto parent = indices.find(bone.parentName);
            if (parent == indices.end())
            {
                error = "bone '" + bone.name + "' has unknown parent '" + bone.parentName + "'";
                return false;
            }
            bone.parent = parent->second;
        }

        std::vector<uint8_t> state(loaded.bones.size());
        std::function<bool(int)> visit = [&](int index)
        {
            if (state[index] == 2)
                return true;
            if (state[index] == 1)
            {
                error = "parent cycle contains bone '" + loaded.bones[index].name + "'";
                return false;
            }
            state[index] = 1;
            const int parent = loaded.bones[index].parent;
            if (parent >= 0 && !visit(parent))
                return false;
            state[index] = 2;
            return true;
        };
        for (int i = 0; i < static_cast<int>(loaded.bones.size()); ++i)
            if (!visit(i))
                return false;

        if (const auto pins = root.find("pins"); pins != root.end())
        {
            if (!pins->is_array())
            {
                error = "'pins' must be an array of bone names";
                return false;
            }
            std::unordered_set<std::string> uniquePins;
            for (const nlohmann::json &pin : *pins)
            {
                if (!pin.is_string() || !indices.contains(pin.get<std::string>()))
                {
                    error = "each pin must name a preset bone";
                    return false;
                }
                if (uniquePins.insert(pin.get<std::string>()).second)
                    loaded.pins.push_back(pin.get<std::string>());
            }
        }
        if (const auto locks = root.find("locks"); locks != root.end())
        {
            if (!locks->is_array())
            {
                error = "'locks' must be an array";
                return false;
            }
            std::unordered_set<std::string> lockedBones;
            for (size_t i = 0; i < locks->size(); ++i)
            {
                const nlohmann::json &jsonLock = (*locks)[i];
                RigPresetLock lock;
                std::string lockError;
                if (!jsonLock.is_object() || !ReadString(jsonLock, "bone", lock.bone, lockError) ||
                    !ReadString(jsonLock, "target", lock.target, lockError, false) ||
                    !ReadOptionalBool(jsonLock, "enabled", lock.enabled, lockError))
                {
                    error = "lock " + std::to_string(i) + ": " +
                            (lockError.empty() ? "must be an object" : lockError);
                    return false;
                }
                if (!indices.contains(lock.bone) || (!lock.target.empty() && !indices.contains(lock.target)))
                {
                    error = "lock " + std::to_string(i) + ": bone and target must name preset bones";
                    return false;
                }
                if (!lockedBones.insert(lock.bone).second)
                {
                    error = "duplicate lock for bone '" + lock.bone + "'";
                    return false;
                }
                if (jsonLock.contains("anchor"))
                {
                    if (!ReadVec3(jsonLock, "anchor", lock.anchor, lockError))
                    {
                        error = "lock " + std::to_string(i) + ": " + lockError;
                        return false;
                    }
                    lock.hasAnchor = true;
                }
                if (jsonLock.contains("reach"))
                {
                    if (!jsonLock["reach"].is_number())
                    {
                        error = "lock " + std::to_string(i) + ": 'reach' must be a finite number from 0.3 to 1";
                        return false;
                    }
                    const double reach = jsonLock["reach"].get<double>();
                    if (!std::isfinite(reach) || reach < 0.3 || reach > 1.0)
                    {
                        error = "lock " + std::to_string(i) + ": 'reach' must be a finite number from 0.3 to 1";
                        return false;
                    }
                    lock.reach = static_cast<float>(reach);
                }
                loaded.locks.push_back(std::move(lock));
            }
        }

        preset = std::move(loaded);
        return true;
    }

    bool RigPresetLibrary::MeasureBiped(std::span<const vec3> points, const AABB &bounds, MeasuredLandmarks &out)
    {
        out = {};
        const vec3 size = bounds.GetSize();
        if (points.size() < 16 || !IsFinite(bounds.min) || !IsFinite(bounds.max) || size.y <= 1e-6f)
            return false;
        std::vector<Slice> slices(48);
        SliceProfile(points, bounds, slices);
        const float step = size.y / static_cast<float>(slices.size());
        auto sliceY = [&](int s)
        { return bounds.min.y + (static_cast<float>(s) + 0.5f) * step; };
        auto sliceAt = [&](float y)
        { return std::clamp(static_cast<int>((y - bounds.min.y) / step), 0, static_cast<int>(slices.size()) - 1); };

        out.feet = bounds.min.y;
        out.top = bounds.max.y;

        // Crotch: lowest slice showing one blob after a run of two-blob (leg) slices.
        int legRun = 0, crotch = -1;
        for (int s = 0; s < static_cast<int>(slices.size()) && crotch < 0; ++s)
        {
            if (!slices[s].filled)
                continue;
            if (slices[s].blobs >= 2)
                ++legRun;
            else if (legRun >= 3)
                crotch = s;
            else
                legRun = 0;
        }
        out.hips = crotch >= 0 ? sliceY(crotch) : bounds.min.y + size.y * 0.5f;
        out.defaulted.hips = crotch < 0;

        // Shoulders: the top of the wide part of the torso (the highest upper-body slice still near the widest
        // one), so a pot belly or a wide chest below does not pass for them; neck: narrowest slice above.
        int shoulders = -1, widest = -1;
        const int bandLo = sliceAt(out.hips + size.y * 0.15f), bandHi = sliceAt(bounds.max.y - size.y * 0.08f);
        for (int s = bandLo; s < bandHi; ++s)
            if (slices[s].filled && (widest < 0 || slices[s].centralHalfWidth > slices[widest].centralHalfWidth))
                widest = s;
        for (int s = bandHi - 1; widest >= 0 && s >= bandLo && shoulders < 0; --s)
            if (slices[s].filled && slices[s].centralHalfWidth >= slices[widest].centralHalfWidth * 0.85f)
                shoulders = s;
        out.defaulted.shoulders = shoulders < 0;
        out.shoulders = out.defaulted.shoulders ? out.hips + (bounds.max.y - out.hips) * 0.6f : sliceY(shoulders);

        int neck = -1;
        for (int s = sliceAt(out.shoulders + size.y * 0.02f); s < sliceAt(bounds.max.y - size.y * 0.05f); ++s)
            if (slices[s].filled && (neck < 0 || slices[s].centralHalfWidth < slices[neck].centralHalfWidth))
                neck = s;
        out.defaulted.neck = neck < 0 || shoulders < 0 || slices[neck].centralHalfWidth > slices[shoulders].centralHalfWidth * 0.8f;
        out.neck = out.defaulted.neck ? out.shoulders + (bounds.max.y - out.shoulders) * 0.35f : sliceY(neck);

        // Widths at the stations; the shoulder slice includes the upper arms, so it is scaled back to the joint.
        const Slice &hipSlice = slices[sliceAt(out.hips + step)];
        const Slice &shoulderSlice = slices[sliceAt(out.shoulders)];
        out.hipHalfWidth = hipSlice.filled ? hipSlice.centralHalfWidth : 0.f;
        out.shoulderHalfWidth = shoulderSlice.filled ? shoulderSlice.centralHalfWidth * 0.85f : 0.f;
        return true;
    }

    bool RigPresetLibrary::Fit(const RigPreset &preset, const AABB &bounds, std::span<const std::string> shellNames,
                               std::vector<FittedRigPresetBone> &bones, std::string &error,
                               const MeasuredLandmarks *landmarks, std::vector<FittedRigPresetLock> *locks)
    {
        bones.clear();
        if (locks)
            locks->clear();
        if (!IsFinite(bounds.min) || !IsFinite(bounds.max))
        {
            error = "target bounds must be finite";
            return false;
        }
        const vec3 size = bounds.GetSize();
        if (size.x < 0.f || size.y <= 1e-6f || size.z < 0.f)
        {
            error = "target bounds must be ordered and have positive height";
            return false;
        }

        // Landmark fit: template Y stations map piecewise-linearly onto the measured ones, X scales from the hip
        // ratio at the hips to the shoulder ratio at the shoulders (clamped so a bad measurement cannot explode).
        const RigPresetLandmarks &t = preset.landmarks;
        const bool useLandmarks = landmarks && t.valid();
        auto fit = [&](const vec3 &p)
        {
            vec3 out = FitPoint(p, bounds);
            if (!useLandmarks)
                return out;
            const float tpl[5] = {t.feet, t.hips, t.shoulders, t.neck, t.top};
            const float measured[5] = {landmarks->feet, landmarks->hips, landmarks->shoulders, landmarks->neck, landmarks->top};
            int seg = 0;
            while (seg < 3 && p.y > tpl[seg + 1])
                ++seg;
            const float span = std::max(tpl[seg + 1] - tpl[seg], 1e-4f);
            out.y = measured[seg] + (p.y - tpl[seg]) / span * (measured[seg + 1] - measured[seg]);

            const float halfWidth = std::max(size.x * 0.5f, 1e-4f);
            auto ratio = [&](float measuredHalf, float templateHalf)
            { return measuredHalf > 0.f ? std::clamp(measuredHalf / (templateHalf * halfWidth), 0.5f, 2.f) : 1.f; };
            const float hipRatio = ratio(landmarks->hipHalfWidth, t.hipHalfWidth);
            const float shoulderRatio = ratio(landmarks->shoulderHalfWidth, t.shoulderHalfWidth);
            const float k = glm::smoothstep(t.hips, t.shoulders, p.y);
            out.x = bounds.GetCenter().x + (out.x - bounds.GetCenter().x) * glm::mix(hipRatio, shoulderRatio, k);
            return out;
        };

        std::unordered_set<std::string> claimedShells;
        bones.reserve(preset.bones.size());
        for (const RigPresetBone &source : preset.bones)
        {
            FittedRigPresetBone bone;
            bone.name = source.name;
            bone.parent = source.parent;
            bone.head = fit(source.head);
            bone.tail = fit(source.tail);
            bone.headRadius = source.headRadius * size.y;
            bone.tailRadius = source.tailRadius * size.y;
            bone.rigid = source.rigid;
            bone.spline = source.spline;
            bone.swingLimitDegrees = source.swingLimitDegrees;
            bone.twistLimitDegrees = source.twistLimitDegrees;

            for (const std::string &pattern : source.shellPatterns)
            {
                for (const std::string &shell : shellNames)
                {
                    if (!claimedShells.contains(shell) && GlobMatches(pattern, shell))
                    {
                        bone.shell = shell;
                        claimedShells.insert(shell);
                        break;
                    }
                }
                if (!bone.shell.empty())
                    break;
            }
            bones.push_back(std::move(bone));
        }
        if (locks)
            for (const RigPresetLock &source : preset.locks)
            {
                FittedRigPresetLock lock;
                lock.bone = source.bone;
                lock.target = source.target;
                lock.anchor = source.hasAnchor ? fit(source.anchor) : vec3(0.f);
                lock.hasAnchor = source.hasAnchor;
                lock.reach = source.reach;
                lock.enabled = source.enabled;
                locks->push_back(std::move(lock));
            }
        return true;
    }
} // namespace pe
