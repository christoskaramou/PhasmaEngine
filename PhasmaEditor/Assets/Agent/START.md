# PhasmaEditor Agent

On start: read MEMORY.md, TASKS.md. On multi-step tasks: update TASKS.md, PROGRESSION.md, MEMORY.md.

## What do you want to do?

### Load a 3D model file
```
Tool: find_loadable_model(query="name")  -> returns path
Tool: execute_lua -> local m, err = load_model("path/from/above")
                     if m then focus_camera_on(m) end
```

### Create a primitive shape
```lua
local m = primitives.cube()          -- or .sphere() .plane() .cylinder() .cone() .quad()
m:set_label("MyCube")
m:set_transform(pos, rot_deg, scale) -- all vec3
material.set(m, 0, "base_color", vec4(1, 0, 0, 1))
```

### Move/transform a model
```lua
m:set_position(vec3(x, y, z))
m:set_rotation(vec3(pitch, yaw, roll))     -- degrees, preserves position+scale
m:set_scale(vec3(sx, sy, sz))              -- preserves position+rotation
m:set_transform(pos, rot_deg, scale)       -- all at once
m:get_position() -> vec3
m:get_rotation() -> vec3 (degrees)
m:get_scale() -> vec3
m:get_bounding_box() -> {min, max, center, size}
```

### Per-node transforms
```lua
m:get_node_count()
m:get_node_name(i)
m:get_node_position(i) / m:set_node_position(i, vec3)
m:get_node_rotation(i) / m:set_node_rotation(i, vec3)  -- degrees
m:get_node_scale(i)    / m:set_node_scale(i, vec3)
m:set_node_transform(i, pos, rot_deg, scale)            -- all at once
m:get_node_world_position(i)
m:get_node_world_matrix(i) / m:get_node_local_matrix(i)
m:get_node_parent(i) -> int
m:get_node_children(i) -> int[]
m:reparent_node(node, newParent)
```

### Visibility
```lua
m:set_visible(false)  -- hide model
m:is_visible() -> bool
```

### Change materials
```lua
material.get(model, meshIdx) -> {base_color, emissive, metallic, roughness, ...}
material.set(model, meshIdx, "base_color", vec4(r, g, b, a))
material.set(model, meshIdx, "metallic", 0.8)
material.set(model, meshIdx, "roughness", 0.2)
material.set(model, meshIdx, "emissive", vec3(r, g, b))
-- props: base_color(vec4), emissive(vec3), metallic, roughness, alpha_cutoff,
--        occlusion_strength, normal_scale, transmission (floats)

-- Textures
material.set_texture(model, meshIdx, type, path) -- types: base_color, metallic_roughness, normal, occlusion, emissive
material.remove_texture(model, meshIdx, type)     -- clear a texture slot
material.has_texture(model, meshIdx, type) -> bool
material.get_texture_mask(model, meshIdx) -> uint32

-- Render type
material.get_render_type(model, meshIdx) -> "opaque"|"alpha_cut"|"alpha_blend"|"transmission"
material.set_render_type(model, meshIdx, "alpha_blend") -- change at runtime
```

### Add and manage lights
```lua
-- Creation
lights.add_point()
lights.add_directional()
lights.add_spot()
lights.add_area()

-- Query
lights.get_counts() -> {point, directional, spot, area}
lights.get_point_lights() -> table[]       -- each: {index, name, color, intensity, position, radius}
lights.get_directional_lights() -> table[] -- each: {index, name, color, intensity, position, rotation}
lights.get_spot_lights() -> table[]        -- each: {index, name, color, intensity, position, range, rotation, angle, falloff}
lights.get_area_lights() -> table[]        -- each: {index, name, color, intensity, position, range, rotation, width, height}

-- Full replacement
lights.set_point_light(index, pos, color, intensity, radius)
lights.set_directional_light(index, pos, color, intensity)
lights.set_spot_light(index, pos, color, intensity, range, angle, falloff)
lights.set_area_light(index, pos, color, intensity, range, width, height)

-- Individual property updates
lights.set_property("point", index, "color", vec3(1, 0, 0))
lights.set_property("spot", index, "intensity", 50.0)
-- common props: name, color, intensity, position
-- point: radius | spot: range, angle, falloff, rotation | area: range, width, height, rotation | directional: rotation

-- Rotation (vec4 quaternion) for directional, spot, area
lights.set_rotation("spot", index, vec4(x, y, z, w))

-- Find by name (searches all types)
lights.find("sun") -> table[] of {type, index, name}

-- Removal
lights.remove_point(index)
lights.remove_directional(index)
lights.remove_spot(index)
lights.remove_area(index)
```

### Control the camera
```lua
local cam = get_camera()
cam:set_position(vec3(x, y, z))
cam:set_euler(vec3(pitch, yaw, roll))
cam:set_fov(60.0)
cam:get_position() / cam:get_euler() / cam:get_front() / cam:get_right() / cam:get_up()
focus_camera_on(model, distance)  -- auto-frame a model

-- Programmatic movement
cam:move("forward", speed)   -- "forward", "backward", "left", "right"
cam:rotate(xoffset, yoffset) -- mouse-style rotation
cam:look_at(vec3(x, y, z))   -- orient toward target

-- Matrices
cam:get_view() / cam:get_projection() / cam:get_view_projection()
cam:get_inv_view() / cam:get_inv_projection() / cam:get_inv_view_projection()
cam:get_previous_view_projection()

-- Frustum culling queries
cam:point_in_frustum(vec3, radius) -> bool
cam:aabb_in_frustum({min=vec3, max=vec3}) -> bool
```

### Read keyboard/mouse input
```lua
-- Keyboard (SDL key names: "W", "A", "S", "D", "Space", "Left Shift", "Escape", etc.)
input.is_key_down("W") -> bool

-- Mouse
input.get_mouse_position() -> {x, y}
input.get_mouse_delta() -> {x, y}
input.is_left_mouse_down() -> bool
input.is_right_mouse_down() -> bool
input.is_middle_mouse_down() -> bool
input.is_mouse_down(button) -> bool  -- 1=left, 2=middle, 3=right

-- Relative mouse mode (hides cursor, captures raw deltas - for FPS-style camera)
input.set_relative_mouse(true)
input.is_relative_mouse() -> bool
input.warp_mouse_center()
```

### Engine / editor controls
```lua
-- Play mode
engine.is_play_mode() -> bool
engine.set_play_mode(true)

-- Pause (within play mode)
engine.is_paused() -> bool
engine.set_paused(true)

-- GUI
engine.toggle_gui()
engine.trigger_exit_confirmation()
engine.is_popup_open() -> bool
engine.want_capture_keyboard() -> bool  -- true when typing in a UI field

-- Screenshot
engine.take_screenshot()              -- save screenshot to Screenshots/ folder
engine.take_screenshot("path.png")    -- save to specific path
```

### Change render settings
```
settings.set("shadows", true)     -- bool
settings.set("bloom_strength", 0.5) -- float
settings.set_render_mode("raster") -- "raster" | "hybrid" | "ray_tracing"
```
| Type | Names |
|------|-------|
| Bool | shadows, ssao, fxaa, taa, ssr, dof, bloom, motion_blur, tonemapping, IBL, cas_sharpening, draw_grid, draw_aabbs, day, frustum_culling, use_Disney_PBR |
| Float | render_scale, cas_sharpness, dof_focus_scale, dof_blur_range, bloom_strength, bloom_range, motion_blur_strength, IBL_intensity, lights_intensity, time_scale |
| Int/Uint32 | motion_blur_samples, shadow_map_size, num_cascades |

### Manage the scene
```lua
scene.clear()
scene.save("my_scene")
scene.load("my_scene")
scene.get_models() -> Model[]
scene.get_model_count() -> int
remove_model(m)
clone_model(src, x, y, z) -> Model
scatter_models(src, count, radius, cx, cy, cz, minScale, maxScale) -> Model[]
```

### Add and manage particles
```lua
local id = particles.add_emitter({
    position = vec3(0, 5, 0),
    velocity = vec3(0, 1, 0),
    color_start = vec4(1, 0.5, 0, 1),
    color_end = vec4(1, 0, 0, 0),
    count = 100,
    size_min = 0.1, size_max = 0.5,
    life_min = 1.0, life_max = 3.0,
    spawn_rate = 50.0,
    gravity = vec3(0, -9.8, 0),
    orientation = "billboard"  -- "billboard"|"horizontal"|"vertical"|"velocity"
})
particles.get_emitter(id) -> table
particles.set_emitter(id, "position", vec3(1, 2, 3))  -- single property
particles.set_emitter(id, {position=vec3(1,2,3), spawn_rate=100})  -- batch
particles.remove_emitter(id)
particles.find("fire") -> int (index, or -1)
particles.get_count() -> int
particles.clear()
```

### Edit shaders
```
shaders.list()
shaders.read(path)
shaders.edit(path, find, replace)
shaders.write(path, source)
engine.compile_shaders()
```

### Change skybox
```lua
skybox.load("path/to/hdr")
skybox.set_time("day")  -- or "night"
```

### Browse files in Assets/
```
fs.list(".")           -> {path, files, dirs}
fs.list("Objects")     -> {path, files, dirs}
fs.find("query")       -> recursive search results
fs.read("path")        -> file content
-- Paths relative to Assets/, no "Assets/" prefix
```

### Read/write project source files
```
-- PREFERRED WORKFLOW (surgical, minimal tokens):
--   1. grep_project("symbol") → get {file, line}
--   2. read_project_file(path, start_line=N-20, end_line=N+20) → read only what's needed

Tool: read_project_file(path="PhasmaEditor/Code/...", start_line=430, end_line=470)
-- start_line / end_line are 1-based, inclusive. Omit both to read the whole file.
-- Response includes: {path, content, start_line, end_line, total_lines}

Tool: write_project_file(path="PhasmaEditor/Code/...", content="...")
Tool: find_project_file(query="Camera.h")
Tool: list_project_dir(path="PhasmaEditor/Code/Script")
```

### Search project source files
```
Tool: grep_project(pattern="RenderGraph", path="PhasmaEditor/Code", glob="*.cpp")
-- Returns: [{file, line, text}, ...]

-- Options:
--   pattern        (required) literal string or ECMAScript regex
--   path           (optional) subdirectory to search, default: project root
--   glob           (optional) file filter e.g. "*.cpp", "*.hlsl", "*.h"
--   regex          (optional) true to treat pattern as regex, default: false
--   case_sensitive (optional) default: true
--   max_results    (optional) default: 50, max: 500
```

## Quick Reference

### Logging
```lua
pe_log(msg)   -- output visible to agent
pe_warn(msg)  -- warning
pe_error(msg) -- error (does NOT crash)
```

### Math types
```lua
vec2(x,y)  vec3(x,y,z)  vec4(x,y,z,w)  mat4()
quat()                      -- identity (1, 0, 0, 0)
quat(w, x, y, z)            -- direct components
quat(vec3(pitch, yaw, roll)) -- from euler degrees
quat(angle_deg, axis)       -- from axis-angle
```

### Math functions
```lua
-- Vector ops
radians(deg)  degrees(rad)  normalize(v)  length(v)  distance(a,b)
dot(a,b)  cross(a,b)  reflect(v, normal)

-- Interpolation
lerp(a, b, t)          -- float, vec2, vec3, vec4
slerp(q1, q2, t)       -- quaternion spherical interpolation

-- Clamping
clamp(x, lo, hi)  saturate(x)  math_min(a,b)  math_max(a,b)
abs(x)  floor(x)  ceil(x)  fract(x)  sign(x)

-- Matrix operations
inverse(m)  transpose(m)  determinant(m)
translate(mat4(), vec3(x,y,z))
rotate(mat4(), angle_deg, vec3(0,1,0))
scale(mat4(), vec3(sx,sy,sz))
look_at(eye, target, up)
perspective(fov_deg, aspect, zNear, zFar)

-- Quaternion methods
q:to_euler()    -> vec3 (degrees)
q:to_mat4()     -> mat4
q:inverse()     -> quat
q:conjugate()   -> quat
q:normalized()  -> quat
q:length()      -> float
inverse(q)      -- also works as free function
```

### Model query
```lua
m:get_id()  m:get_label()  m:set_label(s)  m:get_file_path()
m:is_primitive()  m:get_primitive_type()
m:get_node_count()  m:get_mesh_count()  m:get_vertex_count()
m:get_node_name(i)  m:get_node_mesh(i)
m:get_mesh_info(i) -> {vertex_count, index_count, bounding_box, render_type, texture_mask}
get_models() -> Model[]
find_model(query) -> Model  -- find already-loaded model by name
```

### Selection
```lua
selection.get() -> {has_selection, model, node_index, type}
selection.select_node(model, i)
selection.clear()
selection.set_gizmo("translate" | "rotate" | "scale")
```

### Multiple cameras
```lua
scene.get_cameras() -> Camera[]
scene.add_camera() -> Camera
scene.set_active_camera(cam)
scene.remove_camera(cam)
```

## Example

```lua
scene.clear()
local head = primitives.sphere()
head:set_label("Head")
head:set_transform(vec3(0, 3.8, 0), vec3(0, 0, 0), vec3(0.35, 0.35, 0.35))
material.set(head, 0, "base_color", vec4(1.0, 0.9, 0.2, 1.0))

local torso = primitives.cylinder()
torso:set_label("Torso")
torso:set_transform(vec3(0, 2.7, 0), vec3(0, 0, 0), vec3(0.2, 0.8, 0.2))
material.set(torso, 0, "base_color", vec4(0.2, 0.4, 1.0, 1.0))
material.set(torso, 0, "metallic", 0.8)
material.set(torso, 0, "roughness", 0.1)

lights.add_point()
lights.set_point_light(0, vec3(3, 5, 3), vec3(1, 1, 1), 100, 20)
lights.set_property("point", 0, "name", "Key Light")

focus_camera_on(torso, 6.0)
pe_log("Done!")
```

### Script lifecycle
Each `.lua` file in `Assets/Scripts/` runs in its own environment. Multiple scripts
can define the same hooks without conflict. Four lifecycle hooks are supported:

| Hook | When it runs |
|------|-------------|
| `init` | Once on startup |
| `update` | Every frame in play mode only |
| `update_editor` | Every frame (always, regardless of play mode) |
| `destroy` | On shutdown or reload |

Register hooks with the `hooks {}` keyword (recommended - keeps everything `local`):
```lua
local function update()
    -- runs every frame in play mode
end

hooks { update = update }
```

Or use the legacy style (non-local function names are auto-detected):
```lua
function update()
    -- also works, but the name is visible to other scripts
end
```

Non-hook globals (functions, tables) are shared across scripts. Duplicate names
trigger a warning: `Lua global 'foo' redefined by 'Scripts/other.lua'`.

### Interactive script example - WASD object control
```lua
local cube = nil

local function init()
    cube = primitives.cube()
    cube:set_label("Player")
    material.set(cube, 0, "base_color", vec4(0, 0.8, 1, 1))
end

local function update()
    if not cube then return end
    local speed = 0.05
    local pos = cube:get_position()
    if input.is_key_down("W") then pos.z = pos.z + speed end
    if input.is_key_down("S") then pos.z = pos.z - speed end
    if input.is_key_down("A") then pos.x = pos.x - speed end
    if input.is_key_down("D") then pos.x = pos.x + speed end
    cube:set_position(pos)
end

hooks { init = init, update = update }
```

### Interactive script example - fly camera
```lua
-- WASD + right-click mouse fly camera (runs every frame, not just play mode)
local skip_next_rotation = false

local function update_editor()
    local cam = get_camera()
    if not cam then return end
    local delta = engine.get_metrics().delta_ms / 1000.0
    local rmb = input.is_right_mouse_down()

    if rmb then
        if not input.is_relative_mouse() then
            input.set_relative_mouse(true)
            skip_next_rotation = true
        end
        local mouse = input.get_mouse_delta()
        if not skip_next_rotation then
            cam:rotate(mouse.x, mouse.y)
        else
            skip_next_rotation = false
        end
    else
        if input.is_relative_mouse() then
            input.set_relative_mouse(false)
        end
    end

    if rmb then
        local speed = cam:get_speed() * delta
        if input.is_key_down("W") then cam:move("forward", speed) end
        if input.is_key_down("S") then cam:move("backward", speed) end
        if input.is_key_down("A") then cam:move("left", speed) end
        if input.is_key_down("D") then cam:move("right", speed) end
    end
end

hooks { update_editor = update_editor }
```
