# Agent Startup Instructions

On every conversation start:
1. Read MEMORY.md to recall what you have learned from previous sessions.
2. Read TASKS.md to check for pending tasks from previous sessions.
3. If the user asks you to continue previous work, read PROGRESSION.md for context.

When working on multi-step tasks:
- Update TASKS.md with your current task list and progress.
- Update PROGRESSION.md with what you accomplished and what remains.
- Update MEMORY.md with any important discoveries or preferences.

These files persist across editor restarts and model switches.

## Assets Directory Structure

Always use the `assets_path` global variable to build absolute paths. Never hardcode paths.
The directory structure under Assets is:
- Objects/ -- 3D models (.gltf, .glb, .obj, .fbx)
- Shaders/ -- HLSL shader files
- Skyboxes/ -- HDR skybox images
- Particles/ -- Particle textures
- Scripts/ -- Lua scripts
- Scenes/ -- Saved scene JSON files
- Agent/ -- Your persistent workspace (MEMORY.md, TASKS.md, etc.)
- Fonts/ -- Font files
- Icons/ -- Icon images

IMPORTANT: There is NO "Models" directory. 3D models are in "Objects/".
When unsure about directory names, use fs.list(".") to see available directories first.

## Filesystem Notes

- All fs functions are sandboxed to the Assets directory.
- fs.list() paths must be RELATIVE to the Assets directory, WITHOUT the "Assets/" prefix.
  - CORRECT: fs.list(".") for the root, fs.list("Objects"), fs.list("Shaders")
  - WRONG: fs.list("Assets/Objects"), fs.list("/Assets/Objects"), fs.list(assets_path)
- fs.list(path) returns a table with {path, files, dirs} on success.
- fs.list(path) returns an empty table (no files/dirs fields) if the path does not exist or the format is wrong.
- fs.find(query) searches recursively and is the best way to locate assets by name.

## Lua API Reference

Globals: assets_path, pe_log/pe_warn/pe_error(msg) | Math: vec2,vec3,vec4,mat4

### Models
get_models()->table, find_model(q)->Model, load_model(path)->Model, remove_model(m)
clone_model(src,x,y,z)->Model, scatter_models(src,n,r,cx,cy,cz,sMin,sMax)->table
focus_camera_on(m,[dist]) | primitives: .cube/.sphere/.plane/.cylinder/.cone/.quad
Model methods:
- get/set_position, get/set_scale, get_rotation, set_transform(pos,rot_deg,scale)
- get_bounding_box()->{min,max,center,size}, get/set_label, get_file_path, is_primitive
- get_node_count, get_mesh_count, get/set_node_position(i,vec3), get_node_world_position(i)
- get_node_parent(i)->int, get_node_children(i)->table, get_node_rotation(i)->vec3, get_node_scale(i)->vec3
- get_node_world_matrix(i)->mat4, get_node_local_matrix(i)->mat4, get_node_bounding_box(i)->{min,max,center,size}
- reparent_node(node,newParent), get_mesh_info(meshIdx)->{vertex_count,index_count,bounding_box,render_type,texture_mask}

### Camera
get_camera()->Camera: get/set_position,euler,fov,near,far,speed,rotation_speed
get_aspect,front,right,up,view,projection,view_projection,inv_view,inv_projection

### Lights
lights.add/get/set/remove_point({name,color,intensity,position,radius})
Same for: _directional, _spot(+inner/outer_angle,direction), _area(+width,height,direction)
lights.get_counts()->{point,directional,spot,area}

### Materials
material.get/set(model,meshIdx,prop,val)
Props: base_color,emissive,metallic,roughness,alpha_cutoff,occlusion_strength,normal_scale,transmission
material.set_texture(model,meshIdx,type,path) | types: base_color,metallic_roughness,normal,occlusion,emissive

### Settings
settings.get/set(name,val) | render_mode: raster|hybrid|ray_tracing | depth_bias(c,s,c)
Bools: shadows,ssao,fxaa,taa,ssr,dof,bloom,motion_blur,tonemapping,IBL,cas_sharpening,draw_grid,draw_aabbs,frustum_culling
Floats: render_scale,cas_sharpness,dof_focus_scale,dof_blur_range,bloom_strength,bloom_range,IBL_intensity,lights_intensity,time_scale

### Scene
scene.save/load(name), scene.get_model_count(), scene.get/set_active_camera

### Selection
selection.get()->{has_selection,model,node_index,type}
selection.select_node(model,i), selection.select_mesh(model,i)
selection.clear(), selection.get/set_gizmo('translate'|'rotate'|'scale')

### Skybox
skybox.load(path,[time]), skybox.set_time('day'|'night'), skybox.is_day()

### Engine
engine.get_metrics()->{fps,delta_ms}, engine.compile_shaders()

### Shaders
shaders.list/read/edit(path,find,replace)/write(path,src)

### Filesystem (sandboxed to Assets/)
fs.find(q,[root]), fs.list(path)->{path,files,dirs}, fs.read(path), fs.write(path,content,[append])

### Particles
particles.add_emitter({position(vec3),velocity(vec3),color_start(vec4),color_end(vec4),gravity(vec3),count(int),
  size_min,size_max,life_min,life_max(floats),spawn_rate,spawn_radius,noise_strength,drag(floats),
  orientation('billboard'|'horizontal'|'vertical'|'velocity'),texture_index(int),
  anim_rows,anim_cols,anim_speed(floats),interpolate(bool)})->idx
get_emitter(i)->table, set_emitter(i,prop,val) or set_emitter(i,{k=v,...}), remove_emitter(i)
load_texture(path)->idx, get_texture_names(), get_count(), clear()
