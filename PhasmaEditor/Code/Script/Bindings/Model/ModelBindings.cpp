#include "Script/ScriptSystem.h"
#include "GUI/GUIState.h"
#include "Scene/ModelAsset.h"
#include "Scene/ModelAssetAssimp.h"
#include "Scene/Primitives.h"
#include "Scene/Scene.h"
#include "Scene/SceneNodeHandle.h"
#include "Systems/RendererSystem.h"
#include "Base/ThreadPool.h"

namespace pe
{
    // Construct a std::filesystem::path from a UTF-8 std::string (avoids system code page conversion on Windows)
    static std::filesystem::path U8Path(const std::string &utf8)
    {
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
    }

    static SceneNodeHandle AddPrimitiveDeferred(ModelAsset *model)
    {
        if (!model)
            return SceneNodeHandle();
        auto *r = GetGlobalSystem<RendererSystem>();
        if (!r)
            return SceneNodeHandle();
        auto handle = r->GetScene().AddModelDeferred(model);
        return handle;
    }

    static struct ModelBindings
    {
        ModelBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                // load_model_async(path, callback) - full load on background thread,
                // scene integration + callback on main thread when ready
                lua.set_function("load_model_async", [](const std::string &path, sol::function callback) {
                    std::string fullPath = path;
                    if (!U8Path(path).is_absolute())
                        fullPath = Path::Assets + "Objects/" + path;

                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return;

                    // Set loading flag on main thread before enqueue to avoid race window
                    GUIState::s_modelLoading = true;

                    // CPU-only load on background thread; GPU upload deferred to main thread
                    auto future = ThreadPool::General.Enqueue([fullPath]() -> ModelAsset * {
                        ModelAsset *result = ModelAssetAssimp::LoadCpuOnly(U8Path(fullPath));
                        if (!result)
                            GUIState::s_modelLoading = false;
                        return result;
                    });

                    auto *ss = GetGlobalSystem<ScriptSystem>();
                    if (ss)
                    {
                        PendingAsyncLoad load;
                        load.future = std::move(future);
                        load.callback = std::move(callback);
                        load.sceneGeneration = r->GetScene().GetGeneration();
                        ss->AddPendingAsyncLoad(std::move(load));
                    }
                });

                // load_models(paths, [callback]) - load multiple models in parallel on background threads,
                // scene integration on main thread as each completes, optional callback(models_table) when all done
                lua.set_function("load_models", [&lua](sol::table paths, sol::optional<sol::function> callback) {
                    auto *ss = GetGlobalSystem<ScriptSystem>();
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!ss || !r) return;

                    int count = 0;
                    for (auto &kv : paths)
                    {
                        if (kv.second.is<std::string>())
                            count++;
                    }
                    if (count == 0) return;

                    auto state = std::make_shared<BatchLoadState>();
                    state->total = count;
                    uint32_t gen = r->GetScene().GetGeneration();

                    for (auto &kv : paths)
                    {
                        if (!kv.second.is<std::string>()) continue;

                        std::string path = kv.second.as<std::string>();
                        std::string fullPath = path;
                        if (!std::filesystem::path(path).is_absolute())
                            fullPath = Path::Assets + "Objects/" + path;

                        // Set loading flag on main thread before enqueue to avoid race window
                        GUIState::s_modelLoading = true;

                        // CPU-only load on background thread; GPU upload deferred to main thread
                        auto future = ThreadPool::General.Enqueue([fullPath]() -> ModelAsset * {
                            ModelAsset *result = ModelAssetAssimp::LoadCpuOnly(U8Path(fullPath));
                            if (!result)
                                GUIState::s_modelLoading = false;
                            return result;
                        });

                        PendingAsyncLoad load;
                        load.future = std::move(future);
                        load.batchState = state;
                        load.sceneGeneration = gen;
                        if (callback.has_value())
                            load.batchCallback = callback.value();
                        ss->AddPendingAsyncLoad(std::move(load));
                    }
                });

                sol::table prim = lua.create_named_table("primitives");
                prim.set_function("cube", [](sol::optional<float> size) -> SceneNodeHandle {
                    ModelAsset *m = Primitives::CreateCube(size.value_or(1.0f));
                    return AddPrimitiveDeferred(m);
                });
                prim.set_function("sphere", [](sol::optional<float> radius) -> SceneNodeHandle {
                    ModelAsset *m = Primitives::CreateSphere(radius.value_or(1.0f));
                    return AddPrimitiveDeferred(m);
                });
                prim.set_function("plane", [](sol::optional<float> width, sol::optional<float> depth) -> SceneNodeHandle {
                    ModelAsset *m = Primitives::CreatePlane(width.value_or(10.0f), depth.value_or(10.0f));
                    return AddPrimitiveDeferred(m);
                });
                prim.set_function("cylinder", [](sol::optional<float> radius, sol::optional<float> height) -> SceneNodeHandle {
                    ModelAsset *m = Primitives::CreateCylinder(radius.value_or(1.0f), height.value_or(2.0f));
                    return AddPrimitiveDeferred(m);
                });
                prim.set_function("cone", [](sol::optional<float> radius, sol::optional<float> height) -> SceneNodeHandle {
                    ModelAsset *m = Primitives::CreateCone(radius.value_or(1.0f), height.value_or(2.0f));
                    return AddPrimitiveDeferred(m);
                });
                prim.set_function("quad", [](sol::optional<float> width, sol::optional<float> height) -> SceneNodeHandle {
                    ModelAsset *m = Primitives::CreateQuad(width.value_or(1.0f), height.value_or(1.0f));
                    return AddPrimitiveDeferred(m);
                }); });
        }
    } s_modelBindings;
} // namespace pe
