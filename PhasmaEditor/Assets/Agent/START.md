# Agent Startup

On start: read MEMORY.md, TASKS.md. On multi-step tasks: update TASKS.md, PROGRESSION.md, MEMORY.md.

## Assets Layout
Objects/ (3D models) | Shaders/ | Skyboxes/ | Particles/ | Scripts/ | Scenes/ | Agent/ | Fonts/ | Icons/
NO "Models" dir. Use `assets_path` for absolute paths. Use fs.list(".") when unsure.

## fs Notes
Sandboxed to Assets/. Paths are RELATIVE without "Assets/" prefix.
fs.list(".")|fs.list("Objects") OK. fs.list("Assets/Objects") WRONG.
fs.list(p)->{path,files,dirs} or empty on error. fs.find(q) searches recursively.

## Lua API

Globals: assets_path, pe_log/pe_warn/pe_error(msg) | Math: vec2,vec3,vec4,mat4

**Models**: get_models()->table, find_model(q)->Model, load_model(path)->Model, remove_model(m), clone_model(src,x,y,z)->Model, scatter_models(src,n,r,cx,cy,cz,sMin,sMax)->table, focus_camera_on(m,[dist]) | primitives: .cube/.sphere/.plane/.cylinder/.cone/.quad
Model: get/set_position, get/set_scale, get_rotation, set_transform(pos,rot_deg,scale), get_bounding_box()->{min,max,center,size}, get/set_label, get_file_path, is_primitive, get_node_count, get_mesh_count, get/set_node_position(i,vec3), get_node_world_position(i), get_node_parent(i), get_node_children(i), get_node_rotation(i), get_node_scale(i), get_node_world_matrix(i), get_node_local_matrix(i), get_node_bounding_box(i)->{min,max,center,size}, reparent_node(n,p), get_mesh_info(i)->{vertex_count,index_count,bounding_box,render_type,texture_mask}

**Camera**: get_camera()->Camera: get/set_position,euler,fov,near,far,speed,rotation_speed, get_aspect,front,right,up,view,projection,view_projection,inv_view,inv_projection

**Lights**: lights.add/get/set/remove_point({name,color,intensity,position,radius}), _directional, _spot(+inner/outer_angle,direction), _area(+width,height,direction), lights.get_counts()->{point,directional,spot,area}

**Materials**: material.get/set(model,meshIdx,prop,val) Props: base_color,emissive,metallic,roughness,alpha_cutoff,occlusion_strength,normal_scale,transmission. material.set_texture(model,meshIdx,type,path) types: base_color,metallic_roughness,normal,occlusion,emissive

**Settings**: settings.get/set(name,val) | render_mode: raster|hybrid|ray_tracing | depth_bias(c,s,c)
Bools: shadows,ssao,fxaa,taa,ssr,dof,bloom,motion_blur,tonemapping,IBL,cas_sharpening,draw_grid,draw_aabbs,frustum_culling
Floats: render_scale,cas_sharpness,dof_focus_scale,dof_blur_range,bloom_strength,bloom_range,IBL_intensity,lights_intensity,time_scale

**Scene**: scene.save/load(name), scene.get_model_count(), scene.get/set_active_camera
**Selection**: selection.get()->{has_selection,model,node_index,type}, select_node(m,i), select_mesh(m,i), clear(), get/set_gizmo('translate'|'rotate'|'scale')
**Skybox**: skybox.load(path,[time]), skybox.set_time('day'|'night'), skybox.is_day()
**Engine**: engine.get_metrics()->{fps,delta_ms}, engine.compile_shaders()
**Shaders**: shaders.list/read/edit(path,find,replace)/write(path,src)
**Filesystem**: fs.find(q,[root]), fs.list(p)->{path,files,dirs}, fs.read(p), fs.write(p,content,[append])
**Particles**: particles.add_emitter({position,velocity,color_start,color_end,gravity(vec3/vec4),count(int),size_min,size_max,life_min,life_max,spawn_rate,spawn_radius,noise_strength,drag(floats),orientation('billboard'|'horizontal'|'vertical'|'velocity'),texture_index(int),anim_rows,anim_cols,anim_speed(floats),interpolate(bool)})->idx, get_emitter(i), set_emitter(i,prop,val) or set_emitter(i,{k=v,...}), remove_emitter(i), load_texture(path)->idx, get_texture_names(), get_count(), clear()
