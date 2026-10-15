"""Deterministic shared GameKit driver checks, without launching the editor."""

import json
import os
from pathlib import Path
import sys

from lupa import LuaRuntime, lua_type


ROOT = Path(__file__).resolve().parents[2]


def encode_table(value):
    if lua_type(value) != "table":
        return value
    keys = list(value.keys())
    is_sequence = (
        all(isinstance(key, int) and key > 0 for key in keys)
        and set(keys) == set(range(1, len(keys) + 1))
    )

    if is_sequence:
        return [encode_table(value[index]) for index in range(1, len(keys) + 1)]

    return {key: encode_table(value[key]) for key in sorted(keys)}


def runtime():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.globals().json_encode = lambda value: json.dumps(
        encode_table(value),
        sort_keys=True,
        separators=(",", ":"),
    )
    return lua


def check_template():
    sys.path.insert(0, str(ROOT / "tools"))
    from new_game import ENTRY_TEMPLATE

    lua = runtime()
    assets = ROOT / "Phasma/Runtime/RuntimeAssets"
    lua.globals().fs = lua.table_from({"read": lambda path: (assets / path).read_text(encoding="utf-8")})
    lua.execute(r'''
        local function node(name, x, y, z)
            local self = { name = name, position = { x = x, y = y, z = z }, enabled = true }
            function self:get_name()
                return self.name
            end
            function self:get_position()
                return self.position
            end
            function self:set_position(position)
                self.position = position
            end
            function self:set_enabled(enabled)
                self.enabled = enabled
            end
            return self
        end
        nodes = { Player = node("Player", 0, .5, 0) }
        for index = 1, 64 do
            local name = string.format("Enemy_%03d", index)
            nodes[name] = node(name, 0, -1000, 0)
            nodes[name].enabled = false
        end
        scene = {
            find_model = function(name)
                return nodes[name]
            end,
            instantiate_prefab = function()
                return node("Pickup", 0, 0, 0)
            end,
        }
        vec2 = function(x, y)
            return { x = x, y = y }
        end
        vec3 = function(x, y, z)
            return { x = x, y = y, z = z }
        end
        input = {
            is_key_down = function()
                return false
            end,
        }
        engine = {
            get_metrics = function()
                return { delta_ms = 1000 / 60 }
            end,
        }
        script = {
            on_update = function(_, callback)
                tick = callback
            end,
            remove_update = function()
                tick = nil
            end,
        }
        tools = {}
        mcp = {
            register_tool = function(definition)
                tools[definition.name] = definition
            end,
            unregister_tool = function(name)
                tools[name] = nil
            end,
        }
        hooks = function(value)
            lifecycle = value
        end
    ''')
    lua.execute(ENTRY_TEMPLATE.format(Name="DriverTest", name="drivertest"))
    lua.execute(r'''
        lifecycle.init()
        assert(tools.game_act and tools.game_reset)
        local driver = _G.__gamekit_active_driver
        local initial = driver:reset({ seed = 19 })
        assert(initial.observation.player.x == 0 and initial.observation.player.hp == 100)
        assert(initial.score == 0 and initial.observation.wave == 1)
        local after = driver:act({
            session_id = initial.session_id, state_id = initial.state_id, action_id = "right",
        })
        assert(after.busy and #after.actions == 0)
        for _ = 1, 12 do
            tick()
        end
        local position = nodes.Player:get_position()
        assert(math.abs(position.x - 1.4) < .00001)
        tick()
        local stopped_x = position.x
        for _ = 1, 12 do
            tick()
        end
        assert(nodes.Player:get_position().x == stopped_x and not driver:observe().busy)

        local function enemy_snapshot()
            local result = {}
            for name, node in pairs(nodes) do
                if name:find("Enemy_") and node.enabled then
                    result[name] = { x = node.position.x, z = node.position.z }
                end
            end
            return json_encode(result)
        end
        driver:reset({ seed = 19 })
        for _ = 1, 100 do
            tick()
        end
        local first = enemy_snapshot()
        driver:reset({ seed = 19 })
        for _ = 1, 100 do
            tick()
        end
        assert(enemy_snapshot() == first, "seeded reset did not restore the enemy wave")
        local clean = driver:reset({ seed = 19 })
        assert(clean.observation.player.x == 0 and clean.observation.player.hp == 100)
        assert(#clean.observation.enemies == 0 and clean.observation.wave == 1)
        lifecycle.destroy()
        assert(next(tools) == nil and tick == nil)
        lifecycle.init()
        assert(tools.game_observe and tick)
        lifecycle.destroy()
        print("Generated topdown driver/input/reset/lifecycle checks passed")
    ''')


def main():
    os.chdir(ROOT)
    lua = runtime()
    lua.execute((ROOT / "tools/tests/game_driver_test.lua").read_text(encoding="utf-8"))
    check_template()
    return 0


if __name__ == "__main__":
    sys.exit(main())
