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
```
local m = primitives.cube()          -- or .sphere() .plane() .cylinder() .cone() .quad()
m:set_label("MyCube")
m:set_transform(pos, rot_deg, scale) -- all vec3
material.set(m, 0, "base_color", vec4.new(1, 0, 0, 1))
```

### Move/transform a model
```
m:set_position(vec3.new(x, y, z))
m:set_scale(vec3.new(sx, sy, sz))
m:set_transform(pos, rot_deg, scale)  -- all at once
m:get_bounding_box() -> {min, max, center, size}
```

### Change materials
```
material.set(model, meshIdx, "base_color", vec4.new(r, g, b, a))
material.set(model, meshIdx, "metallic", 0.8)
material.set(model, meshIdx, "roughness", 0.2)
material.set(model, meshIdx, "emissive", vec3.new(r, g, b))
-- props: base_color(vec4), emissive(vec3), metallic, roughness, alpha_cutoff,
--        occlusion_strength, normal_scale, transmission (floats)
material.set_texture(model, meshIdx, type, path) -- types: base_color, metallic_roughness, normal, occlusion, emissive
```

### Add lights
```
lights.add_point({name="L1", color=vec3.new(1,1,1), intensity=100, position=vec3.new(0,5,0), radius=20})
lights.add_directional({name="Sun", color=vec3.new(1,1,1), intensity=5, direction=vec3.new(-1,-1,-1)})
lights.add_spot({name="Spot", color=vec3.new(1,1,1), intensity=50, position=vec3.new(0,5,0), direction=vec3.new(0,-1,0), radius=15, inner_angle=20, outer_angle=40})
lights.add_area({name="Area", color=vec3.new(1,1,1), intensity=30, position=vec3.new(0,5,0), direction=vec3.new(0,-1,0), width=2, height=2})
lights.get_counts() -> {point, directional, spot, area}
```

### Control the camera
```
local cam = get_camera()
cam:set_position(vec3.new(x, y, z))
cam:set_euler(vec3.new(pitch, yaw, roll))
cam:set_fov(60.0)
focus_camera_on(model, distance)  -- auto-frame a model
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
```
scene.clear()
scene.save("my_scene")
scene.load("my_scene")
scene.get_models() -> Model[]
scene.get_model_count() -> int
remove_model(m)
clone_model(src, x, y, z) -> Model
```

### Add particles
```
particles.add_emitter({
    position = vec3.new(0, 5, 0),
    velocity = vec3.new(0, 1, 0),
    color_start = vec4.new(1, 0.5, 0, 1),
    color_end = vec4.new(1, 0, 0, 0),
    count = 100,
    size_min = 0.1, size_max = 0.5,
    life_min = 1.0, life_max = 3.0,
    spawn_rate = 50.0
})
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
```
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
Tool: read_project_file(path="PhasmaEditor/Code/...")
Tool: write_project_file(path="PhasmaEditor/Code/...", content="...")
Tool: find_project_file(query="Camera.h")
Tool: list_project_dir(path="PhasmaEditor/Code/Script")
```

## Quick Reference

### Logging
```
pe_log(msg)   -- output visible to agent
pe_warn(msg)  -- warning
pe_error(msg) -- error (does NOT crash)
```

### Math
```
vec2.new(x,y)  vec3.new(x,y,z)  vec4.new(x,y,z,w)  mat4.new()
radians(deg)  degrees(rad)  normalize(v)  length(v)  distance(a,b)  dot(a,b)  cross(a,b)
```

### Model query
```
m:get_id()  m:get_label()  m:set_label(s)  m:get_file_path()
m:get_node_count()  m:get_mesh_count()  m:get_vertex_count()
m:get_node_name(i)  m:get_node_mesh(i)
m:get_mesh_info(i) -> {vertex_count, index_count, bounding_box, render_type}
get_models() -> Model[]
find_model(query) -> Model  -- find already-loaded model by name
```

### Selection
```
selection.get() -> {has_selection, model, node_index, type}
selection.select_node(model, i)
selection.clear()
selection.set_gizmo("translate" | "rotate" | "scale")
```

### Multiple cameras
```
scene.get_cameras() -> Camera[]
scene.add_camera() -> Camera
scene.set_active_camera(cam)
```

## Example

```lua
scene.clear()
local head = primitives.sphere()
head:set_label("Head")
head:set_transform(vec3.new(0, 3.8, 0), vec3.new(0, 0, 0), vec3.new(0.35, 0.35, 0.35))
material.set(head, 0, "base_color", vec4.new(1.0, 0.9, 0.2, 1.0))

local torso = primitives.cylinder()
torso:set_label("Torso")
torso:set_transform(vec3.new(0, 2.7, 0), vec3.new(0, 0, 0), vec3.new(0.2, 0.8, 0.2))
material.set(torso, 0, "base_color", vec4.new(0.2, 0.4, 1.0, 1.0))

lights.add_point({name="Light", color=vec3.new(1,1,1), intensity=100, position=vec3.new(3,5,3), radius=20})
focus_camera_on(torso, 6.0)
pe_log("Done!")
```
