// WGSL uniformity analysis (W3C WGSL "Uniformity"), which naga leaves disabled for fragment shaders.
//
// Every value and the control flow carry a mask of what they depend on: `NON_UNIFORM`, or a function
// parameter (its value, or the contents behind a pointer parameter). Each function reduces to a summary,
// so a call substitutes its arguments into the callee's masks. Local variables are flow-sensitive and
// joined at control-flow merges; loops iterate to a fixed point.

use naga::{
    Binding, BuiltIn, Expression, Function, GatherMode, Handle, Module, SampleLevel, Statement,
    TypeInner,
};

const NON_UNIFORM: u64 = 1;

// Differs between subgroups but not within one: @builtin(subgroup_id). Subgroup operations accept it (WGSL
// subgroup_uniformity scope); everything else that needs uniformity treats it as non-uniform. The CTS expects
// this even though the subgroup_uniformity language feature is not advertised.
// ponytail: a summary keeps one requirement set, so a subgroup operation inside a called function still rejects a
// subgroup_id-dependent argument; split the set per scope to lift that.
const SUBGROUP_UNIFORM: u64 = 1 << 63;
const ANY_NON_UNIFORM: u64 = NON_UNIFORM | SUBGROUP_UNIFORM;
const MAX_PARAMS: usize = 31;

fn param_value(k: usize) -> u64 {
    if k < MAX_PARAMS {
        1 << (1 + 2 * k)
    } else {
        0
    }
}

fn param_contents(k: usize) -> u64 {
    if k < MAX_PARAMS {
        1 << (2 + 2 * k)
    } else {
        0
    }
}

#[derive(Clone, Default)]
struct Summary {
    requires_uniform_call: bool,
    required_args: u64,
    result: u64,
    control_after: u64,
    contents_out: Vec<u64>,
}

#[derive(Clone, PartialEq)]
struct State {
    vars: Vec<u64>,
    contents: Vec<u64>,
    control: u64,
    alive: bool,
}

impl State {
    fn join(&mut self, other: &State) {
        self.control |= other.control;
        if !other.alive {
            return;
        }

        if !self.alive {
            self.vars = other.vars.clone();
            self.contents = other.contents.clone();
            self.alive = true;
            return;
        }

        for (a, b) in self.vars.iter_mut().zip(&other.vars) {
            *a |= *b;
        }
        for (a, b) in self.contents.iter_mut().zip(&other.contents) {
            *a |= *b;
        }
    }
}

struct Frame {
    is_loop: bool,
    breaks: Option<State>,
    continues: Option<State>,
}

fn add_exit(slot: &mut Option<State>, state: &State) {
    match slot {
        Some(existing) => existing.join(state),
        None => *slot = Some(state.clone()),
    }
}

// Whether a block can leave by anything other than falling off its end.
fn disrupts(block: &naga::Block, breaks: bool, continues: bool) -> bool {
    block.iter().any(|statement| match statement {
        Statement::Return { .. } => true,
        Statement::Break => breaks,
        Statement::Continue => continues,
        Statement::Block(inner) => disrupts(inner, breaks, continues),
        Statement::If { accept, reject, .. } => {
            disrupts(accept, breaks, continues) || disrupts(reject, breaks, continues)
        }
        Statement::Switch { cases, .. } => cases
            .iter()
            .any(|case| disrupts(&case.body, false, continues)),
        Statement::Loop {
            body, continuing, ..
        } => disrupts(body, false, false) || disrupts(continuing, false, false),
        _ => false,
    })
}

struct Walker<'a> {
    module: &'a Module,
    function: &'a Function,
    summaries: &'a [Summary],
    is_entry: bool,
    check_derivatives: bool,
    check_subgroups: bool,
    values: Vec<u64>,
    frames: Vec<Frame>,
    required: u64,
    requires_uniform_call: bool,
    result: u64,
    control_after: u64,
    contents_out: Vec<u64>,
}

impl Walker<'_> {
    fn require(&mut self, mask: u64) {
        self.requires_uniform_call = true;
        self.required |= mask;
    }

    fn global_taint(&self, global: Handle<naga::GlobalVariable>) -> u64 {
        match self.module.global_variables[global].space {
            naga::AddressSpace::Private
            | naga::AddressSpace::WorkGroup
            | naga::AddressSpace::Function => NON_UNIFORM,
            naga::AddressSpace::Storage { access }
                if access.contains(naga::StorageAccess::STORE) =>
            {
                NON_UNIFORM
            }
            _ => 0,
        }
    }

    // WGSL "Uniformity": workgroup_id, num_workgroups, num_subgroups, and subgroup_size in a compute shader.
    fn binding_taint(&self, binding: Option<&Binding>) -> u64 {
        match binding {
            Some(Binding::BuiltIn(
                BuiltIn::WorkGroupId | BuiltIn::NumWorkGroups | BuiltIn::NumSubgroups,
            )) => 0,
            Some(Binding::BuiltIn(BuiltIn::SubgroupSize)) if self.is_compute_entry() => 0,
            Some(Binding::BuiltIn(BuiltIn::SubgroupId)) => SUBGROUP_UNIFORM,
            _ => NON_UNIFORM,
        }
    }

    fn is_compute_entry(&self) -> bool {
        self.is_entry
            && self.module.entry_points.iter().any(|entry| {
                std::ptr::eq(&entry.function, self.function)
                    && entry.stage == naga::ShaderStage::Compute
            })
    }

    fn entry_argument_taint(&self, index: u32) -> u64 {
        let argument = &self.function.arguments[index as usize];
        if let TypeInner::Struct { members, .. } = &self.module.types[argument.ty].inner {
            return members.iter().fold(0, |mask, member| {
                mask | self.binding_taint(member.binding.as_ref())
            });
        }

        self.binding_taint(argument.binding.as_ref())
    }

    fn entry_member_taint(&self, base: Handle<Expression>, member: u32) -> Option<u64> {
        let Expression::FunctionArgument(index) = self.function.expressions[base] else {
            return None;
        };
        if !self.is_entry {
            return None;
        }

        let argument = &self.function.arguments[index as usize];
        let TypeInner::Struct { members, .. } = &self.module.types[argument.ty].inner else {
            return None;
        };
        Some(self.binding_taint(members[member as usize].binding.as_ref()))
    }

    fn value(&self, handle: Handle<Expression>) -> u64 {
        match self.function.expressions[handle] {
            Expression::FunctionArgument(index) if self.is_entry => {
                self.entry_argument_taint(index)
            }
            Expression::FunctionArgument(index) => param_value(index as usize),
            _ => self.values[handle.index()],
        }
    }

    fn optional(&self, handle: Option<Handle<Expression>>) -> u64 {
        handle.map_or(0, |h| self.value(h))
    }

    // What the memory behind a pointer depends on.
    fn pointee(&self, pointer: Handle<Expression>, state: &State) -> u64 {
        let mut current = pointer;
        loop {
            match self.function.expressions[current] {
                Expression::Access { base, .. } | Expression::AccessIndex { base, .. } => {
                    current = base
                }
                Expression::LocalVariable(local) => return state.vars[local.index()],
                Expression::GlobalVariable(global) => return self.global_taint(global),
                Expression::FunctionArgument(index) => return state.contents[index as usize],
                _ => return NON_UNIFORM,
            }
        }
    }

    fn is_read_write_image(&self, image: Handle<Expression>) -> bool {
        let mut current = image;
        loop {
            match self.function.expressions[current] {
                Expression::Access { base, .. } | Expression::AccessIndex { base, .. } => {
                    current = base
                }
                Expression::GlobalVariable(global) => {
                    let ty = self.module.global_variables[global].ty;
                    let TypeInner::Image {
                        class: naga::ImageClass::Storage { access, .. },
                        ..
                    } = self.module.types[ty].inner
                    else {
                        return false;
                    };
                    return access.contains(naga::StorageAccess::STORE);
                }
                _ => return false,
            }
        }
    }

    fn evaluate(&self, handle: Handle<Expression>, state: &State) -> u64 {
        match self.function.expressions[handle] {
            Expression::Compose { ref components, .. } => {
                components.iter().fold(0, |mask, &c| mask | self.value(c))
            }
            Expression::Access { base, index } => self.value(base) | self.value(index),
            Expression::AccessIndex { base, index } => self
                .entry_member_taint(base, index)
                .unwrap_or_else(|| self.value(base)),
            Expression::Splat { value, .. } => self.value(value),
            Expression::Swizzle { vector, .. } => self.value(vector),
            Expression::Load { pointer } => self.value(pointer) | self.pointee(pointer, state),
            Expression::ImageSample { level, .. }
                if matches!(level, SampleLevel::Auto | SampleLevel::Bias(_)) =>
            {
                NON_UNIFORM
            }
            Expression::ImageSample {
                coordinate,
                array_index,
                level,
                depth_ref,
                ..
            } => {
                let level_mask = match level {
                    SampleLevel::Exact(h) | SampleLevel::Bias(h) => self.value(h),
                    SampleLevel::Gradient { x, y } => self.value(x) | self.value(y),
                    _ => 0,
                };
                self.value(coordinate)
                    | self.optional(array_index)
                    | self.optional(depth_ref)
                    | level_mask
            }
            Expression::ImageLoad {
                image,
                coordinate,
                array_index,
                sample,
                level,
            } => {
                let read_write = if self.is_read_write_image(image) {
                    NON_UNIFORM
                } else {
                    0
                };
                read_write
                    | self.value(coordinate)
                    | self.optional(array_index)
                    | self.optional(sample)
                    | self.optional(level)
            }
            Expression::ImageQuery { image, .. } => self.value(image),
            Expression::Unary { expr, .. } | Expression::As { expr, .. } => self.value(expr),
            Expression::Binary { left, right, .. } => self.value(left) | self.value(right),
            Expression::Select {
                condition,
                accept,
                reject,
            } => self.value(condition) | self.value(accept) | self.value(reject),
            Expression::Relational { argument, .. } => self.value(argument),
            Expression::Math {
                arg,
                arg1,
                arg2,
                arg3,
                ..
            } => self.value(arg) | self.optional(arg1) | self.optional(arg2) | self.optional(arg3),
            Expression::ArrayLength(pointer) => self.value(pointer),
            _ => NON_UNIFORM,
        }
    }

    fn is_derivative_operation(&self, handle: Handle<Expression>) -> bool {
        match self.function.expressions[handle] {
            Expression::Derivative { .. } => true,
            Expression::ImageSample { level, .. } => {
                matches!(level, SampleLevel::Auto | SampleLevel::Bias(_))
            }
            _ => false,
        }
    }

    fn write_pointer(&self, pointer: Handle<Expression>, mask: u64, state: &mut State) {
        let direct = !matches!(
            self.function.expressions[pointer],
            Expression::Access { .. } | Expression::AccessIndex { .. }
        );

        let mut current = pointer;
        let slot = loop {
            match self.function.expressions[current] {
                Expression::Access { base, .. } | Expression::AccessIndex { base, .. } => {
                    current = base
                }
                Expression::LocalVariable(local) => break Some(&mut state.vars[local.index()]),
                Expression::FunctionArgument(index) if !self.is_entry => {
                    break Some(&mut state.contents[index as usize])
                }
                _ => break None,
            }
        };

        let Some(slot) = slot else {
            return;
        };

        if direct {
            *slot = mask;
        } else {
            *slot |= mask;
        }
    }

    fn visit_call(
        &mut self,
        callee: Handle<Function>,
        arguments: &[Handle<Expression>],
        result: Option<Handle<Expression>>,
        state: &mut State,
    ) {
        let summary = self.summaries[callee.index()].clone();
        let values: Vec<u64> = arguments.iter().map(|&a| self.value(a)).collect();
        let contents: Vec<u64> = arguments
            .iter()
            .enumerate()
            .map(|(k, &a)| {
                if self.is_pointer_param(callee, k) {
                    self.pointee(a, state)
                } else {
                    0
                }
            })
            .collect();

        let substitute = |mask: u64| -> u64 {
            let mut out = mask & ANY_NON_UNIFORM;
            for k in 0..values.len() {
                if mask & param_value(k) != 0 {
                    out |= values[k];
                }
                if mask & param_contents(k) != 0 {
                    out |= contents[k];
                }
            }
            out
        };

        if summary.requires_uniform_call {
            self.require(state.control | substitute(summary.required_args));
        }

        state.control |= substitute(summary.control_after);
        if let Some(handle) = result {
            self.values[handle.index()] = substitute(summary.result) | state.control;
        }

        for (k, &argument) in arguments.iter().enumerate() {
            if self.is_pointer_param(callee, k) {
                let written = substitute(summary.contents_out[k]) | state.control | values[k];
                self.write_pointer(argument, written, state);
            }
        }
    }

    fn is_pointer_param(&self, callee: Handle<Function>, k: usize) -> bool {
        // A call with too many arguments is reported later by naga's validator.
        self.module.functions[callee]
            .arguments
            .get(k)
            .is_some_and(|argument| {
                matches!(
                    self.module.types[argument.ty].inner,
                    TypeInner::Pointer { .. }
                )
            })
    }

    fn visit_if(
        &mut self,
        condition: Handle<Expression>,
        accept: &naga::Block,
        reject: &naga::Block,
        state: &mut State,
    ) {
        let base_control = state.control;
        let mut accepted = state.clone();
        accepted.control |= self.value(condition);
        let mut rejected = accepted.clone();
        self.walk(accept, &mut accepted);
        self.walk(reject, &mut rejected);

        let disrupted = disrupts(accept, true, true) || disrupts(reject, true, true);
        accepted.join(&rejected);
        *state = accepted;
        if !disrupted {
            state.control = base_control;
        }
    }

    fn visit_switch(
        &mut self,
        selector: Handle<Expression>,
        cases: &[naga::SwitchCase],
        state: &mut State,
    ) {
        let base_control = state.control;
        let selector_mask = self.value(selector);
        let mut entry = state.clone();
        entry.control |= selector_mask;

        let mut merged: Option<State> = None;
        let mut falling: Option<State> = None;
        let mut disrupted = false;
        self.frames.push(Frame {
            is_loop: false,
            breaks: None,
            continues: None,
        });

        for case in cases {
            let mut body = entry.clone();
            if let Some(previous) = falling.take() {
                body.join(&previous);
            }

            self.walk(&case.body, &mut body);
            disrupted |= disrupts(&case.body, false, true);
            if case.fall_through {
                falling = Some(body);
            } else {
                add_exit(&mut merged, &body);
            }
        }

        let frame = self.frames.pop().expect("switch frame");
        if let Some(breaks) = frame.breaks {
            add_exit(&mut merged, &breaks);
        }

        if let Some(merged) = merged {
            *state = merged;
        }
        if !disrupted {
            state.control = base_control;
        }
    }

    fn visit_loop(
        &mut self,
        body: &naga::Block,
        continuing: &naga::Block,
        break_if: Option<Handle<Expression>>,
        state: &mut State,
    ) {
        let mut head = state.clone();
        let mut exit: Option<State> = None;

        // The masks only grow, so this settles; the bound guards against a logic error.
        for _ in 0..256 {
            self.frames.push(Frame {
                is_loop: true,
                breaks: None,
                continues: None,
            });
            let mut iteration = head.clone();
            self.walk(body, &mut iteration);
            let frame = self.frames.pop().expect("loop frame");

            if let Some(continues) = &frame.continues {
                iteration.join(continues);
            }

            exit = frame.breaks;
            self.walk(continuing, &mut iteration);
            if let Some(condition) = break_if {
                iteration.control |= self.value(condition);
                if iteration.alive {
                    add_exit(&mut exit, &iteration);
                }
            }

            let mut next = head.clone();
            next.join(&iteration);
            if next == head {
                break;
            }
            head = next;
        }

        // Breaks and continues rejoin at the loop's end; only an early return leaves the rest non-uniform.
        let returns = disrupts(body, false, false) || disrupts(continuing, false, false);
        let exit_control = exit.as_ref().map_or(0, |e| e.control);
        let control = if returns {
            head.control | exit_control
        } else {
            state.control
        };

        match exit {
            Some(exit) => *state = exit,
            None => state.alive = false,
        }
        state.control = control;
    }

    fn walk(&mut self, block: &naga::Block, state: &mut State) {
        for statement in block.iter() {
            if !state.alive {
                return;
            }

            self.visit(statement, state);
        }
    }

    fn visit(&mut self, statement: &Statement, state: &mut State) {
        match statement {
            Statement::Emit(range) => {
                for handle in range.clone() {
                    self.values[handle.index()] = self.evaluate_or_collective(handle, state);
                }
            }
            Statement::Block(inner) => self.walk(inner, state),
            Statement::If {
                condition,
                accept,
                reject,
            } => self.visit_if(*condition, accept, reject, state),
            Statement::Switch { selector, cases } => self.visit_switch(*selector, cases, state),
            Statement::Loop {
                body,
                continuing,
                break_if,
            } => self.visit_loop(body, continuing, *break_if, state),
            Statement::Break => {
                // A misplaced break or continue is reported later by naga's validator.
                if let Some(frame) = self.frames.last_mut() {
                    add_exit(&mut frame.breaks, state);
                }
                state.alive = false;
            }
            Statement::Continue => {
                if let Some(frame) = self.frames.iter_mut().rev().find(|frame| frame.is_loop) {
                    add_exit(&mut frame.continues, state);
                }
                state.alive = false;
            }
            Statement::Return { value } => {
                self.result |= self.optional(*value) | state.control;
                for (out, current) in self.contents_out.iter_mut().zip(&state.contents) {
                    *out |= *current;
                }
                state.alive = false;
            }
            Statement::ControlBarrier(_) | Statement::MemoryBarrier(_) => {
                self.require(state.control)
            }
            Statement::Store { pointer, value } => {
                let mask = self.value(*value) | self.value(*pointer) | state.control;
                self.write_pointer(*pointer, mask, state);
            }
            Statement::Atomic { result, .. } => {
                if let Some(handle) = result {
                    self.values[handle.index()] = NON_UNIFORM;
                }
            }
            Statement::WorkGroupUniformLoad { pointer, result } => {
                self.require(state.control | self.value(*pointer));
                self.values[result.index()] = 0;
            }
            Statement::Call {
                function,
                arguments,
                result,
            } => self.visit_call(*function, arguments, *result, state),
            Statement::SubgroupBallot { result, .. }
            | Statement::SubgroupGather { result, .. }
            | Statement::SubgroupCollectiveOperation { result, .. } => {
                // Without the subgroup_uniformity language feature, every subgroup and quad call needs
                // uniform control flow, and so does the delta or mask of a relative shuffle.
                if self.check_subgroups {
                    let mut mask = state.control;
                    if let Statement::SubgroupGather {
                        mode:
                            GatherMode::ShuffleUp(operand)
                            | GatherMode::ShuffleDown(operand)
                            | GatherMode::ShuffleXor(operand),
                        ..
                    } = statement
                    {
                        mask |= self.value(*operand);
                    }
                    self.require(mask & !SUBGROUP_UNIFORM);
                }
                self.values[result.index()] = NON_UNIFORM;
            }
            _ => {}
        }
    }

    fn evaluate_or_collective(&mut self, handle: Handle<Expression>, state: &State) -> u64 {
        if self.is_derivative_operation(handle) && self.check_derivatives {
            self.require(state.control);
        }

        self.evaluate(handle, state)
    }
}

// Returns the function's summary and whether it needs a non-uniform value to be uniform.
fn analyze_function(
    module: &Module,
    function: &Function,
    summaries: &[Summary],
    is_entry: bool,
    check_derivatives: bool,
    check_subgroups: bool,
) -> (Summary, bool) {
    let mut contents = vec![0u64; function.arguments.len()];
    for (k, argument) in function.arguments.iter().enumerate() {
        if matches!(module.types[argument.ty].inner, TypeInner::Pointer { .. }) {
            contents[k] = param_contents(k);
        }
    }

    let mut walker = Walker {
        module,
        function,
        summaries,
        is_entry,
        check_derivatives,
        check_subgroups,
        values: vec![0; function.expressions.len()],
        frames: Vec::new(),
        required: 0,
        requires_uniform_call: false,
        result: 0,
        control_after: 0,
        contents_out: vec![0; function.arguments.len()],
    };

    let mut state = State {
        vars: vec![0; function.local_variables.len()],
        contents,
        control: 0,
        alive: true,
    };

    walker.walk(&function.body, &mut state);

    // Falling off the end is an implicit return.
    if state.alive {
        walker.control_after |= state.control;
        for (out, current) in walker.contents_out.iter_mut().zip(&state.contents) {
            *out |= *current;
        }
    }

    let violated = walker.required & ANY_NON_UNIFORM != 0;
    let summary = Summary {
        requires_uniform_call: walker.requires_uniform_call,
        required_args: walker.required & !ANY_NON_UNIFORM,
        result: walker.result,
        control_after: walker.control_after,
        contents_out: walker.contents_out,
    };
    (summary, violated)
}

// `diagnostic(<severity>, <rule>)` filters for derivative_uniformity or subgroup_uniformity, as global and
// per-function severities.
// ponytail: a filter on a statement applies to its whole function; exact scopes need naga's filter tree.
struct DiagnosticFilters {
    global_checks: bool,
    per_function: std::collections::HashMap<String, bool>,
}

impl DiagnosticFilters {
    fn parse(source: &str, rule: &str) -> Self {
        let mut filters = Self {
            global_checks: true,
            per_function: std::collections::HashMap::new(),
        };

        for (position, _) in source.match_indices(rule) {
            let Some(open) = source[..position].rfind("diagnostic") else {
                continue;
            };
            let checks = source[open..position].contains("error");
            if !source[..open].trim_end().ends_with('@') && !source[..open].ends_with('@') {
                filters.global_checks = checks;
                continue;
            }

            if let Some(name) = Self::scoped_function(source, position) {
                // Any lenient scope wins, so the coarse model never rejects a shader the spec accepts.
                *filters
                    .per_function
                    .entry(name.to_string())
                    .or_insert(checks) &= checks;
            }
        }

        filters
    }

    // The function an `@diagnostic` attribute is on, or else the one that contains it.
    fn scoped_function(source: &str, position: usize) -> Option<&str> {
        let rest = &source[position..];
        let attached = rest.find("fn ").filter(|&next| !rest[..next].contains('{'));
        let start = match attached {
            Some(next) => position + next + 3,
            None => source[..position].rfind("fn ")? + 3,
        };

        let name = source[start..].split('(').next()?.trim();
        Some(name)
    }

    fn checks(&self, function: &str) -> bool {
        self.per_function
            .get(function)
            .copied()
            .unwrap_or(self.global_checks)
    }
}

fn violation(name: &str) -> crate::WgslMessage {
    crate::WgslMessage {
        r#type: "error".to_string(),
        message: format!("'{name}' uses a value or control flow that the uniformity analysis requires to be uniform"),
        line_num: 1,
        line_pos: 1,
        offset: 0,
        length: 0,
    }
}

pub(crate) fn validate(module: &Module, source: &str) -> Vec<crate::WgslMessage> {
    let derivative_filters = DiagnosticFilters::parse(source, "derivative_uniformity");
    let subgroup_filters = DiagnosticFilters::parse(source, "subgroup_uniformity");
    let mut errors = Vec::new();
    let mut summaries: Vec<Summary> = Vec::with_capacity(module.functions.len());

    // naga orders the function arena so that a callee comes before its callers.
    for (_, function) in module.functions.iter() {
        let name = function.name.as_deref().unwrap_or("function");
        let (summary, violated) = analyze_function(
            module,
            function,
            &summaries,
            false,
            derivative_filters.checks(name),
            subgroup_filters.checks(name),
        );
        if violated {
            errors.push(violation(name));
        }
        summaries.push(summary);
    }

    for entry in &module.entry_points {
        let (_, violated) = analyze_function(
            module,
            &entry.function,
            &summaries,
            true,
            derivative_filters.checks(&entry.name),
            subgroup_filters.checks(&entry.name),
        );
        if violated {
            errors.push(violation(&entry.name));
        }
    }

    errors
}
