# Scripting

Games are Lua. A script is a `.lua` file. Each one gets its own environment, so two scripts do not share locals. Errors print in the editor **Console**.

There is a second path, C++ scripts, for code that has to live in the engine. That path is at the bottom of this page. Everything above it is Lua.

## Where a script lives

| Place | When it runs |
|---|---|
| `Assets/Scripts/global/` | Loaded with the project, in the editor and the player. |
| `Assets/Scripts/Editor/` | Editor only. |
| `Assets/Scripts/Player/` | Player, and while the editor is in play mode. |
| A node | The file set on that node's **Script** component. |
| The scene | Listed in **Window → Scene Scripts**. |

On a node, **Add → Lua Script** attaches one. **Browse Existing...** picks a file. **New Empty Script** creates one and opens the **Script Editor**. The node's **Script** property sets **Run Mode**: **Play** (default), **Editor**, or **Both**.

`require` does not see project files. Shared code is loaded by hand:

```lua
local src = fs.read("Assets/Scripts/shared.lua")
local chunk = load(src, "@Assets/Scripts/shared.lua", "t", _ENV)
chunk()
```

## Shape of a script

Name the functions `init`, `update`, `update_editor`, `background`, and `destroy`, or pass them to `hooks`. Both work. `exposed` returns a table the editor shows on the node, and the script reads the live values.

```lua
local props = exposed {
    speed = 4.0,
    enabled = true,
}

local function init()
    pe_log(self:get_name() .. " ready")
end

local function update(dt)
    if not props.enabled then
        return
    end
    if not input.is_key_down("W") then
        return
    end

    local p = self:get_position()
    self:set_position(vec3(p.x, p.y, p.z - props.speed * dt))
end

hooks {
    init = init,
    update = update,
}
```

`self` exists only in a node script. It is that node. A script in `Assets/Scripts/` has no `self`.

`exposed_when { occlusion_culling_bias = "occlusion_culling" }` hides that field in the editor until the named switch is on. The default pipeline uses this so a setting disappears when the feature above it is off.

`script.on_update(id, fn)` registers an extra callback. An optional third argument is `"play"` (default), `"editor"`, or `"always"`. `script.remove_update(id)` and `script.clear_updates()` drop them.

## When the functions run

| Function | Called |
|---|---|
| `init()` | Once, when the script starts. |
| `update(dt)` | Every frame it is allowed to run. `dt` is seconds. |
| `update_editor(dt)` | While the editor is not in play mode. |
| `background()` | When the app is sent to the background. |
| `destroy()` | When play stops or the script is unloaded. |
| `on_enter(self, other)` | A trigger zone this script owns is entered. |
| `on_exit(self, other)` | That zone is left. |

A **Play** node script's `update` runs in the player and during play mode. **Editor** runs in the editor outside play mode. **Both** runs in both. The viewport fly camera is an editor script; during play it returns immediately and the game owns the keys.

`pe_log`, `pe_warn`, and `pe_error` write to the console and the log.

## Nodes and the scene

`self` and every node from `scene` are the same kind of object.

| Call | Effect |
|---|---|
| `get_position` / `set_position` | Local position. `vec3`. |
| `get_world_position` / `set_world_position` | World position. |
| `get_rotation` / `set_rotation` | Euler degrees. |
| `get_scale` / `set_scale` | Local scale. |
| `set_transform(pos, rot, scale)` | All three at once. |
| `get_parent` / `get_children` / `set_parent` | Hierarchy. |
| `get_name` / `set_name` | Node name. |
| `is_valid` / `is_ready` | The handle still points at a live node. |
| `set_enabled` / `is_enabled` / `is_hierarchy_enabled` | Enabled, including parents. |
| `set_visible` / `is_visible` | Drawn or not. |
| `set_script(path, mode)` | `mode` is `"player"`, `"editor"`, or `"both"`. |
| `get_script` / `set_script_run_mode` / `get_script_run_mode` | The attached file and when it runs. |
| `get_exposed` | The node's exposed table. |
| `remove` | Deletes the node. |
| `get_camera` | The camera on this node, if it has one. |
| `get_bounding_box` | World bounds. |
| `set_scene_settings` / `set_trigger_zone` / `set_zone` | Scene, trigger, and zone data on this node. |
| `set_voxel_world` / `get_voxel_world` | Voxel world on this node. |
| `set_terrain` / `get_terrain` | Terrain on this node. |

`scene.get_entities()` returns the root nodes that are not bare cameras or lights, each as `{ node, label, type = "node" }`. There is no find-by-name on `scene`. Walk `get_children` and `get_name`, or use `scene.find_model`. `lights.find(name)` finds a light.

| `scene` call | Effect |
|---|---|
| `load(path)` / `load_async(path)` / `save(path)` / `clear()` | Scene file. |
| `add_empty_node()` | A new empty node. |
| `delete_node(node)` | Removes it. |
| `instantiate_prefab(path)` / `save_prefab(node, path)` | Prefabs. |
| `attach_primitive` / `attach_polyline` / `attach_lines` | Mesh helpers on a node. |
| `add_point_light` / `add_spot_light` / `add_directional_light` / `remove_light` | Lights. |
| `get_cameras` / `get_active_camera` / `add_camera` / `remove_camera` / `set_active_camera` | Cameras. |
| `find_model` / `get_model_count` | Loaded models. |
| `set_positions` / `set_rotations` | Bulk transforms. |
| `run_action(name)` | Runs a named scene action. |
| `pick_view_pixel` / `decode_view` / `digest` | Viewport picking and scene info. |

`get_camera()` is the active camera. `camera:move(dir, speed)`, `camera:rotate(x, y)`, `camera:look_at(target)`, and the position, euler, fov, near, far, and orthographic getters and setters are on that object. Fov is in degrees. Projection mode is `"perspective"` or `"orthographic"`.

Vectors are `vec2`, `vec3`, `vec4`, `mat4`, and `quat`.

## Input

| Call | Effect |
|---|---|
| `input.is_key_down("W")` | Held. |
| `input.is_key_pressed("Space")` | Went down this frame. |
| `input.is_key_down_raw("W")` | Held, even while runtime UI has captured the keyboard. |
| `input.get_mouse_position()` | Cursor. |
| `input.get_mouse_delta()` | Movement since last frame. `peek_mouse_delta` reads it without consuming. |
| `input.get_mouse_wheel()` | Wheel. |
| `input.is_mouse_down(button)` | Held. Also `is_left_mouse_down`, `is_right_mouse_down`, `is_middle_mouse_down`. |
| `input.get_touch()` / `input.touch_fingers()` | Touch. |
| `input.is_viewport_focused()` | The game view has the pointer. |
| `input.set_relative_mouse(on)` / `is_relative_mouse()` | Captured mouse. |
| `input.reset_mouse_delta()` / `input.warp_mouse_center()` | Mouse bookkeeping. |
| `input.vibrate(ms)` | Gamepad rumble for that many milliseconds. |

## Physics

`physics.add_body(node, bodyType, shapeType, params)` puts a body on a node. `bodyType` is `"dynamic"`, `"static"`, or `"kinematic"`. `shapeType` is `"box"`, `"sphere"`, `"capsule"`, or `"convex"`. `remove_body` takes it off. `set_layer`, `get_layer`, and `set_layers_collide` are the collision layers. `set_velocity(node, x, y, z)`, `get_velocity`, `set_angular_velocity`, `apply_force`, `apply_impulse`, and `apply_torque` move it. `raycast` returns the hit. `is_simulating` and `has_body` are the queries.

`add_joint(node, type, params)` adds a joint. `type` is `"fixed"`, `"hinge"`, `"distance"`, or `"slider"`. `params` can set `connected` (a node or a name), `anchor`, and `axis`. `remove_joint`, `set_joint_motor`, `get_joint_value`, and `on_joint_break` follow it.

`on_trigger_enter(node, fn)` and `on_trigger_exit(node, fn)` register physics trigger callbacks. `clear_trigger_callbacks` drops them. A trigger **zone** on the node is separate: it calls `on_enter` and `on_exit` in that node's script.

`physics2d` is the 2D world. `is_available` says whether it is built in. `add_body`, `add_box`, `add_circle`, and `add_capsule` create bodies. `set_velocity`, `get_velocity`, `apply_force`, `apply_impulse`, `set_gravity`, `set_paused`, `get_contacts`, and `clear_contacts` drive it.

## Tween

```lua
tween.to(0, 1, 0.4, function(v)
    -- v goes from 0 to 1
end, { ease = "linear", on_done = function() end })
```

The default ease is smooth. `"linear"` is constant speed. The call returns an id. `tween.cancel(id)`, `tween.cancel_all()`, and `tween.active()` manage running tweens.

## What else a game calls

| Table | Used for |
|---|---|
| `audio` | `play(path)` and `play_3d(path, x, y, z)` for one-shots. `play_music` / `stop_music`. `set_master_volume`, `set_music_volume`, `set_sfx_volume`, `set_ambient_volume`. A node source is `add_source`, `play_source`, `stop_source`, `remove_source`, `has_source`. |
| `animation` | On a skinned node: `play(node, clip, loop)`, `stop`, `is_playing`, `get_time`, `set_speed`. A second clip is `play_layer`, `set_layer_mask`, `set_layer_speed`, `stop_layer`, `get_layer_time`. Also `get_clips`, `get_markers`, `get_bone_position`, `set_root_motion`. |
| `sprite` | `create`, `setup`, `play`, `pause`, `stop`, `set_frame`, `get_frame`, `set_speed`, `set_loop`, `set_outline_color`, `get_clips`. |
| `particles` | `add_emitter`, `set_emitter`, `get_emitter`, `find`, `emit_burst`, `kill_emitter_particles`, `remove_emitter`, `clear`, `load_texture`. |
| `lights` | `add_point`, `add_directional`, `add_spot`, `add_area`, and the matching `remove_*`. `find(name)`. `set_point_light`, `set_directional_light`, `set_spot_light`, `set_area_light`, or `set_property(type, index, name, value)`. |
| `skybox` | `load(path)`. A bare filename is looked up under `Skybox/`. |
| `material` | `get` / `set` a parameter on a node's material. `set_texture` / `remove_texture` / `has_texture`. `use_openpbr` / `set_openpbr` / `get_openpbr` and `use_mtlx`. `set_render_type`, `set_double_sided`. |
| `settings` | `get(name)` / `set(name, value)`. `get_render_mode` / `set_render_mode`. `is_ray_tracing_supported`. `get_depth_bias` / `set_depth_bias`. `get_lod_distances` / `set_lod_distances`. |
| `engine` | `is_play_mode` / `set_play_mode`. `is_paused` / `set_paused`. `quit`. `get_metrics()` returns `fps` and `delta_ms`. `get_window_size`, `get_viewport_rect`, `get_window_mode` / `set_window_mode`. `take_screenshot`. |
| `runtime_ui` | Updates screens that already exist, by screen id and widget id. `set_visible`, `show`, `hide`, `set_text`, `set_number`, `set_bool`, `set_button`, `set_image`, `set_quad`, `consume_click`, `get_state`. `get_surface_size` includes the safe area. This is separate from UI nodes authored on a node with `set_ui`. |
| `nav` | `bake`, `is_ready`, `find_path(ax, ay, az, bx, by, bz)`, `nearest`, `raycast`. An agent on a node: `add_agent`, `set_target`, `stop`, `remove`, `get_velocity`. A crowd: `swarm_add`, `swarm_target`, `swarm_stats`. |
| `terrain` | `sculpt`, `sculpt3d`, `flatten`, `smooth`, `height`, `raycast`. |
| `voxel` | `create`, `destroy`, `save_all`, `set_anchor`, `get_block`, `set_block`, `raycast`, `move_aabb`. |
| `fs` | `read`, `write`, `list`, `find`. Paths are project-relative. |
| `net` | `host`, `join`, `accept`, `status`, `peer`, `send`, `read_line`, `close`. Discovery is `advertise`, `discover`, `poll_discover`, `discover_stop`. `last_error` is the last failure. |
| `proc` | `start(path, args)`, `write`, `read_line`, `is_running`, `stop`. One child process. |

`primitives.cube()`, `sphere()`, `plane()`, `quad()`, and the other editor primitives spawn a node and return it. `load_model_async(path, callback)` and `load_models(paths, callback)` load models. `json_encode` and `json_decode` are globals. `profile_begin(name)` / `profile_end()` mark a CPU range. `radians`, `degrees`, `lerp`, and `clamp` are globals.

`script.platform` is `"windows"`, `"linux"`, `"android"`, `"macos"`, or `"unknown"`. `script.launch_option(name)` returns that launch option's value, or nil. `script.random_seed()` returns a new integer each call.

## Pipeline scripts

The frame is also a Lua script. `Scripts/Pipeline/default_pipeline.lua` is the one the engine ships. Its `exposed` values are the switches under **Scene Settings → Pipeline**. A project can replace that script. `pipeline.base()` returns a fresh copy of those default values, so a project script can start from them:

```lua
local p = exposed(pipeline.base())
p.ink = 0.8
```

Its `init` adds passes with `render_graph.add_pass` and `render_graph.add_fullscreen_pass`. A pipeline script can also be `cpp:<Type>` when the pass is native code.

`rhi`, `render_graph`, and `shaders` are the low-level graphics tables those pipeline scripts use. Gameplay scripts do not need them.

## C++ scripts

A node or a pipeline pass can be C++ instead of Lua. The project puts a `Phasma/ProjectNative` target in its own tree, and the type name is what the scene stores. The contract and the build steps are in `Phasma/ProjectNative/README.md`. How the engine calls those types is in `docs/wiki/architecture/runtime/scene-scripts.md`.

## gamekit

`Scripts/gamekit/` is a set of Lua helpers some sample projects share: `json`, `driver`, `loop`, `scene`, `pool`, `actor`, `input`, `camera`, `pick`, `spatial`, `build`, `ui`, `timer`, `tween`, `wave`, `grid`, `deck`, `save`, `audio`. A game loads it like this:

```lua
local kit = (function()
    local src = fs.read("Scripts/gamekit/init.lua")
    return load(src, "@Scripts/gamekit/init.lua", "t", _ENV)()
end)()
```

`kit` is a convention, not a second engine API. The tables on this page are the ones the engine itself provides.
