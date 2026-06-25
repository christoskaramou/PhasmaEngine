#include "Script/ScriptSystem.h"
#include "Script/Bindings/Lerp/Tween.h"
#include "Camera/Camera.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

// General-purpose tween binding: smoothly move any object's vec3 over time.
//
//   local id = tween.to(from, to, duration, function(v) ... end, opts)
//
// Each frame the interpolated vec3 is handed to the setter, so it works for any
// object that exposes a vec3 (camera position, node position, light position,
// colors, ...). The setter closure is the only thing that knows *what* to move,
// keeping this binding object-agnostic. opts (optional table):
//   ease    = "smooth" (default, smoothstep) | "linear"
//   on_done = function called once when the tween reaches the target
//
// Named `tween` (not `lerp`) because `lerp(a, b, t)` is already a global stateless
// math function (MathBindings). The same engine also drives editor camera focus via
// the native TweenCameraTo entry point below.

namespace pe
{
    namespace
    {
        struct ActiveTween
        {
            int id = 0;
            std::function<bool(const vec3 &)> setter; // returns false to drop early
            std::function<void()> on_done;
            vec3 from = vec3(0.0f);
            vec3 to = vec3(0.0f);
            float duration = 0.0f;
            float elapsed = 0.0f;
            bool smooth = true;
        };

        std::vector<ActiveTween> s_tweens;
        int s_nextId = 1;
        int s_cameraTweenId = 0; // the in-flight camera-focus glide, if any
    } // namespace

    void TickTweens(double dt)
    {
        for (size_t i = 0; i < s_tweens.size();)
        {
            ActiveTween &t = s_tweens[i];
            t.elapsed += static_cast<float>(dt);
            const float u = t.duration > 0.0f ? glm::clamp(t.elapsed / t.duration, 0.0f, 1.0f) : 1.0f;
            const float k = t.smooth ? (u * u * (3.0f - 2.0f * u)) : u; // smoothstep
            const vec3 value = glm::mix(t.from, t.to, k);

            bool drop = (u >= 1.0f);
            if (t.setter)
            {
                if (!t.setter(value))
                    drop = true; // setter asked to stop (target gone / Lua error)
            }
            else
            {
                drop = true;
            }

            if (drop)
            {
                const bool fireDone = (u >= 1.0f) && static_cast<bool>(t.on_done);
                std::function<void()> done = t.on_done; // copy before erase invalidates t
                s_tweens.erase(s_tweens.begin() + i);
                if (fireDone)
                    done();
            }
            else
            {
                ++i;
            }
        }
    }

    int TweenStart(const vec3 &from, const vec3 &to, float duration, bool smooth,
                   std::function<bool(const vec3 &)> setter, std::function<void()> onDone)
    {
        if (!setter)
            return 0;

        ActiveTween t;
        t.id = s_nextId++;
        t.setter = std::move(setter);
        t.on_done = std::move(onDone);
        t.from = from;
        t.to = to;
        t.duration = duration;
        t.smooth = smooth;

        const int id = t.id;
        s_tweens.push_back(std::move(t));
        return id;
    }

    void TweenCancel(int id)
    {
        if (id == 0)
            return;
        s_tweens.erase(std::remove_if(s_tweens.begin(), s_tweens.end(),
                                      [id](const ActiveTween &t)
                                      { return t.id == id; }),
                       s_tweens.end());
    }

    void TweenCameraTo(const vec3 &target, float duration)
    {
        Scene *scene = GetActiveScene();
        Camera *cam = scene ? scene->GetActiveCamera() : nullptr;
        if (!cam)
            return;

        TweenCancel(s_cameraTweenId); // replace any in-flight focus glide
        s_cameraTweenId = TweenStart(cam->GetPosition(), target, duration, true,
                                     [](const vec3 &v) -> bool
                                     {
                                         // Re-fetch each frame so a scene/camera swap drops the glide.
                                         Scene *s = GetActiveScene();
                                         Camera *c = s ? s->GetActiveCamera() : nullptr;
                                         if (!c)
                                             return false;
                                         c->SetPosition(v);
                                         return true;
                                     });
    }

    static struct TweenBindings
    {
        TweenBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table tween = lua["tween"].get_or_create<sol::table>();

                tween.set_function("to", [](const vec3 &from, const vec3 &to, float duration,
                                            sol::protected_function setter,
                                            sol::optional<sol::table> opts) -> int {
                    if (!setter.valid())
                    {
                        PE_WARN("[Lua] tween.to requires a setter function");
                        return 0;
                    }

                    bool smooth = true;
                    sol::protected_function onDoneFn;
                    if (opts)
                    {
                        sol::optional<std::string> ease = (*opts)["ease"];
                        if (ease && *ease == "linear")
                            smooth = false;
                        sol::optional<sol::protected_function> done = (*opts)["on_done"];
                        if (done)
                            onDoneFn = *done;
                    }

                    std::function<void()> onDone;
                    if (onDoneFn.valid())
                    {
                        onDone = [onDoneFn]() {
                            auto r = onDoneFn();
                            if (!r.valid())
                            {
                                sol::error e = r;
                                Log::Error(PeFormat("[Lua] tween on_done error: %s", e.what()));
                            }
                        };
                    }

                    return TweenStart(from, to, duration, smooth,
                                      [setter](const vec3 &v) -> bool {
                                          auto r = setter(v);
                                          if (!r.valid())
                                          {
                                              sol::error e = r;
                                              Log::Error(PeFormat("[Lua] tween setter error: %s", e.what()));
                                              return false; // stop a broken tween rather than spam
                                          }
                                          return true;
                                      },
                                      std::move(onDone));
                });

                tween.set_function("cancel", [](int id) { TweenCancel(id); });
                tween.set_function("cancel_all", []() { s_tweens.clear(); });
                tween.set_function("active", []() -> int { return static_cast<int>(s_tweens.size()); }); });
        }
    } s_tweenBindings;
} // namespace pe
