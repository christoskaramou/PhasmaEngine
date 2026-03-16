# Agent Startup

On start: read MEMORY.md, TASKS.md. On multi-step tasks: update TASKS.md, PROGRESSION.md, MEMORY.md.

## Tools

### Engine Interaction
- `execute_lua` — Run Lua code in the engine's ScriptSystem (scene manipulation, camera, lights, materials, settings, shaders, particles, etc.)

### Project Files (scoped to project root, read anywhere, write only PhasmaEditor/)
- `read_project_file` — Read C++ source, headers, shaders, configs (relative to project root)
- `write_project_file` — Write/modify files inside PhasmaEditor/ (C++, shaders, Lua, configs)
- `find_project_file` — Search for files by name substring (case-insensitive, recursive)
- `list_project_dir` — List files and subdirectories at a project path

### Agent Files (scoped to Assets/Agent/)
- `read_agent_file` — Read files from agent workspace (MEMORY.md, TASKS.md, etc.)
- `write_agent_file` — Write/append files in agent workspace

### Meta
- `request_feature` — Request a new tool or capability (saved to REQUESTS.md)
- `complete_feature` — Mark a previously requested feature as completed

## External AI (File-based IPC)

Default provider. Allows any external AI tool (Claude Code, Cursor, etc.) to communicate with the editor.

**Chat files** (configurable filename in the editor UI):
- `Assets/Agent/chat_input.txt` - editor writes the user message here
- `Assets/Agent/chat_input_response.txt` - external tool writes its response here (auto-detected via file watcher)
- `Assets/Agent/chat_history.txt` - full conversation history (written by editor after each message)

**Script execution** (always at `Assets/Agent/`):
1. Write Lua code to `command.lua`
2. Write anything to `command.run` to trigger execution
3. Read result from `result.txt`

## Assets Layout
Objects/ (3D models) | Shaders/ | Skyboxes/ | Particles/ | Scripts/ | Scenes/ | Agent/ | Fonts/ | Icons/
NO "Models" dir. Use `assets_path` for absolute paths. Use fs.list(".") when unsure.

## fs Notes
Sandboxed to Assets/. Paths are RELATIVE without "Assets/" prefix.
fs.list(".")|fs.list("Objects") OK. fs.list("Assets/Objects") WRONG.
fs.list(p)->{path,files,dirs} or empty on error. fs.find(q) searches recursively.

## Lua API

Globals: assets_path, pe_log/pe_warn/pe_error(msg), reload_scripts()
Math: vec2,vec3,vec4,mat4 (constructors + operators), radians(deg), degrees(rad), normalize(v), length(v), distance(a,b), dot(a,b), cross(a,b)

**Models**: get_models()->Model[], find_model(q)->Model, load_model(path)->Model, load_model_async(path,callback), load_models({"path1","path2",...},[callback(models)]) (parallel), remove_model(m), clone_model(src,x,y,z)->Model, scatter_models(src,n,r,cx,cy,cz,sMin,sMax)->table, focus_camera_on(m,[dist])
primitives: .cube(size?) .sphere(radius?) .plane(w?,d?) .cylinder(r?,h?) .cone(r?,h?) .quad(w?,h?) -> Model

Model: get_id, get/set_label, get_file_path, is_primitive, get_primitive_type, get_node_count, get_mesh_count, get_vertex_count, get_index_count
Model transform: get/set_position, get/set_scale, get_rotation, set_transform(pos,rot_deg,scale), get_bounding_box->{min,max,center,size}
Model nodes: get_node_mesh(i), get_node_name(i), get/set_node_position(i,vec3), set_node_transform(i,pos,rot,scale), get_node_world_position(i), get_node_parent(i), get_node_children(i), get_node_rotation(i), get_node_scale(i), get_node_world_matrix(i), get_node_local_matrix(i), get_node_bounding_box(i), reparent_node(n,p), get_mesh_info(i)->{vertex_count,index_count,bounding_box,render_type,texture_mask}

**Models Path**: Assets/Objects/glTF-Sample-Models

**Camera**: get_camera()->Camera
Camera: get/set_position, get/set_euler, get/set_fov, get/set_near, get/set_far, get/set_speed, get/set_rotation_speed, get/set_name, get_aspect, get_front, get_right, get_up, get_view, get_projection, get_view_projection, get_inv_view, get_inv_projection, get/set_jitter, get/set_prev_jitter

**Lights**: lights.add/get/set/remove_point({name,color,intensity,position,radius}), _directional, _spot(+inner/outer_angle,direction), _area(+width,height,direction), lights.get_counts()->{point,directional,spot,area}

**Materials**: material.get(model,meshIdx)->table, material.set(model,meshIdx,prop,val)
Props: base_color(vec4), emissive(vec3), metallic, roughness, alpha_cutoff, occlusion_strength, normal_scale, transmission (floats)
material.get_render_type(model,meshIdx)->string, material.get_texture_mask(model,meshIdx)->uint32, material.has_texture(model,meshIdx,type)->bool
material.set_texture(model,meshIdx,type,path)->bool | types: base_color, metallic_roughness, normal, occlusion, emissive

**Settings**: settings.get/set(name,val) | settings.get/set_render_mode(mode) modes: raster|hybrid|ray_tracing | settings.is_ray_tracing_supported()->bool | settings.get/set_depth_bias(a,b,c)
Bools: shadows, ssao, fxaa, taa, ssr, dof, bloom, motion_blur, tonemapping, IBL, cas_sharpening, draw_grid, draw_aabbs, day, frustum_culling, randomize_lights, use_Disney_PBR, freeze_frustum_culling, aabbs_depth_aware, dynamic_rendering
Floats: render_scale, cas_sharpness, dof_focus_scale, dof_blur_range, bloom_strength, bloom_range, motion_blur_strength, IBL_intensity, lights_intensity, time_scale
Ints: motion_blur_samples | Uint32: shadow_map_size, num_cascades

**Scene**: scene.save/load(name), scene.clear(), scene.get_models()->Model[], scene.get_model_count()->int, scene.get_entities()->[{type,model,label,is_primitive}]
scene.get_cameras()->Camera[], scene.get/set_active_camera, scene.add_camera()->Camera, scene.remove_camera(cam)
**Selection**: selection.get()->{has_selection,model,node_index,type}, select_node(m,i), select_mesh(m,i), clear(), get/set_gizmo('translate'|'rotate'|'scale')
**Skybox**: skybox.load(path,[time]), skybox.set_time('day'|'night'), skybox.is_day()
**Engine**: engine.get_metrics()->{fps,delta_ms}, engine.compile_shaders()
**Shaders**: shaders.list/read/edit(path,find,replace)/write(path,src)
**Filesystem**: fs.find(q,[root]), fs.list(p)->{path,files,dirs}, fs.read(p), fs.write(p,content,[append])
**Particles**: particles.add_emitter(opts?)->idx, get_emitter(i), set_emitter(i,prop,val) or set_emitter(i,{k=v,...}), remove_emitter(i), load_texture(path)->idx, get_texture_names(), get_count(), get_particle_count(), clear()
Emitter opts: position,velocity,color_start,color_end,gravity(vec3/vec4),count(int),size_min,size_max,life_min,life_max,spawn_rate,spawn_radius,noise_strength,drag(floats),orientation('billboard'|'horizontal'|'vertical'|'velocity'),texture_index(int),anim_rows,anim_cols,anim_speed(floats),interpolate(bool)

**Vulkan RHI** (low-level): rhi.get_gpu_name, get_width/height, get_frame_index/counter, get_system_memory, get_gpu_memory, wait_device_idle, change_present_mode, align/align_uniform/align_storage, get_descriptor_pool, get_main_queue, get_surface, get_swapchain, is_*_valid (instance/device extensions, layers)
**Vulkan Objects**: create/destroy_image, create/destroy_buffer, create/destroy_descriptor, create/destroy_semaphore, create/destroy_event, create/destroy_shader, create/destroy_pass_info, create/destroy_acceleration_structure
Image: load_image_rgba/rgba8/rgba32f/raw, create_image, get/set width/height/format/sampler/mip_levels, create/get/has_rtv/srv/uav, clear_color, generate_mip_maps
Buffer: map/unmap/flush/zero, set_data(table,type)/set_struct(entries)/get_data(count,type), size, device_address
Descriptor: set_image_view(s)/set_buffer(s)/set_sampler(s)/set_acceleration_structure, update
CommandBuffer: begin/end_cmd/reset, blit_image, clear_color/depth, begin/end_pass, bind_pipeline/vertex_buffer/index_buffer/descriptors, set_viewport/scissor, dispatch, draw/draw_indexed/draw_indirect, trace_rays, buffer/image/memory_barrier(s), copy_buffer/copy_buffer_staged/copy_image, set_constant_*, push_constants, set_event/wait_event/reset_event
PassInfo: get/set vertex/fragment/compute_shader, topology, polygon_mode, cull_mode, blend, depth, stencil, acceleration, dynamic_states
debug_utils: create/destroy_debug_messenger, set_*_name, start/end_frame_capture, trigger_capture

## Script Example
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
