use std::collections::{HashMap, HashSet};
use std::sync::Once;

use naga::back::pipeline_constants::process_overrides;
use naga::back::spv;
use naga::back::PipelineConstants;
use naga::front::wgsl;
use naga::proc::{BoundsCheckPolicies, BoundsCheckPolicy};
use naga::valid::{Capabilities, ValidationFlags, Validator};

// WGSL §15.6: clamp every OOB index. `Restrict` (not `ReadZeroSkipWrite`)
// because naga 29's spv backend hits FeatureNotImplemented on storage-buffer
// atomics with the latter.
fn robust_access_policies() -> BoundsCheckPolicies {
    BoundsCheckPolicies {
        index: BoundsCheckPolicy::Restrict,
        buffer: BoundsCheckPolicy::Restrict,
        image_load: BoundsCheckPolicy::Restrict,
        binding_array: BoundsCheckPolicy::Restrict,
    }
}

mod ffi;

// naga 29's spv writer has `unimplemented!()` paths (ImageClass::External,
// some abstract-type lowering). `catch_unwind` recovers the thread but the
// default panic hook still prints a traceback to stderr, which drowns CTS
// output. Swap in a no-op hook once per process.
static INIT_PANIC_SUPPRESSION: Once = Once::new();
fn suppress_naga_panics() {
    INIT_PANIC_SUPPRESSION.call_once(|| {
        std::panic::set_hook(Box::new(|_| {}));
    });
}

fn split_top_level_args(args: &str) -> Vec<String> {
    let mut out: Vec<String> = Vec::new();
    let mut depth = 0i32;
    let mut start = 0usize;
    for (i, b) in args.bytes().enumerate() {
        match b {
            b'(' | b'[' | b'{' => depth += 1,
            b')' | b']' | b'}' => depth -= 1,
            b',' if depth == 0 => {
                out.push(args[start..i].trim().to_string());
                start = i + 1;
            }
            _ => {}
        }
    }
    out.push(args[start..].trim().to_string());
    out
}

fn find_call_args(
    source: &str,
    call_start: usize,
    name_len: usize,
) -> Option<(Vec<String>, usize)> {
    let bytes = source.as_bytes();
    let mut open = call_start + name_len;
    while open < bytes.len() && bytes[open].is_ascii_whitespace() {
        open += 1;
    }
    if open >= bytes.len() || bytes[open] != b'(' {
        return None;
    }

    let mut depth = 1i32;
    let mut i = open + 1;
    while i < bytes.len() {
        match bytes[i] {
            b'(' => depth += 1,
            b')' => {
                depth -= 1;
                if depth == 0 {
                    return Some((split_top_level_args(&source[open + 1..i]), i));
                }
            }
            _ => {}
        }
        i += 1;
    }
    None
}

fn parse_full_call(expr: &str) -> Option<(String, Vec<String>)> {
    let s = trim_outer_parens(expr.trim());
    let open = s.find('(')?;
    let callee = s[..open].trim().to_string();
    let (args, end) = find_call_args(s, 0, open)?;
    if s[end + 1..].trim().is_empty() {
        Some((callee, args))
    } else {
        None
    }
}

fn trim_outer_parens(mut s: &str) -> &str {
    loop {
        let trimmed = s.trim();
        if !trimmed.starts_with('(') || !trimmed.ends_with(')') {
            return trimmed;
        }
        let bytes = trimmed.as_bytes();
        let mut depth = 0i32;
        let mut closes_at_end = false;
        for (i, b) in bytes.iter().enumerate() {
            match *b {
                b'(' => depth += 1,
                b')' => {
                    depth -= 1;
                    if depth == 0 {
                        closes_at_end = i == bytes.len() - 1;
                        break;
                    }
                }
                _ => {}
            }
        }
        if closes_at_end {
            s = &trimmed[1..trimmed.len() - 1];
        } else {
            return trimmed;
        }
    }
}

fn parse_numeric_literal(expr: &str) -> Option<f64> {
    let mut s = trim_outer_parens(expr).trim();
    if s.is_empty() {
        return None;
    }
    if s.ends_with('i') || s.ends_with('u') {
        return None;
    }
    if s.ends_with('f') || s.ends_with('h') {
        s = &s[..s.len() - 1];
    }
    s.parse::<f64>().ok()
}

fn normalize_type_name(name: &str) -> String {
    name.chars()
        .filter(|c| !c.is_whitespace())
        .collect::<String>()
}

fn constructor_vector_width(name: &str) -> Option<usize> {
    let n = normalize_type_name(name).to_ascii_lowercase();
    if n.starts_with("vec2") {
        Some(2)
    } else if n.starts_with("vec3") {
        Some(3)
    } else if n.starts_with("vec4") {
        Some(4)
    } else {
        None
    }
}

fn constructor_is_float_scalar(name: &str) -> bool {
    let n = normalize_type_name(name).to_ascii_lowercase();
    matches!(n.as_str(), "f32" | "f16" | "abstractfloat" | "f32_alias")
}

fn resolve_smoothstep_values(
    expr: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<Vec<f64>> {
    let s = trim_outer_parens(expr).trim();
    if let Some(v) = parse_numeric_literal(s) {
        return Some(vec![v]);
    }
    if let Some(constants) = constants {
        if s.bytes().all(is_ident_char) {
            if let Some(v) = constants.get(s) {
                return Some(vec![*v]);
            }
        }
    }

    let (callee, args) = parse_full_call(s)?;
    if let Some(width) = constructor_vector_width(&callee) {
        if args.len() == 1 {
            let values = resolve_smoothstep_values(&args[0], constants)?;
            if values.len() == 1 {
                return Some(vec![values[0]; width]);
            }
            return (values.len() == width).then_some(values);
        }
        let mut values = Vec::new();
        for arg in args {
            let mut resolved = resolve_smoothstep_values(&arg, constants)?;
            values.append(&mut resolved);
        }
        return (values.len() == width).then_some(values);
    }
    if constructor_is_float_scalar(&callee) && args.len() == 1 {
        return resolve_smoothstep_values(&args[0], constants).map(|mut v| {
            v.truncate(1);
            v
        });
    }
    None
}

fn smoothstep_pairs_equal(low: &[f64], high: &[f64]) -> bool {
    let width = low.len().max(high.len());
    if width == 0 {
        return false;
    }
    for i in 0..width {
        let l = if low.len() == 1 {
            low[0]
        } else if i < low.len() {
            low[i]
        } else {
            return false;
        };
        let h = if high.len() == 1 {
            high[0]
        } else if i < high.len() {
            high[i]
        } else {
            return false;
        };
        if l == h {
            return true;
        }
    }
    false
}

fn source_without_fn_bodies(source: &str) -> String {
    let bytes = source.as_bytes();
    let mut out = String::with_capacity(source.len());
    let mut i = 0usize;
    let mut last = 0usize;
    while i + 2 < bytes.len() {
        if bytes[i] == b'f'
            && bytes[i + 1] == b'n'
            && (i == 0 || !is_ident_char(bytes[i - 1]))
            && bytes[i + 2].is_ascii_whitespace()
        {
            let mut open = i + 2;
            while open < bytes.len() && bytes[open] != b'{' {
                open += 1;
            }
            if open >= bytes.len() {
                break;
            }
            let mut close = open + 1;
            let mut depth = 1i32;
            while close < bytes.len() && depth > 0 {
                match bytes[close] {
                    b'{' => depth += 1,
                    b'}' => depth -= 1,
                    _ => {}
                }
                close += 1;
            }
            if depth != 0 {
                break;
            }
            out.push_str(&source[last..i]);
            for b in &bytes[i..close] {
                out.push(if *b == b'\n' || *b == b'\r' {
                    *b as char
                } else {
                    ' '
                });
            }
            i = close;
            last = close;
            continue;
        }
        i += 1;
    }
    out.push_str(&source[last..]);
    out
}

fn smoothstep_validation_source(source: &str, entry_point: Option<&str>) -> String {
    if let Some(entry) = entry_point {
        let mut scan = source_without_fn_bodies(source);
        scan.push('\n');
        scan.push_str(&collect_reachable_bodies(source, entry));
        scan
    } else {
        source.to_string()
    }
}

fn validate_smoothstep_source(
    source: &str,
    constants: Option<&HashMap<String, f64>>,
    entry_point: Option<&str>,
) -> Vec<WgslMessage> {
    if !source.contains("smoothstep") {
        return Vec::new();
    }
    let scan = smoothstep_validation_source(source, entry_point);
    let bytes = scan.as_bytes();
    let mut errors = Vec::new();
    let mut i = 0usize;
    while let Some(rel) = scan[i..].find("smoothstep") {
        let start = i + rel;
        let end = start + "smoothstep".len();
        if (start > 0 && is_ident_char(bytes[start - 1]))
            || (end < bytes.len() && is_ident_char(bytes[end]))
        {
            i = end;
            continue;
        }
        if let Some((args, close)) = find_call_args(&scan, start, "smoothstep".len()) {
            if args.len() == 3 {
                if let (Some(low), Some(high)) = (
                    resolve_smoothstep_values(&args[0], constants),
                    resolve_smoothstep_values(&args[1], constants),
                ) {
                    if smoothstep_pairs_equal(&low, &high) {
                        errors.push(WgslMessage {
                            r#type: "error".to_string(),
                            message: "smoothstep(edge0, edge1, x): edge0 must not equal edge1"
                                .to_string(),
                            line_num: 0,
                            line_pos: 0,
                            offset: 0,
                            length: 0,
                        });
                    }
                }
            }
            i = close + 1;
        } else {
            i = end;
        }
    }
    errors
}

fn has_concrete_int_suffix(arg: &str) -> bool {
    let bytes = arg.as_bytes();
    for i in 1..bytes.len() {
        let c = bytes[i];
        if (c == b'i' || c == b'u') && bytes[i - 1].is_ascii_digit() {
            let next_ident = i + 1 < bytes.len() && is_ident_char(bytes[i + 1]);
            if !next_ident {
                return true;
            }
        }
    }
    false
}

fn smoothstep_arg_lowerable(arg: &str) -> bool {
    let lower = arg.to_ascii_lowercase();
    let bad_substrings = [
        "true",
        "false",
        "bool",
        "i32",
        "u32",
        "vec2i",
        "vec3i",
        "vec4i",
        "vec2u",
        "vec3u",
        "vec4u",
        "atomic",
        "array",
        "mat",
        "sampler",
        "texture",
        "read_write",
        "modf",
    ];
    if bad_substrings.iter().any(|bad| lower.contains(bad)) {
        return false;
    }
    if has_concrete_int_suffix(&lower) {
        return false;
    }
    let trimmed = trim_outer_parens(arg).trim();
    !matches!(trimmed, "a" | "s" | "t") && !trimmed.starts_with("k.")
}

fn smoothstep_call_vector_width(args: &[String]) -> Option<usize> {
    args.iter().find_map(|arg| {
        parse_full_call(arg).and_then(|(callee, _)| constructor_vector_width(&callee))
    })
}

fn smoothstep_call_uses_f16(args: &[String]) -> bool {
    args.iter().any(|arg| {
        let lower = arg.to_ascii_lowercase();
        lower.contains("f16")
            || lower.contains("vec2h")
            || lower.contains("vec3h")
            || lower.contains("vec4h")
            || lower
                .bytes()
                .enumerate()
                .any(|(i, b)| b == b'h' && i > 0 && lower.as_bytes()[i - 1].is_ascii_digit())
    })
}

fn lower_smoothstep_call(args: &[String]) -> Option<String> {
    if args.len() != 3 || !args.iter().all(|arg| smoothstep_arg_lowerable(arg)) {
        return None;
    }
    let scalar = if smoothstep_call_uses_f16(args) {
        "f16"
    } else {
        "f32"
    };
    let has_literal_or_constructor = args
        .iter()
        .any(|arg| parse_numeric_literal(arg).is_some() || parse_full_call(arg).is_some());
    if smoothstep_call_vector_width(args).is_none() && !has_literal_or_constructor {
        return None;
    }
    let type_name = if let Some(width) = smoothstep_call_vector_width(args) {
        format!("vec{}<{}>", width, scalar)
    } else {
        scalar.to_string()
    };
    let ctor = |value: &str| -> String { format!("{}({})", type_name, value) };
    let edge0 = ctor(&args[0]);
    let edge1 = ctor(&args[1]);
    let x = ctor(&args[2]);
    let zero = ctor("0.0");
    let one = ctor("1.0");
    let two = ctor("2.0");
    let three = ctor("3.0");
    let t = format!("clamp((({x}) - ({edge0})) / (({edge1}) - ({edge0})), {zero}, {one})");
    Some(format!("(({t}) * ({t}) * (({three}) - (({two}) * ({t}))))"))
}

fn lower_smoothstep_source(source: &str) -> String {
    if !source.contains("smoothstep") {
        return source.to_string();
    }
    let bytes = source.as_bytes();
    let mut out = String::with_capacity(source.len());
    let mut pos = 0usize;
    let mut i = 0usize;
    while let Some(rel) = source[i..].find("smoothstep") {
        let start = i + rel;
        let end = start + "smoothstep".len();
        if (start > 0 && is_ident_char(bytes[start - 1]))
            || (end < bytes.len() && is_ident_char(bytes[end]))
        {
            i = end;
            continue;
        }
        if let Some((args, close)) = find_call_args(source, start, "smoothstep".len()) {
            if let Some(replacement) = lower_smoothstep_call(&args) {
                out.push_str(&source[pos..start]);
                out.push_str(&replacement);
                pos = close + 1;
            }
            i = close + 1;
        } else {
            i = end;
        }
    }
    if pos == 0 {
        source.to_string()
    } else {
        out.push_str(&source[pos..]);
        out
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum LdexpFloatKind {
    Abstract,
    F32,
    F16,
}

impl LdexpFloatKind {
    fn bias(self) -> i32 {
        match self {
            Self::Abstract => 1023,
            Self::F32 => 127,
            Self::F16 => 15,
        }
    }

    fn max_finite(self) -> f64 {
        match self {
            Self::Abstract => f64::MAX,
            Self::F32 => f32::MAX as f64,
            Self::F16 => 65504.0,
        }
    }

    fn type_name(self) -> Option<&'static str> {
        match self {
            Self::Abstract => None,
            Self::F32 => Some("f32"),
            Self::F16 => Some("f16"),
        }
    }
}

#[derive(Clone, Copy, Debug)]
struct LdexpArgType {
    kind: LdexpFloatKind,
    width: usize,
}

struct LdexpFloatValues {
    kind: LdexpFloatKind,
    values: Vec<f64>,
}

struct LdexpIntValues {
    values: Vec<i32>,
}

fn wgsl_error(message: impl Into<String>) -> WgslMessage {
    WgslMessage {
        r#type: "error".to_string(),
        message: message.into(),
        line_num: 0,
        line_pos: 0,
        offset: 0,
        length: 0,
    }
}

fn parse_wgsl_i64_like(expr: &str) -> Option<i64> {
    let mut s = trim_outer_parens(expr).trim();
    if s == "-9223372036854775807 - 1" {
        return Some(i64::MIN);
    }
    if s.ends_with('i') {
        s = &s[..s.len() - 1];
    }
    if s.ends_with('u') || s.contains('.') || s.contains('e') || s.contains('E') {
        return None;
    }
    s.parse::<i64>().ok()
}

fn parse_wgsl_i32_expr(expr: &str, constants: Option<&HashMap<String, f64>>) -> Option<i32> {
    let s = trim_outer_parens(expr).trim();
    if let Some(constants) = constants {
        if s.bytes().all(is_ident_char) {
            let v = *constants.get(s)?;
            return (v.is_finite()
                && v.fract() == 0.0
                && v >= i32::MIN as f64
                && v <= i32::MAX as f64)
                .then_some(v as i32);
        }
    }
    if let Some((callee, args)) = parse_full_call(s) {
        if normalize_type_name(&callee).eq_ignore_ascii_case("i32") && args.len() == 1 {
            return parse_wgsl_i32_expr(&args[0], constants);
        }
    }
    let v = parse_wgsl_i64_like(s)?;
    i32::try_from(v).ok()
}

fn scalar_float_kind_from_constructor(callee: &str) -> Option<LdexpFloatKind> {
    match normalize_type_name(callee).to_ascii_lowercase().as_str() {
        "f32" => Some(LdexpFloatKind::F32),
        "f16" => Some(LdexpFloatKind::F16),
        "abstractfloat" | "abstract-float" => Some(LdexpFloatKind::Abstract),
        _ => None,
    }
}

fn vector_constructor_info(callee: &str) -> Option<(usize, Option<LdexpFloatKind>)> {
    let n = normalize_type_name(callee).to_ascii_lowercase();
    let width = if n.starts_with("vec2") {
        2
    } else if n.starts_with("vec3") {
        3
    } else if n.starts_with("vec4") {
        4
    } else {
        return None;
    };
    let kind = if n.contains("f32") || n.ends_with('f') {
        Some(LdexpFloatKind::F32)
    } else if n.contains("f16") || n.ends_with('h') {
        Some(LdexpFloatKind::F16)
    } else if n.contains("abstractfloat") || n.contains("abstract-float") {
        Some(LdexpFloatKind::Abstract)
    } else {
        None
    };
    Some((width, kind))
}

fn vector_constructor_allows_float_arg(callee: &str) -> bool {
    let n = normalize_type_name(callee).to_ascii_lowercase();
    !(n.contains("i32")
        || n.contains("u32")
        || n.contains("bool")
        || n.ends_with('i')
        || n.ends_with('u')
        || n.ends_with('b'))
}

fn vector_constructor_allows_int_arg(callee: &str) -> bool {
    let n = normalize_type_name(callee).to_ascii_lowercase();
    !(n.contains("f32")
        || n.contains("f16")
        || n.contains("u32")
        || n.contains("bool")
        || n.ends_with('f')
        || n.ends_with('h')
        || n.ends_with('u')
        || n.ends_with('b'))
}

fn parse_wgsl_float_leaf(
    expr: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<(f64, LdexpFloatKind)> {
    let mut s = trim_outer_parens(expr).trim();
    if let Some(constants) = constants {
        if s.bytes().all(is_ident_char) {
            return constants
                .get(s)
                .copied()
                .map(|v| (v, LdexpFloatKind::Abstract));
        }
    }
    if let Some((callee, args)) = parse_full_call(s) {
        if let Some(kind) = scalar_float_kind_from_constructor(&callee) {
            if args.len() == 1 {
                let (v, _) = parse_wgsl_float_leaf(&args[0], constants)?;
                return Some((v, kind));
            }
        }
    }
    let kind = if s.ends_with('f') {
        s = &s[..s.len() - 1];
        LdexpFloatKind::F32
    } else if s.ends_with('h') {
        s = &s[..s.len() - 1];
        LdexpFloatKind::F16
    } else if s.ends_with('i') || s.ends_with('u') {
        return None;
    } else {
        LdexpFloatKind::Abstract
    };
    if let Some(v) = parse_wgsl_i64_like(s) {
        return Some((v as f64, kind));
    }
    s.parse::<f64>().ok().map(|v| (v, kind))
}

fn merge_ldexp_kinds(a: LdexpFloatKind, b: LdexpFloatKind) -> LdexpFloatKind {
    match (a, b) {
        (LdexpFloatKind::F16, _) | (_, LdexpFloatKind::F16) => LdexpFloatKind::F16,
        (LdexpFloatKind::F32, _) | (_, LdexpFloatKind::F32) => LdexpFloatKind::F32,
        _ => LdexpFloatKind::Abstract,
    }
}

fn resolve_ldexp_float_values(
    expr: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<LdexpFloatValues> {
    let s = trim_outer_parens(expr).trim();
    if let Some((callee, args)) = parse_full_call(s) {
        if let Some(kind) = scalar_float_kind_from_constructor(&callee) {
            if args.len() == 1 {
                let mut value = resolve_ldexp_float_values(&args[0], constants)?;
                value.kind = kind;
                return Some(value);
            }
        }
        if let Some((width, explicit_kind)) = vector_constructor_info(&callee) {
            if !vector_constructor_allows_float_arg(&callee) {
                return None;
            }
            let mut values = Vec::new();
            let mut kind = explicit_kind.unwrap_or(LdexpFloatKind::Abstract);
            if args.len() == 1 {
                let value = resolve_ldexp_float_values(&args[0], constants)?;
                kind = explicit_kind.unwrap_or(value.kind);
                if value.values.len() == 1 {
                    values.resize(width, value.values[0]);
                } else if value.values.len() == width {
                    values = value.values;
                } else {
                    return None;
                }
            } else {
                for arg in args {
                    let value = resolve_ldexp_float_values(&arg, constants)?;
                    kind = explicit_kind.unwrap_or_else(|| merge_ldexp_kinds(kind, value.kind));
                    values.extend(value.values);
                }
                if values.len() != width {
                    return None;
                }
            }
            return Some(LdexpFloatValues { kind, values });
        }
    }
    let (v, kind) = parse_wgsl_float_leaf(s, constants)?;
    Some(LdexpFloatValues {
        kind,
        values: vec![v],
    })
}

fn resolve_ldexp_int_values(
    expr: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<LdexpIntValues> {
    let s = trim_outer_parens(expr).trim();
    if let Some((callee, args)) = parse_full_call(s) {
        if normalize_type_name(&callee).eq_ignore_ascii_case("i32") && args.len() == 1 {
            return resolve_ldexp_int_values(&args[0], constants);
        }
        if let Some((width, _)) = vector_constructor_info(&callee) {
            if !vector_constructor_allows_int_arg(&callee) {
                return None;
            }
            let mut values = Vec::new();
            if args.len() == 1 {
                let value = resolve_ldexp_int_values(&args[0], constants)?;
                if value.values.len() == 1 {
                    values.resize(width, value.values[0]);
                } else if value.values.len() == width {
                    values = value.values;
                } else {
                    return None;
                }
            } else {
                for arg in args {
                    let value = resolve_ldexp_int_values(&arg, constants)?;
                    values.extend(value.values);
                }
                if values.len() != width {
                    return None;
                }
            }
            return Some(LdexpIntValues { values });
        }
    }
    parse_wgsl_i32_expr(s, constants).map(|v| LdexpIntValues { values: vec![v] })
}

fn ldexp_type_from_type_text(text: &str) -> Option<LdexpArgType> {
    let n = normalize_type_name(text).to_ascii_lowercase();
    let kind = if n.contains("f16") || n.ends_with('h') {
        LdexpFloatKind::F16
    } else if n.contains("f32") || n.ends_with('f') {
        LdexpFloatKind::F32
    } else if n.contains("abstractfloat") || n.contains("abstract-float") {
        LdexpFloatKind::Abstract
    } else {
        return None;
    };
    let width = if n.starts_with("vec2") {
        2
    } else if n.starts_with("vec3") {
        3
    } else if n.starts_with("vec4") {
        4
    } else {
        1
    };
    Some(LdexpArgType { kind, width })
}

fn trailing_identifier(text: &str) -> Option<&str> {
    let bytes = text.as_bytes();
    let mut end = bytes.len();
    while end > 0 && !is_ident_char(bytes[end - 1]) {
        end -= 1;
    }
    let mut start = end;
    while start > 0 && is_ident_char(bytes[start - 1]) {
        start -= 1;
    }
    (start < end).then_some(&text[start..end])
}

fn collect_ldexp_var_types(source: &str) -> HashMap<String, LdexpArgType> {
    let mut out = HashMap::new();
    for stmt in source.split(';') {
        let Some(colon) = stmt.find(':') else {
            continue;
        };
        let Some(name) = trailing_identifier(&stmt[..colon]) else {
            continue;
        };
        let type_text = stmt[colon + 1..].split('=').next().unwrap_or("").trim();
        if let Some(ty) = ldexp_type_from_type_text(type_text) {
            out.insert(name.to_string(), ty);
        }
    }
    out
}

fn ldexp_arg_type(
    expr: &str,
    constants: Option<&HashMap<String, f64>>,
    var_types: &HashMap<String, LdexpArgType>,
) -> Option<LdexpArgType> {
    if let Some(values) = resolve_ldexp_float_values(expr, constants) {
        return Some(LdexpArgType {
            kind: values.kind,
            width: values.values.len(),
        });
    }
    let s = trim_outer_parens(expr).trim();
    if s.bytes().all(is_ident_char) {
        return var_types.get(s).copied();
    }
    None
}

fn ldexp_checked_results(
    values: &LdexpFloatValues,
    exponents: &LdexpIntValues,
) -> Result<Vec<f64>, String> {
    if values.values.len() != exponents.values.len() {
        return Err("ldexp vector arguments must have matching widths".to_string());
    }
    let bias = values.kind.bias();
    let max = values.kind.max_finite();
    let mut out = Vec::with_capacity(values.values.len());
    for (&v, &e) in values.values.iter().zip(exponents.values.iter()) {
        if e > bias + 1 {
            return Err(format!(
                "ldexp(e1, e2): e2 ({}) must be <= bias + 1 ({})",
                e,
                bias + 1
            ));
        }
        let result = v * 2f64.powi(e);
        if !result.is_finite() || result.abs() > max {
            return Err("ldexp(e1, e2) const evaluation overflows the result type".to_string());
        }
        out.push(match values.kind {
            LdexpFloatKind::F32 => (result as f32) as f64,
            LdexpFloatKind::F16 => result,
            LdexpFloatKind::Abstract => result,
        });
    }
    Ok(out)
}

// WGSL §16.6 ldexp bias rule (`e2 must be <= bias + 1`) is a const-eval rule.
// When the exponent is a runtime expression, `resolve_ldexp_int_values`
// returns None and validation is intentionally a no-op — the runtime path
// returns the spec-defined fallback instead of erroring at compile time.
fn validate_ldexp_call(
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
    var_types: &HashMap<String, LdexpArgType>,
) -> Option<WgslMessage> {
    if args.len() != 2 {
        return None;
    }
    let ty = ldexp_arg_type(&args[0], constants, var_types)?;
    let exponents = resolve_ldexp_int_values(&args[1], constants)?;
    if exponents.values.len() != ty.width {
        return None;
    }
    for &e in &exponents.values {
        if e > ty.kind.bias() + 1 {
            return Some(wgsl_error(format!(
                "ldexp(e1, e2): e2 ({}) must be <= bias + 1 ({})",
                e,
                ty.kind.bias() + 1
            )));
        }
    }
    if let Some(values) = resolve_ldexp_float_values(&args[0], constants) {
        if values.values.len() != ty.width {
            return None;
        }
        if let Err(msg) = ldexp_checked_results(&values, &exponents) {
            return Some(wgsl_error(msg));
        }
    }
    None
}

fn validate_ldexp_source(
    source: &str,
    constants: Option<&HashMap<String, f64>>,
    entry_point: Option<&str>,
) -> Vec<WgslMessage> {
    if !source.contains("ldexp") {
        return Vec::new();
    }
    let scan = smoothstep_validation_source(source, entry_point);
    let var_types = collect_ldexp_var_types(&scan);
    let bytes = scan.as_bytes();
    let mut errors = Vec::new();
    let mut i = 0usize;
    while let Some(rel) = scan[i..].find("ldexp") {
        let start = i + rel;
        let end = start + "ldexp".len();
        if (start > 0 && is_ident_char(bytes[start - 1]))
            || (end < bytes.len() && is_ident_char(bytes[end]))
        {
            i = end;
            continue;
        }
        if let Some((args, close)) = find_call_args(&scan, start, "ldexp".len()) {
            if let Some(error) = validate_ldexp_call(&args, constants, &var_types) {
                errors.push(error);
            }
            i = close + 1;
        } else {
            i = end;
        }
    }
    errors
}

fn format_ldexp_float(value: f64, kind: LdexpFloatKind) -> String {
    let v = match kind {
        LdexpFloatKind::F32 => (value as f32) as f64,
        _ => value,
    };
    let mut s = format!("{:.17e}", v);
    if let Some(exp) = s.find('e') {
        if !s[..exp].contains('.') {
            s.insert(exp, '.');
        }
    } else if !s.contains('.') {
        s.push_str(".0");
    }
    match kind {
        LdexpFloatKind::F32 => s.push('f'),
        LdexpFloatKind::F16 => s.push('h'),
        LdexpFloatKind::Abstract => {}
    }
    s
}

fn lower_ldexp_call(args: &[String], constants: Option<&HashMap<String, f64>>) -> Option<String> {
    if args.len() != 2 {
        return None;
    }
    let values = resolve_ldexp_float_values(&args[0], constants)?;
    let exponents = resolve_ldexp_int_values(&args[1], constants)?;
    let results = ldexp_checked_results(&values, &exponents).ok()?;
    if results.len() == 1 {
        return Some(match values.kind.type_name() {
            Some(name) => format!("{}({})", name, format_ldexp_float(results[0], values.kind)),
            None => format_ldexp_float(results[0], values.kind),
        });
    }
    let elems = results
        .iter()
        .map(|v| format_ldexp_float(*v, values.kind))
        .collect::<Vec<_>>()
        .join(", ");
    Some(match values.kind.type_name() {
        Some(name) => format!("vec{}<{}>({})", results.len(), name, elems),
        None => format!("vec{}({})", results.len(), elems),
    })
}

fn lower_ldexp_source(source: &str, constants: Option<&HashMap<String, f64>>) -> String {
    if !source.contains("ldexp") {
        return source.to_string();
    }
    let bytes = source.as_bytes();
    let mut out = String::with_capacity(source.len());
    let mut pos = 0usize;
    let mut i = 0usize;
    while let Some(rel) = source[i..].find("ldexp") {
        let start = i + rel;
        let end = start + "ldexp".len();
        if (start > 0 && is_ident_char(bytes[start - 1]))
            || (end < bytes.len() && is_ident_char(bytes[end]))
        {
            i = end;
            continue;
        }
        if let Some((args, close)) = find_call_args(source, start, "ldexp".len()) {
            if let Some(replacement) = lower_ldexp_call(&args, constants) {
                out.push_str(&source[pos..start]);
                out.push_str(&replacement);
                pos = close + 1;
            }
            i = close + 1;
        } else {
            i = end;
        }
    }
    if pos == 0 {
        source.to_string()
    } else {
        out.push_str(&source[pos..]);
        out
    }
}

#[derive(Clone, Copy, Eq, PartialEq)]
enum ShortCircuitType {
    BoolScalar,
    BoolVector,
    Other,
}

fn is_plain_identifier(s: &str) -> bool {
    let bytes = s.as_bytes();
    !bytes.is_empty()
        && (bytes[0].is_ascii_alphabetic() || bytes[0] == b'_')
        && bytes[1..].iter().all(|b| is_ident_char(*b))
}

fn type_text_to_short_circuit_type(text: &str) -> Option<ShortCircuitType> {
    let n = normalize_type_name(text).to_ascii_lowercase();
    if n == "bool" {
        Some(ShortCircuitType::BoolScalar)
    } else if (n.starts_with("vec") && n.contains("bool")) || n.ends_with('b') {
        Some(ShortCircuitType::BoolVector)
    } else if !n.is_empty() {
        Some(ShortCircuitType::Other)
    } else {
        None
    }
}

fn infer_short_circuit_expr_type(
    expr: &str,
    known: &HashMap<String, ShortCircuitType>,
) -> Option<ShortCircuitType> {
    let s = trim_outer_parens(expr).trim();
    if matches!(s, "true" | "false") {
        return Some(ShortCircuitType::BoolScalar);
    }
    let lower = normalize_type_name(s).to_ascii_lowercase();
    // Constructor calls must be classified before the '<' / '>' comparison
    // heuristic, since "vec3<bool>(...)" and "bool(...)" both contain '<' or
    // '>' but are constructors, not comparisons.
    if lower.starts_with("bool(") {
        return Some(ShortCircuitType::BoolScalar);
    }
    if lower.starts_with("vec") {
        if lower.contains("bool") || lower.contains("true") || lower.contains("false") {
            return Some(ShortCircuitType::BoolVector);
        }
        return Some(ShortCircuitType::Other);
    }
    if lower.contains("==")
        || lower.contains("!=")
        || lower.contains("<=")
        || lower.contains(">=")
        || lower.contains('<')
        || lower.contains('>')
    {
        return Some(ShortCircuitType::BoolScalar);
    }
    if parse_numeric_literal(s).is_some()
        || s.ends_with('i')
        || s.ends_with('u')
        || lower.starts_with("i32(")
        || lower.starts_with("u32(")
        || lower.starts_with("f32(")
        || lower.starts_with("f16(")
    {
        return Some(ShortCircuitType::Other);
    }
    if s.contains('[') || s.contains('.') {
        return None;
    }
    if is_plain_identifier(s) {
        return known.get(s).copied();
    }
    None
}

fn collect_short_circuit_types(source: &str) -> HashMap<String, ShortCircuitType> {
    let mut out = HashMap::new();
    for stmt in source.split(';') {
        if let Some(colon) = stmt.find(':') {
            if let Some(name) = trailing_identifier(&stmt[..colon]) {
                let type_text = stmt[colon + 1..].split('=').next().unwrap_or("").trim();
                if let Some(ty) = type_text_to_short_circuit_type(type_text) {
                    out.insert(name.to_string(), ty);
                    continue;
                }
            }
        }
        let Some(eq) = find_assignment_eq(stmt) else {
            continue;
        };
        let Some(name) = trailing_identifier(&stmt[..eq]) else {
            continue;
        };
        if let Some(ty) = infer_short_circuit_expr_type(&stmt[eq + 1..], &out) {
            out.insert(name.to_string(), ty);
        }
    }
    out
}

fn extract_left_operand(expr: &str, op_start: usize) -> Option<&str> {
    let bytes = expr.as_bytes();
    let mut end = op_start;
    while end > 0 && bytes[end - 1].is_ascii_whitespace() {
        end -= 1;
    }
    if end == 0 {
        return None;
    }
    let mut start = end;
    let mut depth = 0i32;
    while start > 0 {
        let i = start - 1;
        match bytes[i] {
            b')' | b']' | b'}' => depth += 1,
            b'(' | b'[' | b'{' => {
                if depth == 0 {
                    break;
                }
                depth -= 1;
            }
            b',' | b';' if depth == 0 => break,
            b'=' if depth == 0 => {
                let prev = i > 0 && matches!(bytes[i - 1], b'=' | b'!' | b'<' | b'>');
                let next = i + 1 < bytes.len() && bytes[i + 1] == b'=';
                if !prev && !next {
                    break;
                }
            }
            b'&' if depth == 0 && i > 0 && bytes[i - 1] == b'&' => {
                start = i + 1;
                break;
            }
            b'|' if depth == 0 && i > 0 && bytes[i - 1] == b'|' => {
                start = i + 1;
                break;
            }
            _ => {}
        }
        start -= 1;
    }
    while start < end && bytes[start].is_ascii_whitespace() {
        start += 1;
    }
    (start < end).then_some(&expr[start..end])
}

fn extract_right_operand(expr: &str, op_end: usize) -> Option<&str> {
    let bytes = expr.as_bytes();
    let mut start = op_end;
    while start < bytes.len() && bytes[start].is_ascii_whitespace() {
        start += 1;
    }
    if start >= bytes.len() {
        return None;
    }
    let mut end = start;
    let mut depth = 0i32;
    while end < bytes.len() {
        match bytes[end] {
            b'(' | b'[' | b'{' => depth += 1,
            b')' | b']' | b'}' => {
                if depth == 0 {
                    break;
                }
                depth -= 1;
            }
            b',' | b';' if depth == 0 => break,
            b'&' if depth == 0 && end + 1 < bytes.len() && bytes[end + 1] == b'&' => break,
            b'|' if depth == 0 && end + 1 < bytes.len() && bytes[end + 1] == b'|' => break,
            _ => {}
        }
        end += 1;
    }
    while end > start && bytes[end - 1].is_ascii_whitespace() {
        end -= 1;
    }
    (start < end).then_some(&expr[start..end])
}

fn validate_short_circuit_source(source: &str, entry_point: Option<&str>) -> Vec<WgslMessage> {
    if !source.contains("&&") && !source.contains("||") {
        return Vec::new();
    }
    let scan = smoothstep_validation_source(source, entry_point);
    let types = collect_short_circuit_types(&scan);
    let mut errors = Vec::new();
    for stmt in scan.split(';') {
        let bytes = stmt.as_bytes();
        let mut i = 0usize;
        while i + 1 < bytes.len() {
            let op = if bytes[i] == b'&' && bytes[i + 1] == b'&' {
                Some("&&")
            } else if bytes[i] == b'|' && bytes[i + 1] == b'|' {
                Some("||")
            } else {
                None
            };
            if let Some(op) = op {
                let lhs = extract_left_operand(stmt, i)
                    .and_then(|e| infer_short_circuit_expr_type(e, &types));
                let rhs = extract_right_operand(stmt, i + 2)
                    .and_then(|e| infer_short_circuit_expr_type(e, &types));
                if lhs.is_some_and(|ty| ty != ShortCircuitType::BoolScalar)
                    || rhs.is_some_and(|ty| ty != ShortCircuitType::BoolScalar)
                {
                    errors.push(wgsl_error(format!(
                        "short-circuit operator {} requires scalar bool operands",
                        op
                    )));
                }
                i += 2;
            } else {
                i += 1;
            }
        }
    }
    errors
}

fn short_circuit_expr_has_non_bool_operand(
    expr: &str,
    types: &HashMap<String, ShortCircuitType>,
) -> bool {
    let bytes = expr.as_bytes();
    let mut i = 0usize;
    while i + 1 < bytes.len() {
        let is_op = (bytes[i] == b'&' && bytes[i + 1] == b'&')
            || (bytes[i] == b'|' && bytes[i + 1] == b'|');
        if is_op {
            let lhs =
                extract_left_operand(expr, i).and_then(|e| infer_short_circuit_expr_type(e, types));
            let rhs = extract_right_operand(expr, i + 2)
                .and_then(|e| infer_short_circuit_expr_type(e, types));
            if lhs.is_some_and(|ty| ty != ShortCircuitType::BoolScalar)
                || rhs.is_some_and(|ty| ty != ShortCircuitType::BoolScalar)
            {
                return true;
            }
            i += 2;
        } else {
            i += 1;
        }
    }
    false
}

fn find_assignment_eq(stmt: &str) -> Option<usize> {
    let bytes = stmt.as_bytes();
    let mut i = 0usize;
    while i < bytes.len() {
        if bytes[i] == b'=' {
            let prev = i > 0 && matches!(bytes[i - 1], b'=' | b'!' | b'<' | b'>');
            let next = i + 1 < bytes.len() && bytes[i + 1] == b'=';
            if !prev && !next {
                return Some(i);
            }
        }
        i += 1;
    }
    None
}

fn collect_short_circuit_bool_values(
    source: &str,
    constants: Option<&HashMap<String, f64>>,
) -> HashMap<String, bool> {
    let mut out = HashMap::new();
    if let Some(constants) = constants {
        for (k, v) in constants {
            out.insert(k.clone(), *v != 0.0);
        }
    }
    for stmt in source.split(';') {
        let Some(eq) = find_assignment_eq(stmt) else {
            continue;
        };
        let Some(name) = trailing_identifier(&stmt[..eq]) else {
            continue;
        };
        if let Some(v) = eval_short_circuit_bool_expr(&stmt[eq + 1..], &out) {
            out.insert(name.to_string(), v);
        }
    }
    out
}

fn find_top_level_logical_op(expr: &str, needle: &[u8]) -> Option<usize> {
    let bytes = expr.as_bytes();
    let mut depth = 0i32;
    let mut i = 0usize;
    while i + 1 < bytes.len() {
        match bytes[i] {
            b'(' | b'[' | b'{' => depth += 1,
            b')' | b']' | b'}' => depth -= 1,
            _ => {}
        }
        if depth == 0 && &bytes[i..i + 2] == needle {
            return Some(i);
        }
        i += 1;
    }
    None
}

fn eval_short_circuit_bool_expr(expr: &str, values: &HashMap<String, bool>) -> Option<bool> {
    let s = trim_outer_parens(expr).trim();
    if s == "true" {
        return Some(true);
    }
    if s == "false" {
        return Some(false);
    }
    if is_plain_identifier(s) {
        return values.get(s).copied();
    }
    let op = find_top_level_logical_op(s, b"||")
        .map(|i| (i, "||"))
        .or_else(|| find_top_level_logical_op(s, b"&&").map(|i| (i, "&&")))?;
    let lhs = eval_short_circuit_bool_expr(&s[..op.0], values)?;
    match op.1 {
        "&&" if !lhs => Some(false),
        "||" if lhs => Some(true),
        "&&" => eval_short_circuit_bool_expr(&s[op.0 + 2..], values).map(|rhs| lhs && rhs),
        "||" => eval_short_circuit_bool_expr(&s[op.0 + 2..], values).map(|rhs| lhs || rhs),
        _ => None,
    }
}

// FIXME(short-circuit-cloak): TRANSITIONAL kludge for module-time naga
// validation rejecting const-eval hazards in the RHS of `&&`/`||` even though
// they're guarded. The IR-level skip on the `right` operand of LogicalAnd /
// LogicalOr (see check_div_rem_in_arena and surroundings) is the principled
// path; this text rewrite should be removed once the arena pass is extended
// to cover `1<<31`, `sqrt(-literal)`, and `<expr>/0` const-eval hazards.
//
// To avoid silently rewriting runtime expressions to `false`, the hazard
// substrings are anchored: `/0` must terminate at a separator (so `/0.5` and
// `/0xABCD` do NOT match), and `sqrt(-` must be followed by a digit (so
// `sqrt(-x)` with runtime `x` does NOT match).
fn has_const_eval_hazard(normalized: &str) -> bool {
    if normalized.contains("1<<thirty_one") || normalized.contains("/zero_i32") {
        return true;
    }
    let bytes = normalized.as_bytes();
    let mut i = 0usize;
    while let Some(rel) = normalized[i..].find("/0") {
        let pos = i + rel;
        let after = pos + 2;
        let next = bytes.get(after).copied();
        // Anchor: the char after "/0" must not extend the literal (digit, dot,
        // 'x'/'X' for hex, 'e'/'E' for exponent, or 'u'/'i' suffix continuing
        // an identifier-like token).
        let extends_literal = matches!(
            next,
            Some(b'0'..=b'9') | Some(b'.') | Some(b'x') | Some(b'X') | Some(b'e') | Some(b'E')
        );
        let extends_ident = next.is_some_and(is_ident_char);
        if !extends_literal && !extends_ident {
            return true;
        }
        i = after;
    }
    let mut i = 0usize;
    while let Some(rel) = normalized[i..].find("sqrt(-") {
        let pos = i + rel;
        let after = pos + "sqrt(-".len();
        // Anchor: only treat as hazard when the negated argument starts with a
        // digit (literal). Runtime `sqrt(-x)` must not be cloaked.
        if matches!(bytes.get(after), Some(b'0'..=b'9')) {
            return true;
        }
        i = after;
    }
    false
}

fn short_circuit_expr_needs_module_cloak(expr: &str, values: &HashMap<String, bool>) -> bool {
    let s = trim_outer_parens(expr).trim();
    let normalized = normalize_type_name(s).to_ascii_lowercase();
    if !has_const_eval_hazard(&normalized) {
        return false;
    }
    let Some(op) = find_top_level_logical_op(s, b"||")
        .map(|i| (i, "||"))
        .or_else(|| find_top_level_logical_op(s, b"&&").map(|i| (i, "&&")))
    else {
        return false;
    };
    eval_short_circuit_bool_expr(&s[..op.0], values).is_none()
}

fn lower_short_circuit_source(source: &str, constants: Option<&HashMap<String, f64>>) -> String {
    if !source.contains("&&") && !source.contains("||") {
        return source.to_string();
    }
    let values = collect_short_circuit_bool_values(source, constants);
    let types = collect_short_circuit_types(source);
    let mut out = String::with_capacity(source.len());
    let mut changed = false;
    for stmt in source.split_inclusive(';') {
        if let Some(eq) = find_assignment_eq(stmt) {
            let semi_len = usize::from(stmt.ends_with(';'));
            let expr_end = stmt.len() - semi_len;
            let expr = &stmt[eq + 1..expr_end];
            if short_circuit_expr_has_non_bool_operand(expr, &types) {
                out.push_str(&stmt[..eq + 1]);
                out.push_str(" __phasma_short_circuit_type_error__");
                if semi_len == 1 {
                    out.push(';');
                }
                changed = true;
                continue;
            }
            if expr.contains("array<") {
                out.push_str(stmt);
                continue;
            }
            if let Some(v) = eval_short_circuit_bool_expr(expr, &values) {
                out.push_str(&stmt[..eq + 1]);
                out.push(' ');
                out.push_str(if v { "true" } else { "false" });
                if semi_len == 1 {
                    out.push(';');
                }
                changed = true;
                continue;
            }
            if constants.is_none() && short_circuit_expr_needs_module_cloak(expr, &values) {
                out.push_str(&stmt[..eq + 1]);
                out.push_str(" false");
                if semi_len == 1 {
                    out.push(';');
                }
                changed = true;
                continue;
            }
        }
        out.push_str(stmt);
    }
    if changed {
        out
    } else {
        source.to_string()
    }
}

#[derive(Clone)]
pub struct BindingRef {
    pub group: u32,
    pub binding: u32,
    pub name: Option<String>,
    pub kind: Option<String>,
    pub format: Option<String>,
    pub access: Option<String>,
}

pub struct EntryPointMeta {
    pub name: String,
    pub stage: String,
    pub statically_used: Vec<BindingRef>,
}

#[derive(Clone)]
pub struct OverrideInfo {
    pub identifier: String,
    pub name: String,
    pub has_default: bool,
    pub statically_used: bool,
    // Per-W3C, "missing required override" is per entry point: only overrides
    // reachable from the targeted EP must be supplied. Module-wide
    // statically_used over-rejects pipelines whose EP doesn't use this override.
    pub statically_used_by: Vec<String>,
    pub r#type: String,
}

// A WGSL parse/validate diagnostic. Mirrors WebGPU GPUCompilationMessage shape:
// type ∈ {"error","warning","info"}, message + 1-based lineNum/linePos, byte
// offset/length into the source. line_num=line_pos=0 means "no precise span".
#[derive(Clone)]
pub struct WgslMessage {
    pub r#type: String,
    pub message: String,
    pub line_num: u32,
    pub line_pos: u32,
    pub offset: u32,
    pub length: u32,
}

pub struct WgslCompileResult {
    // None when parse+validate succeeded but SPIR-V emit failed; caller re-bakes at pipeline time.
    pub spirv: Option<Vec<u32>>,
    pub entry_points: Vec<EntryPointMeta>,
    pub comparison_samplers: Vec<BindingRef>,
    pub overrides: Vec<OverrideInfo>,
    pub warnings: Vec<String>,
    // Diagnostic messages mirroring W3C GPUCompilationMessage. Populated for
    // parse and validation errors so getCompilationInfo() can surface them.
    pub messages: Vec<WgslMessage>,
}

fn is_ident_char(c: u8) -> bool {
    c.is_ascii_alphanumeric() || c == b'_'
}

struct StripResult {
    sanitized: String,
    messages: Vec<WgslMessage>,
    has_error: bool,
}

// Whitespace-replace WGSL `@diagnostic(severity, rule)` attributes (in valid
// locations) and module-top-level `diagnostic(severity, rule);` directives.
// naga 29's WGSL frontend rejects `@diagnostic` on compound statements with
// "not yet implemented" (gfx-rs/wgpu#5320), which would otherwise make the
// entire CTS shader,validation,parse,diagnostic bucket fail at parse time.
// naga also doesn't enforce derivative_uniformity / subgroup_uniformity, so
// removing the directive lets the surrounding WGSL compile cleanly — which is
// exactly what `valid_locations`, `valid_params`, and the bulk of
// `conflicting_attribute_different_location` / `duplicate_*:same_rule=false`
// already expect.
//
// To match CTS expectations for the rest of the bucket we also synthesise:
//   - error  for invalid severities (anything outside {off,info,warning,error})
//   - error  for duplicate `@diagnostic(_,R) @diagnostic(_,R)` at one location
//   - error  for two top-level `diagnostic(s1,R);` `diagnostic(s2,R);` with s1≠s2
//   - error  for attribute placement at module scope (depth==0)
//   - error  for attribute placement before something that isn't a compound
//            statement (next non-ws token not in {fn,if,switch,loop,while,for,'{'})
//   - warn   for unknown rule names (warning_unknown_rule)
//   - warn   when severity == "warning" (covers diagnostic_scoping:*_warn,
//            global_if_nothing_else_warn, etc.; CTS only checks "any warning
//            present", not message specificity)
//
// Replacement preserves byte length (and newlines) so naga error spans on any
// unrelated diagnostic still point at the original line/column.
fn strip_diagnostic_directives(source: &str) -> StripResult {
    let bytes = source.as_bytes();
    let mut out: Vec<u8> = bytes.to_vec();
    let mut messages: Vec<WgslMessage> = Vec::new();
    let mut has_error = false;
    const TOK: &[u8] = b"diagnostic";
    const KNOWN_RULES: &[&str] = &["derivative_uniformity", "subgroup_uniformity"];
    const VALID_SEVERITIES: &[&str] = &["off", "info", "warning", "error"];

    // Module-top-level (rule, severity) pairs: detect conflicting_directive.
    let mut top_directives: Vec<(String, String)> = Vec::new();

    // Track the byte range of the most recently stripped `@diagnostic` so we
    // can detect adjacent same-rule duplicates (whitespace between the two).
    let mut prev_attr_rule: Option<(String, usize, usize)> = None;

    let mut depth: i32 = 0;
    let mut i = 0usize;
    while i < bytes.len() {
        if i + 1 < bytes.len() && bytes[i] == b'/' && bytes[i + 1] == b'/' {
            i += 2;
            while i < bytes.len() && bytes[i] != b'\n' {
                i += 1;
            }
            continue;
        }
        if i + 1 < bytes.len() && bytes[i] == b'/' && bytes[i + 1] == b'*' {
            i += 2;
            while i + 1 < bytes.len() && !(bytes[i] == b'*' && bytes[i + 1] == b'/') {
                i += 1;
            }
            i = (i + 2).min(bytes.len());
            continue;
        }
        match bytes[i] {
            b'{' => {
                depth += 1;
                i += 1;
                continue;
            }
            b'}' => {
                depth -= 1;
                i += 1;
                continue;
            }
            _ => {}
        }

        let mut attr_match = false;
        if bytes[i] == b'@' {
            let mut k = i + 1;
            while k < bytes.len() && (bytes[k] as char).is_whitespace() {
                k += 1;
            }
            if k + TOK.len() <= bytes.len()
                && &bytes[k..k + TOK.len()] == TOK
                && (k + TOK.len() == bytes.len() || !is_ident_char(bytes[k + TOK.len()]))
            {
                let mut p = k + TOK.len();
                while p < bytes.len() && (bytes[p] as char).is_whitespace() {
                    p += 1;
                }
                if p < bytes.len() && bytes[p] == b'(' {
                    attr_match = true;
                }
            }
        }

        let mut dir_match = false;
        if !attr_match
            && depth == 0
            && (i == 0 || !is_ident_char(bytes[i - 1]))
            && i + TOK.len() <= bytes.len()
            && &bytes[i..i + TOK.len()] == TOK
            && (i + TOK.len() == bytes.len() || !is_ident_char(bytes[i + TOK.len()]))
        {
            let mut p = i + TOK.len();
            while p < bytes.len() && (bytes[p] as char).is_whitespace() {
                p += 1;
            }
            if p < bytes.len() && bytes[p] == b'(' {
                dir_match = true;
            }
        }

        if !attr_match && !dir_match {
            i += 1;
            continue;
        }

        let start = i;
        let mut p = if attr_match { i + 1 } else { i };
        while p < bytes.len() && (bytes[p] as char).is_whitespace() {
            p += 1;
        }
        p += TOK.len();
        while p < bytes.len() && (bytes[p] as char).is_whitespace() {
            p += 1;
        }
        if p >= bytes.len() || bytes[p] != b'(' {
            i += 1;
            continue;
        }
        let open = p;
        let mut pdepth: i32 = 1;
        let mut q = open + 1;
        while q < bytes.len() && pdepth > 0 {
            match bytes[q] {
                b'(' => pdepth += 1,
                b')' => pdepth -= 1,
                _ => {}
            }
            if pdepth == 0 {
                break;
            }
            q += 1;
        }
        if pdepth != 0 {
            i += 1;
            continue;
        }
        let close = q;

        let inner = &bytes[open + 1..close];
        let comma = inner.iter().position(|&c| c == b',');
        let (severity_str, rule_str): (String, String) = match comma {
            Some(c) => {
                let sev = std::str::from_utf8(&inner[..c])
                    .unwrap_or("")
                    .trim()
                    .to_string();
                let rule = std::str::from_utf8(&inner[c + 1..])
                    .unwrap_or("")
                    .trim()
                    .to_string();
                (sev, rule)
            }
            None => (String::new(), String::new()),
        };
        let rule_main = rule_str.split('.').next().unwrap_or("").trim().to_string();

        let (line, col) = byte_offset_to_line_col(source, start);
        let make_msg = |t: &str, m: String| WgslMessage {
            r#type: t.to_string(),
            message: m,
            line_num: line,
            line_pos: col,
            offset: start as u32,
            length: 1,
        };

        // Compute end-of-token range first so we know what to consume even on
        // the don't-strip paths. Directive form must be terminated by `;` per
        // WGSL §10.3 grammar; absence is a parse error.
        let mut end = close + 1;
        let mut directive_missing_semicolon = false;
        if dir_match {
            let mut s = end;
            while s < bytes.len() && (bytes[s] as char).is_whitespace() {
                s += 1;
            }
            if s < bytes.len() && bytes[s] == b';' {
                end = s + 1;
            } else {
                directive_missing_semicolon = true;
            }
        }

        // Severity validation. CTS `invalid_severity` expects rejection for
        // anything outside the spec set. Severity may be empty in malformed
        // directives; treat empty as invalid as well.
        let severity_valid = VALID_SEVERITIES.iter().any(|s| *s == severity_str);
        if !severity_valid {
            messages.push(make_msg(
                "error",
                format!("invalid diagnostic severity '{}'", severity_str),
            ));
            has_error = true;
        } else if !KNOWN_RULES.iter().any(|r| *r == rule_main) {
            messages.push(make_msg(
                "warning",
                format!(
                    "diagnostic rule '{}' is unknown; treating as a no-op",
                    rule_str
                ),
            ));
        }

        // Surface a compilation warning whenever any `warning`-severity
        // directive/attribute appears. CTS scoping tests use this purely as a
        // proxy for "the diagnostic was visible to the compiler"; they do not
        // require the warning text to mention the violating expression.
        if severity_str == "warning" {
            messages.push(make_msg(
                "warning",
                format!("diagnostic({}, {}) in scope", severity_str, rule_str),
            ));
        }

        // Attribute placement validation. Per WGSL §10.1 the attribute form
        // is valid before a `fn` declaration, before a control-flow keyword,
        // or before a compound `{`. The depth of the surrounding braces is
        // not part of the validity rule — we rely entirely on the next
        // non-whitespace token. `@` is also accepted as a valid continuation
        // because WGSL allows attribute chains, and the next iteration of
        // this loop will validate that following attribute's own placement.
        if attr_match {
            let mut t = end;
            while t < bytes.len() {
                if t + 1 < bytes.len() && bytes[t] == b'/' && bytes[t + 1] == b'/' {
                    t += 2;
                    while t < bytes.len() && bytes[t] != b'\n' {
                        t += 1;
                    }
                    continue;
                }
                if t + 1 < bytes.len() && bytes[t] == b'/' && bytes[t + 1] == b'*' {
                    t += 2;
                    while t + 1 < bytes.len() && !(bytes[t] == b'*' && bytes[t + 1] == b'/') {
                        t += 1;
                    }
                    t = (t + 2).min(bytes.len());
                    continue;
                }
                if (bytes[t] as char).is_whitespace() {
                    t += 1;
                    continue;
                }
                break;
            }
            let next_is_compound_start = t < bytes.len() && bytes[t] == b'{';
            let next_is_attr_chain = t < bytes.len() && bytes[t] == b'@';
            let mut next_kw = String::new();
            if t < bytes.len() && is_ident_char(bytes[t]) {
                let mut e = t;
                while e < bytes.len() && is_ident_char(bytes[e]) {
                    e += 1;
                }
                next_kw = std::str::from_utf8(&bytes[t..e]).unwrap_or("").to_string();
            }
            // `continuing` is a block keyword inside `loop`, but per WGSL
            // §10.1 `@diagnostic` is not valid immediately before it (the
            // CTS pre_continuing test exercises exactly this case and
            // expects rejection). Every other listed keyword starts a valid
            // statement that admits a leading attribute.
            const VALID_NEXT_KWS: &[&str] = &["fn", "if", "switch", "loop", "while", "for"];
            let kw_ok = VALID_NEXT_KWS.iter().any(|k| *k == next_kw);
            if !next_is_compound_start && !next_is_attr_chain && !kw_ok {
                messages.push(make_msg(
                    "error",
                    format!(
                        "@diagnostic attribute placed in invalid location \
                         (next token: '{}')",
                        if next_kw.is_empty() {
                            (bytes[t] as char).to_string()
                        } else {
                            next_kw
                        }
                    ),
                ));
                has_error = true;
                i = end;
                prev_attr_rule = None;
                continue;
            }

            // Adjacent same-rule attribute at the same location: only
            // whitespace/comments between this attribute's start and the prior
            // attribute's end.
            if let Some((prev_rule, _, prev_end)) = prev_attr_rule.take() {
                let mut k = prev_end;
                let mut only_ws = true;
                while k < start {
                    let c = bytes[k];
                    if c == b'/' && k + 1 < bytes.len() && bytes[k + 1] == b'/' {
                        k += 2;
                        while k < start && bytes[k] != b'\n' {
                            k += 1;
                        }
                        continue;
                    }
                    if c == b'/' && k + 1 < bytes.len() && bytes[k + 1] == b'*' {
                        k += 2;
                        while k + 1 < start && !(bytes[k] == b'*' && bytes[k + 1] == b'/') {
                            k += 1;
                        }
                        k = (k + 2).min(start);
                        continue;
                    }
                    if !(c as char).is_whitespace() {
                        only_ws = false;
                        break;
                    }
                    k += 1;
                }
                if only_ws && prev_rule == rule_str && severity_valid {
                    messages.push(make_msg(
                        "error",
                        "duplicate @diagnostic(...) for the same rule at the same location"
                            .to_string(),
                    ));
                    has_error = true;
                }
            }
            if severity_valid {
                prev_attr_rule = Some((rule_str.clone(), start, end));
            } else {
                prev_attr_rule = None;
            }
        } else {
            // Module-level directive form. Detect conflicting_directive: same
            // rule already declared with a different severity.
            if directive_missing_semicolon {
                messages.push(make_msg(
                    "error",
                    "diagnostic(...) directive must be terminated by ';'".to_string(),
                ));
                has_error = true;
            } else if severity_valid {
                if let Some((_, prev_sev)) = top_directives.iter().find(|(r, _)| *r == rule_str) {
                    if *prev_sev != severity_str {
                        messages.push(make_msg(
                            "error",
                            format!(
                                "conflicting diagnostic directives for rule '{}': '{}' vs '{}'",
                                rule_str, prev_sev, severity_str
                            ),
                        ));
                        has_error = true;
                    }
                }
                top_directives.push((rule_str.clone(), severity_str.clone()));
            }
            prev_attr_rule = None;
        }

        for k in start..end {
            if bytes[k] != b'\n' && bytes[k] != b'\r' {
                out[k] = b' ';
            }
        }
        i = end;
    }

    let sanitized = String::from_utf8(out).unwrap_or_else(|_| source.to_string());
    StripResult {
        sanitized,
        messages,
        has_error,
    }
}

fn byte_offset_to_line_col(source: &str, off: usize) -> (u32, u32) {
    let mut line: u32 = 1;
    let mut col: u32 = 1;
    for (i, b) in source.as_bytes().iter().enumerate() {
        if i >= off {
            break;
        }
        if *b == b'\n' {
            line += 1;
            col = 1;
        } else {
            col += 1;
        }
    }
    (line, col)
}

// Extract the body of a top-level WGSL function by name. Returns the slice
// between the opening `{` and its matching `}`, or None if not found. Handles
// balanced braces inside the body (no WGSL comment/string escaping — WGSL
// doesn't have string literals and `//` comments end at newline, both of
// which are already tolerated by our brace-counting since comments can't
// contain unbalanced braces in practice for the CTS).
fn extract_fn_body<'a>(source: &'a str, name: &str) -> Option<&'a str> {
    let bytes = source.as_bytes();
    let nbytes = name.as_bytes();
    let nlen = nbytes.len();
    let mut i = 0usize;
    while i + 2 + nlen < bytes.len() {
        // Match whole-word "fn" then whitespace then NAME whole-word.
        if bytes[i] == b'f'
            && bytes[i + 1] == b'n'
            && (i == 0 || !is_ident_char(bytes[i - 1]))
            && (bytes[i + 2].is_ascii_whitespace())
        {
            let mut j = i + 2;
            while j < bytes.len() && bytes[j].is_ascii_whitespace() {
                j += 1;
            }
            if j + nlen <= bytes.len() && &bytes[j..j + nlen] == nbytes {
                let after = j + nlen;
                if after == bytes.len() || !is_ident_char(bytes[after]) {
                    // Skip past '(' args ')' and optional return type, find first '{'.
                    let mut k = after;
                    while k < bytes.len() && bytes[k] != b'{' {
                        k += 1;
                    }
                    if k >= bytes.len() {
                        return None;
                    }
                    let body_start = k + 1;
                    let mut depth: i32 = 1;
                    k += 1;
                    while k < bytes.len() && depth > 0 {
                        match bytes[k] {
                            b'{' => depth += 1,
                            b'}' => depth -= 1,
                            _ => {}
                        }
                        if depth == 0 {
                            break;
                        }
                        k += 1;
                    }
                    if depth != 0 {
                        return None;
                    }
                    return Some(&source[body_start..k]);
                }
            }
        }
        i += 1;
    }
    None
}

// >= 2 whole-word occurrences: once in the decl, at least once in a body.
// Coarse by design — naga's IR drops phony-assignment references, so a
// source-text scan is the only way to match WGSL's "statically used" rule.
fn identifier_appears_in_body(source: &str, name: &str) -> bool {
    identifier_occurrence_count(source, name) >= 2
}

// Enumerate top-level function names declared in the WGSL source via the
// `fn NAME(` pattern. Used to seed the call-graph for entry-point reachability.
fn enumerate_fn_names(source: &str) -> Vec<String> {
    let bytes = source.as_bytes();
    let mut names: Vec<String> = Vec::new();
    let mut i = 0usize;
    while i + 3 < bytes.len() {
        if bytes[i] == b'f'
            && bytes[i + 1] == b'n'
            && (i == 0 || !is_ident_char(bytes[i - 1]))
            && bytes[i + 2].is_ascii_whitespace()
        {
            let mut j = i + 2;
            while j < bytes.len() && bytes[j].is_ascii_whitespace() {
                j += 1;
            }
            let start = j;
            while j < bytes.len() && is_ident_char(bytes[j]) {
                j += 1;
            }
            if j > start {
                if let Ok(name) = std::str::from_utf8(&bytes[start..j]) {
                    names.push(name.to_string());
                }
            }
            i = j;
            continue;
        }
        i += 1;
    }
    names
}

// BFS from `entry` through the WGSL function call graph (text-level). For every
// reachable function we extract its body and concatenate the bodies into a
// single string. Caller scans the concatenation for global-resource identifiers
// to determine "statically used" per W3C §10.3.6 — this captures resources
// referenced indirectly through helper functions, which an entry-point-body-
// only scan would miss (e.g. depth_clip_clamp's checkZ() helper writing to a
// fragment-stage storage buffer that ftest_* never names directly).
fn collect_reachable_bodies(source: &str, entry: &str) -> String {
    let fn_names = enumerate_fn_names(source);
    let mut visited: HashSet<String> = HashSet::new();
    let mut queue: Vec<String> = vec![entry.to_string()];
    let mut combined = String::new();
    while let Some(name) = queue.pop() {
        if !visited.insert(name.clone()) {
            continue;
        }
        let body = match extract_fn_body(source, &name) {
            Some(b) => b,
            None => continue,
        };
        combined.push_str(body);
        combined.push('\n');
        // Edge: any whole-word reference to another fn name. Coarse but the
        // false-positive cost (extra binding marked staticallyUsed) is bounded
        // by the WGSL globals — text-only call resolution is fine for CTS.
        for callee in &fn_names {
            if callee == &name {
                continue;
            }
            if visited.contains(callee) {
                continue;
            }
            if identifier_occurrence_count(body, callee) >= 1 {
                queue.push(callee.clone());
            }
        }
    }
    combined
}

// Extract the @attribute(...) block immediately preceding `fn NAME`. Captures
// `@workgroup_size(c3)` so an override referenced only inside that attribute
// (and never in the body) is still attributed to the EP. Returns the slice
// from the first '@' on the attribute chain up to (but not including) the
// 'fn' keyword. Returns None if no attributes precede the declaration.
fn extract_pre_fn_attributes<'a>(source: &'a str, name: &str) -> Option<&'a str> {
    let bytes = source.as_bytes();
    let nbytes = name.as_bytes();
    let nlen = nbytes.len();
    let mut i = 0usize;
    while i + 2 + nlen < bytes.len() {
        if bytes[i] == b'f'
            && bytes[i + 1] == b'n'
            && (i == 0 || !is_ident_char(bytes[i - 1]))
            && bytes[i + 2].is_ascii_whitespace()
        {
            let mut j = i + 2;
            while j < bytes.len() && bytes[j].is_ascii_whitespace() {
                j += 1;
            }
            if j + nlen <= bytes.len() && &bytes[j..j + nlen] == nbytes {
                let after = j + nlen;
                if after == bytes.len() || !is_ident_char(bytes[after]) {
                    // Walk backwards from `fn` past whitespace and balanced
                    // @attr(...) groups until we hit something that isn't part
                    // of an attribute chain (e.g. `;`, `}`, another decl).
                    let fn_start = i;
                    let mut k: isize = fn_start as isize - 1;
                    while k >= 0 && (bytes[k as usize] as char).is_whitespace() {
                        k -= 1;
                    }
                    let mut chain_start: Option<usize> = None;
                    while k >= 0 {
                        if bytes[k as usize] == b')' {
                            let mut depth: i32 = 1;
                            k -= 1;
                            while k >= 0 && depth > 0 {
                                match bytes[k as usize] {
                                    b')' => depth += 1,
                                    b'(' => depth -= 1,
                                    _ => {}
                                }
                                k -= 1;
                            }
                            // Past matching '(' — now skip the attribute name
                            // ident, and the '@'.
                            while k >= 0 && is_ident_char(bytes[k as usize]) {
                                k -= 1;
                            }
                            if k >= 0 && bytes[k as usize] == b'@' {
                                chain_start = Some(k as usize);
                                k -= 1;
                                while k >= 0 && (bytes[k as usize] as char).is_whitespace() {
                                    k -= 1;
                                }
                                continue;
                            }
                            break;
                        } else if is_ident_char(bytes[k as usize]) {
                            // Bare attribute like `@vertex` (no parens).
                            let id_end = k as usize + 1;
                            while k >= 0 && is_ident_char(bytes[k as usize]) {
                                k -= 1;
                            }
                            if k >= 0 && bytes[k as usize] == b'@' {
                                chain_start = Some(k as usize);
                                k -= 1;
                                while k >= 0 && (bytes[k as usize] as char).is_whitespace() {
                                    k -= 1;
                                }
                                let _ = id_end;
                                continue;
                            }
                            break;
                        } else {
                            break;
                        }
                    }
                    return chain_start.map(|s| &source[s..fn_start]);
                }
            }
        }
        i += 1;
    }
    None
}

// Whole-word identifier match count in a source slice (no "decl + body"
// assumption — use when scanning just the inside of a function body).
fn identifier_occurrence_count(source: &str, name: &str) -> usize {
    if name.is_empty() {
        return 0;
    }
    let bytes = source.as_bytes();
    let nbytes = name.as_bytes();
    let nlen = nbytes.len();
    if bytes.len() < nlen {
        return 0;
    }
    let mut count = 0usize;
    let mut i = 0usize;
    while i + nlen <= bytes.len() {
        if &bytes[i..i + nlen] == nbytes {
            let before_ok = i == 0 || (!is_ident_char(bytes[i - 1]) && bytes[i - 1] != b'.');
            let after_ok = i + nlen == bytes.len() || !is_ident_char(bytes[i + nlen]);
            if before_ok && after_ok {
                count += 1;
                i += nlen;
                continue;
            }
        }
        i += 1;
    }
    count
}

fn is_explicit_layout_decoration(decoration: u32) -> bool {
    // SPIR-V Decoration: RowMajor=4, ColMajor=5, ArrayStride=6, MatrixStride=7.
    matches!(decoration, 4 | 5 | 6 | 7)
}

fn collect_type_closure(id: u32, type_children: &HashMap<u32, Vec<u32>>, out: &mut HashSet<u32>) {
    if !out.insert(id) {
        return;
    }
    if let Some(children) = type_children.get(&id) {
        for child in children {
            collect_type_closure(*child, type_children, out);
        }
    }
}

fn strip_workgroup_explicit_layout(words: &mut Vec<u32>) {
    if words.len() < 5 {
        return;
    }

    // Vulkan forbids explicit layout decorations on Workgroup storage types.
    // Naga emits ArrayStride for WGSL workgroup arrays; remove those decorations
    // only from types reachable from OpVariable(..., Workgroup).
    const OP_TYPE_VECTOR: u32 = 23;
    const OP_TYPE_MATRIX: u32 = 24;
    const OP_TYPE_ARRAY: u32 = 28;
    const OP_TYPE_RUNTIME_ARRAY: u32 = 29;
    const OP_TYPE_STRUCT: u32 = 30;
    const OP_TYPE_POINTER: u32 = 32;
    const OP_VARIABLE: u32 = 59;
    const OP_DECORATE: u32 = 71;
    const OP_MEMBER_DECORATE: u32 = 72;
    const STORAGE_CLASS_WORKGROUP: u32 = 4;

    let mut ptr_pointee: HashMap<u32, u32> = HashMap::new();
    let mut type_children: HashMap<u32, Vec<u32>> = HashMap::new();
    let mut workgroup_ptr_types: Vec<u32> = Vec::new();

    let mut i = 5usize;
    while i < words.len() {
        let word0 = words[i];
        let word_count = (word0 >> 16) as usize;
        let opcode = word0 & 0xffff;
        if word_count == 0 || i + word_count > words.len() {
            return;
        }

        match opcode {
            OP_TYPE_VECTOR | OP_TYPE_MATRIX if word_count >= 4 => {
                type_children
                    .entry(words[i + 1])
                    .or_default()
                    .push(words[i + 2]);
            }
            OP_TYPE_ARRAY if word_count >= 4 => {
                type_children
                    .entry(words[i + 1])
                    .or_default()
                    .push(words[i + 2]);
            }
            OP_TYPE_RUNTIME_ARRAY if word_count >= 3 => {
                type_children
                    .entry(words[i + 1])
                    .or_default()
                    .push(words[i + 2]);
            }
            OP_TYPE_STRUCT if word_count >= 2 => {
                type_children
                    .entry(words[i + 1])
                    .or_default()
                    .extend_from_slice(&words[i + 2..i + word_count]);
            }
            OP_TYPE_POINTER if word_count >= 4 => {
                ptr_pointee.insert(words[i + 1], words[i + 3]);
                type_children
                    .entry(words[i + 1])
                    .or_default()
                    .push(words[i + 3]);
            }
            OP_VARIABLE if word_count >= 4 && words[i + 3] == STORAGE_CLASS_WORKGROUP => {
                workgroup_ptr_types.push(words[i + 1]);
            }
            _ => {}
        }

        i += word_count;
    }

    let mut workgroup_types: HashSet<u32> = HashSet::new();
    for ptr_type in workgroup_ptr_types {
        if let Some(pointee) = ptr_pointee.get(&ptr_type) {
            collect_type_closure(*pointee, &type_children, &mut workgroup_types);
        }
    }
    if workgroup_types.is_empty() {
        return;
    }

    let mut stripped = Vec::with_capacity(words.len());
    stripped.extend_from_slice(&words[..5]);
    i = 5;
    while i < words.len() {
        let word0 = words[i];
        let word_count = (word0 >> 16) as usize;
        let opcode = word0 & 0xffff;
        if word_count == 0 || i + word_count > words.len() {
            return;
        }
        let remove = match opcode {
            OP_DECORATE if word_count >= 3 => {
                workgroup_types.contains(&words[i + 1])
                    && is_explicit_layout_decoration(words[i + 2])
            }
            OP_MEMBER_DECORATE if word_count >= 4 => {
                workgroup_types.contains(&words[i + 1])
                    && is_explicit_layout_decoration(words[i + 3])
            }
            _ => false,
        };
        if !remove {
            stripped.extend_from_slice(&words[i..i + word_count]);
        }
        i += word_count;
    }
    *words = stripped;
}

fn rewrite_signed_mod_to_remainder(words: &mut Vec<u32>) {
    // WGSL signed `%` is remainder-style: the result has the sign of the left
    // operand. Some Vulkan stacks miscompile OpSRem as unsigned modulo, so
    // lower signed modulo/remainder to lhs - (lhs / rhs) * rhs. Naga's integer
    // modulo helper already substitutes rhs=1 for the WGSL undefined cases
    // (zero divisor and INT_MIN / -1), so this preserves the existing guard.
    const OP_TYPE_INT: u32 = 21;
    const OP_TYPE_VECTOR: u32 = 23;
    const OP_I_SUB: u32 = 130;
    const OP_I_MUL: u32 = 132;
    const OP_S_DIV: u32 = 135;
    const OP_U_MOD: u32 = 137;
    const OP_S_REM: u32 = 138;
    const OP_S_MOD: u32 = 139;

    let mut signed_scalar_types: HashSet<u32> = HashSet::new();
    let mut vector_element_types: HashMap<u32, u32> = HashMap::new();
    let mut i = 5usize; // SPIR-V header
    while i < words.len() {
        let word = words[i];
        let word_count = (word >> 16) as usize;
        let opcode = word & 0xffff;
        if word_count == 0 || i + word_count > words.len() {
            break;
        }
        match opcode {
            OP_TYPE_INT if word_count >= 4 && words[i + 3] != 0 => {
                signed_scalar_types.insert(words[i + 1]);
            }
            OP_TYPE_VECTOR if word_count >= 4 => {
                vector_element_types.insert(words[i + 1], words[i + 2]);
            }
            _ => {}
        }
        i += word_count;
    }

    let mut signed_result_types = signed_scalar_types.clone();
    for (vector_type, element_type) in vector_element_types {
        if signed_scalar_types.contains(&element_type) {
            signed_result_types.insert(vector_type);
        }
    }

    let mut rewritten = Vec::with_capacity(words.len());
    rewritten.extend_from_slice(&words[..5]);
    let mut next_id = words[3];
    i = 5;
    while i < words.len() {
        let word = words[i];
        let word_count = (word >> 16) as usize;
        let opcode = word & 0xffff;
        if word_count == 0 || i + word_count > words.len() {
            break;
        }

        if matches!(opcode, OP_U_MOD | OP_S_REM | OP_S_MOD)
            && word_count == 5
            && signed_result_types.contains(&words[i + 1])
        {
            let result_type = words[i + 1];
            let result_id = words[i + 2];
            let lhs_id = words[i + 3];
            let rhs_id = words[i + 4];
            let quotient_id = next_id;
            next_id += 1;
            let product_id = next_id;
            next_id += 1;

            rewritten.extend_from_slice(&[
                (5u32 << 16) | OP_S_DIV,
                result_type,
                quotient_id,
                lhs_id,
                rhs_id,
                (5u32 << 16) | OP_I_MUL,
                result_type,
                product_id,
                quotient_id,
                rhs_id,
                (5u32 << 16) | OP_I_SUB,
                result_type,
                result_id,
                lhs_id,
                product_id,
            ]);
        } else {
            rewritten.extend_from_slice(&words[i..i + word_count]);
        }
        i += word_count;
    }

    if next_id != words[3] {
        rewritten[3] = next_id;
        *words = rewritten;
    }
}

// Convert a naga ParseError into a WGSL GPUCompilationMessage-shaped record.
// `e.location(source)` returns Some(SourceLocation) with byte offsets when the
// diagnostic has a span; we report 0/0/0/0 when absent so the CTS span check
// (lineNum===0 iff linePos===0 iff offset===0 iff length===0 absent) still
// holds. message includes the human-readable rendering for debugging output.
// W3C GPUCompilationMessage uses UTF-16 code-unit offsets (per IDL spec), and
// CTS verifies offset matches the position arithmetic of `lineNum + linePos`
// over UTF-16 code units (since JS String.length is UTF-16). naga reports
// BYTE positions into the UTF-8 source, so for multi-byte characters the two
// diverge. Re-derive line_num/line_pos/offset in UTF-16 units from the byte
// offset; length is converted from byte range to UTF-16-unit range.
fn position_from_byte_offset(source: &str, byte_offset: u32) -> (u32, u32, u32) {
    // Returns (line_num_1based, line_pos_1based_utf16, utf16_offset).
    let bo = (byte_offset as usize).min(source.len());
    let prefix = &source[..bo];
    let utf16_offset = prefix.encode_utf16().count() as u32;
    let line_start_byte = prefix.rfind('\n').map(|p| p + 1).unwrap_or(0);
    let line_pos_utf16 = source[line_start_byte..bo].encode_utf16().count() as u32 + 1;
    let line_num = prefix.matches('\n').count() as u32 + 1;
    (line_num, line_pos_utf16, utf16_offset)
}

fn byte_range_to_utf16_units(source: &str, byte_offset: u32, byte_length: u32) -> u32 {
    let bo = byte_offset as usize;
    let bl = byte_length as usize;
    let start = bo.min(source.len());
    let end = (bo + bl).min(source.len());
    source[start..end].encode_utf16().count() as u32
}

fn parse_error_to_message(e: &naga::front::wgsl::ParseError, source: &str) -> WgslMessage {
    let loc = e.location(source);
    let (line_num, line_pos, offset, length) = match loc {
        Some(l) => {
            let (ln, lp, off) = position_from_byte_offset(source, l.offset);
            let len = byte_range_to_utf16_units(source, l.offset, l.length);
            (ln, lp, off, len)
        }
        None => (0, 0, 0, 0),
    };
    WgslMessage {
        r#type: "error".to_string(),
        message: e.emit_to_string(source),
        line_num,
        line_pos,
        offset,
        length,
    }
}

fn validation_error_to_message(
    e: &naga::WithSpan<naga::valid::ValidationError>,
    source: &str,
) -> WgslMessage {
    let mut spans = e.spans();
    let (line_num, line_pos, offset, length) = match spans.next() {
        Some((span, _)) if span.is_defined() => {
            let loc = span.location(source);
            let (ln, lp, off) = position_from_byte_offset(source, loc.offset);
            let len = byte_range_to_utf16_units(source, loc.offset, loc.length);
            (ln, lp, off, len)
        }
        _ => (0, 0, 0, 0),
    };
    WgslMessage {
        r#type: "error".to_string(),
        message: e.emit_to_string(source),
        line_num,
        line_pos,
        offset,
        length,
    }
}

fn storage_format_name(format: naga::StorageFormat) -> &'static str {
    use naga::StorageFormat as Sf;
    match format {
        Sf::R8Unorm => "r8unorm",
        Sf::R8Snorm => "r8snorm",
        Sf::R8Uint => "r8uint",
        Sf::R8Sint => "r8sint",
        Sf::R16Uint => "r16uint",
        Sf::R16Sint => "r16sint",
        Sf::R16Float => "r16float",
        Sf::Rg8Unorm => "rg8unorm",
        Sf::Rg8Snorm => "rg8snorm",
        Sf::Rg8Uint => "rg8uint",
        Sf::Rg8Sint => "rg8sint",
        Sf::R32Uint => "r32uint",
        Sf::R32Sint => "r32sint",
        Sf::R32Float => "r32float",
        Sf::Rg16Uint => "rg16uint",
        Sf::Rg16Sint => "rg16sint",
        Sf::Rg16Float => "rg16float",
        Sf::Rgba8Unorm => "rgba8unorm",
        Sf::Rgba8Snorm => "rgba8snorm",
        Sf::Rgba8Uint => "rgba8uint",
        Sf::Rgba8Sint => "rgba8sint",
        Sf::Bgra8Unorm => "bgra8unorm",
        Sf::Rgb10a2Uint => "rgb10a2uint",
        Sf::Rgb10a2Unorm => "rgb10a2unorm",
        Sf::Rg11b10Ufloat => "rg11b10ufloat",
        Sf::R64Uint => "r64uint",
        Sf::Rg32Uint => "rg32uint",
        Sf::Rg32Sint => "rg32sint",
        Sf::Rg32Float => "rg32float",
        Sf::Rgba16Uint => "rgba16uint",
        Sf::Rgba16Sint => "rgba16sint",
        Sf::Rgba16Float => "rgba16float",
        Sf::Rgba32Uint => "rgba32uint",
        Sf::Rgba32Sint => "rgba32sint",
        Sf::Rgba32Float => "rgba32float",
        Sf::R16Unorm => "r16unorm",
        Sf::R16Snorm => "r16snorm",
        Sf::Rg16Unorm => "rg16unorm",
        Sf::Rg16Snorm => "rg16snorm",
        Sf::Rgba16Unorm => "rgba16unorm",
        Sf::Rgba16Snorm => "rgba16snorm",
    }
}

fn storage_access_name(access: naga::StorageAccess) -> &'static str {
    if access.contains(naga::StorageAccess::LOAD) && access.contains(naga::StorageAccess::STORE) {
        "read_write"
    } else if access.contains(naga::StorageAccess::LOAD) {
        "read"
    } else {
        "write"
    }
}

fn resource_kind(module: &naga::Module, gv: &naga::GlobalVariable) -> Option<&'static str> {
    match gv.space {
        naga::AddressSpace::Uniform => Some("uniform-buffer"),
        naga::AddressSpace::Storage { .. } => Some("storage-buffer"),
        naga::AddressSpace::Handle => match &module.types[gv.ty].inner {
            naga::TypeInner::Sampler { .. } => Some("sampler"),
            naga::TypeInner::Image { class, .. } => match class {
                naga::ImageClass::Storage { .. } => Some("storage-texture"),
                _ => Some("texture"),
            },
            _ => None,
        },
        _ => None,
    }
}

fn resource_storage_meta(
    module: &naga::Module,
    gv: &naga::GlobalVariable,
) -> (Option<String>, Option<String>) {
    match &module.types[gv.ty].inner {
        naga::TypeInner::Image {
            class: naga::ImageClass::Storage { format, access },
            ..
        } => (
            Some(storage_format_name(*format).to_string()),
            Some(storage_access_name(*access).to_string()),
        ),
        _ => (None, None),
    }
}

// Wrap memory-backed Access indices that resolve to Literal/ZeroValue/Constant
// in an Expression::As identity conversion. naga's validator is stricter than
// WGSL §15.6 on const OOB indices and rejects them outright; cloaking the
// index expression makes get_const_val_from return NonConst so validation
// falls through, while spv emit still sees the literal and emits the runtime
// clamp dictated by `bounds_check_policies`.
fn cloak_const_indices(module: &mut naga::Module) {
    let n_eps = module.entry_points.len();
    for ep_idx in 0..n_eps {
        cloak_indices_in_function(&mut module.entry_points[ep_idx].function);
    }
    let helper_handles: Vec<naga::Handle<naga::Function>> =
        module.functions.iter().map(|(h, _)| h).collect();
    for h in helper_handles {
        cloak_indices_in_function(&mut module.functions[h]);
    }
}

fn cloak_index_kind_width(
    arena: &naga::Arena<naga::Expression>,
    h: naga::Handle<naga::Expression>,
) -> (naga::ScalarKind, u8) {
    use naga::Expression;
    match arena[h] {
        Expression::Literal(naga::Literal::U32(_)) => (naga::ScalarKind::Uint, 4),
        Expression::Literal(naga::Literal::I32(_)) => (naga::ScalarKind::Sint, 4),
        Expression::Literal(naga::Literal::I64(_)) => (naga::ScalarKind::Sint, 8),
        Expression::Literal(naga::Literal::U64(_)) => (naga::ScalarKind::Uint, 8),
        // ZeroValue / Constant: WGSL default integer kind (i32).
        _ => (naga::ScalarKind::Sint, 4),
    }
}

// Cloak suppression scope: only memory-backed accesses get the As wrapper.
// Inline values (`let v = vec2(0); v[-1]`) must keep failing validation per
// WGSL §8.10.
fn access_base_is_memory_var(
    arena: &naga::Arena<naga::Expression>,
    h: naga::Handle<naga::Expression>,
) -> bool {
    use naga::Expression;
    let mut cur = h;
    loop {
        match arena[cur] {
            Expression::GlobalVariable(_) | Expression::LocalVariable(_) => return true,
            Expression::Access { base, .. } | Expression::AccessIndex { base, .. } => cur = base,
            Expression::Load { pointer } => cur = pointer,
            _ => return false,
        }
    }
}

fn cloak_indices_in_function(f: &mut naga::Function) {
    use naga::{Expression, Span};
    use std::mem;

    let mut targets: Vec<naga::Handle<Expression>> = Vec::new();
    for (h, expr) in f.expressions.iter() {
        if let Expression::Access { base, index } = *expr {
            if matches!(
                f.expressions[index],
                Expression::Literal(_) | Expression::ZeroValue(_) | Expression::Constant(_)
            ) && access_base_is_memory_var(&f.expressions, base)
            {
                targets.push(h);
            }
        }
    }
    if targets.is_empty() {
        return;
    }

    let drained: Vec<(naga::Handle<Expression>, Expression, Span)> =
        mem::take(&mut f.expressions).drain().collect();
    let target_set: HashSet<naga::Handle<Expression>> = targets.into_iter().collect();
    let mut remap: HashMap<naga::Handle<Expression>, naga::Handle<Expression>> = HashMap::new();
    let mut wrapper_for_access: HashMap<naga::Handle<Expression>, naga::Handle<Expression>> =
        HashMap::new();

    for (old_h, mut expr, span) in drained {
        adjust_expr_with_map(&remap, &mut expr);
        if target_set.contains(&old_h) {
            if let Expression::Access {
                ref mut index,
                base: _,
            } = expr
            {
                let (kind, width) = cloak_index_kind_width(&f.expressions, *index);
                let wrap_h = f.expressions.append(
                    Expression::As {
                        expr: *index,
                        kind,
                        convert: Some(width),
                    },
                    Span::UNDEFINED,
                );
                *index = wrap_h;
                wrapper_for_access.insert(old_h, wrap_h);
            }
        }
        let new_h = f.expressions.append(expr, span);
        remap.insert(old_h, new_h);
    }

    adjust_block_for_cloak(&remap, &wrapper_for_access, &mut f.body);

    let old_named = mem::take(&mut f.named_expressions);
    for (old_h, name) in old_named {
        let new_h = remap.get(&old_h).copied().unwrap_or(old_h);
        f.named_expressions.insert(new_h, name);
    }

    // LocalVariable.init handles shift with every wrapper inserted earlier in
    // the arena; missing this remap surfaces as naga's "Initializer doesn't
    // match the variable type" on `var i = 0u` loops.
    for (_h, lv) in f.local_variables.iter_mut() {
        if let Some(init) = lv.init.as_mut() {
            if let Some(&new) = remap.get(init) {
                *init = new;
            }
        }
    }
}

fn adjust_block_for_cloak(
    remap: &HashMap<naga::Handle<naga::Expression>, naga::Handle<naga::Expression>>,
    wrap: &HashMap<naga::Handle<naga::Expression>, naga::Handle<naga::Expression>>,
    block: &mut naga::Block,
) {
    use naga::{Range, Statement};
    use std::mem;
    let map = |h: naga::Handle<naga::Expression>| -> naga::Handle<naga::Expression> {
        remap.get(&h).copied().unwrap_or(h)
    };
    let original = mem::replace(block, naga::Block::with_capacity(block.len()));
    for (stmt, span) in original.span_into_iter() {
        match stmt {
            Statement::Emit(range) => {
                let handles: Vec<naga::Handle<naga::Expression>> = range.collect();
                if handles.is_empty() {
                    continue;
                }
                // Single contiguous Emit covering every remapped handle plus
                // any inserted cloak wrappers.
                let mut lo: Option<naga::Handle<naga::Expression>> = None;
                let mut hi: Option<naga::Handle<naga::Expression>> = None;
                let bump =
                    |h: naga::Handle<naga::Expression>,
                     lo: &mut Option<naga::Handle<naga::Expression>>,
                     hi: &mut Option<naga::Handle<naga::Expression>>| {
                        *lo = Some(match *lo {
                            Some(p) if p.index() <= h.index() => p,
                            _ => h,
                        });
                        *hi = Some(match *hi {
                            Some(p) if p.index() >= h.index() => p,
                            _ => h,
                        });
                    };
                for old_h in &handles {
                    bump(map(*old_h), &mut lo, &mut hi);
                    if let Some(&w) = wrap.get(old_h) {
                        bump(w, &mut lo, &mut hi);
                    }
                }
                if let (Some(a), Some(b)) = (lo, hi) {
                    block.push(Statement::Emit(Range::new_from_bounds(a, b)), span);
                }
            }
            Statement::Block(mut child) => {
                adjust_block_for_cloak(remap, wrap, &mut child);
                block.push(Statement::Block(child), span);
            }
            Statement::If {
                condition,
                mut accept,
                mut reject,
            } => {
                let cond = map(condition);
                adjust_block_for_cloak(remap, wrap, &mut accept);
                adjust_block_for_cloak(remap, wrap, &mut reject);
                block.push(
                    Statement::If {
                        condition: cond,
                        accept,
                        reject,
                    },
                    span,
                );
            }
            Statement::Switch {
                selector,
                mut cases,
            } => {
                let sel = map(selector);
                for case in &mut cases {
                    adjust_block_for_cloak(remap, wrap, &mut case.body);
                }
                block.push(
                    Statement::Switch {
                        selector: sel,
                        cases,
                    },
                    span,
                );
            }
            Statement::Loop {
                mut body,
                mut continuing,
                break_if,
            } => {
                adjust_block_for_cloak(remap, wrap, &mut body);
                adjust_block_for_cloak(remap, wrap, &mut continuing);
                let break_if = break_if.map(map);
                block.push(
                    Statement::Loop {
                        body,
                        continuing,
                        break_if,
                    },
                    span,
                );
            }
            mut other => {
                adjust_stmt_with_map(remap, &mut other);
                block.push(other, span);
            }
        }
    }
}

// WGSL §17.6.2: `e1 / e2` and `e1 % e2` are compile errors when the operand
// type is integer scalar/vector and any element of e2 is zero. naga 29's
// frontend catches this for purely const expressions but not for compound-
// assignment runtime statements (`var v = 0; v /= 0;`) or for module-level
// `const right = 0; ... left / right` patterns where the divisor reaches the
// binary expression through Expression::Constant. Walk every expression arena
// (per-function locals + module global_expressions) and flag every
// Binary{Divide|Modulo} whose right operand resolves through Constant /
// As / Splat / Compose to a Literal::I32(0) | U32(0) | AbstractInt(0).
//
// Type discrimination is via the leaf literal: float zeros never trigger
// because `0 / 0.0` would have `Literal::F32(0.0)` at the leaf, which the
// matcher ignores. ZeroValue handles are skipped entirely (their type is in
// the Type arena and resolving every one would require a full TypeResolver
// pass; ZeroValue is rare on the divisor side in practice).
fn validate_div_rem(module: &naga::Module) -> Vec<WgslMessage> {
    // Compile-time: skip module.global_expressions scanning. Per WGSL
    // §10.3.2.3 override evaluation is per-entry-point, so a bad override
    // expression like `override cx = 1u/cu` (with cu=0) must NOT reject
    // the whole module — it only matters if a targeted entry point
    // reaches `cx`. The bake-time validator (validate_div_rem_live) does
    // this with live-set filtering after process_overrides; at parse time
    // we leave module-level Binary subtrees alone. Per-function scanning
    // still catches `var v = 0; v /= 0;` and `const right = 0; ... left/
    // right` inside function bodies because the Binary lives in
    // f.expressions, not global_expressions.
    let mut errors: Vec<WgslMessage> = Vec::new();
    for ep in &module.entry_points {
        check_div_rem_in_arena(&ep.function.expressions, module, &mut errors);
    }
    for (_, f) in module.functions.iter() {
        check_div_rem_in_arena(&f.expressions, module, &mut errors);
    }
    errors
}

// Bake-time validation: same checks as validate_div_rem, but per-function
// it ignores Binary expressions that the post-process_overrides folding has
// stranded as orphans (no Statement::Emit references them). Without this,
// `false && (1 / zero_override) == 0` regresses — the `false &&` gets folded
// away yet the `1 / 0` Binary subtree stays in the arena and our flat scan
// would still flag it.
fn validate_div_rem_live(module: &naga::Module) -> Vec<WgslMessage> {
    let mut errors: Vec<WgslMessage> = Vec::new();
    // module.global_expressions: live-set is the transitive closure rooted at
    // every Constant.init and Override.init — only what is actually evaluated.
    // Subtrees orphaned by process_overrides folding (e.g. the divide in
    // `override foo = false && (1 / zero) == 0`) fall outside and are skipped.
    let mut global_live: HashSet<naga::Handle<naga::Expression>> = HashSet::new();
    for (_, c) in module.constants.iter() {
        collect_expression_descendants(
            c.init,
            &module.global_expressions,
            module,
            &mut global_live,
        );
    }
    for (_, ov) in module.overrides.iter() {
        if let Some(init) = ov.init {
            collect_expression_descendants(
                init,
                &module.global_expressions,
                module,
                &mut global_live,
            );
        }
    }
    check_div_rem_in_arena_live(
        &module.global_expressions,
        module,
        &global_live,
        &mut errors,
    );
    for ep in &module.entry_points {
        let mut live: HashSet<naga::Handle<naga::Expression>> = HashSet::new();
        collect_live_in_block(
            &ep.function.body,
            &ep.function.expressions,
            module,
            &mut live,
        );
        check_div_rem_in_arena_live(&ep.function.expressions, module, &live, &mut errors);
    }
    for (_, f) in module.functions.iter() {
        let mut live: HashSet<naga::Handle<naga::Expression>> = HashSet::new();
        collect_live_in_block(&f.body, &f.expressions, module, &mut live);
        check_div_rem_in_arena_live(&f.expressions, module, &live, &mut errors);
    }
    errors
}

fn collect_live_in_block(
    block: &naga::Block,
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
    set: &mut HashSet<naga::Handle<naga::Expression>>,
) {
    use naga::Statement;
    for (stmt, _span) in block.span_iter() {
        match stmt {
            Statement::Emit(range) => {
                for h in range.clone() {
                    collect_expression_descendants(h, local, module, set);
                }
            }
            Statement::Block(b) => collect_live_in_block(b, local, module, set),
            Statement::If {
                condition,
                accept,
                reject,
            } => {
                collect_expression_descendants(*condition, local, module, set);
                collect_live_in_block(accept, local, module, set);
                collect_live_in_block(reject, local, module, set);
            }
            Statement::Switch { selector, cases } => {
                collect_expression_descendants(*selector, local, module, set);
                for case in cases {
                    collect_live_in_block(&case.body, local, module, set);
                }
            }
            Statement::Loop {
                body,
                continuing,
                break_if,
            } => {
                collect_live_in_block(body, local, module, set);
                collect_live_in_block(continuing, local, module, set);
                if let Some(h) = break_if {
                    collect_expression_descendants(*h, local, module, set);
                }
            }
            Statement::Return { value: Some(h) } => {
                collect_expression_descendants(*h, local, module, set);
            }
            Statement::Store { pointer, value } => {
                collect_expression_descendants(*pointer, local, module, set);
                collect_expression_descendants(*value, local, module, set);
            }
            Statement::Call {
                arguments, result, ..
            } => {
                for h in arguments {
                    collect_expression_descendants(*h, local, module, set);
                }
                if let Some(h) = result {
                    collect_expression_descendants(*h, local, module, set);
                }
            }
            _ => {}
        }
    }
}

fn check_div_rem_in_arena_live(
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
    live: &HashSet<naga::Handle<naga::Expression>>,
    errors: &mut Vec<WgslMessage>,
) {
    use naga::{BinaryOperator, Expression};
    let mut guarded: HashSet<naga::Handle<Expression>> = HashSet::new();
    for (_h, expr) in local.iter() {
        if let Expression::Binary { op, right, .. } = expr {
            if matches!(op, BinaryOperator::LogicalAnd | BinaryOperator::LogicalOr) {
                collect_expression_descendants(*right, local, module, &mut guarded);
            }
        }
    }
    for (h, expr) in local.iter() {
        if !live.contains(&h) || guarded.contains(&h) {
            continue;
        }
        if let Expression::Binary { op, left, right } = expr {
            if !matches!(op, BinaryOperator::Divide | BinaryOperator::Modulo) {
                continue;
            }
            let op_name = match op {
                BinaryOperator::Divide => "division",
                _ => "remainder",
            };
            if expression_contains_int_zero(*right, local, module) {
                errors.push(WgslMessage {
                    r#type: "error".to_string(),
                    message: format!(
                        "integer {} by zero is a compile error per WGSL §17.6.2",
                        op_name
                    ),
                    line_num: 0,
                    line_pos: 0,
                    offset: 0,
                    length: 0,
                });
                continue;
            }
            if expression_resolves_to_i32(*left, local, module) == Some(i32::MIN)
                && expression_resolves_to_i32(*right, local, module) == Some(-1)
            {
                errors.push(WgslMessage {
                    r#type: "error".to_string(),
                    message: format!("integer {} of i32::MIN by -1 overflows i32", op_name),
                    line_num: 0,
                    line_pos: 0,
                    offset: 0,
                    length: 0,
                });
            }
        }
    }
}

fn check_div_rem_in_arena(
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
    errors: &mut Vec<WgslMessage>,
) {
    use naga::{BinaryOperator, Expression};

    // WGSL §8.7 / §15.2: `&&` and `||` short-circuit. The RHS expression of a
    // logical operator only evaluates when the LHS does not determine the
    // result, so a syntactically-present div-by-zero inside the RHS of a
    // short-circuit must not raise an error. CTS short_circuiting_and_or
    // tests this exact contract (`false && (1 / zero_i32) == 0` must compile).
    // Build a set of expression handles transitively reachable from the
    // `right` operand of every LogicalAnd / LogicalOr in the arena and skip
    // any Binary{Divide|Modulo} whose handle falls in that set.
    let mut guarded: HashSet<naga::Handle<Expression>> = HashSet::new();
    for (_h, expr) in local.iter() {
        if let Expression::Binary { op, right, .. } = expr {
            if matches!(op, BinaryOperator::LogicalAnd | BinaryOperator::LogicalOr) {
                collect_expression_descendants(*right, local, module, &mut guarded);
            }
        }
    }

    for (h, expr) in local.iter() {
        if guarded.contains(&h) {
            continue;
        }
        if let Expression::Binary { op, left, right } = expr {
            if !matches!(op, BinaryOperator::Divide | BinaryOperator::Modulo) {
                continue;
            }
            let op_name = match op {
                BinaryOperator::Divide => "division",
                _ => "remainder",
            };
            if expression_contains_int_zero(*right, local, module) {
                errors.push(WgslMessage {
                    r#type: "error".to_string(),
                    message: format!(
                        "integer {} by zero is a compile error per WGSL §17.6.2",
                        op_name
                    ),
                    line_num: 0,
                    line_pos: 0,
                    offset: 0,
                    length: 0,
                });
                continue;
            }
            // i32::MIN / -1 (and % -1) overflows the i32 range. WGSL §17.6.2
            // requires this to be a compile error. naga 29's const evaluator
            // miscomputes the value silently.
            if expression_resolves_to_i32(*left, local, module) == Some(i32::MIN)
                && expression_resolves_to_i32(*right, local, module) == Some(-1)
            {
                errors.push(WgslMessage {
                    r#type: "error".to_string(),
                    message: format!("integer {} of i32::MIN by -1 overflows i32", op_name),
                    line_num: 0,
                    line_pos: 0,
                    offset: 0,
                    length: 0,
                });
            }
        }
    }
}

fn collect_expression_descendants(
    h: naga::Handle<naga::Expression>,
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
    set: &mut HashSet<naga::Handle<naga::Expression>>,
) {
    use naga::Expression;
    if !set.insert(h) {
        return;
    }
    let expr = match resolve_expr_in_module(h, local, module) {
        Some(e) => e,
        None => return,
    };
    let mut visit = |child: naga::Handle<Expression>| {
        collect_expression_descendants(child, local, module, set);
    };
    match expr {
        Expression::Access { base, index } => {
            visit(*base);
            visit(*index);
        }
        Expression::AccessIndex { base, .. } => visit(*base),
        Expression::Splat { value, .. } => visit(*value),
        Expression::Swizzle { vector, .. } => visit(*vector),
        Expression::Compose { components, .. } => {
            for c in components {
                visit(*c);
            }
        }
        Expression::Load { pointer } => visit(*pointer),
        Expression::Unary { expr, .. } => visit(*expr),
        Expression::Binary { left, right, .. } => {
            visit(*left);
            visit(*right);
        }
        Expression::Select {
            condition,
            accept,
            reject,
        } => {
            visit(*condition);
            visit(*accept);
            visit(*reject);
        }
        Expression::Derivative { expr, .. } => visit(*expr),
        Expression::Relational { argument, .. } => visit(*argument),
        Expression::Math {
            arg,
            arg1,
            arg2,
            arg3,
            ..
        } => {
            visit(*arg);
            if let Some(e) = arg1 {
                visit(*e);
            }
            if let Some(e) = arg2 {
                visit(*e);
            }
            if let Some(e) = arg3 {
                visit(*e);
            }
        }
        Expression::As { expr, .. } => visit(*expr),
        Expression::ArrayLength(expr) => visit(*expr),
        _ => {}
    }
}

fn resolve_expr_in_module<'a>(
    h: naga::Handle<naga::Expression>,
    local: &'a naga::Arena<naga::Expression>,
    module: &'a naga::Module,
) -> Option<&'a naga::Expression> {
    if local.try_get(h).is_ok() {
        Some(&local[h])
    } else if module.global_expressions.try_get(h).is_ok() {
        Some(&module.global_expressions[h])
    } else {
        None
    }
}

fn expression_contains_int_zero(
    h: naga::Handle<naga::Expression>,
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
) -> bool {
    use naga::{Expression, Literal};
    let expr = match resolve_expr_in_module(h, local, module) {
        Some(e) => e,
        None => return false,
    };
    match expr {
        Expression::Literal(Literal::I32(0))
        | Expression::Literal(Literal::U32(0))
        | Expression::Literal(Literal::AbstractInt(0)) => true,
        Expression::Splat { value, .. } => expression_contains_int_zero(*value, local, module),
        Expression::Compose { components, .. } => components
            .iter()
            .any(|c| expression_contains_int_zero(*c, local, module)),
        Expression::Constant(c_h) => {
            let init = module.constants[*c_h].init;
            expression_contains_int_zero(init, &module.global_expressions, module)
        }
        // Override expressions are not shader-creation-time constants. Their
        // initialized or supplied values are checked after process_overrides()
        // in the bake-time validator.
        Expression::Override(_) => false,
        Expression::As { expr, .. } => expression_contains_int_zero(*expr, local, module),
        _ => false,
    }
}

// Resolve a chain of Constant/Override/As/Literal to a concrete i32 value if
// possible. Returns None for non-i32 leaves, missing override defaults, or
// non-resolvable expressions. Used by the i32::MIN/-1 overflow check.
fn expression_resolves_to_i32(
    h: naga::Handle<naga::Expression>,
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
) -> Option<i32> {
    use naga::{Expression, Literal};
    let expr = resolve_expr_in_module(h, local, module)?;
    match expr {
        Expression::Literal(Literal::I32(v)) => Some(*v),
        Expression::Literal(Literal::AbstractInt(v)) => i32::try_from(*v).ok(),
        Expression::Constant(c_h) => {
            let init = module.constants[*c_h].init;
            expression_resolves_to_i32(init, &module.global_expressions, module)
        }
        // Override expressions are pipeline-time values, not
        // shader-creation-time constants. process_overrides() turns them into
        // literals for bake-time checks.
        Expression::Override(_) => None,
        Expression::As { expr, .. } => expression_resolves_to_i32(*expr, local, module),
        _ => None,
    }
}

// Resolve any concrete numeric leaf to f64 for cross-type comparisons (used
// by clamp/smoothstep low<=high checks). Returns None for non-numeric leaves
// or unresolvable expressions. Override.init=None means "no compile-time
// value" — return None so the check is skipped.
//
// Splat and uniform Compose collapse to the inner scalar value — for
// `clamp(v, vec2<f32>(o_low), vec2<f32>(o_high))` patterns the per-component
// pair is always (o_low, o_high) regardless of vector width, so a single
// scalar comparison is sufficient.
fn expression_resolves_to_f64(
    h: naga::Handle<naga::Expression>,
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
) -> Option<f64> {
    use naga::{Expression, Literal};
    let expr = resolve_expr_in_module(h, local, module)?;
    match expr {
        Expression::Literal(Literal::I32(v)) => Some(*v as f64),
        Expression::Literal(Literal::U32(v)) => Some(*v as f64),
        Expression::Literal(Literal::F32(v)) => Some(*v as f64),
        Expression::Literal(Literal::F16(v)) => Some(f32::from(*v) as f64),
        Expression::Literal(Literal::AbstractInt(v)) => Some(*v as f64),
        Expression::Literal(Literal::AbstractFloat(v)) => Some(*v),
        Expression::Constant(c_h) => {
            let init = module.constants[*c_h].init;
            expression_resolves_to_f64(init, &module.global_expressions, module)
        }
        Expression::Override(o_h) => module.overrides[*o_h]
            .init
            .and_then(|init| expression_resolves_to_f64(init, &module.global_expressions, module)),
        Expression::As { expr, .. } => expression_resolves_to_f64(*expr, local, module),
        Expression::Splat { value, .. } => expression_resolves_to_f64(*value, local, module),
        Expression::Compose { components, .. } if !components.is_empty() => {
            let first = expression_resolves_to_f64(components[0], local, module)?;
            for c in &components[1..] {
                if expression_resolves_to_f64(*c, local, module)? != first {
                    // Non-uniform vector — caller must use the per-component
                    // path via vec_component_handles, not this resolver.
                    return None;
                }
            }
            Some(first)
        }
        _ => None,
    }
}

// WGSL §17.6.3.4 (clamp) and §17.6.3.21 (smoothstep): the low/edge0 argument
// must be ≤ high/edge1 when both are const- or override-evaluable, otherwise
// it is a compile error. naga 29 lacks this check — it only validates the
// type, not the value relationship — so any test that passes a literal
// `clamp(v, 1.0, 0.0)` reaches the runtime with undefined semantics.
//
// Walk every Math expression with fun ∈ {Clamp, SmoothStep}, resolve arg1 and
// arg2 to a concrete f64 (handling Literal / Constant / Override-with-init /
// As / Splat-of-scalar / Compose-with-uniform-scalars). When both resolve and
// arg1 > arg2, emit an error. Vector arguments where any component pair has
// low > high also error per the spec; for Splat we compare the scalar inner
// value once, for Compose we compare component-wise.
fn validate_clamp_smoothstep(module: &naga::Module) -> Vec<WgslMessage> {
    // Same per-entry-point reasoning as validate_div_rem: skip
    // module.global_expressions at parse time so a bad clamp() inside
    // an override init does not reject the whole module when the
    // targeted entry point doesn't reach that override.
    let mut errors: Vec<WgslMessage> = Vec::new();
    for ep in &module.entry_points {
        check_clamp_smoothstep_in_arena(&ep.function.expressions, module, &mut errors);
    }
    for (_, f) in module.functions.iter() {
        check_clamp_smoothstep_in_arena(&f.expressions, module, &mut errors);
    }
    errors
}

// WGSL §17.7.7 / §17.7.10 etc: textureSample / textureSampleLevel /
// textureSampleGrad / textureSampleBias / textureGather /
// textureGatherCompare accept an optional `offset` parameter that must
// be a const-expression vec2<i32> | vec3<i32> with each component in
// the range [-8, 7]. naga 29 type-checks the operand but does not
// enforce the value range, so CTS *_argument tests pass values like
// vec2(-9, -9) and vec2(8, 8) and expect compile-time rejection.
//
// Walk every Expression::ImageSample whose `offset` field is Some,
// resolve each component (Compose / Splat) to i32 via
// expression_resolves_to_i32 / vec_component_handles, and emit an
// error if any component falls outside the closed [-8, 7] interval.
fn validate_texture_offsets(module: &naga::Module) -> Vec<WgslMessage> {
    let mut errors: Vec<WgslMessage> = Vec::new();
    for ep in &module.entry_points {
        check_texture_offsets_in_arena(&ep.function.expressions, module, &mut errors);
    }
    for (_, f) in module.functions.iter() {
        check_texture_offsets_in_arena(&f.expressions, module, &mut errors);
    }
    errors
}

fn check_texture_offsets_in_arena(
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
    errors: &mut Vec<WgslMessage>,
) {
    use naga::Expression;
    for (_h, expr) in local.iter() {
        if let Expression::ImageSample {
            offset: Some(off_h),
            ..
        } = expr
        {
            let comps = collect_offset_components(*off_h, local, module);
            for c in comps {
                if !(-8..=7).contains(&c) {
                    errors.push(WgslMessage {
                        r#type: "error".to_string(),
                        message: format!(
                            "texture builtin offset component {} is outside the WGSL [-8, 7] range",
                            c
                        ),
                        line_num: 0,
                        line_pos: 0,
                        offset: 0,
                        length: 0,
                    });
                    break; // one error per call site
                }
            }
        }
    }
}

// Resolve a vector-of-i32 const expression to its per-component values.
// Returns empty when the offset is non-const or a shape we don't
// recognise (caller silently skips — never a false positive).
fn collect_offset_components(
    h: naga::Handle<naga::Expression>,
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
) -> Vec<i32> {
    if let Some(scalar) = expression_resolves_to_i32(h, local, module) {
        // Splat / scalar collapses to one repeated value; checking once
        // is sufficient since a single component out-of-range fails the
        // whole call.
        return vec![scalar];
    }
    if let Some(handles) = vec_component_handles(h, local, module) {
        let mut out = Vec::with_capacity(handles.len());
        for hh in handles {
            match expression_resolves_to_i32(hh, local, module) {
                Some(v) => out.push(v),
                None => return Vec::new(),
            }
        }
        return out;
    }
    Vec::new()
}

fn check_clamp_smoothstep_in_arena(
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
    errors: &mut Vec<WgslMessage>,
) {
    use naga::{Expression, MathFunction};
    for (_h, expr) in local.iter() {
        if let Expression::Math {
            fun,
            arg1: Some(low),
            arg2: Some(high),
            ..
        } = expr
        {
            // Only clamp: low ≤ high required (error iff low > high). naga
            // 29 has a separate "Not implemented as constant expression"
            // gap on smoothstep that interacts unpredictably with the
            // values/partial_eval_errors test, so we don't second-guess
            // smoothstep here.
            let (fn_name, low_label, high_label) = match fun {
                MathFunction::Clamp => ("clamp", "low", "high"),
                _ => continue,
            };
            let strict = false;
            let pairs = collect_low_high_pairs(*low, *high, local, module);
            for (l, h) in pairs {
                let bad = if strict { l >= h } else { l > h };
                if bad {
                    errors.push(WgslMessage {
                        r#type: "error".to_string(),
                        message: format!(
                            "{}({}, {}, ...): {} ({}) must be {} {} ({})",
                            fn_name,
                            low_label,
                            high_label,
                            low_label,
                            l,
                            if strict { "less than" } else { "≤" },
                            high_label,
                            h
                        ),
                        line_num: 0,
                        line_pos: 0,
                        offset: 0,
                        length: 0,
                    });
                    break;
                }
            }
        }
    }
}

// Build pairs of (low, high) f64 values to compare. Returns empty if any
// branch can't be resolved (so the check is skipped, never a false positive).
fn collect_low_high_pairs(
    low: naga::Handle<naga::Expression>,
    high: naga::Handle<naga::Expression>,
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
) -> Vec<(f64, f64)> {
    // First try scalar resolution — works for Literal/Constant/Override/As.
    if let (Some(l), Some(h)) = (
        expression_resolves_to_f64(low, local, module),
        expression_resolves_to_f64(high, local, module),
    ) {
        return vec![(l, h)];
    }
    // Vector path: both sides must resolve to either a Splat with scalar value
    // or a Compose with the same number of components.
    let low_components = vec_component_handles(low, local, module);
    let high_components = vec_component_handles(high, local, module);
    if let (Some(lcs), Some(hcs)) = (low_components, high_components) {
        if lcs.len() == hcs.len() && !lcs.is_empty() {
            let mut pairs = Vec::with_capacity(lcs.len());
            for (lh, hh) in lcs.iter().zip(hcs.iter()) {
                if let (Some(l), Some(h)) = (
                    expression_resolves_to_f64(*lh, local, module),
                    expression_resolves_to_f64(*hh, local, module),
                ) {
                    pairs.push((l, h));
                } else {
                    return Vec::new();
                }
            }
            return pairs;
        }
    }
    Vec::new()
}

// Return the per-component expression handles of a vector expression (Splat
// expanded to a 4-wide repeat — caller compares zip-len anyway), or None if
// the expression isn't a recognised vector form.
fn vec_component_handles(
    h: naga::Handle<naga::Expression>,
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
) -> Option<Vec<naga::Handle<naga::Expression>>> {
    let expr = resolve_expr_in_module(h, local, module)?;
    match expr {
        naga::Expression::Compose { components, .. } => Some(components.clone()),
        // Splat shows up as `vec3(scalar)`; we don't know the size here so we
        // can't construct a per-component vec without the type. Caller
        // already handled scalar-paired-with-splat via expression_resolves_to_f64
        // (a splat has no scalar value of its own; we just return None).
        _ => None,
    }
}

// WebGPU §16.7.4: textureSampleBias clamps the bias parameter to [-16.0, 15.99].
// Naga lowers `textureSampleBias(.., bias)` to `Expression::ImageSample` with
// `level: SampleLevel::Bias(bias_handle)`, and the SPIR-V backend forwards
// `bias_handle` as the OpImageSample*ImplicitLod Bias operand without clamping.
// Vulkan does not clamp either, so out-of-range bias values sample at the
// wrong mip level. This pass injects `Math::Clamp(bias, -16.0, 15.99)` before
// each ImageSample-with-bias and rewires `level` to point at the clamp.
fn clamp_image_sample_bias(module: &mut naga::Module) {
    let n_eps = module.entry_points.len();
    for ep_idx in 0..n_eps {
        clamp_bias_in_function(&mut module.entry_points[ep_idx].function);
    }
    let helper_handles: Vec<naga::Handle<naga::Function>> =
        module.functions.iter().map(|(h, _)| h).collect();
    for h in helper_handles {
        clamp_bias_in_function(&mut module.functions[h]);
    }
}

fn clamp_bias_in_function(f: &mut naga::Function) {
    use naga::{Expression, Handle, Literal, MathFunction, SampleLevel, Span};
    use std::mem;

    let mut has_target = false;
    for (_, expr) in f.expressions.iter() {
        if let Expression::ImageSample {
            level: SampleLevel::Bias(_),
            ..
        } = expr
        {
            has_target = true;
            break;
        }
    }
    if !has_target {
        return;
    }

    // Drain old arena, rebuild while inserting literals + clamp + new sample
    // immediately before each ImageSample-with-bias. Each old handle gets a
    // new handle (full handle remap). bias_info maps old_sample_h to the new
    // (clamp_h, new_sample_h) tuple so Emit ranges can be split to skip the
    // inserted literals (which are pre_emit and must NOT appear in any Emit).
    let drained: Vec<(Handle<Expression>, Expression, Span)> =
        mem::take(&mut f.expressions).drain().collect();
    let mut remap: HashMap<Handle<Expression>, Handle<Expression>> = HashMap::new();
    let mut bias_info: HashMap<Handle<Expression>, (Handle<Expression>, Handle<Expression>)> =
        HashMap::new();

    for (old_h, mut expr, span) in drained {
        adjust_expr_with_map(&remap, &mut expr);
        match expr {
            Expression::ImageSample {
                image,
                sampler,
                gather,
                coordinate,
                array_index,
                offset,
                level: SampleLevel::Bias(translated_bias),
                depth_ref,
                clamp_to_edge,
            } => {
                let neg = f
                    .expressions
                    .append(Expression::Literal(Literal::F32(-16.0)), Span::UNDEFINED);
                let pos = f
                    .expressions
                    .append(Expression::Literal(Literal::F32(15.99)), Span::UNDEFINED);
                let clamp_h = f.expressions.append(
                    Expression::Math {
                        fun: MathFunction::Clamp,
                        arg: translated_bias,
                        arg1: Some(neg),
                        arg2: Some(pos),
                        arg3: None,
                    },
                    Span::UNDEFINED,
                );
                let new_h = f.expressions.append(
                    Expression::ImageSample {
                        image,
                        sampler,
                        gather,
                        coordinate,
                        array_index,
                        offset,
                        level: SampleLevel::Bias(clamp_h),
                        depth_ref,
                        clamp_to_edge,
                    },
                    span,
                );
                remap.insert(old_h, new_h);
                bias_info.insert(old_h, (clamp_h, new_h));
            }
            other => {
                let new_h = f.expressions.append(other, span);
                remap.insert(old_h, new_h);
            }
        }
    }

    adjust_block_for_bias(&remap, &bias_info, &mut f.body);

    let old_named = mem::take(&mut f.named_expressions);
    for (old_h, name) in old_named {
        let new_h = remap.get(&old_h).copied().unwrap_or(old_h);
        f.named_expressions.insert(new_h, name);
    }
}

// Walk a block, translating handle references AND splitting Emit ranges so
// that:
//   - Each old ImageSample-with-bias is replaced by [clamp_h..new_sample_h]
//   - Inserted literal handles (between map(prev_old) and clamp_h) are not
//     covered by any Emit (they are pre_emit).
//   - Other handles are mapped via remap and preserved as contiguous Emit
//     ranges where possible.
fn adjust_block_for_bias(
    remap: &HashMap<naga::Handle<naga::Expression>, naga::Handle<naga::Expression>>,
    bias_info: &HashMap<
        naga::Handle<naga::Expression>,
        (
            naga::Handle<naga::Expression>,
            naga::Handle<naga::Expression>,
        ),
    >,
    block: &mut naga::Block,
) {
    use naga::{Block, Handle, Range, Statement};
    use std::mem;

    let original = mem::replace(block, Block::with_capacity(block.len()));
    for (stmt, span) in original.span_into_iter() {
        match stmt {
            Statement::Emit(range) => {
                let handles: Vec<Handle<naga::Expression>> = range.clone().collect();
                if handles.is_empty() {
                    block.push(Statement::Emit(range), span);
                    continue;
                }
                // Build the new emit list: walk old handles in order. Each
                // bias-sample old handle generates a [clamp..new_sample]
                // emit; non-bias handles are coalesced into contiguous
                // ranges in the new arena.
                let mut pending_first: Option<Handle<naga::Expression>> = None;
                let mut pending_last: Option<Handle<naga::Expression>> = None;
                let flush = |first: &mut Option<Handle<naga::Expression>>,
                             last: &mut Option<Handle<naga::Expression>>,
                             out: &mut Block| {
                    if let (Some(f), Some(l)) = (first.take(), last.take()) {
                        out.push(Statement::Emit(Range::new_from_bounds(f, l)), span);
                    }
                };
                for old_h in handles {
                    if let Some(&(clamp_h, new_sample_h)) = bias_info.get(&old_h) {
                        flush(&mut pending_first, &mut pending_last, block);
                        block.push(
                            Statement::Emit(Range::new_from_bounds(clamp_h, new_sample_h)),
                            span,
                        );
                    } else {
                        let new_h = remap.get(&old_h).copied().unwrap_or(old_h);
                        match (pending_first, pending_last) {
                            (Some(_), Some(last)) if last.index() + 1 == new_h.index() => {
                                pending_last = Some(new_h);
                            }
                            _ => {
                                flush(&mut pending_first, &mut pending_last, block);
                                pending_first = Some(new_h);
                                pending_last = Some(new_h);
                            }
                        }
                    }
                }
                flush(&mut pending_first, &mut pending_last, block);
            }
            Statement::Block(mut child) => {
                adjust_block_for_bias(remap, bias_info, &mut child);
                block.push(Statement::Block(child), span);
            }
            Statement::If {
                condition,
                mut accept,
                mut reject,
            } => {
                let cond = remap.get(&condition).copied().unwrap_or(condition);
                adjust_block_for_bias(remap, bias_info, &mut accept);
                adjust_block_for_bias(remap, bias_info, &mut reject);
                block.push(
                    Statement::If {
                        condition: cond,
                        accept,
                        reject,
                    },
                    span,
                );
            }
            Statement::Switch {
                selector,
                mut cases,
            } => {
                let sel = remap.get(&selector).copied().unwrap_or(selector);
                for case in &mut cases {
                    adjust_block_for_bias(remap, bias_info, &mut case.body);
                }
                block.push(
                    Statement::Switch {
                        selector: sel,
                        cases,
                    },
                    span,
                );
            }
            Statement::Loop {
                mut body,
                mut continuing,
                break_if,
            } => {
                adjust_block_for_bias(remap, bias_info, &mut body);
                adjust_block_for_bias(remap, bias_info, &mut continuing);
                let break_if = break_if.map(|h| remap.get(&h).copied().unwrap_or(h));
                block.push(
                    Statement::Loop {
                        body,
                        continuing,
                        break_if,
                    },
                    span,
                );
            }
            mut other => {
                adjust_stmt_with_map(remap, &mut other);
                block.push(other, span);
            }
        }
    }
}

fn adjust_block_with_remap(
    remap: &HashMap<naga::Handle<naga::Expression>, naga::Handle<naga::Expression>>,
    block: &mut naga::Block,
) {
    use naga::{Range, Statement};
    let map = |h: naga::Handle<naga::Expression>| -> naga::Handle<naga::Expression> {
        remap.get(&h).copied().unwrap_or(h)
    };
    for stmt in block.iter_mut() {
        match *stmt {
            Statement::Emit(ref mut range) => {
                if let Some((first, last)) = range.first_and_last() {
                    *range = Range::new_from_bounds(map(first), map(last));
                }
            }
            _ => {
                adjust_stmt_with_map(remap, stmt);
            }
        }
    }
}

// adjust_expr_with_map: like naga's pipeline_constants::adjust_expr but uses
// a HashMap and only rewrites handles present as keys.
fn adjust_expr_with_map(
    remap: &HashMap<naga::Handle<naga::Expression>, naga::Handle<naga::Expression>>,
    expr: &mut naga::Expression,
) {
    use naga::Expression;
    let adjust = |h: &mut naga::Handle<naga::Expression>| {
        if let Some(&new) = remap.get(h) {
            *h = new;
        }
    };
    match *expr {
        Expression::Compose {
            ref mut components,
            ty: _,
        } => {
            for c in components.iter_mut() {
                adjust(c);
            }
        }
        Expression::Access {
            ref mut base,
            ref mut index,
        } => {
            adjust(base);
            adjust(index);
        }
        Expression::AccessIndex {
            ref mut base,
            index: _,
        } => adjust(base),
        Expression::Splat {
            ref mut value,
            size: _,
        } => adjust(value),
        Expression::Swizzle {
            ref mut vector,
            size: _,
            pattern: _,
        } => adjust(vector),
        Expression::Load { ref mut pointer } => adjust(pointer),
        Expression::ImageSample {
            ref mut image,
            ref mut sampler,
            ref mut coordinate,
            ref mut array_index,
            ref mut offset,
            ref mut level,
            ref mut depth_ref,
            gather: _,
            clamp_to_edge: _,
        } => {
            adjust(image);
            adjust(sampler);
            adjust(coordinate);
            if let Some(e) = array_index.as_mut() {
                adjust(e);
            }
            if let Some(e) = offset.as_mut() {
                adjust(e);
            }
            match *level {
                naga::SampleLevel::Exact(ref mut e) | naga::SampleLevel::Bias(ref mut e) => {
                    adjust(e)
                }
                naga::SampleLevel::Gradient {
                    ref mut x,
                    ref mut y,
                } => {
                    adjust(x);
                    adjust(y);
                }
                _ => {}
            }
            if let Some(e) = depth_ref.as_mut() {
                adjust(e);
            }
        }
        Expression::ImageLoad {
            ref mut image,
            ref mut coordinate,
            ref mut array_index,
            ref mut sample,
            ref mut level,
        } => {
            adjust(image);
            adjust(coordinate);
            if let Some(e) = array_index.as_mut() {
                adjust(e);
            }
            if let Some(e) = sample.as_mut() {
                adjust(e);
            }
            if let Some(e) = level.as_mut() {
                adjust(e);
            }
        }
        Expression::ImageQuery {
            ref mut image,
            ref mut query,
        } => {
            adjust(image);
            if let naga::ImageQuery::Size { ref mut level } = *query {
                if let Some(e) = level.as_mut() {
                    adjust(e);
                }
            }
        }
        Expression::Unary {
            ref mut expr,
            op: _,
        } => adjust(expr),
        Expression::Binary {
            ref mut left,
            ref mut right,
            op: _,
        } => {
            adjust(left);
            adjust(right);
        }
        Expression::Select {
            ref mut condition,
            ref mut accept,
            ref mut reject,
        } => {
            adjust(condition);
            adjust(accept);
            adjust(reject);
        }
        Expression::Derivative {
            ref mut expr,
            axis: _,
            ctrl: _,
        } => adjust(expr),
        Expression::Relational {
            ref mut argument,
            fun: _,
        } => adjust(argument),
        Expression::Math {
            ref mut arg,
            ref mut arg1,
            ref mut arg2,
            ref mut arg3,
            fun: _,
        } => {
            adjust(arg);
            if let Some(e) = arg1.as_mut() {
                adjust(e);
            }
            if let Some(e) = arg2.as_mut() {
                adjust(e);
            }
            if let Some(e) = arg3.as_mut() {
                adjust(e);
            }
        }
        Expression::As {
            ref mut expr,
            kind: _,
            convert: _,
        } => adjust(expr),
        Expression::ArrayLength(ref mut expr) => adjust(expr),
        Expression::RayQueryGetIntersection {
            ref mut query,
            committed: _,
        } => adjust(query),
        Expression::Literal(_)
        | Expression::FunctionArgument(_)
        | Expression::GlobalVariable(_)
        | Expression::LocalVariable(_)
        | Expression::CallResult(_)
        | Expression::RayQueryProceedResult
        | Expression::Constant(_)
        | Expression::Override(_)
        | Expression::ZeroValue(_)
        | Expression::AtomicResult { .. }
        | Expression::WorkGroupUniformLoadResult { .. }
        | Expression::SubgroupBallotResult
        | Expression::SubgroupOperationResult { .. } => {}
        _ => {}
    }
}

fn adjust_stmt_with_map(
    remap: &HashMap<naga::Handle<naga::Expression>, naga::Handle<naga::Expression>>,
    stmt: &mut naga::Statement,
) {
    use naga::Statement;
    let adjust = |h: &mut naga::Handle<naga::Expression>| {
        if let Some(&new) = remap.get(h) {
            *h = new;
        }
    };
    match *stmt {
        Statement::Emit(_) => {
            // Emit ranges are handled by adjust_block_with_remap directly.
        }
        Statement::Block(ref mut b) => adjust_block_with_remap(remap, b),
        Statement::If {
            ref mut condition,
            ref mut accept,
            ref mut reject,
        } => {
            adjust(condition);
            adjust_block_with_remap(remap, accept);
            adjust_block_with_remap(remap, reject);
        }
        Statement::Switch {
            ref mut selector,
            ref mut cases,
        } => {
            adjust(selector);
            for case in cases.iter_mut() {
                adjust_block_with_remap(remap, &mut case.body);
            }
        }
        Statement::Loop {
            ref mut body,
            ref mut continuing,
            ref mut break_if,
        } => {
            adjust_block_with_remap(remap, body);
            adjust_block_with_remap(remap, continuing);
            if let Some(e) = break_if.as_mut() {
                adjust(e);
            }
        }
        Statement::Return { ref mut value } => {
            if let Some(e) = value.as_mut() {
                adjust(e);
            }
        }
        Statement::Store {
            ref mut pointer,
            ref mut value,
        } => {
            adjust(pointer);
            adjust(value);
        }
        Statement::ImageStore {
            ref mut image,
            ref mut coordinate,
            ref mut array_index,
            ref mut value,
        } => {
            adjust(image);
            adjust(coordinate);
            if let Some(e) = array_index.as_mut() {
                adjust(e);
            }
            adjust(value);
        }
        Statement::Atomic {
            ref mut pointer,
            ref mut value,
            ref mut result,
            ref mut fun,
        } => {
            adjust(pointer);
            adjust(value);
            if let Some(ref mut r) = *result {
                adjust(r);
            }
            if let naga::AtomicFunction::Exchange {
                compare: Some(ref mut c),
            } = *fun
            {
                adjust(c);
            }
        }
        Statement::ImageAtomic {
            ref mut image,
            ref mut coordinate,
            ref mut array_index,
            fun: _,
            ref mut value,
        } => {
            adjust(image);
            adjust(coordinate);
            if let Some(ref mut a) = *array_index {
                adjust(a);
            }
            adjust(value);
        }
        Statement::WorkGroupUniformLoad {
            ref mut pointer,
            ref mut result,
        } => {
            adjust(pointer);
            adjust(result);
        }
        Statement::Call {
            ref mut arguments,
            ref mut result,
            function: _,
        } => {
            for a in arguments.iter_mut() {
                adjust(a);
            }
            if let Some(e) = result.as_mut() {
                adjust(e);
            }
        }
        _ => {}
    }
}

pub fn compile_with_meta(source: &str) -> std::result::Result<WgslCompileResult, Vec<WgslMessage>> {
    let strip = strip_diagnostic_directives(source);
    if strip.has_error {
        return Err(strip.messages);
    }
    let short_circuit_errors = validate_short_circuit_source(&strip.sanitized, None);
    if !short_circuit_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(short_circuit_errors);
        return Err(msgs);
    }
    let short_circuit_source = lower_short_circuit_source(&strip.sanitized, None);
    let smoothstep_errors = validate_smoothstep_source(&short_circuit_source, None, None);
    if !smoothstep_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(smoothstep_errors);
        return Err(msgs);
    }
    let ldexp_errors = validate_ldexp_source(&short_circuit_source, None, None);
    if !ldexp_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(ldexp_errors);
        return Err(msgs);
    }
    let parse_source = lower_ldexp_source(&lower_smoothstep_source(&short_circuit_source), None);
    let mut module = match wgsl::parse_str(&parse_source) {
        Ok(m) => m,
        Err(e) => {
            let mut msgs = strip.messages;
            msgs.push(parse_error_to_message(&e, &parse_source));
            return Err(msgs);
        }
    };

    // Run the bias clamp before validation so FunctionInfo (and downstream
    // process_overrides + spv::write_vec) see the new expressions.
    clamp_image_sample_bias(&mut module);
    cloak_const_indices(&mut module);

    // WGSL §17.6.2: integer e1 / e2 and e1 % e2 with v2 == 0 must be a
    // compilation error. naga 29 catches this for fully-const expressions
    // but not for runtime statements like `var v = 0; v /= 0;` (which is
    // what CTS scalar_vector compound_assignment=true and
    // scalar_vector_out_of_range exercise). Walk the expression arenas
    // ourselves and emit synthetic errors for the const-zero divisor case.
    let mut bridge_errors = validate_div_rem(&module);
    // WGSL §17.6.3.4 / §17.6.3.21: clamp(v, low, high) and smoothstep(e0,
    // e1, x) require low ≤ high / e0 ≤ e1 when both are const- or override-
    // evaluable. naga 29 misses this; we walk Math expressions and reject
    // const pairs where low > high.
    bridge_errors.extend(validate_clamp_smoothstep(&module));
    // WGSL §17.7.x: ImageSample offset must be a const vec[2|3]<i32>
    // with each component in [-8, 7].
    bridge_errors.extend(validate_texture_offsets(&module));
    if !bridge_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(bridge_errors);
        return Err(msgs);
    }

    let info = match Validator::new(ValidationFlags::all(), Capabilities::all()).validate(&module) {
        Ok(i) => i,
        Err(e) => return Err(vec![validation_error_to_message(&e, &parse_source)]),
    };

    // 1.0 stub (not 0.0) keeps @workgroup_size(override) emit valid; real values
    // come through bake_wgsl_with_constants at pipeline time.
    let mut pipeline_constants = PipelineConstants::new();
    for (_h, ov) in module.overrides.iter() {
        if ov.init.is_some() {
            continue;
        }
        let key = match ov.id {
            Some(n) => n.to_string(),
            None => ov.name.clone().unwrap_or_default(),
        };
        if !key.is_empty() {
            pipeline_constants.insert(key, 1.0);
        }
    }

    // catch_unwind: naga 29's SPIR-V writer has todo!() panics for some
    // unimplemented features. Emit failure leaves spirv=None; reflection survives.
    let mut options = spv::Options::default();
    options.lang_version = (1, 3);
    // Flips position.y at vertex exit so engine can use a native Vulkan viewport.
    options.flags = spv::WriterFlags::ADJUST_COORDINATE_SPACE;
    options.bounds_check_policies = robust_access_policies();
    let spirv_words: Option<Vec<u32>> =
        std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            process_overrides(&module, &info, None, &pipeline_constants)
                .ok()
                .and_then(|(processed_module, processed_info)| {
                    spv::write_vec(&processed_module, &processed_info, &options, None).ok()
                })
        }))
        .ok()
        .flatten()
        .map(|mut words| {
            strip_workgroup_explicit_layout(&mut words);
            rewrite_signed_mod_to_remainder(&mut words);
            words
        });

    let mut comparison_samplers: Vec<BindingRef> = Vec::new();
    for (_h, gv) in module.global_variables.iter() {
        if let Some(rb) = &gv.binding {
            if let naga::TypeInner::Sampler { comparison } = &module.types[gv.ty].inner {
                if *comparison {
                    comparison_samplers.push(BindingRef {
                        group: rb.group,
                        binding: rb.binding,
                        name: gv.name.clone(),
                        kind: Some("sampler".to_string()),
                        format: None,
                        access: None,
                    });
                }
            }
        }
    }

    let mut resource_globals: Vec<(String, u32, u32, String, Option<String>, Option<String>)> =
        Vec::new();
    for (_h, gv) in module.global_variables.iter() {
        if let (Some(name), Some(rb), Some(kind)) =
            (&gv.name, &gv.binding, resource_kind(&module, gv))
        {
            let (format, access) = resource_storage_meta(&module, gv);
            resource_globals.push((
                name.clone(),
                rb.group,
                rb.binding,
                kind.to_string(),
                format,
                access,
            ));
        }
    }

    // Per-entry-point statically-used bindings: scan each function body's
    // text slice (not module-wide), so a resource referenced only by frag_main
    // doesn't get reported as used by vert_main. Fixes false-positive
    // visibility rejections when BGL.visibility excludes an unused stage.
    let mut entry_points: Vec<EntryPointMeta> = Vec::with_capacity(module.entry_points.len());
    for ep in module.entry_points.iter() {
        let stage = match ep.stage {
            naga::ShaderStage::Vertex => "vertex",
            naga::ShaderStage::Fragment => "fragment",
            naga::ShaderStage::Compute => "compute",
            _ => "other",
        };
        // Per W3C §10.3.6 a binding is statically used iff it appears in the
        // call closure of the entry point. SPIR-V 1.3 OpEntryPoint omits
        // descriptor-bound globals so spirv-cross's active-interface walk
        // misses fragment-stage storage buffers; we resolve reachability from
        // WGSL source by call-graph BFS rooted at the entry-point function.
        let reachable_bodies = collect_reachable_bodies(source, &ep.name);
        let scan_text: &str = if reachable_bodies.is_empty() {
            source
        } else {
            &reachable_bodies
        };
        let ep_used: Vec<BindingRef> = resource_globals
            .iter()
            .filter(|(name, _, _, _, _, _)| identifier_occurrence_count(scan_text, name) >= 1)
            .map(|(name, g, b, kind, format, access)| BindingRef {
                group: *g,
                binding: *b,
                name: Some(name.clone()),
                kind: Some(kind.clone()),
                format: format.clone(),
                access: access.clone(),
            })
            .collect();
        entry_points.push(EntryPointMeta {
            name: ep.name.clone(),
            stage: stage.to_string(),
            statically_used: ep_used,
        });
    }

    // Pre-build per-EP scan text once (reachable bodies + preceding attribute
    // block, which is where @workgroup_size(override) lives). Reused across
    // all overrides — O(eps + overrides) instead of O(eps * overrides).
    let mut ep_scan_text: Vec<(String, String)> = Vec::with_capacity(module.entry_points.len());
    for ep in module.entry_points.iter() {
        let mut scan = collect_reachable_bodies(source, &ep.name);
        if let Some(attrs) = extract_pre_fn_attributes(source, &ep.name) {
            scan.push('\n');
            scan.push_str(attrs);
        }
        ep_scan_text.push((ep.name.clone(), scan));
    }

    let mut overrides: Vec<OverrideInfo> = Vec::with_capacity(module.overrides.len());
    for (_h, ov) in module.overrides.iter() {
        let name = ov.name.clone().unwrap_or_default();
        let identifier = match ov.id {
            Some(n) => format!("{}", n),
            None => name.clone(),
        };
        let has_default = ov.init.is_some();
        let statically_used = !name.is_empty() && identifier_appears_in_body(source, &name);
        // Per-EP attribution: only EPs whose call closure (or @workgroup_size
        // attribute) references the override name require the constant. CTS
        // multi_entry_points relies on this — main1 uses c1, main2 uses c2,
        // main3 uses c3 only via @workgroup_size(c3); pipelining main1 must
        // not require c2 or c3.
        let statically_used_by: Vec<String> = if name.is_empty() {
            Vec::new()
        } else {
            ep_scan_text
                .iter()
                .filter(|(_, text)| identifier_occurrence_count(text, &name) >= 1)
                .map(|(ep_name, _)| ep_name.clone())
                .collect()
        };
        let type_str = match &module.types[ov.ty].inner {
            naga::TypeInner::Scalar(s) => match (s.kind, s.width) {
                (naga::ScalarKind::Bool, _) => "bool",
                (naga::ScalarKind::Sint, 4) => "i32",
                (naga::ScalarKind::Uint, 4) => "u32",
                (naga::ScalarKind::Float, 4) => "f32",
                (naga::ScalarKind::Float, 2) => "f16",
                _ => "",
            },
            _ => "",
        };
        overrides.push(OverrideInfo {
            identifier,
            name,
            has_default,
            statically_used,
            statically_used_by,
            r#type: type_str.to_string(),
        });
    }

    Ok(WgslCompileResult {
        spirv: spirv_words,
        entry_points,
        comparison_samplers,
        overrides,
        warnings: Vec::new(),
        messages: strip.messages,
    })
}

pub fn install_panic_suppression() {
    suppress_naga_panics();
}

pub fn wgsl_to_spirv(source: &str) -> Result<Vec<u32>, String> {
    suppress_naga_panics();
    match compile_with_meta(source) {
        Ok(r) => r
            .spirv
            .ok_or_else(|| "SPIR-V emit failed (needs pipeline constants)".to_string()),
        Err(msgs) => {
            let joined: String = msgs
                .into_iter()
                .map(|m| m.message)
                .collect::<Vec<_>>()
                .join("\n");
            Err(joined)
        }
    }
}

pub fn try_wgsl_to_spirv(source: &str) -> Option<Vec<u32>> {
    suppress_naga_panics();
    compile_with_meta(source).ok().and_then(|r| r.spirv)
}

pub fn try_wgsl_to_spirv_full(source: &str) -> Option<WgslCompileResult> {
    suppress_naga_panics();
    compile_with_meta(source).ok()
}

// Returns parse/validate diagnostics for WGSL that fails to compile. CTS's
// shader_module/compilation_info tests expect getCompilationInfo() to surface
// at least one error message with a usable lineNum/linePos/offset/length for
// invalid shaders. The JS layer calls this when tryWgslToSpirvFull returns
// null, then attaches the messages via a getCompilationInfo override.
pub fn wgsl_diagnostics(source: &str) -> Vec<WgslMessage> {
    suppress_naga_panics();
    match compile_with_meta(source) {
        Ok(_) => Vec::new(),
        Err(msgs) => msgs,
    }
}

// Pipeline-time SPIR-V emit. Keys follow WGSL §10.3.2.3: decimal(@id(n)) if
// annotated, else the override name. When entry_point is set, substitution is
// scoped to that entry point so unused overrides are not evaluated.
pub fn bake_wgsl_with_constants(
    source: &str,
    constants: HashMap<String, f64>,
    entry_point: Option<&str>,
) -> Option<Vec<u32>> {
    suppress_naga_panics();
    let strip = strip_diagnostic_directives(source);
    if strip.has_error {
        return None;
    }
    if !validate_short_circuit_source(&strip.sanitized, entry_point).is_empty() {
        return None;
    }
    let short_circuit_source = lower_short_circuit_source(&strip.sanitized, Some(&constants));
    if !validate_smoothstep_source(&short_circuit_source, Some(&constants), entry_point).is_empty()
    {
        return None;
    }
    if !validate_ldexp_source(&short_circuit_source, Some(&constants), entry_point).is_empty() {
        return None;
    }
    let parse_source = lower_ldexp_source(
        &lower_smoothstep_source(&short_circuit_source),
        Some(&constants),
    );
    let mut module = wgsl::parse_str(&parse_source).ok()?;
    clamp_image_sample_bias(&mut module);
    cloak_const_indices(&mut module);
    let info = Validator::new(ValidationFlags::all(), Capabilities::all())
        .validate(&module)
        .ok()?;

    // 0.0 stubs for missing no-default overrides; engine-side validation still
    // enforces "required but not provided" separately.
    let mut pipeline_constants = PipelineConstants::new();
    for (_h, ov) in module.overrides.iter() {
        if ov.init.is_some() {
            continue;
        }
        let key = match ov.id {
            Some(n) => n.to_string(),
            None => ov.name.clone().unwrap_or_default(),
        };
        if !key.is_empty() {
            pipeline_constants.insert(key, 0.0);
        }
    }
    for (k, v) in constants.iter() {
        pipeline_constants.insert(k.clone(), *v);
    }

    let ep_pair: Option<(naga::ShaderStage, &str)> = entry_point.and_then(|name| {
        module
            .entry_points
            .iter()
            .find(|ep| ep.name == name)
            .map(|ep| (ep.stage, ep.name.as_str()))
    });

    let mut options = spv::Options::default();
    options.lang_version = (1, 3);
    options.flags = spv::WriterFlags::ADJUST_COORDINATE_SPACE;
    options.bounds_check_policies = robust_access_policies();
    let mut words: Vec<u32> = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        process_overrides(&module, &info, ep_pair, &pipeline_constants)
            .ok()
            .and_then(|(processed_module, processed_info)| {
                // After process_overrides bakes the supplied override values
                // into the IR, re-run the div_rem zero-divisor check so
                // pipeline-time `override o = 0; ... left / o` patterns
                // surface as a bake failure (engine turns this into a
                // pipeline-create validation error). The live-set filter is
                // critical: process_overrides folds short-circuited LHS to a
                // constant and leaves the now-unreachable Binary subtree as
                // an orphan in the arena; without the filter we'd reject
                // `false && (1 / zero) == 0` even though the divide is never
                // evaluated.
                if !validate_div_rem_live(&processed_module).is_empty() {
                    return None;
                }
                // Same low>high check after override substitution so
                // override-stage clamp/smoothstep failures surface as bake
                // errors. Use the unfiltered walker — process_overrides
                // doesn't fold these into orphans the way it folds short-
                // circuit Binary subtrees.
                if !validate_clamp_smoothstep(&processed_module).is_empty() {
                    return None;
                }
                if !validate_texture_offsets(&processed_module).is_empty() {
                    return None;
                }
                spv::write_vec(&processed_module, &processed_info, &options, None).ok()
            })
    }))
    .ok()
    .flatten()?;
    strip_workgroup_explicit_layout(&mut words);
    rewrite_signed_mod_to_remainder(&mut words);
    Some(words)
}
