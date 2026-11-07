// Bake-time converter for the WebGPU RHI backend (docs/wiki/superpowers/specs/2026-11-06-webgpu-rhi-backend-design.md,
// decisions 5, 7 and 8): engine SPIR-V, after the pre-steps in tools/convert_web_shaders.py, to WGSL plus a binding
// sidecar. Started from the P0 census converter (build-p0-census/naga-conv).
//
// Usage: spv-to-wgsl <in.spv> <out.wgsl> <out.bindings>
// Exit: 0 ok, 2 parse error, 3 validation error even with all capabilities, 4 WGSL write error,
//       5 the module breaks the web profile (a resource array, or set 3 declared by the shader itself).
// stderr "VALIDATE:" means naga needed capabilities beyond core WebGPU; the WGSL is still written for Chrome to judge.
//
// Sidecar: one line per WGSL binding, "<group> <binding> <kind> [details]", in WebGPU's own spellings:
//   uniform
//   storage <read|read-write>
//   texture <view dimension> <sample type> <multisampled 0|1>
//   storage-texture <view dimension> <format> <write-only|read-only|read-write>
//   sampler <filtering|comparison>
use naga::{
    AddressSpace, Expression, ImageClass, ImageDimension, ResourceBinding, ScalarKind, StorageAccess, Type, TypeInner,
};
use std::collections::{HashMap, HashSet};

type GlobalHandle = naga::Handle<naga::GlobalVariable>;

// Decision 8: the push block's slot. The engine itself never declares set 3 on the web.
const PUSH_GROUP: u32 = 3;

#[derive(Default)]
struct Use {
    sampled: bool,
    compared: bool,
    loaded: bool,
}

fn global_of(f: &naga::Function, h: naga::Handle<Expression>) -> Option<GlobalHandle> {
    match f.expressions[h] {
        Expression::GlobalVariable(g) => Some(g),
        _ => None,
    }
}

fn collect_uses(module: &naga::Module) -> HashMap<GlobalHandle, Use> {
    let mut uses: HashMap<GlobalHandle, Use> = HashMap::new();
    let functions = module.functions.iter().map(|(_, f)| f);
    let entries = module.entry_points.iter().map(|e| &e.function);

    for f in functions.chain(entries) {
        for (_, e) in f.expressions.iter() {
            match *e {
                Expression::ImageSample { image, depth_ref, .. } => {
                    if let Some(i) = global_of(f, image) {
                        let u = uses.entry(i).or_default();
                        u.sampled = true;
                        u.compared |= depth_ref.is_some();
                    }
                }
                Expression::ImageLoad { image, .. } => {
                    if let Some(i) = global_of(f, image) {
                        uses.entry(i).or_default().loaded = true;
                    }
                }
                _ => {}
            }
        }
    }

    uses
}

fn is_sampler(module: &naga::Module, g: &naga::GlobalVariable) -> bool {
    matches!(module.types[g.ty].inner, TypeInner::Sampler { .. })
}

// Resource arrays do not survive the web profile (decision 10), and set 3 is the backend's push slot (decision 8).
fn check_profile(module: &naga::Module) -> Result<(), String> {
    for (_, g) in module.global_variables.iter() {
        let name = g.name.clone().unwrap_or_default();
        if matches!(module.types[g.ty].inner, TypeInner::BindingArray { .. }) {
            return Err(format!("'{name}' is a resource array, which the web profile removes"));
        }

        let declares_push_group = g.binding.as_ref().is_some_and(|b| b.group == PUSH_GROUP);
        if declares_push_group && g.space != AddressSpace::Immediate {
            return Err(format!("'{name}' is in set 3, which WebGPU keeps for push constants"));
        }
    }

    Ok(())
}

// Decision 8: push constants become var<uniform> at group 3 binding 0.
fn rewrite_push_constants(module: &mut naga::Module) -> HashSet<GlobalHandle> {
    let mut push = HashSet::new();
    for (h, g) in module.global_variables.iter_mut() {
        if g.space == AddressSpace::Immediate {
            g.space = AddressSpace::Uniform;
            g.binding = Some(ResourceBinding {
                group: PUSH_GROUP,
                binding: 0,
            });
            push.insert(h);
        }
    }

    push
}

// Decision 5 binding rule: engine binding b becomes 2b; a sampler that shares b with another resource becomes 2b + 1.
fn apply_binding_rule(module: &mut naga::Module, push: &HashSet<GlobalHandle>) {
    let mut resource_slots = HashSet::new();
    for (h, g) in module.global_variables.iter() {
        if push.contains(&h) || is_sampler(module, g) {
            continue;
        }

        if let Some(b) = &g.binding {
            resource_slots.insert((b.group, b.binding));
        }
    }

    let samplers: HashSet<GlobalHandle> = module
        .global_variables
        .iter()
        .filter(|(_, g)| is_sampler(module, g))
        .map(|(h, _)| h)
        .collect();

    for (h, g) in module.global_variables.iter_mut() {
        if push.contains(&h) {
            continue;
        }

        if let Some(b) = g.binding.as_mut() {
            let shared = samplers.contains(&h) && resource_slots.contains(&(b.group, b.binding));
            b.binding = b.binding * 2 + u32::from(shared);
        }
    }
}

// Storage textures are write-only unless the shader loads them: core WebGPU allows read-write only for r32 formats.
fn narrow_storage_access(module: &mut naga::Module, uses: &HashMap<GlobalHandle, Use>) {
    let handles: Vec<GlobalHandle> = module.global_variables.iter().map(|(h, _)| h).collect();
    for h in handles {
        let ty = module.global_variables[h].ty;
        let TypeInner::Image {
            dim,
            arrayed,
            class: ImageClass::Storage { format, access },
        } = module.types[ty].inner.clone()
        else {
            continue;
        };

        let loaded = uses.get(&h).is_some_and(|u| u.loaded);
        let want = if loaded { access } else { StorageAccess::STORE };
        if want == access {
            continue;
        }

        let inner = TypeInner::Image {
            dim,
            arrayed,
            class: ImageClass::Storage { format, access: want },
        };
        let new_ty = module.types.insert(Type { name: None, inner }, naga::Span::UNDEFINED);
        module.global_variables[h].ty = new_ty;
    }
}

fn root_global(f: &naga::Function, mut h: naga::Handle<Expression>) -> Option<GlobalHandle> {
    loop {
        match f.expressions[h] {
            Expression::Access { base, .. } | Expression::AccessIndex { base, .. } => h = base,
            Expression::GlobalVariable(g) => return Some(g),
            _ => return None,
        }
    }
}

fn collect_writes(f: &naga::Function, block: &naga::Block, written: &mut HashSet<GlobalHandle>) {
    for stmt in block.iter() {
        match stmt {
            naga::Statement::Store { pointer, .. } | naga::Statement::Atomic { pointer, .. } => {
                if let Some(g) = root_global(f, *pointer) {
                    written.insert(g);
                }
            }
            naga::Statement::Block(b) => collect_writes(f, b, written),
            naga::Statement::If { accept, reject, .. } => {
                collect_writes(f, accept, written);
                collect_writes(f, reject, written);
            }
            naga::Statement::Switch { cases, .. } => {
                for case in cases {
                    collect_writes(f, &case.body, written);
                }
            }
            naga::Statement::Loop { body, continuing, .. } => {
                collect_writes(f, body, written);
                collect_writes(f, continuing, written);
            }
            _ => {}
        }
    }
}

// Storage buffers are read-only unless the shader writes them; vertex stages reject read-write storage.
fn narrow_buffer_access(module: &mut naga::Module) {
    let mut written = HashSet::new();
    for (_, f) in module.functions.iter() {
        collect_writes(f, &f.body, &mut written);
    }

    for e in module.entry_points.iter() {
        collect_writes(&e.function, &e.function.body, &mut written);
    }

    for (h, g) in module.global_variables.iter_mut() {
        if let AddressSpace::Storage { access } = g.space {
            if access.contains(StorageAccess::STORE) && !written.contains(&h) {
                g.space = AddressSpace::Storage {
                    access: StorageAccess::LOAD,
                };
            }
        }
    }
}

fn reads_global(f: &naga::Function, g: GlobalHandle) -> bool {
    f.expressions
        .iter()
        .any(|(_, e)| matches!(e, Expression::GlobalVariable(x) if *x == g))
}

// Wrapper shape from naga's SPIR-V reader: stores of built-in arguments into var<private> globals, then one call.
fn builtin_wrapper(wrapper: &naga::Function) -> Option<(naga::Handle<naga::Function>, Vec<(GlobalHandle, u32)>)> {
    let mut copies = Vec::new();
    let mut callee = None;
    for stmt in wrapper.body.iter() {
        match stmt {
            naga::Statement::Store { pointer, value } => {
                match (&wrapper.expressions[*pointer], &wrapper.expressions[*value]) {
                    (Expression::GlobalVariable(g), Expression::FunctionArgument(i)) => copies.push((*g, *i)),
                    _ => return None,
                }
            }
            naga::Statement::Call {
                function,
                arguments,
                result: None,
            } if arguments.is_empty() && callee.is_none() => {
                callee = Some(*function);
            }
            naga::Statement::Emit(_) | naga::Statement::Return { value: None } => {}
            _ => return None,
        }
    }

    let callee = callee?;
    if copies.is_empty() {
        return None;
    }

    Some((callee, copies))
}

// WGSL treats loads of var<private> as non-uniform, so a barrier after an early return on workgroup_id fails
// uniformity. For compute entry points, make the wrapped body the entry point with the built-ins as its arguments.
fn pass_compute_builtins(module: &mut naga::Module) {
    for ep in 0..module.entry_points.len() {
        if module.entry_points[ep].stage != naga::ShaderStage::Compute {
            continue;
        }

        let wrapper = &module.entry_points[ep].function;
        let Some((callee, copies)) = builtin_wrapper(wrapper) else {
            continue;
        };

        let others_read = module
            .functions
            .iter()
            .any(|(h, f)| h != callee && copies.iter().any(|(g, _)| reads_global(f, *g)));
        if others_read || !module.functions[callee].arguments.is_empty() {
            continue;
        }

        let arguments: Vec<naga::FunctionArgument> = copies
            .iter()
            .map(|(_, i)| wrapper.arguments[*i as usize].clone())
            .collect();
        let name = wrapper.name.clone();
        let mut body = module.functions[callee].clone();

        // Each built-in's private copy becomes a function variable, stored from its argument on entry. The body's
        // pointer uses stay valid (id.x is Load(AccessIndex(copy)), not a whole Load), and WGSL tracks the
        // uniformity of function variables.
        let mut prologue = naga::Block::new();
        for (pos, (global, _)) in copies.iter().enumerate() {
            let variable = naga::LocalVariable {
                name: module.global_variables[*global].name.clone(),
                ty: module.global_variables[*global].ty,
                init: None,
            };
            let local = body.local_variables.append(variable, naga::Span::UNDEFINED);
            for (_, e) in body.expressions.iter_mut() {
                if matches!(e, Expression::GlobalVariable(g) if g == global) {
                    *e = Expression::LocalVariable(local);
                }
            }

            let pointer = body
                .expressions
                .append(Expression::LocalVariable(local), naga::Span::UNDEFINED);
            let value = body
                .expressions
                .append(Expression::FunctionArgument(pos as u32), naga::Span::UNDEFINED);
            prologue.push(naga::Statement::Store { pointer, value }, naga::Span::UNDEFINED);
        }

        prologue.extend_block(std::mem::take(&mut body.body));
        body.body = prologue;
        body.arguments = arguments;
        body.name = name;
        module.entry_points[ep].function = body;

        // The old body is no longer called; leave it as an empty helper.
        let old_name = module.functions[callee].name.clone();
        module.functions[callee] = naga::Function {
            name: old_name,
            ..Default::default()
        };
    }
}

fn view_dimension(dim: ImageDimension, arrayed: bool) -> &'static str {
    match (dim, arrayed) {
        (ImageDimension::D1, _) => "1d",
        (ImageDimension::D2, false) => "2d",
        (ImageDimension::D2, true) => "2d-array",
        (ImageDimension::D3, _) => "3d",
        (ImageDimension::Cube, false) => "cube",
        (ImageDimension::Cube, true) => "cube-array",
    }
}

fn storage_access(access: StorageAccess) -> &'static str {
    let load = access.contains(StorageAccess::LOAD);
    let store = access.contains(StorageAccess::STORE);
    match (load, store) {
        (true, true) => "read-write",
        (true, false) => "read-only",
        _ => "write-only",
    }
}

// The details after "<group> <binding>", by the rules WebGPU's default ("auto") layouts use.
fn sidecar_entry(module: &naga::Module, g: &naga::GlobalVariable, u: Option<&Use>) -> Result<String, String> {
    let entry = match &module.types[g.ty].inner {
        TypeInner::Sampler { comparison } => {
            let kind = if *comparison { "comparison" } else { "filtering" };
            format!("sampler {kind}")
        }
        TypeInner::Image { dim, arrayed, class } => {
            let view = view_dimension(*dim, *arrayed);
            match class {
                ImageClass::Sampled { kind, multi } => {
                    let filtered = u.is_some_and(|u| u.sampled && !u.compared);
                    let sample_type = match kind {
                        ScalarKind::Float if filtered => "float",
                        ScalarKind::Float => "unfilterable-float",
                        ScalarKind::Uint => "uint",
                        _ => "sint",
                    };
                    format!("texture {view} {sample_type} {}", u8::from(*multi))
                }
                ImageClass::Depth { multi } => format!("texture {view} depth {}", u8::from(*multi)),
                ImageClass::Storage { format, access } => {
                    // naga's storage format names lower-cased are WebGPU's (Rgba8Unorm -> rgba8unorm).
                    let format = format!("{format:?}").to_lowercase();
                    format!("storage-texture {view} {format} {}", storage_access(*access))
                }
                ImageClass::External => return Err("external textures are not engine resources".into()),
            }
        }
        _ => match g.space {
            AddressSpace::Uniform => "uniform".to_string(),
            AddressSpace::Storage { access } if access.contains(StorageAccess::STORE) => "storage read-write".into(),
            AddressSpace::Storage { .. } => "storage read".to_string(),
            _ => return Err(format!("binding in address space {:?}", g.space)),
        },
    };

    Ok(entry)
}

fn write_sidecar(module: &naga::Module, uses: &HashMap<GlobalHandle, Use>, path: &str) -> Result<(), String> {
    let mut lines = Vec::new();
    for (h, g) in module.global_variables.iter() {
        let Some(b) = &g.binding else {
            continue;
        };

        let name = g.name.clone().unwrap_or_default();
        let entry = sidecar_entry(module, g, uses.get(&h)).map_err(|e| format!("'{name}': {e}"))?;
        lines.push((b.group, b.binding, entry));
    }

    lines.sort();
    let text: String = lines
        .iter()
        .map(|(group, binding, entry)| format!("{group} {binding} {entry}\n"))
        .collect();
    std::fs::write(path, text).map_err(|e| format!("write {path}: {e}"))
}

fn fail(code: i32, what: &str, message: String) -> ! {
    eprintln!("{what}: {message}");
    std::process::exit(code);
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 4 {
        fail(1, "USAGE", "spv-to-wgsl <in.spv> <out.wgsl> <out.bindings>".into());
    }

    let bytes = std::fs::read(&args[1]).unwrap_or_else(|e| fail(1, "READ", e.to_string()));
    let options = naga::front::spv::Options {
        adjust_coordinate_space: true, // Vulkan's Y-down clip space to WebGPU's Y-up (decision 7)
        strict_capabilities: false,
        block_ctx_dump_prefix: None,
    };
    let mut module =
        naga::front::spv::parse_u8_slice(&bytes, &options).unwrap_or_else(|e| fail(2, "PARSE", format!("{e:?}")));

    if let Err(e) = check_profile(&module) {
        fail(5, "PROFILE", e);
    }

    pass_compute_builtins(&mut module);
    let push = rewrite_push_constants(&mut module);
    apply_binding_rule(&mut module, &push);
    let uses = collect_uses(&module);
    narrow_storage_access(&mut module, &uses);
    narrow_buffer_access(&mut module);

    let flags = naga::valid::ValidationFlags::all();
    let strict = naga::valid::Validator::new(flags, naga::valid::Capabilities::empty()).validate(&module);
    let info = match strict {
        Ok(i) => i,
        Err(e) => {
            eprintln!("VALIDATE: {:?}", e.as_inner());
            naga::valid::Validator::new(flags, naga::valid::Capabilities::all())
                .validate(&module)
                .unwrap_or_else(|e2| fail(3, "VALIDATE_ALL", e2.as_inner().to_string()))
        }
    };

    let wgsl = naga::back::wgsl::write_string(&module, &info, naga::back::wgsl::WriterFlags::empty())
        .unwrap_or_else(|e| fail(4, "WRITE", format!("{e:?}")));

    // The HLSL samples in non-uniform control flow (GBufferPS's HasTexture branches); WGSL section 2.3.3.
    let text = format!("diagnostic(off, derivative_uniformity);\n{wgsl}");
    std::fs::write(&args[2], text).unwrap_or_else(|e| fail(4, "WRITE", e.to_string()));
    write_sidecar(&module, &uses, &args[3]).unwrap_or_else(|e| fail(4, "SIDECAR", e));
}
