#include "Scene/HingeField.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Script/ScriptSystem.h"

// Lua `hinge_field`: millions of small boxes that topple over an edge (Scene/HingeField.h). Lists are flat tables read
// with the raw stack API: one crossing per list, at a cost that stays small at a million entries.
namespace pe
{
    namespace
    {
        constexpr int kAppendStride = 5; // x, y, z, yaw, RGBA8 colour
        constexpr int kFallStride = 6;   // id, tStart, tContact, contact, final, side (negative: falls backward)

        HingeField *ActiveField()
        {
            Scene *scene = GetActiveScene();
            return scene ? scene->EnsureHingeField() : nullptr;
        }

        double RawNumber(lua_State *L, int table, int index)
        {
            lua_rawgeti(L, table, index);
            const double value = lua_tonumber(L, -1);
            lua_pop(L, 1);
            return value;
        }
    }

    static struct HingeFieldBindings
    {
        HingeFieldBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table field = lua.create_named_table("hinge_field");

                field.set_function("clear", []() {
                    if (HingeField *f = ActiveField())
                        f->Clear();
                });

                // append(flat, count) -> the first box's id; ids run on from there.
                field.set_function("append", [](sol::stack_table flat, int count) -> uint32_t {
                    HingeField *f = ActiveField();
                    if (!f || count <= 0)
                        return 0;

                    lua_State *L = flat.lua_state();
                    const int table = flat.stack_index();
                    uint32_t firstId = 0;
                    HingeField::Instance *out = f->Append(static_cast<uint32_t>(count), firstId);
                    for (int i = 0; i < count; i++)
                    {
                        const int base = i * kAppendStride;
                        out[i].x = static_cast<float>(RawNumber(L, table, base + 1));
                        out[i].y = static_cast<float>(RawNumber(L, table, base + 2));
                        out[i].z = static_cast<float>(RawNumber(L, table, base + 3));
                        out[i].yaw = static_cast<float>(RawNumber(L, table, base + 4));
                        out[i].tStart = out[i].tContact = 0.f;
                        out[i].color = static_cast<uint32_t>(RawNumber(L, table, base + 5));
                        out[i].state = 0;
                    }
                    return firstId;
                });

                field.set_function("hide", [](sol::stack_table flat, int count) {
                    HingeField *f = ActiveField();
                    if (!f)
                        return;

                    lua_State *L = flat.lua_state();
                    const int table = flat.stack_index();
                    for (int i = 1; i <= count; i++)
                        f->Hide(static_cast<uint32_t>(RawNumber(L, table, i)));
                });

                field.set_function("fall", [](sol::stack_table flat, int count) {
                    HingeField *f = ActiveField();
                    if (!f)
                        return;

                    lua_State *L = flat.lua_state();
                    const int table = flat.stack_index();
                    for (int i = 0; i < count; i++)
                    {
                        const int base = i * kFallStride;
                        f->Fall(static_cast<uint32_t>(RawNumber(L, table, base + 1)),
                                static_cast<float>(RawNumber(L, table, base + 2)),
                                static_cast<float>(RawNumber(L, table, base + 3)),
                                static_cast<float>(RawNumber(L, table, base + 4)),
                                static_cast<float>(RawNumber(L, table, base + 5)),
                                RawNumber(L, table, base + 6) < 0.0);
                    }
                });

                field.set_function("stand_all", []() {
                    if (HingeField *f = ActiveField())
                        f->StandAll();
                });

                field.set_function("set_clock", [](float time) {
                    if (HingeField *f = ActiveField())
                        f->SetClock(time);
                });

                field.set_function("set_near_distance", [](float distance) {
                    if (HingeField *f = ActiveField())
                        f->SetNearDistance(distance);
                });

                field.set_function("set_enabled", [](bool enabled) {
                    if (HingeField *f = ActiveField())
                        f->SetEnabled(enabled);
                });

                field.set_function("set_casts_shadows", [](bool casts) {
                    if (HingeField *f = ActiveField())
                        f->SetCastsShadows(casts);
                });

                field.set_function("rebuild", []() {
                    if (HingeField *f = ActiveField())
                        f->RequestRebuild();
                });

                field.set_function("count", []() -> uint32_t {
                    HingeField *f = ActiveField();
                    return f ? f->GetCount() : 0u;
                }); });
        }
    } s_hingeFieldBindings;
} // namespace pe
