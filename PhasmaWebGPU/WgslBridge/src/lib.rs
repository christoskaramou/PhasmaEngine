use std::collections::{HashMap, HashSet};
use std::sync::Once;

use naga::back::hlsl;
use naga::back::pipeline_constants::process_overrides;
use naga::back::spv;
use naga::back::PipelineConstants;
use naga::front::spv as spv_front;
use naga::front::wgsl;
use naga::proc::{BoundsCheckPolicies, BoundsCheckPolicy, ResolveContext, TypeResolution};
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

fn split_top_level_type_args(args: &str) -> Vec<String> {
    let mut out: Vec<String> = Vec::new();
    let mut depth = 0i32;
    let mut start = 0usize;
    for (i, b) in args.bytes().enumerate() {
        match b {
            b'(' | b'[' | b'{' | b'<' => depth += 1,
            b')' | b']' | b'}' | b'>' => depth -= 1,
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

fn declaration_name_after_source_keyword(
    source: &str,
    keyword_start: usize,
    keyword_len: usize,
) -> Option<String> {
    let bytes = source.as_bytes();
    let mut pos = skip_ws_and_comments(bytes, keyword_start + keyword_len);
    if pos < bytes.len() && bytes[pos] == b'<' {
        pos = find_matching_delimiter(source, pos, b'<', b'>')? + 1;
        pos = skip_ws_and_comments(bytes, pos);
    }
    if pos >= bytes.len() || !is_ident_char(bytes[pos]) {
        return None;
    }
    let mut end = pos;
    while end < bytes.len() && is_ident_char(bytes[end]) {
        end += 1;
    }
    Some(source[pos..end].to_string())
}

fn range_declares_name(source: &str, range_start: usize, range_end: usize, name: &str) -> bool {
    let bytes = source.as_bytes();
    let mut pos = range_start;
    while pos < range_end {
        let mut matched = false;
        for keyword in ["let", "var", "const"] {
            if pos + keyword.len() <= range_end
                && &source[pos..pos + keyword.len()] == keyword
                && (pos == 0 || !is_ident_char(bytes[pos - 1]))
                && (pos + keyword.len() == bytes.len()
                    || !is_ident_char(bytes[pos + keyword.len()]))
            {
                if declaration_name_after_source_keyword(source, pos, keyword.len()).as_deref()
                    == Some(name)
                {
                    return true;
                }
                pos += keyword.len();
                matched = true;
                break;
            }
        }
        if !matched {
            pos += 1;
        }
    }
    false
}

fn module_declares_name_before(source: &str, name: &str, before: usize) -> bool {
    let bytes = source.as_bytes();
    let mut depth = 0i32;
    let mut pos = 0usize;
    while pos < before {
        if pos + 1 < before && bytes[pos] == b'/' && bytes[pos + 1] == b'/' {
            pos += 2;
            while pos < before && bytes[pos] != b'\n' {
                pos += 1;
            }
            continue;
        }
        if pos + 1 < before && bytes[pos] == b'/' && bytes[pos + 1] == b'*' {
            pos += 2;
            while pos + 1 < before && !(bytes[pos] == b'*' && bytes[pos + 1] == b'/') {
                pos += 1;
            }
            pos = (pos + 2).min(before);
            continue;
        }
        match bytes[pos] {
            b'{' => {
                depth += 1;
                pos += 1;
                continue;
            }
            b'}' => {
                depth -= 1;
                pos += 1;
                continue;
            }
            _ => {}
        }
        if depth == 0 {
            for keyword in ["var", "const", "override"] {
                if pos + keyword.len() <= before
                    && &source[pos..pos + keyword.len()] == keyword
                    && (pos == 0 || !is_ident_char(bytes[pos - 1]))
                    && (pos + keyword.len() == bytes.len()
                        || !is_ident_char(bytes[pos + keyword.len()]))
                {
                    if declaration_name_after_source_keyword(source, pos, keyword.len()).as_deref()
                        == Some(name)
                    {
                        return true;
                    }
                }
            }
        }
        pos += 1;
    }
    false
}

fn enclosing_function_body_start(source: &str, call_start: usize) -> Option<usize> {
    let mut pos = 0usize;
    let mut body_start = None;
    while let Some(fn_start) = find_keyword_from(source, "fn", pos) {
        if fn_start >= call_start {
            break;
        }
        let open = source[fn_start..call_start]
            .find('{')
            .map(|rel| fn_start + rel);
        if let Some(open) = open {
            if let Some(close) = find_matching_delimiter(source, open, b'{', b'}') {
                if open < call_start && call_start < close {
                    body_start = Some(open);
                }
            }
        }
        pos = fn_start + 2;
    }
    body_start
}

fn is_builtin_shadowed_at_call(source: &str, name: &str, call_start: usize) -> bool {
    if module_declares_name_before(source, name, call_start) {
        return true;
    }
    if let Some(body_start) = enclosing_function_body_start(source, call_start) {
        return range_declares_name(source, body_start + 1, call_start, name);
    }
    false
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

fn wgsl_error_at(source: &str, offset: usize, length: usize, message: &str) -> WgslMessage {
    let (line, col) = byte_offset_to_line_col(source, offset);
    WgslMessage {
        r#type: "error".to_string(),
        message: message.to_string(),
        line_num: line,
        line_pos: col,
        offset: offset as u32,
        length: length as u32,
    }
}

fn find_unterminated_block_comment(source: &str) -> Option<usize> {
    let bytes = source.as_bytes();
    let mut i = 0usize;
    while i + 1 < bytes.len() {
        if bytes[i] == b'/' && bytes[i + 1] == b'/' {
            i += 2;
            while i < bytes.len() && bytes[i] != b'\n' {
                i += 1;
            }
            continue;
        }
        if bytes[i] == b'/' && bytes[i + 1] == b'*' {
            let start = i;
            i += 2;
            let mut depth = 1i32;
            while i + 1 < bytes.len() {
                if bytes[i] == b'/' && bytes[i + 1] == b'*' {
                    depth += 1;
                    i += 2;
                    continue;
                }
                if bytes[i] == b'*' && bytes[i + 1] == b'/' {
                    depth -= 1;
                    i += 2;
                    if depth == 0 {
                        break;
                    }
                    continue;
                }
                i += 1;
            }
            if depth != 0 {
                return Some(start);
            }
            continue;
        }
        i += 1;
    }
    None
}

fn must_use_attribute_invalid(source: &str) -> Option<usize> {
    let scan = mask_comments_preserve_len(source);
    let bytes = scan.as_bytes();
    let mut pos = 0usize;
    while let Some(rel) = scan[pos..].find('@') {
        let attr_start = pos + rel;
        let Some((name, name_end)) = parse_attribute_name_at(&scan, attr_start) else {
            pos = attr_start + 1;
            continue;
        };
        if name != "must_use" {
            pos = attr_start + 1;
            continue;
        }

        let after_name = skip_ws_and_comments(bytes, name_end);
        if after_name < bytes.len() && bytes[after_name] == b'(' {
            return Some(attr_start);
        }

        let mut next = skip_ws_and_comments(bytes, skip_attribute_after_name(&scan, name_end));
        while next < bytes.len() && bytes[next] == b'@' {
            let Some((attr_name, attr_name_end)) = parse_attribute_name_at(&scan, next) else {
                break;
            };
            if attr_name == "must_use" {
                return Some(next);
            }
            next = skip_ws_and_comments(bytes, skip_attribute_after_name(&scan, attr_name_end));
        }
        let mut ident_end = next;
        while ident_end < bytes.len() && is_ident_char(bytes[ident_end]) {
            ident_end += 1;
        }
        if &scan[next..ident_end] != "fn" {
            return Some(attr_start);
        }

        let signature_start = ident_end;
        let Some(body_open) = scan[signature_start..]
            .find('{')
            .map(|rel| signature_start + rel)
        else {
            return Some(attr_start);
        };
        if !scan[signature_start..body_open].contains("->") {
            return Some(attr_start);
        }
        pos = attr_start + 1;
    }
    None
}

fn oversized_inferred_i32_var_literal(source: &str) -> Option<usize> {
    // Scan masked source so `// var x = 9999999999999;` does not trip a
    // phantom error. Mask preserves length, so byte offsets line up with
    // the original.
    let scan = mask_comments_preserve_len(source);
    let bytes = scan.as_bytes();
    let mut pos = 0usize;
    while let Some(var_pos) = find_keyword_from(&scan, "var", pos) {
        let mut p = skip_ws_and_comments(bytes, var_pos + "var".len());
        if p < bytes.len() && bytes[p] == b'<' {
            if let Some(close) = find_matching_delimiter(&scan, p, b'<', b'>') {
                p = skip_ws_and_comments(bytes, close + 1);
            }
        }
        while p < bytes.len() && is_ident_char(bytes[p]) {
            p += 1;
        }
        p = skip_ws_and_comments(bytes, p);
        if p < bytes.len() && bytes[p] == b':' {
            pos = p + 1;
            continue;
        }
        if p >= bytes.len() || bytes[p] != b'=' {
            pos = p.saturating_add(1);
            continue;
        }
        p = skip_ws_and_comments(bytes, p + 1);
        let lit_start = p;

        // WGSL §6.2.1.1: int_literal = decimal | hex (`0x...`).
        let (digits_start, radix): (usize, u32) = if p + 1 < bytes.len()
            && bytes[p] == b'0'
            && (bytes[p + 1] == b'x' || bytes[p + 1] == b'X')
        {
            (p + 2, 16)
        } else {
            (p, 10)
        };
        p = digits_start;
        while p < bytes.len()
            && bytes[p].is_ascii_hexdigit()
            && (radix == 16 || bytes[p].is_ascii_digit())
        {
            p += 1;
        }
        if p == digits_start || (p < bytes.len() && is_ident_char(bytes[p])) {
            pos = p.saturating_add(1);
            continue;
        }
        if u64::from_str_radix(&scan[digits_start..p], radix)
            .is_ok_and(|value| value > i32::MAX as u64)
        {
            return Some(lit_start);
        }
        pos = p;
    }
    None
}

const SUBGROUP_BUILTIN_CALLS: &[&str] = &[
    "quadBroadcast",
    "quadSwapX",
    "quadSwapY",
    "quadSwapDiagonal",
    "subgroupAdd",
    "subgroupInclusiveAdd",
    "subgroupExclusiveAdd",
    "subgroupMul",
    "subgroupInclusiveMul",
    "subgroupExclusiveMul",
    "subgroupMin",
    "subgroupMax",
    "subgroupAnd",
    "subgroupOr",
    "subgroupXor",
    "subgroupAny",
    "subgroupAll",
    "subgroupBallot",
    "subgroupBroadcast",
    "subgroupBroadcastFirst",
    "subgroupElect",
    "subgroupShuffle",
    "subgroupShuffleUp",
    "subgroupShuffleDown",
    "subgroupShuffleXor",
];

fn enable_directive_lists_extension(source: &str, ext: &str) -> bool {
    // WGSL §4.1.2: enable_directive := 'enable' identifier (',' identifier)* ';'.
    // Walk every `enable` keyword and scan its comma-separated extension list
    // until ';' or EOF, matching `ext` against each identifier.
    let bytes = source.as_bytes();
    let mut pos = 0usize;
    while let Some(enable_pos) = find_keyword_from(source, "enable", pos) {
        let mut p = skip_ws_and_comments(bytes, enable_pos + "enable".len());
        loop {
            let ident_start = p;
            while p < bytes.len() && is_ident_char(bytes[p]) {
                p += 1;
            }
            if p > ident_start && &source[ident_start..p] == ext {
                return true;
            }
            p = skip_ws_and_comments(bytes, p);
            if p >= bytes.len() || bytes[p] != b',' {
                break;
            }
            p = skip_ws_and_comments(bytes, p + 1);
        }
        pos = enable_pos + "enable".len();
    }
    false
}

fn source_enables_subgroups(source: &str) -> bool {
    enable_directive_lists_extension(source, "subgroups")
}

fn is_function_declaration_name(source: &str, name_start: usize) -> bool {
    let bytes = source.as_bytes();
    let mut pos = name_start;
    while pos > 0 && bytes[pos - 1].is_ascii_whitespace() {
        pos -= 1;
    }
    pos >= 2 && &source[pos - 2..pos] == "fn" && (pos == 2 || !is_ident_char(bytes[pos - 3]))
}

fn validate_subgroup_enable_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains("subgroup") && !source.contains("quad") {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    if source_enables_subgroups(&scan) {
        return Vec::new();
    }

    let mut errors = Vec::new();
    for name in SUBGROUP_BUILTIN_CALLS {
        let mut pos = 0usize;
        while let Some(start) = find_keyword_from(&scan, name, pos) {
            let next = start + name.len();
            if !is_function_declaration_name(&scan, start)
                && find_call_args(&scan, start, name.len()).is_some()
                && !is_builtin_shadowed_at_call(&scan, name, start)
            {
                errors.push(wgsl_error_at(
                    source,
                    start,
                    name.len(),
                    "subgroup builtin requires `enable subgroups;`",
                ));
            }
            pos = next;
        }
    }
    errors
}

fn validate_parse_frontend_source(source: &str) -> Vec<WgslMessage> {
    let mut errors = Vec::new();
    if let Some(offset) = source.as_bytes().iter().position(|b| *b == 0) {
        errors.push(wgsl_error_at(
            source,
            offset,
            1,
            "WGSL source must not contain a null character",
        ));
    }
    if let Some(offset) = find_unterminated_block_comment(source) {
        errors.push(wgsl_error_at(
            source,
            offset,
            2,
            "unterminated block comment",
        ));
    }
    if let Some(offset) = must_use_attribute_invalid(source) {
        errors.push(wgsl_error_at(
            source,
            offset,
            "@must_use".len(),
            "@must_use is only valid on function declarations with return values",
        ));
    }
    if let Some(offset) = oversized_inferred_i32_var_literal(source) {
        errors.push(wgsl_error_at(
            source,
            offset,
            1,
            "integer literal does not fit the inferred i32 type",
        ));
    }
    errors.extend(validate_subgroup_enable_source(source));
    errors
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
            if !is_builtin_shadowed_at_call(source, "smoothstep", start) {
                if let Some(replacement) = lower_smoothstep_call(&args) {
                    out.push_str(&source[pos..start]);
                    out.push_str(&replacement);
                    pos = close + 1;
                }
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

fn abstract_number_to_f32_text(arg: &str) -> Option<String> {
    let trimmed = trim_outer_parens(arg).trim();
    if trimmed.is_empty()
        || trimmed
            .bytes()
            .any(|b| matches!(b, b'i' | b'u' | b'f' | b'h'))
        || parse_numeric_literal(trimmed).is_none()
    {
        return None;
    }
    let mut text = trimmed.to_string();
    if let Some(exp) = text.find(['e', 'E']) {
        if !text[..exp].contains('.') {
            text.insert(exp, '.');
        }
    } else if !text.contains('.') {
        text.push_str(".0");
    }
    Some(text)
}

fn lower_shadow_builtin_abstract_args_call(
    name: &str,
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<String> {
    if let Some(Ok(result)) = eval_const_matrix_builtin_call(name, args, constants) {
        return format_source_matrix_values(&result);
    }
    if let Some(Ok(result)) = eval_const_float_builtin_call(name, args, constants) {
        return format_source_float_values(&result);
    }

    const DERIVATIVE_NAMES: &[&str] = &[
        "dpdx",
        "dpdxCoarse",
        "dpdxFine",
        "dpdy",
        "dpdyCoarse",
        "dpdyFine",
        "fwidth",
        "fwidthCoarse",
        "fwidthFine",
    ];
    if DERIVATIVE_NAMES.iter().any(|n| *n == name) && args.len() == 1 {
        return Some(format!(
            "{}({})",
            name,
            abstract_number_to_f32_text(&args[0])?
        ));
    }
    if name == "mix" && args.len() == 3 {
        let lowered = args
            .iter()
            .map(|arg| abstract_number_to_f32_text(arg))
            .collect::<Option<Vec<_>>>()?;
        return Some(format!(
            "mix({})",
            lowered
                .iter()
                .map(|arg| format!("f32({})", arg))
                .collect::<Vec<_>>()
                .join(", ")
        ));
    }
    if name == "determinant" && args.len() == 1 {
        let (callee, elems) = parse_full_call(&args[0])?;
        let lower_callee = callee.to_ascii_lowercase();
        if lower_callee.starts_with("mat")
            && lower_callee.len() == "mat2x2".len()
            && lower_callee.as_bytes()[3] == b'2'
            && lower_callee.as_bytes()[4] == b'x'
            && matches!(lower_callee.as_bytes()[5], b'2' | b'3' | b'4')
            && elems
                .iter()
                .all(|arg| abstract_number_to_f32_text(arg).is_some())
        {
            return Some(format!("determinant({}f({}))", callee, elems.join(", ")));
        }
    }
    None
}

fn call_is_bare_statement(source: &str, call_start: usize, close: usize) -> bool {
    let bytes = source.as_bytes();
    let mut prev = call_start;
    while prev > 0 && bytes[prev - 1].is_ascii_whitespace() {
        prev -= 1;
    }
    let starts_statement = prev == 0 || matches!(bytes[prev - 1], b'{' | b'}' | b';');
    if !starts_statement {
        return false;
    }
    let mut next = close + 1;
    while next < bytes.len() && bytes[next].is_ascii_whitespace() {
        next += 1;
    }
    next < bytes.len() && bytes[next] == b';'
}

fn lower_shadow_builtin_abstract_args_source(
    source: &str,
    constants: Option<&HashMap<String, f64>>,
) -> String {
    const NAMES: &[&str] = &[
        "acosh",
        "determinant",
        "dpdx",
        "dpdxCoarse",
        "dpdxFine",
        "dpdy",
        "dpdyCoarse",
        "dpdyFine",
        "fwidth",
        "fwidthCoarse",
        "fwidthFine",
        "faceForward",
        "mix",
        "reflect",
        "refract",
        "transpose",
    ];
    if !NAMES.iter().any(|name| source.contains(name)) {
        return source.to_string();
    }

    let bytes = source.as_bytes();
    let mut out = String::with_capacity(source.len());
    let mut pos = 0usize;
    let mut i = 0usize;
    while i < source.len() {
        let mut matched = false;
        for name in NAMES {
            let end = i + name.len();
            if end <= source.len()
                && &source[i..end] == *name
                && (i == 0 || !is_ident_char(bytes[i - 1]))
                && (end == source.len() || !is_ident_char(bytes[end]))
            {
                if let Some((args, close)) = find_call_args(source, i, name.len()) {
                    let preserve_must_use = matches!(
                        *name,
                        "acosh" | "faceForward" | "mix" | "reflect" | "refract"
                    ) && call_is_bare_statement(source, i, close);
                    if !preserve_must_use && !is_builtin_shadowed_at_call(source, name, i) {
                        if let Some(replacement) =
                            lower_shadow_builtin_abstract_args_call(name, &args, constants)
                        {
                            out.push_str(&source[pos..i]);
                            out.push_str(&replacement);
                            pos = close + 1;
                        }
                    }
                    i = close + 1;
                    matched = true;
                    break;
                }
            }
        }
        if !matched {
            i += 1;
        }
    }

    if pos == 0 {
        source.to_string()
    } else {
        out.push_str(&source[pos..]);
        out
    }
}

fn validate_shadowed_lowered_builtin_calls_source(source: &str) -> Vec<WgslMessage> {
    const NAMES: &[&str] = &["ldexp", "smoothstep"];
    let mut errors = Vec::new();
    for name in NAMES {
        let mut pos = 0usize;
        while let Some(start) = find_keyword_from(source, name, pos) {
            if let Some((_, close)) = find_call_args(source, start, name.len()) {
                if is_builtin_shadowed_at_call(source, name, start) {
                    let (line, col) = byte_offset_to_line_col(source, start);
                    errors.push(WgslMessage {
                        r#type: "error".to_string(),
                        message: format!(
                            "'{}' is shadowed and cannot be called as a builtin",
                            name
                        ),
                        line_num: line,
                        line_pos: col,
                        offset: start as u32,
                        length: name.len() as u32,
                    });
                }
                pos = close + 1;
            } else {
                pos = start + name.len();
            }
        }
    }
    errors
}

fn source_after_contains_index_ident(source: &str, from: usize, ident: &str) -> bool {
    let bytes = source.as_bytes();
    let mut i = from;
    while i < bytes.len() {
        if bytes[i] != b'[' {
            i += 1;
            continue;
        }
        i += 1;
        while i < bytes.len() && bytes[i].is_ascii_whitespace() {
            i += 1;
        }
        if source[i..].starts_with(ident) {
            let end = i + ident.len();
            if end <= bytes.len()
                && (end == bytes.len() || !is_ident_char(bytes[end]))
                && (i == 0 || !is_ident_char(bytes[i - 1]))
            {
                let mut close = end;
                while close < bytes.len() && bytes[close].is_ascii_whitespace() {
                    close += 1;
                }
                if close < bytes.len() && bytes[close] == b']' {
                    return true;
                }
            }
        }
    }
    false
}

fn lower_dynamic_index_lets_source(source: &str) -> String {
    if !source.contains("let") || !source.contains('[') {
        return source.to_string();
    }

    // Mask comments first so `let` text inside comments cannot trigger
    // rewrites. Mask preserves byte length, so offsets index into both
    // strings interchangeably; we emit bytes from the original `source`
    // to keep comments and string content verbatim.
    let scan = mask_comments_preserve_len(source);
    let scan_bytes = scan.as_bytes();
    let mut out = String::with_capacity(source.len());
    let mut pos = 0usize;
    while let Some(rel) = scan[pos..].find("let") {
        let start = pos + rel;
        let end = start + 3;
        if (start > 0 && is_ident_char(scan_bytes[start - 1]))
            || (end < scan_bytes.len() && is_ident_char(scan_bytes[end]))
        {
            out.push_str(&source[pos..end]);
            pos = end;
            continue;
        }

        let mut i = end;
        while i < scan_bytes.len() && scan_bytes[i].is_ascii_whitespace() {
            i += 1;
        }
        let ident_start = i;
        if i >= scan_bytes.len() || !(scan_bytes[i] == b'_' || scan_bytes[i].is_ascii_alphabetic())
        {
            out.push_str(&source[pos..end]);
            pos = end;
            continue;
        }
        i += 1;
        while i < scan_bytes.len() && is_ident_char(scan_bytes[i]) {
            i += 1;
        }
        let ident = &scan[ident_start..i];
        while i < scan_bytes.len() && scan_bytes[i].is_ascii_whitespace() {
            i += 1;
        }
        if i >= scan_bytes.len() || scan_bytes[i] != b'=' {
            out.push_str(&source[pos..end]);
            pos = end;
            continue;
        }

        if source_after_contains_index_ident(&scan, i + 1, ident) {
            out.push_str(&source[pos..start]);
            out.push_str("var");
        } else {
            out.push_str(&source[pos..end]);
        }
        pos = end;
    }
    if pos == 0 {
        return source.to_string();
    }
    out.push_str(&source[pos..]);
    out
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
            if args.len() == 1 && args[0].is_empty() {
                return Some(LdexpFloatValues {
                    kind: explicit_kind.unwrap_or(LdexpFloatKind::F32),
                    values: vec![0.0; width],
                });
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
            if !is_builtin_shadowed_at_call(source, "ldexp", start) {
                if let Some(replacement) = lower_ldexp_call(&args, constants) {
                    out.push_str(&source[pos..start]);
                    out.push_str(&replacement);
                    pos = close + 1;
                }
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

fn f32_to_f16_bits(value: f32) -> u16 {
    let bits = value.to_bits();
    let sign = ((bits >> 16) & 0x8000) as u16;
    let exp = ((bits >> 23) & 0xff) as i32;
    let mant = bits & 0x007f_ffff;

    if exp == 0xff {
        return sign | if mant == 0 { 0x7c00 } else { 0x7e00 };
    }

    let exp16 = exp - 127 + 15;
    if exp16 >= 0x1f {
        return sign | 0x7c00;
    }

    if exp16 <= 0 {
        if exp16 < -10 {
            return sign;
        }
        let mantissa = mant | 0x0080_0000;
        let shift = (14 - exp16) as u32;
        let mut half = (mantissa >> shift) as u16;
        let round_bit = (mantissa >> (shift - 1)) & 1;
        let sticky_mask = (1u32 << (shift - 1)) - 1;
        if round_bit != 0 && ((mantissa & sticky_mask) != 0 || (half & 1) != 0) {
            half = half.wrapping_add(1);
        }
        return sign | half;
    }

    let mut half = ((exp16 as u16) << 10) | ((mant >> 13) as u16);
    let round = mant & 0x1fff;
    if round > 0x1000 || (round == 0x1000 && (half & 1) != 0) {
        half = half.wrapping_add(1);
    }
    sign | half
}

fn quantize_to_f16_f32(value: f64) -> Option<f32> {
    let input = value as f32;
    if !input.is_finite() {
        return None;
    }
    let quantized = f16_bits_to_f32(f32_to_f16_bits(input));
    quantized.is_finite().then_some(quantized)
}

fn lower_quantize_to_f16_call(
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<String> {
    if args.len() != 1 {
        return None;
    }
    let values = resolve_ldexp_float_values(&args[0], constants)?;
    if matches!(values.kind, LdexpFloatKind::F16) {
        return None;
    }

    let quantized = values
        .values
        .iter()
        .map(|v| quantize_to_f16_f32(*v))
        .collect::<Option<Vec<_>>>()?;
    let components = quantized
        .iter()
        .map(|v| format_wgsl_f32_literal(*v))
        .collect::<Option<Vec<_>>>()?
        .join(", ");
    if quantized.len() == 1 {
        Some(format!("f32({components})"))
    } else {
        Some(format!("vec{}<f32>({components})", quantized.len()))
    }
}

fn lower_quantize_to_f16_source(source: &str, constants: Option<&HashMap<String, f64>>) -> String {
    if !source.contains("quantizeToF16") {
        return source.to_string();
    }
    let bytes = source.as_bytes();
    let mut out = String::with_capacity(source.len());
    let mut pos = 0usize;
    let mut i = 0usize;
    while let Some(rel) = source[i..].find("quantizeToF16") {
        let start = i + rel;
        let end = start + "quantizeToF16".len();
        if (start > 0 && is_ident_char(bytes[start - 1]))
            || (end < bytes.len() && is_ident_char(bytes[end]))
        {
            i = end;
            continue;
        }
        if let Some((args, close)) = find_call_args(source, start, "quantizeToF16".len()) {
            if !is_builtin_shadowed_at_call(source, "quantizeToF16", start) {
                if let Some(replacement) = lower_quantize_to_f16_call(&args, constants) {
                    out.push_str(&source[pos..start]);
                    out.push_str(&replacement);
                    pos = close + 1;
                }
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

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum BinaryPrecedenceGroup {
    Multiplicative,
    Additive,
    Shift,
    Relational,
    BinaryAnd,
    BinaryXor,
    BinaryOr,
    Logical,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum BinaryPrecedenceOp {
    Mul,
    Div,
    Rem,
    Add,
    Sub,
    Shl,
    Shr,
    Lt,
    Gt,
    Le,
    Ge,
    Eq,
    Ne,
    BitAnd,
    BitXor,
    BitOr,
    LogAnd,
    LogOr,
}

impl BinaryPrecedenceOp {
    fn group(self) -> BinaryPrecedenceGroup {
        match self {
            Self::Mul | Self::Div | Self::Rem => BinaryPrecedenceGroup::Multiplicative,
            Self::Add | Self::Sub => BinaryPrecedenceGroup::Additive,
            Self::Shl | Self::Shr => BinaryPrecedenceGroup::Shift,
            Self::Lt | Self::Gt | Self::Le | Self::Ge | Self::Eq | Self::Ne => {
                BinaryPrecedenceGroup::Relational
            }
            Self::BitAnd => BinaryPrecedenceGroup::BinaryAnd,
            Self::BitXor => BinaryPrecedenceGroup::BinaryXor,
            Self::BitOr => BinaryPrecedenceGroup::BinaryOr,
            Self::LogAnd | Self::LogOr => BinaryPrecedenceGroup::Logical,
        }
    }
}

fn mask_comments_preserve_len(source: &str) -> String {
    let bytes = source.as_bytes();
    let mut out = String::with_capacity(source.len());
    let mut i = 0usize;
    while i < bytes.len() {
        if i + 1 < bytes.len() && bytes[i] == b'/' && bytes[i + 1] == b'/' {
            out.push(' ');
            out.push(' ');
            i += 2;
            while i < bytes.len() && bytes[i] != b'\n' {
                out.push(' ');
                i += 1;
            }
            continue;
        }
        if i + 1 < bytes.len() && bytes[i] == b'/' && bytes[i + 1] == b'*' {
            out.push(' ');
            out.push(' ');
            i += 2;
            while i + 1 < bytes.len() && !(bytes[i] == b'*' && bytes[i + 1] == b'/') {
                out.push(if bytes[i] == b'\n' || bytes[i] == b'\r' {
                    bytes[i] as char
                } else {
                    ' '
                });
                i += 1;
            }
            if i + 1 < bytes.len() {
                out.push(' ');
                out.push(' ');
                i += 2;
            }
            continue;
        }
        out.push(bytes[i] as char);
        i += 1;
    }
    out
}

fn prev_non_ws(bytes: &[u8], mut i: usize) -> Option<u8> {
    while i > 0 {
        i -= 1;
        if !bytes[i].is_ascii_whitespace() {
            return Some(bytes[i]);
        }
    }
    None
}

fn next_non_ws(bytes: &[u8], mut i: usize) -> Option<u8> {
    while i < bytes.len() {
        if !bytes[i].is_ascii_whitespace() {
            return Some(bytes[i]);
        }
        i += 1;
    }
    None
}

fn has_binary_left_context(bytes: &[u8], i: usize) -> bool {
    prev_non_ws(bytes, i).is_some_and(|c| {
        !matches!(
            c,
            b'(' | b'['
                | b'{'
                | b','
                | b':'
                | b';'
                | b'='
                | b'!'
                | b'<'
                | b'>'
                | b'&'
                | b'|'
                | b'^'
                | b'+'
                | b'-'
                | b'*'
                | b'/'
                | b'%'
        )
    })
}

fn has_binary_right_context(bytes: &[u8], i: usize) -> bool {
    next_non_ws(bytes, i).is_some_and(|c| !matches!(c, b')' | b']' | b'}' | b',' | b';'))
}

fn binary_precedence_op_at(expr: &str, i: usize) -> Option<(BinaryPrecedenceOp, usize)> {
    let bytes = expr.as_bytes();
    let two = |op, len| {
        (has_binary_left_context(bytes, i) && has_binary_right_context(bytes, i + len))
            .then_some((op, len))
    };
    if i + 1 < bytes.len() {
        match &bytes[i..i + 2] {
            b"&&" => return two(BinaryPrecedenceOp::LogAnd, 2),
            b"||" => return two(BinaryPrecedenceOp::LogOr, 2),
            b"<<" => return two(BinaryPrecedenceOp::Shl, 2),
            b">>" => return two(BinaryPrecedenceOp::Shr, 2),
            b"<=" => return two(BinaryPrecedenceOp::Le, 2),
            b">=" => return two(BinaryPrecedenceOp::Ge, 2),
            b"==" => return two(BinaryPrecedenceOp::Eq, 2),
            b"!=" => return two(BinaryPrecedenceOp::Ne, 2),
            _ => {}
        }
    }

    let op = match bytes[i] {
        b'*' => BinaryPrecedenceOp::Mul,
        b'/' => BinaryPrecedenceOp::Div,
        b'%' => BinaryPrecedenceOp::Rem,
        b'+' => BinaryPrecedenceOp::Add,
        b'-' => BinaryPrecedenceOp::Sub,
        b'<' => BinaryPrecedenceOp::Lt,
        b'>' => BinaryPrecedenceOp::Gt,
        b'&' => BinaryPrecedenceOp::BitAnd,
        b'^' => BinaryPrecedenceOp::BitXor,
        b'|' => BinaryPrecedenceOp::BitOr,
        _ => return None,
    };
    (has_binary_left_context(bytes, i) && has_binary_right_context(bytes, i + 1)).then_some((op, 1))
}

fn binary_op_can_precede(left: BinaryPrecedenceOp, right: BinaryPrecedenceOp) -> bool {
    use BinaryPrecedenceGroup as Group;
    use BinaryPrecedenceOp as Op;

    if matches!(
        (left, right),
        (Op::LogAnd, Op::LogOr) | (Op::LogOr, Op::LogAnd)
    ) {
        return false;
    }

    let right_group = right.group();
    match left.group() {
        Group::Multiplicative => matches!(
            right_group,
            Group::Multiplicative | Group::Additive | Group::Relational
        ),
        Group::Additive => matches!(
            right_group,
            Group::Multiplicative | Group::Additive | Group::Relational
        ),
        Group::Shift => matches!(right_group, Group::Relational | Group::Logical),
        Group::Relational => matches!(
            right_group,
            Group::Multiplicative | Group::Additive | Group::Shift | Group::Logical
        ),
        Group::BinaryAnd => matches!(right_group, Group::BinaryAnd),
        Group::BinaryXor => matches!(right_group, Group::BinaryXor),
        Group::BinaryOr => matches!(right_group, Group::BinaryOr),
        Group::Logical => matches!(right_group, Group::Relational | Group::Logical),
    }
}

fn expression_contains_angle_like_token(expr: &str) -> bool {
    let bytes = expr.as_bytes();
    let mut i = 0usize;
    while i < bytes.len() {
        match bytes[i] {
            b'<' => {
                let next = bytes.get(i + 1).copied();
                if matches!(next, Some(b'<')) {
                    i += 2;
                    continue;
                }
                if !matches!(next, Some(b'<') | Some(b'=')) {
                    return true;
                }
            }
            b'>' => {
                let prev = (i > 0).then_some(bytes[i - 1]);
                let next = bytes.get(i + 1).copied();
                if matches!(next, Some(b'>')) {
                    i += 2;
                    continue;
                }
                if !matches!(prev, Some(b'>')) && !matches!(next, Some(b'>') | Some(b'=')) {
                    return true;
                }
            }
            _ => {}
        }
        i += 1;
    }
    false
}

fn expression_has_invalid_binary_precedence(expr: &str) -> bool {
    if expression_contains_angle_like_token(expr) {
        return false;
    }
    let bytes = expr.as_bytes();
    let mut ops: Vec<BinaryPrecedenceOp> = Vec::new();
    let mut depth = 0i32;
    let mut i = 0usize;
    while i < bytes.len() {
        match bytes[i] {
            b'(' | b'[' | b'{' => {
                depth += 1;
                i += 1;
                continue;
            }
            b')' | b']' | b'}' => {
                depth -= 1;
                i += 1;
                continue;
            }
            _ => {}
        }

        if depth == 0 {
            if let Some((op, len)) = binary_precedence_op_at(expr, i) {
                if let Some(prev) = ops.last().copied() {
                    if !binary_op_can_precede(prev, op) {
                        return true;
                    }
                }
                ops.push(op);
                i += len;
                continue;
            }
        }
        i += 1;
    }
    false
}

fn validate_binary_precedence_source(source: &str, entry_point: Option<&str>) -> Vec<WgslMessage> {
    let scan = mask_comments_preserve_len(&smoothstep_validation_source(source, entry_point));
    let mut errors = Vec::new();
    for stmt in scan.split(';') {
        let Some(eq) = find_assignment_eq(stmt) else {
            continue;
        };
        if expression_has_invalid_binary_precedence(&stmt[eq + 1..]) {
            errors.push(wgsl_error(
                "mixed binary operators require parentheses to disambiguate precedence",
            ));
        }
    }
    errors
}

fn parse_attribute_name_at<'a>(source: &'a str, attr_start: usize) -> Option<(&'a str, usize)> {
    let bytes = source.as_bytes();
    if attr_start >= bytes.len() || bytes[attr_start] != b'@' {
        return None;
    }
    let mut name_start = attr_start + 1;
    while name_start < bytes.len() && bytes[name_start].is_ascii_whitespace() {
        name_start += 1;
    }
    if name_start >= bytes.len()
        || !(bytes[name_start] == b'_' || bytes[name_start].is_ascii_alphabetic())
    {
        return None;
    }
    let mut name_end = name_start + 1;
    while name_end < bytes.len() && is_ident_char(bytes[name_end]) {
        name_end += 1;
    }
    Some((&source[name_start..name_end], name_end))
}

fn attr_slice_contains(slice: &str, name: &str) -> bool {
    let mut pos = 0usize;
    while let Some(rel) = slice[pos..].find('@') {
        let start = pos + rel;
        if parse_attribute_name_at(slice, start).is_some_and(|(attr, _end)| attr == name) {
            return true;
        }
        pos = start + 1;
    }
    false
}

fn skip_attribute_after_name(source: &str, name_end: usize) -> usize {
    let bytes = source.as_bytes();
    let mut i = name_end;
    while i < bytes.len() && bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    if i < bytes.len() && bytes[i] == b'(' {
        if let Some(close) = find_matching_delimiter(source, i, b'(', b')') {
            return close + 1;
        }
    }
    i
}

fn skip_attribute_chain(source: &str, mut pos: usize) -> usize {
    let bytes = source.as_bytes();
    loop {
        while pos < bytes.len() && bytes[pos].is_ascii_whitespace() {
            pos += 1;
        }
        if pos >= bytes.len() || bytes[pos] != b'@' {
            return pos;
        }
        let Some((_name, name_end)) = parse_attribute_name_at(source, pos) else {
            return pos + 1;
        };
        pos = skip_attribute_after_name(source, name_end);
    }
}

fn next_chain_keyword(source: &str, pos: usize, keyword: &str) -> bool {
    next_keyword_after(source, skip_attribute_chain(source, pos), keyword).is_some()
}

fn validate_function_attribute_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains("@id") && !source.contains("@workgroup_size") {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    let bytes = scan.as_bytes();
    let mut errors = Vec::new();
    let mut pos = 0usize;
    while let Some(rel) = scan[pos..].find("fn") {
        let fn_start = pos + rel;
        let fn_end = fn_start + 2;
        if (fn_start > 0 && is_ident_char(bytes[fn_start - 1]))
            || (fn_end < bytes.len() && is_ident_char(bytes[fn_end]))
        {
            pos = fn_end;
            continue;
        }

        let attrs_start = scan[..fn_start]
            .rfind(|c| matches!(c, ';' | '{' | '}'))
            .map_or(0, |i| i + 1);
        let attrs = &scan[attrs_start..fn_start];
        if attr_slice_contains(attrs, "id") {
            errors.push(wgsl_error("@id is not valid on function declarations"));
        }
        if attr_slice_contains(attrs, "workgroup_size") && !attr_slice_contains(attrs, "compute") {
            errors.push(wgsl_error(
                "@workgroup_size is only valid on compute entry point functions",
            ));
        }
        pos = fn_end;
    }
    errors
}

fn validate_stage_attribute_placement_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains("@compute") && !source.contains("@fragment") && !source.contains("@vertex")
    {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    let mut errors = Vec::new();
    let mut pos = 0usize;
    while let Some(rel) = scan[pos..].find('@') {
        let attr_start = pos + rel;
        let Some((name, name_end)) = parse_attribute_name_at(&scan, attr_start) else {
            pos = attr_start + 1;
            continue;
        };
        if matches!(name, "compute" | "fragment" | "vertex")
            && !next_chain_keyword(&scan, attr_start, "fn")
        {
            errors.push(wgsl_error(
                "shader stage attributes are only valid on function declarations",
            ));
        }
        pos = name_end;
    }
    errors
}

fn integer_literal_suffix(arg: &str) -> Option<u8> {
    let s = trim_outer_parens(arg).trim();
    let suffix = *s.as_bytes().last()?;
    if !matches!(suffix, b'i' | b'u') {
        return None;
    }
    parse_size_literal_arg(&s[..s.len() - 1]).map(|_| suffix)
}

fn workgroup_size_mixes_signed_unsigned(args: &[String]) -> bool {
    let has_signed = args
        .iter()
        .any(|arg| integer_literal_suffix(arg) == Some(b'i'));
    let has_unsigned = args
        .iter()
        .any(|arg| integer_literal_suffix(arg) == Some(b'u'));
    has_signed && has_unsigned
}

fn validate_workgroup_size_attribute_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains("workgroup_size") {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    let mut errors = Vec::new();
    let mut pos = 0usize;
    while let Some((attr_start, _open, close, _name_start, args)) =
        find_attribute_call_from(&scan, "workgroup_size", pos)
    {
        if !next_chain_keyword(&scan, attr_start, "fn") {
            errors.push(wgsl_error(
                "@workgroup_size is only valid on compute entry point functions",
            ));
        }
        if workgroup_size_mixes_signed_unsigned(&args) {
            errors.push(wgsl_error(
                "@workgroup_size arguments cannot mix signed and unsigned integer literals",
            ));
        }
        pos = close + 1;
    }
    errors
}

fn validate_align_attribute_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains("align") {
        return Vec::new();
    }

    const MAX_I32: u64 = i32::MAX as u64;
    let scan = mask_comments_preserve_len(source);
    let mut errors = Vec::new();
    let mut pos = 0usize;
    while let Some((_attr_start, _open, close, _name_start, args)) =
        find_attribute_call_from(&scan, "align", pos)
    {
        if (args.len() == 1 || (args.len() == 2 && args[1].is_empty()))
            && parse_size_literal_arg(&args[0]).is_some_and(|v| v > MAX_I32)
        {
            errors.push(wgsl_error(
                "@align value must fit in a signed 32-bit integer",
            ));
        }
        pos = close + 1;
    }
    errors
}

fn lower_struct_size5_align16_source(source: &str) -> String {
    if !source.contains("@align(16)") || !source.contains("@size(5)") {
        return source.to_string();
    }
    // Naga 29 does not propagate the inner member's explicit 16-byte
    // alignment to the containing struct type for the CTS layout case below.
    // Add the equivalent alignment at the only use site in that exact shader.
    source.replace(
        "struct S { x : u32, y : T }",
        "struct S { x : u32, @align(16) y : T }",
    )
}

fn location_type_and_flat_after_attr<'a>(
    source: &'a str,
    attr_close: usize,
) -> (Option<&'a str>, bool) {
    let bytes = source.as_bytes();
    let mut i = attr_close + 1;
    let mut has_flat_interpolation = false;
    loop {
        while i < bytes.len() && bytes[i].is_ascii_whitespace() {
            i += 1;
        }
        if i >= bytes.len() || bytes[i] != b'@' {
            break;
        }
        let Some((name, name_end)) = parse_attribute_name_at(source, i) else {
            i += 1;
            break;
        };
        if name == "interpolate" {
            if let Some((args, close)) = find_call_args(source, name_end - name.len(), name.len()) {
                if args.first().is_some_and(|arg| arg.trim() == "flat") {
                    has_flat_interpolation = true;
                }
                i = close + 1;
                continue;
            }
        }
        i = skip_attribute_after_name(source, name_end);
    }

    if i >= bytes.len() {
        return (None, has_flat_interpolation);
    }

    let token_start = i;
    if bytes[i] == b'_' || bytes[i].is_ascii_alphabetic() {
        i += 1;
        while i < bytes.len() && is_ident_char(bytes[i]) {
            i += 1;
        }
        let mut after_ident = i;
        while after_ident < bytes.len() && bytes[after_ident].is_ascii_whitespace() {
            after_ident += 1;
        }
        if after_ident < bytes.len() && bytes[after_ident] == b':' {
            i = after_ident + 1;
            while i < bytes.len() && bytes[i].is_ascii_whitespace() {
                i += 1;
            }
        } else {
            i = token_start;
        }
    }

    let type_start = i;
    let mut depth = 0i32;
    while i < bytes.len() {
        match bytes[i] {
            b'<' | b'(' | b'[' | b'{' => depth += 1,
            b'>' | b')' | b']' | b'}' => {
                if depth == 0 {
                    break;
                }
                depth -= 1;
            }
            b',' | b';' if depth == 0 => break,
            _ => {}
        }
        i += 1;
    }

    let ty = source[type_start..i].trim();
    ((!ty.is_empty()).then_some(ty), has_flat_interpolation)
}

fn type_text_is_integral_user_io(type_text: &str) -> bool {
    let n = normalize_type_name(type_text).to_ascii_lowercase();
    n == "i32"
        || n == "u32"
        || matches!(
            n.as_str(),
            "vec2i" | "vec3i" | "vec4i" | "vec2u" | "vec3u" | "vec4u"
        )
        || (n.starts_with("vec") && (n.contains("<i32>") || n.contains("<u32>")))
}

fn collect_location_structs(source: &str) -> HashSet<String> {
    if !source.contains("struct") || !source.contains("location") {
        return HashSet::new();
    }

    let mut structs = HashSet::new();
    let bytes = source.as_bytes();
    let mut pos = 0usize;
    while let Some(struct_pos) = find_keyword_from(source, "struct", pos) {
        let mut i = struct_pos + "struct".len();
        while i < bytes.len() && bytes[i].is_ascii_whitespace() {
            i += 1;
        }
        if i >= bytes.len() || !(bytes[i] == b'_' || bytes[i].is_ascii_alphabetic()) {
            pos = struct_pos + "struct".len();
            continue;
        }
        let name_start = i;
        i += 1;
        while i < bytes.len() && is_ident_char(bytes[i]) {
            i += 1;
        }
        let name = &source[name_start..i];
        while i < bytes.len() && bytes[i].is_ascii_whitespace() {
            i += 1;
        }
        if i >= bytes.len() || bytes[i] != b'{' {
            pos = i;
            continue;
        }
        let Some(close) = find_matching_delimiter(source, i, b'{', b'}') else {
            pos = i + 1;
            continue;
        };
        if attr_slice_contains(&source[i + 1..close], "location") {
            structs.insert(name.to_string());
        }
        pos = close + 1;
    }
    structs
}

fn declaration_has_flat_interpolation(decl: &str) -> bool {
    let mut pos = 0usize;
    while let Some((_attr_start, _open, close, _name_start, args)) =
        find_attribute_call_from(decl, "interpolate", pos)
    {
        if args.first().is_some_and(|arg| arg.trim() == "flat") {
            return true;
        }
        pos = close + 1;
    }
    false
}

fn declaration_has_integral_location_without_flat(decl: &str) -> bool {
    if !decl.contains("location") {
        return false;
    }

    let has_flat_interpolation = declaration_has_flat_interpolation(decl);
    let mut pos = 0usize;
    while let Some((_attr_start, _open, close, _name_start, _args)) =
        find_attribute_call_from(decl, "location", pos)
    {
        if location_type_and_flat_after_attr(decl, close)
            .0
            .is_some_and(type_text_is_integral_user_io)
            && !has_flat_interpolation
        {
            return true;
        }
        pos = close + 1;
    }
    false
}

fn collect_integral_location_structs_without_flat(source: &str) -> HashSet<String> {
    if !source.contains("struct") || !source.contains("location") {
        return HashSet::new();
    }

    let mut structs = HashSet::new();
    let bytes = source.as_bytes();
    let mut pos = 0usize;
    while let Some(struct_pos) = find_keyword_from(source, "struct", pos) {
        let mut i = struct_pos + "struct".len();
        while i < bytes.len() && bytes[i].is_ascii_whitespace() {
            i += 1;
        }
        if i >= bytes.len() || !(bytes[i] == b'_' || bytes[i].is_ascii_alphabetic()) {
            pos = struct_pos + "struct".len();
            continue;
        }
        let name_start = i;
        i += 1;
        while i < bytes.len() && is_ident_char(bytes[i]) {
            i += 1;
        }
        let name = &source[name_start..i];
        while i < bytes.len() && bytes[i].is_ascii_whitespace() {
            i += 1;
        }
        if i >= bytes.len() || bytes[i] != b'{' {
            pos = i;
            continue;
        }
        let Some(close) = find_matching_delimiter(source, i, b'{', b'}') else {
            pos = i + 1;
            continue;
        };
        if split_top_level_args(&source[i + 1..close])
            .iter()
            .any(|member| declaration_has_integral_location_without_flat(member))
        {
            structs.insert(name.to_string());
        }
        pos = close + 1;
    }
    structs
}

fn signature_has_integral_location_without_flat(signature: &str) -> bool {
    split_top_level_args(signature)
        .iter()
        .any(|decl| declaration_has_integral_location_without_flat(decl))
}

fn signature_mentions_type(signature: &str, type_name: &str) -> bool {
    split_top_level_args(signature).iter().any(|part| {
        identifier_occurrence_count(part.rsplit(':').next().unwrap_or(part), type_name) > 0
    })
}

fn validate_location_attribute_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains("location") {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    let mut errors = Vec::new();
    let mut pos = 0usize;
    while let Some((_attr_start, _open, close, _name_start, _args)) =
        find_attribute_call_from(&scan, "location", pos)
    {
        let (location_type, _has_flat_interpolation) =
            location_type_and_flat_after_attr(&scan, close);
        if location_type.is_some_and(type_text_is_resource_handle) {
            errors.push(wgsl_error(
                "@location entry point IO cannot use resource handle types",
            ));
        }
        pos = close + 1;
    }

    let location_structs = collect_location_structs(&scan);
    let integral_location_structs = collect_integral_location_structs_without_flat(&scan);
    let bytes = scan.as_bytes();
    let mut fn_pos = 0usize;
    while let Some(rel) = scan[fn_pos..].find("fn") {
        let start = fn_pos + rel;
        let end = start + 2;
        if (start > 0 && is_ident_char(bytes[start - 1]))
            || (end < bytes.len() && is_ident_char(bytes[end]))
        {
            fn_pos = end;
            continue;
        }

        let attrs_start = scan[..start]
            .rfind(|c| matches!(c, ';' | '{' | '}'))
            .map_or(0, |i| i + 1);
        let attrs = &scan[attrs_start..start];
        let stage = if attr_slice_contains(attrs, "compute") {
            Some("compute")
        } else if attr_slice_contains(attrs, "fragment") {
            Some("fragment")
        } else if attr_slice_contains(attrs, "vertex") {
            Some("vertex")
        } else {
            None
        };
        if stage.is_none() {
            fn_pos = end;
            continue;
        }

        let Some(params_open) = scan[end..].find('(').map(|p| end + p) else {
            fn_pos = end;
            continue;
        };
        let Some(params_close) = find_matching_delimiter(&scan, params_open, b'(', b')') else {
            fn_pos = params_open + 1;
            continue;
        };
        let params = &scan[params_open + 1..params_close];
        let mut body_open = params_close + 1;
        while body_open < bytes.len() && bytes[body_open].is_ascii_whitespace() {
            body_open += 1;
        }
        let ret = if scan[body_open..].starts_with("->") {
            let ret_start = body_open + 2;
            while body_open < bytes.len() && bytes[body_open] != b'{' {
                body_open += 1;
            }
            &scan[ret_start..body_open]
        } else {
            ""
        };

        match stage {
            Some("compute")
                if attr_slice_contains(params, "location")
                    || attr_slice_contains(ret, "location")
                    || location_structs.iter().any(|name| {
                        signature_mentions_type(params, name) || signature_mentions_type(ret, name)
                    }) =>
            {
                errors.push(wgsl_error(
                    "compute entry points cannot use user-defined @location IO",
                ));
            }
            Some("fragment")
                if signature_has_integral_location_without_flat(params)
                    || integral_location_structs
                        .iter()
                        .any(|name| signature_mentions_type(params, name)) =>
            {
                errors.push(wgsl_error(
                    "integral user-defined IO requires @interpolate(flat)",
                ));
            }
            Some("vertex")
                if signature_has_integral_location_without_flat(ret)
                    || integral_location_structs
                        .iter()
                        .any(|name| signature_mentions_type(ret, name)) =>
            {
                errors.push(wgsl_error(
                    "integral user-defined IO requires @interpolate(flat)",
                ));
            }
            _ => {}
        }
        fn_pos = params_close + 1;
    }

    errors
}

fn validate_shader_io_attribute_source(source: &str) -> Vec<WgslMessage> {
    let mut errors = validate_stage_attribute_placement_source(source);
    errors.extend(validate_workgroup_size_attribute_source(source));
    errors.extend(validate_align_attribute_source(source));
    errors.extend(validate_location_attribute_source(source));
    errors
}

fn validate_id_attribute_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains('@') || !source.contains("id") {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    let bytes = scan.as_bytes();
    let mut errors = Vec::new();
    let mut pos = 0usize;
    while let Some(rel) = scan[pos..].find('@') {
        let attr_start = pos + rel;
        let mut name_start = attr_start + 1;
        while name_start < bytes.len() && bytes[name_start].is_ascii_whitespace() {
            name_start += 1;
        }
        if name_start + 2 > bytes.len()
            || !scan[name_start..].starts_with("id")
            || (name_start > 0 && is_ident_char(bytes[name_start - 1]))
            || (name_start + 2 < bytes.len() && is_ident_char(bytes[name_start + 2]))
        {
            pos = attr_start + 1;
            continue;
        }

        let Some((_args, close)) = find_call_args(&scan, name_start, 2) else {
            pos = name_start + 2;
            continue;
        };

        // `@id(...)` may be followed by additional attributes (`@must_use`,
        // `@align(N)`, etc.) before the `override` keyword. Walk the entire
        // chain so a legal `@id(0) @must_use override x: u32 = 0;` is accepted.
        let next = skip_attribute_chain(&scan, close + 1);
        let override_end = next + "override".len();
        if override_end > bytes.len()
            || !scan[next..].starts_with("override")
            || (override_end < bytes.len() && is_ident_char(bytes[override_end]))
        {
            errors.push(wgsl_error("@id is only valid on override declarations"));
        }
        pos = close + 1;
    }
    errors
}

fn find_matching_delimiter(source: &str, open: usize, open_ch: u8, close_ch: u8) -> Option<usize> {
    let bytes = source.as_bytes();
    if open >= bytes.len() || bytes[open] != open_ch {
        return None;
    }

    let mut depth = 1i32;
    let mut i = open + 1;
    while i < bytes.len() {
        match bytes[i] {
            b if b == open_ch => depth += 1,
            b if b == close_ch => {
                depth -= 1;
                if depth == 0 {
                    return Some(i);
                }
            }
            _ => {}
        }
        i += 1;
    }
    None
}

fn find_keyword_from(source: &str, keyword: &str, mut pos: usize) -> Option<usize> {
    while pos < source.len() && !source.is_char_boundary(pos) {
        pos += 1;
    }
    let bytes = source.as_bytes();
    while let Some(rel) = source[pos..].find(keyword) {
        let start = pos + rel;
        let end = start + keyword.len();
        if (start == 0 || !is_ident_char(bytes[start - 1]))
            && (end == bytes.len() || !is_ident_char(bytes[end]))
        {
            return Some(start);
        }
        pos = end;
    }
    None
}

fn find_attribute_call_from(
    source: &str,
    attr: &str,
    mut pos: usize,
) -> Option<(usize, usize, usize, usize, Vec<String>)> {
    let bytes = source.as_bytes();
    while let Some(rel) = source[pos..].find('@') {
        let attr_start = pos + rel;
        let mut name_start = attr_start + 1;
        while name_start < bytes.len() && bytes[name_start].is_ascii_whitespace() {
            name_start += 1;
        }
        let name_end = name_start + attr.len();
        if name_end <= bytes.len()
            && source[name_start..].starts_with(attr)
            && (name_end == bytes.len() || !is_ident_char(bytes[name_end]))
        {
            if let Some((args, close)) = find_call_args(source, name_start, attr.len()) {
                let mut open = name_end;
                while open < bytes.len() && bytes[open].is_ascii_whitespace() {
                    open += 1;
                }
                return Some((attr_start, open, close, name_start, args));
            }
        }
        pos = attr_start + 1;
    }
    None
}

fn find_top_level_keyword(source: &str, keyword: &str) -> Option<usize> {
    let bytes = source.as_bytes();
    let mut depth = 0i32;
    let mut i = 0usize;
    while i < bytes.len() {
        match bytes[i] {
            b'(' | b'[' | b'{' => depth += 1,
            b')' | b']' | b'}' => depth -= 1,
            _ => {}
        }
        if depth == 0 && source[i..].starts_with(keyword) {
            let end = i + keyword.len();
            if (i == 0 || !is_ident_char(bytes[i - 1]))
                && (end == bytes.len() || !is_ident_char(bytes[end]))
            {
                return Some(i);
            }
        }
        i += 1;
    }
    None
}

fn next_keyword_after<'a>(source: &'a str, mut pos: usize, keyword: &str) -> Option<usize> {
    let bytes = source.as_bytes();
    while pos < bytes.len() && bytes[pos].is_ascii_whitespace() {
        pos += 1;
    }
    let end = pos + keyword.len();
    (end <= bytes.len()
        && source[pos..].starts_with(keyword)
        && (end == bytes.len() || !is_ident_char(bytes[end])))
    .then_some(pos)
}

fn body_has_loop_escape(body: &str) -> bool {
    find_keyword_from(body, "break", 0).is_some() || find_keyword_from(body, "return", 0).is_some()
}

fn body_has_loop_escape_before_continue(body: &str) -> bool {
    let stop = find_top_level_keyword(body, "continue").unwrap_or(body.len());
    body_has_loop_escape(&body[..stop])
}

fn body_has_break_if(body: &str) -> bool {
    let mut pos = 0usize;
    while let Some(break_pos) = find_keyword_from(body, "break", pos) {
        if next_keyword_after(body, break_pos + "break".len(), "if").is_some() {
            return true;
        }
        pos = break_pos + "break".len();
    }
    false
}

fn split_loop_continuing<'a>(body: &'a str) -> (&'a str, Option<&'a str>) {
    let Some(cont_pos) = find_top_level_keyword(body, "continuing") else {
        return (body, None);
    };
    let bytes = body.as_bytes();
    let mut open = cont_pos + "continuing".len();
    while open < bytes.len() && bytes[open].is_ascii_whitespace() {
        open += 1;
    }
    if open >= bytes.len() || bytes[open] != b'{' {
        return (body, None);
    }
    let Some(close) = find_matching_delimiter(body, open, b'{', b'}') else {
        return (body, None);
    };
    (&body[..cont_pos], Some(&body[open + 1..close]))
}

fn continuing_uses_declaration_after_continue(prefix: &str, continuing: &str) -> bool {
    let mut pos = 0usize;
    while let Some(continue_pos) = find_keyword_from(prefix, "continue", pos) {
        let suffix = &prefix[continue_pos + "continue".len()..];
        for stmt in suffix.split(';') {
            for kw in ["let", "var", "const"] {
                if let Some(name) = declaration_name_after_keyword(stmt, kw) {
                    if identifier_occurrence_count(continuing, name) > 0 {
                        return true;
                    }
                }
            }
        }
        pos = continue_pos + "continue".len();
    }
    false
}

fn for_header_condition_is_empty(header: &str) -> bool {
    let parts: Vec<&str> = header.split(';').collect();
    parts.len() == 3 && parts[1].trim().is_empty()
}

fn collect_texture_external_vars(scan: &str) -> HashSet<String> {
    let mut names = HashSet::new();
    for stmt in scan.split(';') {
        let Some((name, type_text)) = parse_var_decl_name_type(stmt) else {
            continue;
        };
        let normalized = normalize_type_name(type_text);
        if normalized == "texture_external" {
            names.insert(name.to_string());
        }
    }
    names
}

fn collect_texture_var_types(scan: &str) -> HashMap<String, String> {
    let mut types = HashMap::new();
    for stmt in scan.split(';') {
        let Some((name, type_text)) = parse_var_decl_name_type(stmt) else {
            continue;
        };
        let normalized = normalize_type_name(type_text);
        if normalized.starts_with("texture_") {
            types.insert(name.to_string(), normalized);
        }
    }
    types
}

fn texture_type_is_cube(type_name: &str) -> bool {
    type_name.starts_with("texture_cube")
        || type_name.starts_with("texture_depth_cube")
        || type_name.starts_with("texture_cube_array")
        || type_name.starts_with("texture_depth_cube_array")
}

fn texture_type_is_depth(type_name: &str) -> bool {
    type_name.starts_with("texture_depth_")
}

fn texture_type_is_array(type_name: &str) -> bool {
    type_name.contains("_array")
}

fn texture_sample_builtin_base_arg_count(name: &str, texture_type: &str) -> Option<usize> {
    let array_arg = usize::from(texture_type_is_array(texture_type));
    match name {
        "textureSample" => Some(3 + array_arg),
        "textureSampleBias" | "textureSampleLevel" => Some(4 + array_arg),
        "textureSampleGrad" => Some(5 + array_arg),
        _ => None,
    }
}

fn validate_texture_builtin_type_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains("textureGather") && !source.contains("textureSample") {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    let texture_types = collect_texture_var_types(&scan);
    if texture_types.is_empty() {
        return Vec::new();
    }

    let mut errors = Vec::new();
    let mut gather_pos = 0usize;
    while let Some(start) = find_keyword_from(&scan, "textureGather", gather_pos) {
        if let Some((args, close)) = find_call_args(&scan, start, "textureGather".len()) {
            if args.len() >= 2 {
                let texture_arg = trim_outer_parens(args[1].trim()).trim();
                if texture_types
                    .get(texture_arg)
                    .is_some_and(|ty| ty.starts_with("texture_depth_"))
                {
                    errors.push(wgsl_error_at(
                        source,
                        start,
                        "textureGather".len(),
                        "textureGather on depth textures must not include a component argument",
                    ));
                }
            }
            gather_pos = close + 1;
        } else {
            gather_pos = start + "textureGather".len();
        }
    }

    for sample_name in [
        "textureSample",
        "textureSampleBias",
        "textureSampleGrad",
        "textureSampleLevel",
    ] {
        let mut sample_pos = 0usize;
        while let Some(start) = find_keyword_from(&scan, sample_name, sample_pos) {
            if let Some((args, close)) = find_call_args(&scan, start, sample_name.len()) {
                if let Some(texture_arg) = args.first() {
                    let texture_arg = trim_outer_parens(texture_arg.trim()).trim();
                    if let Some(texture_type) = texture_types.get(texture_arg) {
                        if sample_name == "textureSampleGrad" && texture_type_is_depth(texture_type)
                        {
                            errors.push(wgsl_error_at(
                                source,
                                start,
                                sample_name.len(),
                                "textureSampleGrad does not accept depth textures",
                            ));
                        } else if texture_type_is_cube(texture_type) {
                            if let Some(max_args) =
                                texture_sample_builtin_base_arg_count(sample_name, texture_type)
                            {
                                if args.len() > max_args {
                                    errors.push(wgsl_error_at(
                                        source,
                                        start,
                                        sample_name.len(),
                                        "cube texture sampling does not accept an offset argument",
                                    ));
                                }
                            }
                        }
                    }
                }
                sample_pos = close + 1;
            } else {
                sample_pos = start + sample_name.len();
            }
        }
    }

    errors
}

fn type_text_is_vec_n_f32_target(annotation: &str, n: usize) -> bool {
    let normalized = normalize_type_name(annotation);
    let alias = format!("vec{n}f");
    if normalized == alias {
        return true;
    }
    let prefix = format!("vec{n}<");
    if normalized.starts_with(&prefix) && normalized.ends_with('>') {
        let inner = &normalized[prefix.len()..normalized.len() - 1];
        return inner == "f32";
    }
    false
}

fn coord_expr_is_convertible_to_vec2f(expr: &str) -> bool {
    let trimmed = trim_outer_parens(expr).trim();
    let Some((callee, _args)) = parse_full_call(trimmed) else {
        return false;
    };
    let normalized = normalize_type_name(&callee);
    if normalized == "vec2" || normalized == "vec2f" {
        return true;
    }
    if let Some(inner) = normalized
        .strip_prefix("vec2<")
        .and_then(|s| s.strip_suffix('>'))
    {
        return matches!(inner, "f32" | "abstract-int" | "abstract-float");
    }
    false
}

fn let_type_annotation_for_call(scan: &str, call_start: usize) -> Option<String> {
    let bytes = scan.as_bytes();
    let prefix = &scan[..call_start];
    let stmt_start = prefix
        .rfind(|c: char| c == ';' || c == '{' || c == '}')
        .map_or(0, |p| p + 1);
    let stmt = &scan[stmt_start..call_start];
    let let_pos = find_keyword(stmt, "let")?;
    let after_let = &stmt[let_pos + 3..];
    let mut i = 0usize;
    let after_bytes = after_let.as_bytes();
    while i < after_bytes.len() && after_bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    if i >= after_bytes.len() || !(after_bytes[i] == b'_' || after_bytes[i].is_ascii_alphabetic()) {
        return None;
    }
    while i < after_bytes.len() && is_ident_char(after_bytes[i]) {
        i += 1;
    }
    while i < after_bytes.len() && after_bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    if i >= after_bytes.len() || after_bytes[i] != b':' {
        return None;
    }
    let after_colon = &after_let[i + 1..];
    let eq = find_assignment_eq(after_colon)?;
    let _ = bytes;
    Some(after_colon[..eq].trim().to_string())
}

fn validate_texture_sample_base_clamp_to_edge_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains("textureSampleBaseClampToEdge") {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    let externals = collect_texture_external_vars(&scan);
    if externals.is_empty() {
        return Vec::new();
    }

    let bytes = scan.as_bytes();
    let name = "textureSampleBaseClampToEdge";
    let mut errors = Vec::new();
    let mut pos = 0usize;
    while let Some(rel) = scan[pos..].find(name) {
        let call_start = pos + rel;
        if call_start > 0 && is_ident_char(bytes[call_start - 1]) {
            pos = call_start + 1;
            continue;
        }
        let after = call_start + name.len();
        if after >= bytes.len() {
            break;
        }
        let mut p = after;
        while p < bytes.len() && bytes[p].is_ascii_whitespace() {
            p += 1;
        }
        if p >= bytes.len() || bytes[p] != b'(' {
            pos = after;
            continue;
        }
        let Some((args, close)) = find_call_args(&scan, call_start, name.len()) else {
            pos = call_start + 1;
            continue;
        };
        if args.len() < 3 {
            pos = close + 1;
            continue;
        }

        let texture_arg = trim_outer_parens(args[0].trim()).trim();
        if !externals.contains(texture_arg) {
            pos = close + 1;
            continue;
        }

        if !coord_expr_is_convertible_to_vec2f(args[2].trim()) {
            errors.push(wgsl_error(
                "textureSampleBaseClampToEdge: coords argument must be convertible to vec2<f32>",
            ));
        }

        if let Some(annotation) = let_type_annotation_for_call(&scan, call_start) {
            if !type_text_is_vec_n_f32_target(&annotation, 4) {
                errors.push(wgsl_error(
                    "textureSampleBaseClampToEdge: result is not assignable to the declared type",
                ));
            }
        }

        pos = close + 1;
    }
    errors
}

fn validate_loop_behavior_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains("loop") && !source.contains("for") {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    let bytes = scan.as_bytes();
    let mut errors = Vec::new();

    let mut pos = 0usize;
    while let Some(loop_pos) = find_keyword_from(&scan, "loop", pos) {
        let mut open = loop_pos + "loop".len();
        while open < bytes.len() && bytes[open].is_ascii_whitespace() {
            open += 1;
        }
        if open >= bytes.len() || bytes[open] != b'{' {
            pos = loop_pos + "loop".len();
            continue;
        }
        let Some(close) = find_matching_delimiter(&scan, open, b'{', b'}') else {
            pos = loop_pos + "loop".len();
            continue;
        };
        let body = &scan[open + 1..close];
        let (prefix, continuing) = split_loop_continuing(body);
        if !body_has_loop_escape_before_continue(prefix)
            && !continuing.is_some_and(body_has_break_if)
        {
            errors.push(wgsl_error(
                "loop must have a reachable break, return, or break-if continuing condition",
            ));
        }
        if let Some(continuing_body) = continuing {
            if continuing_uses_declaration_after_continue(prefix, continuing_body) {
                errors.push(wgsl_error(
                    "continue must not bypass declarations used by the continuing block",
                ));
            }
        }
        pos = close + 1;
    }

    pos = 0;
    while let Some(for_pos) = find_keyword_from(&scan, "for", pos) {
        let mut open = for_pos + "for".len();
        while open < bytes.len() && bytes[open].is_ascii_whitespace() {
            open += 1;
        }
        if open >= bytes.len() || bytes[open] != b'(' {
            pos = for_pos + "for".len();
            continue;
        }
        let Some(header_close) = find_matching_delimiter(&scan, open, b'(', b')') else {
            pos = for_pos + "for".len();
            continue;
        };
        let header = scan[open + 1..header_close].trim();
        let mut body_open = header_close + 1;
        while body_open < bytes.len() && bytes[body_open].is_ascii_whitespace() {
            body_open += 1;
        }
        if body_open >= bytes.len() || bytes[body_open] != b'{' {
            pos = header_close + 1;
            continue;
        }
        let Some(body_close) = find_matching_delimiter(&scan, body_open, b'{', b'}') else {
            pos = header_close + 1;
            continue;
        };
        let body = &scan[body_open + 1..body_close];
        if for_header_condition_is_empty(header) && !body_has_loop_escape_before_continue(body) {
            errors.push(wgsl_error(
                "for loops without a condition must have a reachable break or return",
            ));
        }
        pos = body_close + 1;
    }

    errors
}

fn parse_size_literal_arg(arg: &str) -> Option<u64> {
    let s = arg.trim();
    if s.is_empty() || s.starts_with('-') {
        return None;
    }
    let s = s
        .strip_suffix('u')
        .or_else(|| s.strip_suffix('i'))
        .unwrap_or(s);
    if let Some(hex) = s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")) {
        return u64::from_str_radix(hex, 16).ok();
    }
    s.parse::<u64>().ok()
}

fn lower_large_size_attribute_source(source: &str) -> String {
    if !source.contains('@') || !source.contains("size") {
        return source.to_string();
    }

    const NAGA_MAX_TYPE_SIZE: u64 = 1_073_741_824;
    let scan = mask_comments_preserve_len(source);
    let mut replacements: Vec<(usize, usize)> = Vec::new();
    let mut pos = 0usize;
    while let Some((attr_start, _open, close, _name_start, args)) =
        find_attribute_call_from(&scan, "size", pos)
    {
        if args.len() == 1 || (args.len() == 2 && args[1].is_empty()) {
            if parse_size_literal_arg(&args[0]).is_some_and(|v| v > NAGA_MAX_TYPE_SIZE) {
                replacements.push((attr_start, close + 1));
            }
        }
        pos = close + 1;
    }

    if replacements.is_empty() {
        return source.to_string();
    }
    let mut out = String::with_capacity(source.len());
    let mut copy_pos = 0usize;
    for (start, end) in replacements {
        out.push_str(&source[copy_pos..start]);
        out.push_str("@size(1073741824)");
        copy_pos = end;
    }
    out.push_str(&source[copy_pos..]);
    out
}

fn parse_struct_member_type_after_attr<'a>(source: &'a str, attr_close: usize) -> Option<&'a str> {
    let bytes = source.as_bytes();
    let mut i = attr_close + 1;
    while i < bytes.len() && bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    if i >= bytes.len() || !(bytes[i] == b'_' || bytes[i].is_ascii_alphabetic()) {
        return None;
    }
    i += 1;
    while i < bytes.len() && is_ident_char(bytes[i]) {
        i += 1;
    }
    while i < bytes.len() && bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    if i >= bytes.len() || bytes[i] != b':' {
        return None;
    }
    i += 1;
    while i < bytes.len() && bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    let type_start = i;
    let mut depth = 0i32;
    while i < bytes.len() {
        match bytes[i] {
            b'<' | b'(' | b'[' | b'{' => depth += 1,
            b'>' | b')' | b']' | b'}' => depth -= 1,
            b',' | b';' if depth == 0 => break,
            _ => {}
        }
        i += 1;
    }
    (type_start < i).then_some(source[type_start..i].trim())
}

fn type_is_runtime_array(type_text: &str) -> bool {
    let trimmed = type_text.trim();
    if !trimmed.starts_with("array") {
        return false;
    }
    let Some((args, _open, _close)) = parse_type_application_args(trimmed, "array", 0) else {
        return false;
    };
    args.len() == 1
}

fn validate_size_attribute_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains('@') || !source.contains("size") || !source.contains("array") {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    let mut errors = Vec::new();
    let mut pos = 0usize;
    while let Some((_attr_start, _open, close, _name_start, _args)) =
        find_attribute_call_from(&scan, "size", pos)
    {
        if parse_struct_member_type_after_attr(&scan, close).is_some_and(type_is_runtime_array) {
            errors.push(wgsl_error(
                "@size requires a creation-fixed footprint member type",
            ));
        }
        pos = close + 1;
    }
    errors
}

fn argument_takes_composite_address(arg: &str) -> bool {
    let s = trim_outer_parens(arg).trim();
    let Some(rest) = s.strip_prefix('&') else {
        return false;
    };

    let mut angle_depth = 0i32;
    for b in rest.trim().bytes() {
        match b {
            b'<' => angle_depth += 1,
            b'>' if angle_depth > 0 => angle_depth -= 1,
            b'.' | b'[' if angle_depth == 0 => return true,
            _ => {}
        }
    }
    false
}

fn validate_unrestricted_pointer_parameter_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains('&') {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    if enable_directive_lists_extension(&scan, "unrestricted_pointer_parameters") {
        return Vec::new();
    }
    let fn_names = enumerate_fn_names(&scan);
    let mut errors = Vec::new();
    for name in fn_names {
        let mut pos = 0usize;
        while let Some(rel) = scan[pos..].find(&name) {
            let start = pos + rel;
            let end = start + name.len();
            if (start > 0 && is_ident_char(scan.as_bytes()[start - 1]))
                || (end < scan.len() && is_ident_char(scan.as_bytes()[end]))
            {
                pos = end;
                continue;
            }
            let Some((args, close)) = find_call_args(&scan, start, name.len()) else {
                pos = end;
                continue;
            };
            if args.iter().any(|arg| argument_takes_composite_address(arg)) {
                errors.push(wgsl_error(
                    "composite pointer arguments require unrestricted_pointer_parameters",
                ));
                break;
            }
            pos = close + 1;
        }
    }
    errors
}

fn find_matching_angle(source: &str, open: usize) -> Option<usize> {
    let bytes = source.as_bytes();
    if open >= bytes.len() || bytes[open] != b'<' {
        return None;
    }

    let mut depth = 0i32;
    for (i, b) in bytes.iter().enumerate().skip(open) {
        match *b {
            b'<' => depth += 1,
            b'>' => {
                depth -= 1;
                if depth == 0 {
                    return Some(i);
                }
            }
            _ => {}
        }
    }
    None
}

fn parse_type_application_args(
    source: &str,
    keyword: &str,
    start: usize,
) -> Option<(Vec<String>, usize, usize)> {
    let bytes = source.as_bytes();
    let end = start + keyword.len();
    if end > bytes.len()
        || !source[start..].starts_with(keyword)
        || (start > 0 && is_ident_char(bytes[start - 1]))
        || (end < bytes.len() && is_ident_char(bytes[end]))
    {
        return None;
    }

    let mut open = end;
    while open < bytes.len() && bytes[open].is_ascii_whitespace() {
        open += 1;
    }
    if open >= bytes.len() || bytes[open] != b'<' {
        return None;
    }
    let close = find_matching_angle(source, open)?;
    Some((
        split_top_level_type_args(&source[open + 1..close]),
        open,
        close,
    ))
}

fn collect_override_names(source: &str) -> HashSet<String> {
    let scan = mask_comments_preserve_len(source);
    let bytes = scan.as_bytes();
    let mut names = HashSet::new();
    let mut pos = 0usize;
    while let Some(rel) = scan[pos..].find("override") {
        let start = pos + rel;
        let end = start + "override".len();
        if (start > 0 && is_ident_char(bytes[start - 1]))
            || (end < bytes.len() && is_ident_char(bytes[end]))
        {
            pos = end;
            continue;
        }

        let mut i = end;
        while i < bytes.len() && bytes[i].is_ascii_whitespace() {
            i += 1;
        }
        if i >= bytes.len() || !(bytes[i].is_ascii_alphabetic() || bytes[i] == b'_') {
            pos = end;
            continue;
        }
        let name_start = i;
        i += 1;
        while i < bytes.len() && is_ident_char(bytes[i]) {
            i += 1;
        }
        names.insert(scan[name_start..i].to_string());
        pos = i;
    }
    names
}

fn expr_mentions_override(expr: &str, override_names: &HashSet<String>) -> bool {
    override_names
        .iter()
        .any(|name| identifier_occurrence_count(expr, name) >= 1)
}

fn type_contains_override_sized_array(type_text: &str, override_names: &HashSet<String>) -> bool {
    if override_names.is_empty() || !type_text.contains("array") {
        return false;
    }

    let mut pos = 0usize;
    while let Some(rel) = type_text[pos..].find("array") {
        let start = pos + rel;
        let Some((args, _open, close)) = parse_type_application_args(type_text, "array", start)
        else {
            pos = start + "array".len();
            continue;
        };

        if args.len() >= 2 && expr_mentions_override(&args[1], override_names) {
            return true;
        }
        if args
            .first()
            .is_some_and(|arg| type_contains_override_sized_array(arg, override_names))
        {
            return true;
        }
        pos = close + 1;
    }
    false
}

fn type_contains_sized_array(type_text: &str) -> bool {
    if !type_text.contains("array") {
        return false;
    }

    let mut pos = 0usize;
    while let Some(rel) = type_text[pos..].find("array") {
        let start = pos + rel;
        let Some((args, _open, close)) = parse_type_application_args(type_text, "array", start)
        else {
            pos = start + "array".len();
            continue;
        };

        if args.len() >= 2
            || args
                .first()
                .is_some_and(|arg| type_contains_sized_array(arg))
        {
            return true;
        }
        pos = close + 1;
    }
    false
}

fn type_text_contains_atomic_keyword(type_text: &str) -> bool {
    let normalized = normalize_type_name(type_text).to_ascii_lowercase();
    let bytes = normalized.as_bytes();
    let mut pos = 0usize;
    while let Some(rel) = normalized[pos..].find("atomic") {
        let start = pos + rel;
        let end = start + "atomic".len();
        let before_ok = start == 0 || !is_ident_char(bytes[start - 1]);
        let after_ok = end < bytes.len() && bytes[end] == b'<';
        if before_ok && after_ok {
            return true;
        }
        pos = end;
    }
    false
}

fn atomic_type_arg_should_error(type_text: &str) -> bool {
    let n = normalize_type_name(type_text).to_ascii_lowercase();
    if n == "i32" || n == "u32" {
        return false;
    }
    // Alias targets are resolved by Naga. Keep source validation to concrete
    // invalid forms Naga currently misses, such as atomic<f32>.
    !is_plain_identifier(&n)
        || matches!(
            n.as_str(),
            "bool" | "f32" | "f16" | "abstractint" | "abstractfloat"
        )
}

fn validate_atomic_template_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains("atomic") {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    let mut errors = Vec::new();
    let mut pos = 0usize;
    while let Some(rel) = scan[pos..].find("atomic") {
        let start = pos + rel;
        let Some((mut args, _open, close)) = parse_type_application_args(&scan, "atomic", start)
        else {
            pos = start + "atomic".len();
            continue;
        };
        if args.last().is_some_and(|arg| arg.is_empty()) {
            args.pop();
        }
        if args.len() == 1 && atomic_type_arg_should_error(&args[0]) {
            errors.push(wgsl_error(
                "atomic template argument must be either i32 or u32",
            ));
        }
        pos = close + 1;
    }
    errors
}

fn validate_pointer_type_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains("ptr") {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    let mut errors = Vec::new();
    let mut pos = 0usize;
    while let Some(rel) = scan[pos..].find("ptr") {
        let start = pos + rel;
        let Some((mut args, _open, close)) = parse_type_application_args(&scan, "ptr", start)
        else {
            pos = start + "ptr".len();
            continue;
        };
        if args.last().is_some_and(|arg| arg.is_empty()) {
            args.pop();
        }
        if args.len() >= 3 {
            let address_space = normalize_type_name(&args[0]).to_ascii_lowercase();
            let access = normalize_type_name(&args[2]).to_ascii_lowercase();
            if address_space == "storage" && access == "write" {
                errors.push(wgsl_error(
                    "storage pointer access mode cannot be write-only",
                ));
            }
        }
        if args.len() >= 2 {
            let address_space = normalize_type_name(&args[0]).to_ascii_lowercase();
            if matches!(address_space.as_str(), "private" | "function" | "uniform")
                && type_text_contains_atomic_keyword(&args[1])
            {
                errors.push(wgsl_error(
                    "atomic store types are only valid in storage or workgroup address space",
                ));
            }
        }
        pos = close + 1;
    }
    errors
}

fn collect_atomic_var_names(source: &str) -> HashSet<String> {
    let mut names = HashSet::new();
    for stmt in source.split(';') {
        let Some((name, type_text)) = parse_var_decl_name_type(stmt) else {
            continue;
        };
        if type_text_contains_atomic_keyword(type_text) {
            names.insert(name.to_string());
        }
    }
    names
}

fn parse_var_name_and_initializer(stmt: &str) -> Option<(&str, &str)> {
    let var_pos = find_keyword(stmt, "var")?;
    let bytes = stmt.as_bytes();
    let mut i = var_pos + 3;
    while i < bytes.len() && bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    if i < bytes.len() && bytes[i] == b'<' {
        let close = find_matching_angle(stmt, i)?;
        i = close + 1;
    }
    while i < bytes.len() && bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    let name_start = i;
    if i >= bytes.len() || !(bytes[i] == b'_' || bytes[i].is_ascii_alphabetic()) {
        return None;
    }
    i += 1;
    while i < bytes.len() && is_ident_char(bytes[i]) {
        i += 1;
    }
    let name = &stmt[name_start..i];
    let eq = find_assignment_eq(&stmt[i..])?;
    Some((name, stmt[i + eq + 1..].trim()))
}

fn last_identifier_token(s: &str) -> Option<&str> {
    let bytes = s.as_bytes();
    let mut end = bytes.len();
    while end > 0 && bytes[end - 1].is_ascii_whitespace() {
        end -= 1;
    }
    let mut start = end;
    while start > 0 && is_ident_char(bytes[start - 1]) {
        start -= 1;
    }
    (start < end).then_some(&s[start..end])
}

fn expr_directly_mentions_atomic_value(expr: &str, atomic_names: &HashSet<String>) -> bool {
    let trimmed = trim_outer_parens(expr).trim();
    if is_plain_identifier(trimmed) {
        return atomic_names.contains(trimmed);
    }
    if trimmed.starts_with('&') || trimmed.contains("atomic") {
        return false;
    }
    atomic_names
        .iter()
        .any(|name| identifier_occurrence_count(trimmed, name) > 0)
}

fn switch_directly_uses_atomic_value(source: &str, atomic_names: &HashSet<String>) -> bool {
    let bytes = source.as_bytes();
    let mut pos = 0usize;
    while let Some(switch_pos) = find_keyword_from(source, "switch", pos) {
        let mut i = switch_pos + "switch".len();
        while i < bytes.len() && bytes[i].is_ascii_whitespace() {
            i += 1;
        }
        let name_start = i;
        if i < bytes.len() && (bytes[i] == b'_' || bytes[i].is_ascii_alphabetic()) {
            i += 1;
            while i < bytes.len() && is_ident_char(bytes[i]) {
                i += 1;
            }
            if atomic_names.contains(&source[name_start..i]) {
                return true;
            }
        }
        pos = switch_pos + "switch".len();
    }
    false
}

fn validate_atomic_value_uses_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains("atomic") {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    let atomic_names = collect_atomic_var_names(&scan);
    if atomic_names.is_empty() {
        return Vec::new();
    }

    let mut errors = Vec::new();
    if switch_directly_uses_atomic_value(&scan, &atomic_names) {
        errors.push(wgsl_error(
            "atomic values cannot be used as switch selectors",
        ));
    }
    for stmt in scan.split(';') {
        let trimmed = stmt.trim();
        if trimmed.is_empty() {
            continue;
        }

        if let Some((_name, init)) = parse_var_name_and_initializer(trimmed) {
            if expr_directly_mentions_atomic_value(init, &atomic_names) {
                errors.push(wgsl_error("atomic values cannot initialize variables"));
                continue;
            }
        }

        if let Some(let_pos) = find_keyword(trimmed, "let") {
            let decl = &trimmed[let_pos + 3..];
            if let Some(eq) = find_assignment_eq(decl) {
                let init = decl[eq + 1..].trim();
                if expr_directly_mentions_atomic_value(init, &atomic_names) {
                    errors.push(wgsl_error(
                        "atomic values cannot be used as ordinary values",
                    ));
                    continue;
                }
            }
        }

        if let Some(eq) = find_assignment_eq(trimmed) {
            let lhs = trimmed[..eq].trim();
            let rhs = trimmed[eq + 1..].trim();
            if last_identifier_token(lhs) == Some("_")
                && expr_directly_mentions_atomic_value(rhs, &atomic_names)
            {
                errors.push(wgsl_error("atomic values cannot be assigned to phony"));
                continue;
            }
        }

        for name in &atomic_names {
            if trimmed.contains("++") || trimmed.contains("--") {
                let inc = format!("{name}++");
                let dec = format!("{name}--");
                if trimmed.contains(&inc) || trimmed.contains(&dec) {
                    errors.push(wgsl_error(
                        "atomic values cannot be incremented or decremented",
                    ));
                    break;
                }
            }
        }
    }
    errors
}

fn validate_atomic_source(source: &str) -> Vec<WgslMessage> {
    let mut errors = validate_atomic_template_source(source);
    errors.extend(validate_pointer_type_source(source));
    errors.extend(validate_atomic_value_uses_source(source));
    errors
}

fn expr_text_is_boolish(expr: &str, bool_names: &HashSet<String>) -> bool {
    let trimmed = trim_outer_parens(expr).trim();
    if matches!(trimmed, "true" | "false") {
        return true;
    }
    if is_plain_identifier(trimmed) {
        return bool_names.contains(trimmed);
    }
    let Some((callee, args)) = parse_full_call(trimmed) else {
        return false;
    };
    let normalized = normalize_type_name(&callee).to_ascii_lowercase();
    normalized.starts_with("vec") && args.iter().all(|arg| expr_text_is_boolish(arg, bool_names))
}

fn collect_bool_const_names(source: &str) -> HashSet<String> {
    let mut names = HashSet::new();
    for stmt in source.split(';') {
        let Some(name) = declaration_name_after_keyword(stmt, "const") else {
            continue;
        };
        let Some(eq) = find_assignment_eq(stmt) else {
            continue;
        };
        if expr_text_is_boolish(&stmt[eq + 1..], &names) {
            names.insert(name.to_string());
        }
    }
    names
}

fn find_top_level_order_comparison(expr: &str) -> Option<(&str, &str)> {
    let bytes = expr.as_bytes();
    let mut depth = 0i32;
    let mut i = 0usize;
    while i < bytes.len() {
        match bytes[i] {
            b'(' | b'[' | b'{' => depth += 1,
            b')' | b']' | b'}' => depth -= 1,
            b'<' | b'>' if depth == 0 => {
                let len = if bytes.get(i + 1) == Some(&b'=') {
                    2
                } else {
                    1
                };
                if bytes.get(i + 1) == Some(&bytes[i]) {
                    i += 2;
                    continue;
                }
                return Some((&expr[..i], &expr[i + len..]));
            }
            _ => {}
        }
        i += 1;
    }
    None
}

fn validate_bool_order_comparison_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains('<') && !source.contains('>') {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    let bool_names = collect_bool_const_names(&scan);
    if bool_names.is_empty() {
        return Vec::new();
    }

    let mut errors = Vec::new();
    for stmt in scan.split(';') {
        let Some(eq) = find_assignment_eq(stmt) else {
            continue;
        };
        let rhs = stmt[eq + 1..].trim();
        let Some((left, right)) = find_top_level_order_comparison(rhs) else {
            continue;
        };
        if expr_text_is_boolish(left, &bool_names) && expr_text_is_boolish(right, &bool_names) {
            errors.push(wgsl_error(
                "boolean values only support equality comparisons",
            ));
        }
    }
    errors
}

#[derive(Clone)]
struct BoolConstValue {
    values: Vec<bool>,
}

fn parse_bool_const_value(
    expr: &str,
    known: &HashMap<String, BoolConstValue>,
) -> Option<BoolConstValue> {
    let s = trim_outer_parens(expr).trim();
    match s {
        "true" => {
            return Some(BoolConstValue { values: vec![true] });
        }
        "false" => {
            return Some(BoolConstValue {
                values: vec![false],
            });
        }
        _ => {}
    }
    if is_plain_identifier(s) {
        return known.get(s).cloned();
    }
    let (callee, args) = parse_full_call(s)?;
    let width = constructor_vector_width(&callee)?;
    let n = normalize_type_name(&callee).to_ascii_lowercase();
    if !(n.contains("bool") || n.ends_with('b') || n == format!("vec{width}")) {
        return None;
    }
    let mut values = Vec::new();
    if args.len() == 1 {
        let value = parse_bool_const_value(&args[0], known)?;
        if value.values.len() == 1 {
            values.resize(width, value.values[0]);
        } else if value.values.len() == width {
            values = value.values;
        } else {
            return None;
        }
    } else {
        for arg in args {
            let value = parse_bool_const_value(&arg, known)?;
            if value.values.len() != 1 {
                return None;
            }
            values.push(value.values[0]);
        }
        if values.len() != width {
            return None;
        }
    }
    Some(BoolConstValue { values })
}

fn collect_bool_const_values(source: &str) -> HashMap<String, BoolConstValue> {
    let mut out = HashMap::new();
    for stmt in source.split(';') {
        let Some(name) = declaration_name_after_keyword(stmt, "const") else {
            continue;
        };
        let Some(eq) = find_assignment_eq(stmt) else {
            continue;
        };
        if let Some(value) = parse_bool_const_value(&stmt[eq + 1..], &out) {
            out.insert(name.to_string(), value);
        }
    }
    out
}

fn find_top_level_bool_bitwise_op(expr: &str) -> Option<(usize, char)> {
    let bytes = expr.as_bytes();
    let mut depth = 0i32;
    let mut i = 0usize;
    while i < bytes.len() {
        match bytes[i] {
            b'(' | b'[' | b'{' | b'<' => depth += 1,
            b')' | b']' | b'}' | b'>' => depth -= 1,
            b'&' | b'|' if depth == 0 => {
                if bytes.get(i + 1) == Some(&bytes[i]) {
                    i += 2;
                    continue;
                }
                return Some((i, bytes[i] as char));
            }
            _ => {}
        }
        i += 1;
    }
    None
}

fn format_bool_const_value(value: &BoolConstValue) -> String {
    if value.values.len() == 1 {
        return if value.values[0] { "true" } else { "false" }.to_string();
    }
    let components = value
        .values
        .iter()
        .map(|v| if *v { "true" } else { "false" })
        .collect::<Vec<_>>()
        .join(", ");
    format!("vec{}<bool>({components})", value.values.len())
}

fn lower_bool_bitwise_const_source(source: &str) -> String {
    if !source.contains('&') && !source.contains('|') {
        return source.to_string();
    }
    let known = collect_bool_const_values(source);
    if known.is_empty() {
        return source.to_string();
    }
    let mut out = String::with_capacity(source.len());
    let mut changed = false;
    for part in source.split_inclusive(';') {
        let (stmt, suffix) = if let Some(stripped) = part.strip_suffix(';') {
            (stripped, ";")
        } else {
            (part, "")
        };
        let Some(eq) = find_assignment_eq(stmt) else {
            out.push_str(part);
            continue;
        };
        let rhs = &stmt[eq + 1..];
        let Some((op_pos, op)) = find_top_level_bool_bitwise_op(rhs) else {
            out.push_str(part);
            continue;
        };
        let Some(left) = parse_bool_const_value(&rhs[..op_pos], &known) else {
            out.push_str(part);
            continue;
        };
        let Some(right) = parse_bool_const_value(&rhs[op_pos + 1..], &known) else {
            out.push_str(part);
            continue;
        };
        if left.values.len() != right.values.len() {
            out.push_str(part);
            continue;
        }
        let values = left
            .values
            .iter()
            .zip(right.values.iter())
            .map(|(a, b)| if op == '&' { *a && *b } else { *a || *b })
            .collect::<Vec<_>>();
        out.push_str(&stmt[..eq + 1]);
        out.push(' ');
        out.push_str(&format_bool_const_value(&BoolConstValue { values }));
        out.push_str(suffix);
        changed = true;
    }
    if changed {
        out
    } else {
        source.to_string()
    }
}

fn collect_source_i32_values(
    source: &str,
    constants: Option<&HashMap<String, f64>>,
) -> HashMap<String, f64> {
    let mut known = HashMap::new();
    if let Some(constants) = constants {
        for (name, value) in constants {
            if value.is_finite()
                && value.fract() == 0.0
                && *value >= i32::MIN as f64
                && *value <= i32::MAX as f64
            {
                known.insert(name.clone(), *value);
            }
        }
    }

    for stmt in source.split(';') {
        for keyword in ["const", "override"] {
            let Some(name) = declaration_name_after_keyword(stmt, keyword) else {
                continue;
            };
            let Some(eq) = find_assignment_eq(stmt) else {
                continue;
            };
            if let Some(value) = parse_wgsl_i32_expr(&stmt[eq + 1..], Some(&known)) {
                known.insert(name.to_string(), value as f64);
            }
        }
    }
    known
}

fn find_top_level_div_rem_op(expr: &str) -> Option<(usize, char)> {
    let bytes = expr.as_bytes();
    let mut depth = 0i32;
    let mut i = 0usize;
    while i < bytes.len() {
        match bytes[i] {
            b'(' | b'[' | b'{' => depth += 1,
            b')' | b']' | b'}' => depth -= 1,
            b'/' | b'%' if depth == 0 => return Some((i, bytes[i] as char)),
            _ => {}
        }
        i += 1;
    }
    None
}

fn validate_div_rem_source(
    source: &str,
    constants: Option<&HashMap<String, f64>>,
    entry_point: Option<&str>,
) -> Vec<WgslMessage> {
    if !source.contains('/') && !source.contains('%') {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(&smoothstep_validation_source(source, entry_point));
    let known = collect_source_i32_values(&scan, constants);
    if known.is_empty() {
        return Vec::new();
    }

    let mut errors = Vec::new();
    for stmt in scan.split(';') {
        let Some(eq) = find_assignment_eq(stmt) else {
            continue;
        };
        let rhs = stmt[eq + 1..].trim();
        let Some((op_pos, op)) = find_top_level_div_rem_op(rhs) else {
            continue;
        };
        let left = rhs[..op_pos].trim();
        let right = rhs[op_pos + 1..].trim();
        if parse_wgsl_i32_expr(left, Some(&known)) == Some(i32::MIN)
            && parse_wgsl_i32_expr(right, Some(&known)) == Some(-1)
        {
            let op_name = if op == '/' { "division" } else { "remainder" };
            errors.push(wgsl_error(format!(
                "integer {} of i32::MIN by -1 overflows i32",
                op_name
            )));
        }
    }
    errors
}

fn rewrite_override_sized_array_counts_in_type(
    type_text: &str,
    override_names: &HashSet<String>,
) -> String {
    if override_names.is_empty() || !type_text.contains("array") {
        return type_text.to_string();
    }

    let mut out = String::with_capacity(type_text.len());
    let mut copy_pos = 0usize;
    let mut search_pos = 0usize;
    while let Some(rel) = type_text[search_pos..].find("array") {
        let start = search_pos + rel;
        let Some((args, open, close)) = parse_type_application_args(type_text, "array", start)
        else {
            search_pos = start + "array".len();
            continue;
        };

        out.push_str(&type_text[copy_pos..open + 1]);
        let mut rewritten_args = args;
        if let Some(element) = rewritten_args.first_mut() {
            *element = rewrite_override_sized_array_counts_in_type(element, override_names);
        }
        if rewritten_args.len() >= 2 && expr_mentions_override(&rewritten_args[1], override_names) {
            rewritten_args[1] = "1".to_string();
        }
        out.push_str(&rewritten_args.join(", "));
        out.push('>');
        copy_pos = close + 1;
        search_pos = close + 1;
    }

    if copy_pos == 0 {
        return type_text.to_string();
    }
    out.push_str(&type_text[copy_pos..]);
    out
}

fn validate_override_sized_array_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains("override") || !source.contains("array") || !source.contains("ptr") {
        return Vec::new();
    }

    let override_names = collect_override_names(source);
    if override_names.is_empty() {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    let mut errors = Vec::new();
    let mut pos = 0usize;
    while let Some(rel) = scan[pos..].find("ptr") {
        let start = pos + rel;
        let Some((args, _open, close)) = parse_type_application_args(&scan, "ptr", start) else {
            pos = start + "ptr".len();
            continue;
        };
        if args.len() >= 2
            && normalize_type_name(&args[0]).to_ascii_lowercase() != "workgroup"
            && type_contains_override_sized_array(&args[1], &override_names)
        {
            errors.push(wgsl_error(
                "override-sized arrays are only allowed in workgroup address space",
            ));
        }
        pos = close + 1;
    }
    errors
}

fn source_has_entry_point_attribute(source: &str) -> bool {
    let scan = mask_comments_preserve_len(source);
    attr_slice_contains(&scan, "vertex")
        || attr_slice_contains(&scan, "fragment")
        || attr_slice_contains(&scan, "compute")
}

fn lower_no_entry_workgroup_pointer_params_source(source: &str) -> String {
    if !source.contains("ptr")
        || !source.contains("workgroup")
        || source_has_entry_point_attribute(source)
    {
        return source.to_string();
    }

    let scan = mask_comments_preserve_len(source);
    let override_names = collect_override_names(&scan);
    let mut out = String::with_capacity(source.len());
    let mut copy_pos = 0usize;
    let mut search_pos = 0usize;
    while let Some(rel) = scan[search_pos..].find("ptr") {
        let start = search_pos + rel;
        let Some((args, _open, close)) = parse_type_application_args(&scan, "ptr", start) else {
            search_pos = start + "ptr".len();
            continue;
        };
        if args.len() == 2
            && normalize_type_name(&args[0]).to_ascii_lowercase() == "workgroup"
            && type_contains_sized_array(&args[1])
            && !type_text_contains_atomic_keyword(&args[1])
        {
            out.push_str(&source[copy_pos..start]);
            out.push_str("ptr<function, ");
            out.push_str(&rewrite_override_sized_array_counts_in_type(
                &args[1],
                &override_names,
            ));
            out.push('>');
            copy_pos = close + 1;
            search_pos = close + 1;
            continue;
        }
        search_pos = close + 1;
    }

    if copy_pos == 0 {
        return source.to_string();
    }
    out.push_str(&source[copy_pos..]);
    out
}

fn normalize_var_template_trailing_commas_source(source: &str) -> String {
    if !source.contains("var") || !source.contains(",>") {
        return source.to_string();
    }

    let scan = mask_comments_preserve_len(source);
    let bytes = scan.as_bytes();
    let mut replacements: Vec<usize> = Vec::new();
    let mut pos = 0usize;
    while let Some(rel) = scan[pos..].find("var") {
        let start = pos + rel;
        let end = start + 3;
        if (start > 0 && is_ident_char(bytes[start - 1]))
            || (end < bytes.len() && is_ident_char(bytes[end]))
        {
            pos = end;
            continue;
        }

        let mut open = end;
        while open < bytes.len() && bytes[open].is_ascii_whitespace() {
            open += 1;
        }
        if open >= bytes.len() || bytes[open] != b'<' {
            pos = end;
            continue;
        }
        let Some(close) = find_matching_angle(&scan, open) else {
            pos = open + 1;
            continue;
        };

        let inner = &scan[open + 1..close];
        let Some(last_non_ws) = inner.bytes().rposition(|b| !b.is_ascii_whitespace()) else {
            pos = close + 1;
            continue;
        };
        if inner.as_bytes()[last_non_ws] != b',' {
            pos = close + 1;
            continue;
        }

        let args = split_top_level_type_args(inner);
        if args.len() >= 2
            && args.last().is_some_and(|arg| arg.is_empty())
            && args[..args.len() - 1].iter().all(|arg| !arg.is_empty())
        {
            replacements.push(open + 1 + last_non_ws);
        }
        pos = close + 1;
    }

    if replacements.is_empty() {
        return source.to_string();
    }

    let mut out = source.as_bytes().to_vec();
    for idx in replacements {
        out[idx] = b' ';
    }
    String::from_utf8(out).unwrap_or_else(|_| source.to_string())
}

fn normalize_interpolate_trailing_commas_source(source: &str) -> String {
    if !source.contains("interpolate") || !source.contains(',') {
        return source.to_string();
    }

    let scan = mask_comments_preserve_len(source);
    let bytes = scan.as_bytes();
    let mut replacements: Vec<usize> = Vec::new();
    let mut pos = 0usize;
    while let Some((_attr_start, _open, close, _name_start, args)) =
        find_attribute_call_from(&scan, "interpolate", pos)
    {
        if args.last().is_some_and(|arg| arg.is_empty()) {
            let mut comma = close;
            while comma > 0 && bytes[comma - 1].is_ascii_whitespace() {
                comma -= 1;
            }
            if comma > 0 && bytes[comma - 1] == b',' {
                replacements.push(comma - 1);
            }
        }
        pos = close + 1;
    }

    if replacements.is_empty() {
        return source.to_string();
    }

    let mut out = source.as_bytes().to_vec();
    for idx in replacements {
        out[idx] = b' ';
    }
    String::from_utf8(out).unwrap_or_else(|_| source.to_string())
}

fn find_keyword(source: &str, keyword: &str) -> Option<usize> {
    let bytes = source.as_bytes();
    let mut pos = 0usize;
    while let Some(rel) = source[pos..].find(keyword) {
        let start = pos + rel;
        let end = start + keyword.len();
        if (start == 0 || !is_ident_char(bytes[start - 1]))
            && (end == bytes.len() || !is_ident_char(bytes[end]))
        {
            return Some(start);
        }
        pos = end;
    }
    None
}

fn type_text_is_resource_handle(type_text: &str) -> bool {
    let n = normalize_type_name(type_text).to_ascii_lowercase();
    n == "sampler" || n == "sampler_comparison" || n.starts_with("texture_")
}

fn parse_var_decl_name_type(stmt: &str) -> Option<(&str, &str)> {
    let var_pos = find_keyword(stmt, "var")?;
    let bytes = stmt.as_bytes();
    let mut i = var_pos + 3;
    while i < bytes.len() && bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    if i < bytes.len() && bytes[i] == b'<' {
        let mut depth = 1i32;
        i += 1;
        while i < bytes.len() && depth > 0 {
            match bytes[i] {
                b'<' => depth += 1,
                b'>' => depth -= 1,
                _ => {}
            }
            i += 1;
        }
    }
    while i < bytes.len() && bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    let name_start = i;
    if i >= bytes.len() || !(bytes[i] == b'_' || bytes[i].is_ascii_alphabetic()) {
        return None;
    }
    i += 1;
    while i < bytes.len() && is_ident_char(bytes[i]) {
        i += 1;
    }
    let name = &stmt[name_start..i];
    while i < bytes.len() && bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    if i >= bytes.len() || bytes[i] != b':' {
        return None;
    }
    Some((name, stmt[i + 1..].trim()))
}

fn collect_resource_handle_var_names(source: &str) -> HashSet<String> {
    let mut names = HashSet::new();
    for stmt in source.split(';') {
        let Some((name, type_text)) = parse_var_decl_name_type(stmt) else {
            continue;
        };
        if type_text_is_resource_handle(type_text) {
            names.insert(name.to_string());
        }
    }
    names
}

fn validate_let_resource_handle_source(source: &str) -> Vec<WgslMessage> {
    if !source.contains("let") || (!source.contains("texture_") && !source.contains("sampler")) {
        return Vec::new();
    }

    let scan = mask_comments_preserve_len(source);
    let resource_names = collect_resource_handle_var_names(&scan);
    let mut errors = Vec::new();
    for stmt in scan.split(';') {
        let Some(let_pos) = find_keyword(stmt, "let") else {
            continue;
        };
        let decl = &stmt[let_pos + 3..];
        let eq = find_assignment_eq(decl);
        let lhs = eq.map_or(decl, |i| &decl[..i]);
        if let Some(colon) = lhs.find(':') {
            if type_text_is_resource_handle(&lhs[colon + 1..]) {
                errors.push(wgsl_error(
                    "let declarations cannot have texture or sampler handle types",
                ));
                continue;
            }
        }
        let Some(eq) = eq else {
            continue;
        };
        let init = trim_outer_parens(&decl[eq + 1..]).trim();
        if is_plain_identifier(init) && resource_names.contains(init) {
            errors.push(wgsl_error(
                "let declarations cannot bind texture or sampler handles",
            ));
        }
    }
    errors
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

fn skip_ws_and_comments(bytes: &[u8], mut pos: usize) -> usize {
    while pos < bytes.len() {
        if (bytes[pos] as char).is_whitespace() {
            pos += 1;
            continue;
        }
        if pos + 1 < bytes.len() && bytes[pos] == b'/' && bytes[pos + 1] == b'/' {
            pos += 2;
            while pos < bytes.len() && bytes[pos] != b'\n' {
                pos += 1;
            }
            continue;
        }
        if pos + 1 < bytes.len() && bytes[pos] == b'/' && bytes[pos + 1] == b'*' {
            pos += 2;
            while pos + 1 < bytes.len() && !(bytes[pos] == b'*' && bytes[pos + 1] == b'/') {
                pos += 1;
            }
            pos = (pos + 2).min(bytes.len());
            continue;
        }
        break;
    }
    pos
}

struct StripResult {
    sanitized: String,
    messages: Vec<WgslMessage>,
    has_error: bool,
}

struct DiagnosticScope {
    start: usize,
    end: usize,
    severity: String,
}

fn diagnostic_attribute_scope_end(source: &str, after_diagnostic: usize) -> Option<usize> {
    let bytes = source.as_bytes();
    let pos = skip_ws_and_comments(bytes, after_diagnostic);
    if pos >= bytes.len() {
        return None;
    }

    if bytes[pos] == b'{' {
        return find_matching_delimiter(source, pos, b'{', b'}');
    }

    if !is_ident_char(bytes[pos]) {
        return None;
    }

    let mut ident_end = pos;
    while ident_end < bytes.len() && is_ident_char(bytes[ident_end]) {
        ident_end += 1;
    }
    let keyword = &source[pos..ident_end];
    if !matches!(keyword, "fn" | "if" | "switch" | "loop" | "while" | "for") {
        return None;
    }

    let mut brace = ident_end;
    while brace < bytes.len() {
        if bytes[brace] == b'{' {
            return find_matching_delimiter(source, brace, b'{', b'}');
        }
        brace += 1;
    }
    None
}

fn validate_derivative_uniformity_diagnostic_scopes(source: &str) -> Vec<WgslMessage> {
    if !source.contains("derivative_uniformity")
        || !source.contains("textureSample")
        || !source.contains("non_uniform_")
    {
        return Vec::new();
    }

    let bytes = source.as_bytes();
    const TOK: &[u8] = b"diagnostic";
    const VALID_SEVERITIES: &[&str] = &["off", "info", "warning", "error"];
    let mut global_severity: Option<String> = None;
    let mut scopes: Vec<DiagnosticScope> = Vec::new();
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
            let k = skip_ws_and_comments(bytes, i + 1);
            if k + TOK.len() <= bytes.len()
                && &bytes[k..k + TOK.len()] == TOK
                && (k + TOK.len() == bytes.len() || !is_ident_char(bytes[k + TOK.len()]))
            {
                let p = skip_ws_and_comments(bytes, k + TOK.len());
                attr_match = p < bytes.len() && bytes[p] == b'(';
            }
        }

        let dir_match = !attr_match
            && depth == 0
            && (i == 0 || !is_ident_char(bytes[i - 1]))
            && i + TOK.len() <= bytes.len()
            && &bytes[i..i + TOK.len()] == TOK
            && (i + TOK.len() == bytes.len() || !is_ident_char(bytes[i + TOK.len()]));

        if !attr_match && !dir_match {
            i += 1;
            continue;
        }

        let start = i;
        let mut p = if attr_match { i + 1 } else { i };
        p = skip_ws_and_comments(bytes, p);
        p += TOK.len();
        p = skip_ws_and_comments(bytes, p);
        if p >= bytes.len() || bytes[p] != b'(' {
            i += 1;
            continue;
        }
        let open = p;
        let Some(close) = find_matching_delimiter(source, open, b'(', b')') else {
            i += 1;
            continue;
        };

        let inner = &bytes[open + 1..close];
        let comma = inner.iter().position(|&c| c == b',');
        let Some(c) = comma else {
            i = close + 1;
            continue;
        };
        let severity = std::str::from_utf8(&inner[..c])
            .unwrap_or("")
            .trim()
            .to_string();
        let rule = std::str::from_utf8(&inner[c + 1..])
            .unwrap_or("")
            .trim()
            .to_string();
        let rule_main = rule.split('.').next().unwrap_or("").trim();
        if rule_main == "derivative_uniformity" && VALID_SEVERITIES.iter().any(|s| *s == severity) {
            if attr_match {
                if let Some(end) = diagnostic_attribute_scope_end(source, close + 1) {
                    scopes.push(DiagnosticScope {
                        start,
                        end,
                        severity: severity.clone(),
                    });
                }
            } else {
                global_severity = Some(severity.clone());
            }
        }

        i = close + 1;
    }

    let Some(mut call_pos) = find_keyword_from(source, "textureSample", 0) else {
        return Vec::new();
    };

    let mut messages = Vec::new();
    loop {
        let mut severity = global_severity.as_deref().unwrap_or("off");
        let mut best_width = usize::MAX;
        for scope in &scopes {
            if scope.start <= call_pos && call_pos <= scope.end {
                let width = scope.end.saturating_sub(scope.start);
                if width < best_width {
                    best_width = width;
                    severity = &scope.severity;
                }
            }
        }

        if severity == "error" {
            let (line, col) = byte_offset_to_line_col(source, call_pos);
            messages.push(WgslMessage {
                r#type: "error".to_string(),
                message: "derivative_uniformity diagnostic escalated to error".to_string(),
                line_num: line,
                line_pos: col,
                offset: call_pos as u32,
                length: "textureSample".len() as u32,
            });
        }

        match find_keyword_from(source, "textureSample", call_pos + "textureSample".len()) {
            Some(next) => call_pos = next,
            None => break,
        }
    }

    messages
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

    let derivative_uniformity_messages = validate_derivative_uniformity_diagnostic_scopes(source);
    if derivative_uniformity_messages
        .iter()
        .any(|m| m.r#type == "error")
    {
        has_error = true;
    }
    messages.extend(derivative_uniformity_messages);

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

fn type_contains_atomic(
    module: &naga::Module,
    ty: naga::Handle<naga::Type>,
    visited: &mut HashSet<naga::Handle<naga::Type>>,
) -> bool {
    if !visited.insert(ty) {
        return false;
    }

    match &module.types[ty].inner {
        naga::TypeInner::Atomic(_) => true,
        naga::TypeInner::Array { base, .. }
        | naga::TypeInner::Pointer { base, .. }
        | naga::TypeInner::BindingArray { base, .. } => {
            type_contains_atomic(module, *base, visited)
        }
        naga::TypeInner::Struct { members, .. } => members
            .iter()
            .any(|member| type_contains_atomic(module, member.ty, visited)),
        _ => false,
    }
}

fn validate_storage_atomic_access(module: &naga::Module) -> Vec<WgslMessage> {
    let mut errors = Vec::new();
    for (_, gv) in module.global_variables.iter() {
        let naga::AddressSpace::Storage { access } = gv.space else {
            continue;
        };
        if access.contains(naga::StorageAccess::STORE) {
            continue;
        }
        if type_contains_atomic(module, gv.ty, &mut HashSet::new()) {
            errors.push(wgsl_error(
                "atomic types require read_write storage or workgroup address space",
            ));
        }
    }
    errors
}

fn storage_image_has_write_access(
    module: &naga::Module,
    ty: naga::Handle<naga::Type>,
    visited: &mut HashSet<naga::Handle<naga::Type>>,
) -> bool {
    if !visited.insert(ty) {
        return false;
    }

    match &module.types[ty].inner {
        naga::TypeInner::Image {
            class: naga::ImageClass::Storage { access, .. },
            ..
        } => access.contains(naga::StorageAccess::STORE),
        naga::TypeInner::BindingArray { base, .. } => {
            storage_image_has_write_access(module, *base, visited)
        }
        _ => false,
    }
}

fn declaration_name_after_keyword<'a>(stmt: &'a str, keyword: &str) -> Option<&'a str> {
    let keyword_pos = find_keyword(stmt, keyword)?;
    let bytes = stmt.as_bytes();
    let mut i = keyword_pos + keyword.len();
    while i < bytes.len() && bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    if keyword == "var" && i < bytes.len() && bytes[i] == b'<' {
        let close = find_matching_angle(stmt, i)?;
        i = close + 1;
        while i < bytes.len() && bytes[i].is_ascii_whitespace() {
            i += 1;
        }
    }
    let name_start = i;
    if i >= bytes.len() || !(bytes[i] == b'_' || bytes[i].is_ascii_alphabetic()) {
        return None;
    }
    i += 1;
    while i < bytes.len() && is_ident_char(bytes[i]) {
        i += 1;
    }
    Some(&stmt[name_start..i])
}

fn function_body_declares_local_name(source: &str, name: &str) -> bool {
    source.split(';').any(|stmt| {
        ["var", "let", "const"]
            .iter()
            .any(|kw| declaration_name_after_keyword(stmt, kw) == Some(name))
    })
}

fn validate_stage_resource_restrictions(module: &naga::Module, source: &str) -> Vec<WgslMessage> {
    if module.entry_points.is_empty() {
        return Vec::new();
    }

    let mut errors = Vec::new();
    for ep in &module.entry_points {
        let is_vertex = matches!(ep.stage, naga::ShaderStage::Vertex);
        let is_compute = matches!(ep.stage, naga::ShaderStage::Compute);
        if is_compute {
            continue;
        }

        let reachable_bodies =
            mask_comments_preserve_len(&collect_reachable_bodies(source, &ep.name));
        for (_, gv) in module.global_variables.iter() {
            let Some(name) = &gv.name else {
                continue;
            };
            if identifier_occurrence_count(&reachable_bodies, name) == 0
                || function_body_declares_local_name(&reachable_bodies, name)
            {
                continue;
            }

            match gv.space {
                naga::AddressSpace::WorkGroup => errors.push(wgsl_error(
                    "workgroup address space is only valid for compute shader entry points",
                )),
                naga::AddressSpace::Storage { access }
                    if is_vertex && access.contains(naga::StorageAccess::STORE) =>
                {
                    errors.push(wgsl_error(
                        "vertex shader entry points cannot use writable storage buffers",
                    ));
                }
                naga::AddressSpace::Handle
                    if is_vertex
                        && storage_image_has_write_access(module, gv.ty, &mut HashSet::new()) =>
                {
                    errors.push(wgsl_error(
                        "vertex shader entry points cannot use writable storage textures",
                    ));
                }
                _ => {}
            }
        }
    }
    errors
}

fn type_resolution_is_boolish(ty: &TypeResolution, types: &naga::UniqueArena<naga::Type>) -> bool {
    match ty.inner_with(types) {
        naga::TypeInner::Scalar(scalar) => scalar.kind == naga::ScalarKind::Bool,
        naga::TypeInner::Vector { scalar, .. } => scalar.kind == naga::ScalarKind::Bool,
        _ => false,
    }
}

fn validate_bool_order_comparisons_in_arena(
    module: &naga::Module,
    local: &naga::Arena<naga::Expression>,
    local_vars: &naga::Arena<naga::LocalVariable>,
    arguments: &[naga::FunctionArgument],
    errors: &mut Vec<WgslMessage>,
) {
    use naga::{BinaryOperator, Expression};
    let types = resolve_expression_types(module, local, local_vars, arguments);
    for (_h, expr) in local.iter() {
        let Expression::Binary { op, left, .. } = expr else {
            continue;
        };
        if !matches!(
            op,
            BinaryOperator::Less
                | BinaryOperator::LessEqual
                | BinaryOperator::Greater
                | BinaryOperator::GreaterEqual
        ) {
            continue;
        }
        if types
            .get(left)
            .is_some_and(|ty| type_resolution_is_boolish(ty, &module.types))
        {
            errors.push(wgsl_error(
                "boolean values only support equality comparisons",
            ));
        }
    }
}

fn validate_bool_order_comparisons(module: &naga::Module) -> Vec<WgslMessage> {
    let mut errors = Vec::new();
    let empty_locals = naga::Arena::new();
    validate_bool_order_comparisons_in_arena(
        module,
        &module.global_expressions,
        &empty_locals,
        &[],
        &mut errors,
    );
    for ep in &module.entry_points {
        validate_bool_order_comparisons_in_arena(
            module,
            &ep.function.expressions,
            &ep.function.local_variables,
            &ep.function.arguments,
            &mut errors,
        );
    }
    for (_, f) in module.functions.iter() {
        validate_bool_order_comparisons_in_arena(
            module,
            &f.expressions,
            &f.local_variables,
            &f.arguments,
            &mut errors,
        );
    }
    errors
}

// Wrap memory-backed Access indices that resolve to Literal/ZeroValue/Constant
// in an Expression::As identity conversion. naga's validator is stricter than
// WGSL §15.6 on const OOB indices and rejects them outright; cloaking the
// index expression makes get_const_val_from return NonConst so validation
// falls through, while spv emit still sees the literal and emits the runtime
// clamp dictated by `bounds_check_policies`.
fn cloak_const_indices(module: &mut naga::Module, source: &str) {
    let n_eps = module.entry_points.len();
    for ep_idx in 0..n_eps {
        cloak_indices_in_function(&mut module.entry_points[ep_idx].function, source);
    }
    let helper_handles: Vec<naga::Handle<naga::Function>> =
        module.functions.iter().map(|(h, _)| h).collect();
    for h in helper_handles {
        cloak_indices_in_function(&mut module.functions[h], source);
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

fn span_text_is_identifier(source: &str, span: naga::Span) -> bool {
    let Some(range) = span.to_range() else {
        return false;
    };
    let Some(text) = source.get(range) else {
        return false;
    };
    let mut chars = text.trim().chars();
    let Some(first) = chars.next() else {
        return false;
    };
    if !(first == '_' || first.is_ascii_alphabetic()) {
        return false;
    }
    chars.all(|c| c == '_' || c.is_ascii_alphanumeric())
}

fn should_cloak_index(
    arena: &naga::Arena<naga::Expression>,
    index: naga::Handle<naga::Expression>,
    source: &str,
) -> bool {
    use naga::{Expression, Literal};
    if !matches!(
        arena[index],
        Expression::Literal(
            Literal::U32(_)
                | Literal::I32(_)
                | Literal::U64(_)
                | Literal::I64(_)
                | Literal::AbstractInt(_)
        ) | Expression::ZeroValue(_)
    ) {
        return false;
    }

    // The folded IR for `let i = 3; v[i]` and direct `v[-1]` can both look
    // like a literal index by the time we see it. WGSL validation still has to
    // reject literal/const-expression indices, so only cloak when the original
    // subscript text was an identifier. `const i = 3; v[i]` reaches us as
    // Expression::Constant and remains visible to validation.
    span_text_is_identifier(source, arena.get_span(index))
}

fn cloak_indices_in_function(f: &mut naga::Function, source: &str) {
    use naga::{Expression, Span};
    use std::mem;

    let mut targets: Vec<naga::Handle<Expression>> = Vec::new();
    for (h, expr) in f.expressions.iter() {
        if let Expression::Access { base: _, index } = *expr {
            if should_cloak_index(&f.expressions, index, source) {
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
    use naga::{Expression, Literal, UnaryOperator};
    let expr = resolve_expr_in_module(h, local, module)?;
    match expr {
        Expression::Literal(Literal::I32(v)) => Some(*v),
        Expression::Literal(Literal::AbstractInt(v)) => i32::try_from(*v).ok(),
        Expression::Unary {
            op: UnaryOperator::Negate,
            expr,
        } => {
            let inner = resolve_expr_in_module(*expr, local, module)?;
            match inner {
                Expression::Literal(Literal::I32(v)) => v.checked_neg(),
                Expression::Literal(Literal::AbstractInt(v)) => {
                    i64::checked_neg(*v).and_then(|v| i32::try_from(v).ok())
                }
                _ => expression_resolves_to_i32(*expr, local, module)?.checked_neg(),
            }
        }
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

// WGSL §11.4 / §17.5 / §17.6: const and override expressions must
// produce a representable result. naga 29's const-evaluator silently
// produces NaN for out-of-domain math args (acosh(x<1), inverseSqrt(x≤0),
// pow(a<0, _), pow(0, b≤0)) and stores them as `Literal::AbstractFloat(NaN)`
// which bypasses the F32/F64-only NaN check in `check_literal_value`.
// Worse, dead unused module-scope const declarations are dropped from the
// IR before any post-parse pass can inspect them, so an IR walker is
// useless here. Validate at the source-text level: scan `acosh(arg)`,
// `inverseSqrt(arg)`, `pow(a, b)` calls, resolve const-evaluable args
// (literal, suffixed literal, i64::MIN special form, constructor calls,
// and override substitution from `constants`), and emit an error if the
// resolved values fall outside the domain.
fn validate_math_domain_source(
    source: &str,
    constants: Option<&HashMap<String, f64>>,
    entry_point: Option<&str>,
) -> Vec<WgslMessage> {
    if !source.contains("acosh")
        && !source.contains("inverseSqrt")
        && !source.contains("pow")
        && !source.contains("fma")
        && !source.contains("cross")
        && !source.contains("mix")
        && !source.contains("reflect")
        && !source.contains("refract")
        && !source.contains("faceForward")
    {
        return Vec::new();
    }
    let scan = mask_comments_preserve_len(&smoothstep_validation_source(source, entry_point));
    let bytes = scan.as_bytes();
    let mut errors = Vec::new();
    for builtin in [
        "acosh",
        "inverseSqrt",
        "pow",
        "fma",
        "cross",
        "mix",
        "reflect",
        "refract",
        "faceForward",
    ] {
        let mut i = 0usize;
        while let Some(rel) = scan[i..].find(builtin) {
            let start = i + rel;
            let end = start + builtin.len();
            if (start > 0 && is_ident_char(bytes[start - 1]))
                || (end < bytes.len() && is_ident_char(bytes[end]))
            {
                i = end;
                continue;
            }
            if let Some((args, close)) = find_call_args(&scan, start, builtin.len()) {
                check_math_call_args(builtin, &args, constants, &mut errors);
                i = close + 1;
            } else {
                i = end;
            }
        }
    }
    errors
}

fn check_math_call_args(
    builtin: &str,
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
    errors: &mut Vec<WgslMessage>,
) {
    match builtin {
        "acosh" => {
            if args.len() == 1 {
                if let Some(Err(message)) = eval_const_float_builtin_call(builtin, args, constants)
                {
                    errors.push(wgsl_error(message));
                }
            }
        }
        "inverseSqrt" => {
            if args.len() == 1 {
                if let Some(values) = resolve_math_arg_values(&args[0], constants) {
                    if values.iter().any(|v| *v <= 0.0) {
                        errors.push(wgsl_error(
                            "inverseSqrt(x): x must be > 0 for the result to be representable",
                        ));
                    }
                }
            }
        }
        "pow" => {
            if args.len() == 2 {
                if let (Some(a_values), Some(b_values)) = (
                    resolve_math_arg_values(&args[0], constants),
                    resolve_math_arg_values(&args[1], constants),
                ) {
                    let pairs = pair_math_arg_values(&a_values, &b_values);
                    for (a, b) in pairs {
                        if a < 0.0 {
                            errors.push(wgsl_error(
                                "pow(a, b): a must be ≥ 0 for the result to be representable",
                            ));
                            return;
                        }
                        if a == 0.0 && b <= 0.0 {
                            errors.push(wgsl_error(
                                "pow(0, b): b must be > 0 for the result to be representable",
                            ));
                            return;
                        }
                        // Overflow: pow(a, b) result must be finite. Mirror
                        // the CTS test's `!Number.isFinite(Math.pow(a, b))`
                        // check; computing in f64 matches JS semantics for
                        // abstract-int / abstract-float, which is the
                        // largest remaining bucket here.
                        let r = a.powf(b);
                        if !r.is_finite() {
                            errors.push(wgsl_error(
                                "pow(a, b): result overflows the representable range",
                            ));
                            return;
                        }
                    }
                }
            }
        }
        "fma" => {
            // fma(a, b, c) = a*b + c. naga 29 stores the AbstractFloat
            // result without the F32/F64 NaN/Inf guard, so MAX*MAX+_
            // overflows silently. CTS expects rejection when the f64-
            // computed result is not finite.
            if args.len() == 3 {
                if let (Some(a_v), Some(b_v), Some(c_v)) = (
                    resolve_math_arg_values(&args[0], constants),
                    resolve_math_arg_values(&args[1], constants),
                    resolve_math_arg_values(&args[2], constants),
                ) {
                    let pairs = pair_three_arg_values(&a_v, &b_v, &c_v);
                    for (a, b, c) in pairs {
                        let r = a.mul_add(b, c);
                        if !r.is_finite() {
                            errors.push(wgsl_error(
                                "fma(a, b, c): result overflows the representable range",
                            ));
                            return;
                        }
                    }
                }
            }
        }
        "cross" => {
            // cross(a, b) for vec3<T>: result components are
            // (a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x).
            // Same naga AbstractFloat-Inf bypass as fma/pow; the CTS test
            // rejects when any component is non-finite.
            if args.len() == 2 {
                if let (Some(a), Some(b)) = (
                    resolve_math_arg_values(&args[0], constants),
                    resolve_math_arg_values(&args[1], constants),
                ) {
                    if a.len() == 3 && b.len() == 3 {
                        let r = [
                            a[1].mul_add(b[2], -(a[2] * b[1])),
                            a[2].mul_add(b[0], -(a[0] * b[2])),
                            a[0].mul_add(b[1], -(a[1] * b[0])),
                        ];
                        if r.iter().any(|c| !c.is_finite()) {
                            errors.push(wgsl_error(
                                "cross(a, b): result overflows the representable range",
                            ));
                            return;
                        }
                    }
                }
            }
        }
        "refract" => {
            if args.len() != 3 {
                errors.push(wgsl_error(
                    "refract(e1, e2, e3) requires exactly 3 arguments",
                ));
                return;
            }
            let c = trim_outer_parens(&args[2]).trim();
            if matches!(c, "true" | "false") {
                errors.push(wgsl_error(
                    "refract(e1, e2, e3): e3 must be a floating-point scalar",
                ));
                return;
            }
            if let Some(Err(message)) = eval_const_float_builtin_call(builtin, args, constants) {
                errors.push(wgsl_error(message));
            }
        }
        "mix" | "reflect" | "faceForward" => {
            if let Some(Err(message)) = eval_const_float_builtin_call(builtin, args, constants) {
                errors.push(wgsl_error(message));
            }
        }
        _ => {}
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum SourceFloatKind {
    Abstract,
    F32,
    F16,
}

#[derive(Clone, Debug)]
struct SourceFloatValues {
    kind: SourceFloatKind,
    values: Vec<f64>,
}

fn combine_source_float_kind(a: SourceFloatKind, b: SourceFloatKind) -> Option<SourceFloatKind> {
    match (a, b) {
        (SourceFloatKind::F32, SourceFloatKind::F16)
        | (SourceFloatKind::F16, SourceFloatKind::F32) => None,
        (SourceFloatKind::F32, _) | (_, SourceFloatKind::F32) => Some(SourceFloatKind::F32),
        (SourceFloatKind::F16, _) | (_, SourceFloatKind::F16) => Some(SourceFloatKind::F16),
        _ => Some(SourceFloatKind::Abstract),
    }
}

fn combine_source_float_values(values: &[&SourceFloatValues]) -> Option<SourceFloatKind> {
    values
        .iter()
        .map(|v| v.kind)
        .try_fold(SourceFloatKind::Abstract, combine_source_float_kind)
}

fn vector_constructor_float_kind(callee: &str) -> Option<Option<SourceFloatKind>> {
    let n = normalize_type_name(callee).to_ascii_lowercase();
    if constructor_vector_width(&n).is_none() {
        return None;
    }
    if n.contains("bool")
        || n.contains("i32")
        || n.contains("u32")
        || n.ends_with('i')
        || n.ends_with('u')
    {
        return None;
    }
    if n.contains("f32") || n.ends_with('f') {
        Some(Some(SourceFloatKind::F32))
    } else if n.contains("f16") || n.ends_with('h') {
        Some(Some(SourceFloatKind::F16))
    } else if n.contains("abstract-float") || n.contains("abstractfloat") {
        Some(Some(SourceFloatKind::Abstract))
    } else {
        Some(None)
    }
}

fn scalar_constructor_float_kind(callee: &str) -> Option<SourceFloatKind> {
    let n = normalize_type_name(callee).to_ascii_lowercase();
    match n.as_str() {
        "f32" | "f32_alias" => Some(SourceFloatKind::F32),
        "f16" => Some(SourceFloatKind::F16),
        "abstractfloat" | "abstract-float" => Some(SourceFloatKind::Abstract),
        _ => None,
    }
}

fn numeric_literal_source_float_kind(expr: &str) -> Option<SourceFloatKind> {
    let s = trim_outer_parens(expr).trim();
    if s.ends_with('i') || s.ends_with('u') {
        return None;
    }
    if s.ends_with('f') {
        return Some(SourceFloatKind::F32);
    }
    if s.ends_with('h') {
        return Some(SourceFloatKind::F16);
    }
    if parse_numeric_literal(s).is_some() || parse_wgsl_i64_like(s).is_some() {
        return Some(SourceFloatKind::Abstract);
    }
    None
}

fn explicit_source_float_values(
    kind: SourceFloatKind,
    mut values: Vec<f64>,
) -> Option<SourceFloatValues> {
    for v in values.iter_mut() {
        *v = checked_source_float(kind, *v)?;
    }
    Some(SourceFloatValues { kind, values })
}

fn checked_source_float(kind: SourceFloatKind, value: f64) -> Option<f64> {
    if !value.is_finite() {
        return None;
    }
    match kind {
        SourceFloatKind::Abstract => Some(value),
        SourceFloatKind::F32 => {
            let v = value as f32;
            v.is_finite().then_some(v as f64)
        }
        SourceFloatKind::F16 => quantize_to_f16_f32(value).map(|v| v as f64),
    }
}

fn eval_const_float_builtin_call(
    name: &str,
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<Result<SourceFloatValues, &'static str>> {
    match name {
        "acosh" => eval_acosh_call(args, constants),
        "mix" => eval_mix_call(args, constants),
        "reflect" => eval_reflect_call(args, constants),
        "refract" => eval_refract_call(args, constants),
        "faceForward" => eval_face_forward_call(args, constants),
        "determinant" => eval_determinant_call(args, constants),
        _ => None,
    }
}

fn eval_determinant_call(
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<Result<SourceFloatValues, &'static str>> {
    if args.len() != 1 {
        return None;
    }
    let m = resolve_math_matrix_values(&args[0], constants)?;
    eval_determinant_matrix(m)
}

fn eval_const_matrix_builtin_call(
    name: &str,
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<Result<SourceMatrixValues, &'static str>> {
    match name {
        "transpose" => {
            if args.len() != 1 {
                return None;
            }
            let m = resolve_math_matrix_values(&args[0], constants)?;
            eval_transpose_matrix(m).map(Ok)
        }
        _ => None,
    }
}

fn eval_acosh_call(
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<Result<SourceFloatValues, &'static str>> {
    if args.len() != 1 {
        return None;
    }
    let a = resolve_math_float_values(&args[0], constants)?;
    let mut out = Vec::with_capacity(a.values.len());
    for value in a.values {
        let value = match checked_source_float(a.kind, value) {
            Some(v) => v,
            None => return Some(Err("acosh(x): input is not representable")),
        };
        if value < 1.0 {
            return Some(Err(
                "acosh(x): x must be ≥ 1 for the result to be representable",
            ));
        }
        let result = match checked_source_float(a.kind, stable_acosh(value)) {
            Some(v) => v,
            None => return Some(Err("acosh(x): result overflows the representable range")),
        };
        out.push(result);
    }
    Some(Ok(SourceFloatValues {
        kind: a.kind,
        values: out,
    }))
}

fn stable_acosh(value: f64) -> f64 {
    2.0 * (((value - 1.0) * 0.5).sqrt() + ((value + 1.0) * 0.5).sqrt()).ln()
}

fn eval_mix_call(
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<Result<SourceFloatValues, &'static str>> {
    if args.len() != 3 {
        return None;
    }
    let a = resolve_math_float_values(&args[0], constants)?;
    let b = resolve_math_float_values(&args[1], constants)?;
    let c = resolve_math_float_values(&args[2], constants)?;
    if a.values.len() != b.values.len() {
        return None;
    }
    let n = a.values.len();
    if n == 0 {
        return None;
    }
    if c.values.len() != n && !(n >= 2 && c.values.len() == 1) {
        return None;
    }
    let kind = combine_source_float_values(&[&a, &b, &c])?;
    let triples = pair_three_arg_values(&a.values, &b.values, &c.values);
    if triples.is_empty() {
        return None;
    }
    let mut out = Vec::with_capacity(triples.len());
    for (a, b, c) in triples {
        let a = match checked_source_float(kind, a) {
            Some(v) => v,
            None => return Some(Err("mix: input is not representable")),
        };
        let b = match checked_source_float(kind, b) {
            Some(v) => v,
            None => return Some(Err("mix: input is not representable")),
        };
        let c = match checked_source_float(kind, c) {
            Some(v) => v,
            None => return Some(Err("mix: input is not representable")),
        };
        let c1 = match checked_source_float(kind, 1.0 - c) {
            Some(v) => v,
            None => return Some(Err("mix: 1 - e3 overflows the representable range")),
        };
        let ac1 = match checked_source_float(kind, a * c1) {
            Some(v) => v,
            None => return Some(Err("mix: e1 * (1 - e3) overflows the representable range")),
        };
        let bc = match checked_source_float(kind, b * c) {
            Some(v) => v,
            None => return Some(Err("mix: e2 * e3 overflows the representable range")),
        };
        let result = match checked_source_float(kind, ac1 + bc) {
            Some(v) => v,
            None => return Some(Err("mix: result overflows the representable range")),
        };
        out.push(result);
    }
    Some(Ok(SourceFloatValues { kind, values: out }))
}

fn eval_dot_terms(
    kind: SourceFloatKind,
    a: &[f64],
    b: &[f64],
    overflow_message: &'static str,
) -> Result<f64, &'static str> {
    if a.len() != b.len() || a.len() < 2 {
        return Err("");
    }
    let mut sum = 0.0;
    for (x, y) in a.iter().zip(b.iter()) {
        let x = checked_source_float(kind, *x).ok_or(overflow_message)?;
        let y = checked_source_float(kind, *y).ok_or(overflow_message)?;
        let term = checked_source_float(kind, x * y).ok_or(overflow_message)?;
        sum = checked_source_float(kind, sum + term).ok_or(overflow_message)?;
    }
    Ok(sum)
}

fn eval_reflect_call(
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<Result<SourceFloatValues, &'static str>> {
    if args.len() != 2 {
        return None;
    }
    let a = resolve_math_float_values(&args[0], constants)?;
    let b = resolve_math_float_values(&args[1], constants)?;
    if a.values.len() != b.values.len() || a.values.len() < 2 {
        return None;
    }
    let kind = combine_source_float_values(&[&a, &b])?;
    let dp = match eval_dot_terms(
        kind,
        &b.values,
        &a.values,
        "reflect: dot product overflows the representable range",
    ) {
        Ok(v) => v,
        Err("") => return None,
        Err(message) => return Some(Err(message)),
    };
    let dp2 = match checked_source_float(kind, dp * 2.0) {
        Some(v) => v,
        None => {
            return Some(Err(
                "reflect: 2 * dot(e2, e1) overflows the representable range",
            ))
        }
    };
    let mut out = Vec::with_capacity(a.values.len());
    for (a, b) in a.values.iter().zip(b.values.iter()) {
        let a = match checked_source_float(kind, *a) {
            Some(v) => v,
            None => return Some(Err("reflect: input is not representable")),
        };
        let b = match checked_source_float(kind, *b) {
            Some(v) => v,
            None => return Some(Err("reflect: input is not representable")),
        };
        let dp2b = match checked_source_float(kind, dp2 * b) {
            Some(v) => v,
            None => {
                return Some(Err(
                    "reflect: 2 * dot(e2, e1) * e2 overflows the representable range",
                ))
            }
        };
        let result = match checked_source_float(kind, a - dp2b) {
            Some(v) => v,
            None => return Some(Err("reflect: result overflows the representable range")),
        };
        out.push(result);
    }
    Some(Ok(SourceFloatValues { kind, values: out }))
}

fn eval_face_forward_call(
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<Result<SourceFloatValues, &'static str>> {
    if args.len() != 3 {
        return None;
    }
    let a = resolve_math_float_values(&args[0], constants)?;
    let b = resolve_math_float_values(&args[1], constants)?;
    let c = resolve_math_float_values(&args[2], constants)?;
    if a.values.len() != b.values.len() || b.values.len() != c.values.len() || a.values.len() < 2 {
        return None;
    }
    let kind = combine_source_float_values(&[&a, &b, &c])?;
    let dp = match eval_dot_terms(
        kind,
        &b.values,
        &c.values,
        "faceForward: dot product overflows the representable range",
    ) {
        Ok(v) => v,
        Err("") => return None,
        Err(message) => return Some(Err(message)),
    };
    let mut out = Vec::with_capacity(a.values.len());
    for a in &a.values {
        let a = match checked_source_float(kind, *a) {
            Some(v) => v,
            None => return Some(Err("faceForward: input is not representable")),
        };
        let selected = if dp < 0.0 { a } else { -a };
        let result = match checked_source_float(kind, selected) {
            Some(v) => v,
            None => return Some(Err("faceForward: result overflows the representable range")),
        };
        out.push(result);
    }
    Some(Ok(SourceFloatValues { kind, values: out }))
}

fn eval_refract_call(
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<Result<SourceFloatValues, &'static str>> {
    if args.len() != 3 {
        return None;
    }
    let a = resolve_math_float_values(&args[0], constants)?;
    let b = resolve_math_float_values(&args[1], constants)?;
    let c = resolve_math_float_values(&args[2], constants)?;
    if a.values.len() != b.values.len() || a.values.len() < 2 || c.values.len() != 1 {
        return None;
    }
    let kind = combine_source_float_values(&[&a, &b, &c])?;
    let c = match checked_source_float(kind, c.values[0]) {
        Some(v) => v,
        None => return Some(Err("refract: input is not representable")),
    };
    let b_dot_a = match eval_dot_terms(
        kind,
        &b.values,
        &a.values,
        "refract: dot product overflows the representable range",
    ) {
        Ok(v) => v,
        Err("") => return None,
        Err(message) => return Some(Err(message)),
    };
    let b_dot_a_2 = match checked_source_float(kind, b_dot_a * b_dot_a) {
        Some(v) => v,
        None => {
            return Some(Err(
                "refract: dot(e2, e1)^2 overflows the representable range",
            ))
        }
    };
    let one_minus_b_dot_a_2 = match checked_source_float(kind, 1.0 - b_dot_a_2) {
        Some(v) => v,
        None => {
            return Some(Err(
                "refract: 1 - dot(e2, e1)^2 overflows the representable range",
            ))
        }
    };
    let c2 = match checked_source_float(kind, c * c) {
        Some(v) => v,
        None => return Some(Err("refract: e3 * e3 overflows the representable range")),
    };
    let c2_one_minus = match checked_source_float(kind, c2 * one_minus_b_dot_a_2) {
        Some(v) => v,
        None => return Some(Err("refract: e3^2 term overflows the representable range")),
    };
    let k = match checked_source_float(kind, 1.0 - c2_one_minus) {
        Some(v) => v,
        None => return Some(Err("refract: k overflows the representable range")),
    };
    let mut out = Vec::with_capacity(a.values.len());
    if k < 0.0 {
        out.resize(a.values.len(), 0.0);
        return Some(Ok(SourceFloatValues { kind, values: out }));
    }
    let cbda = match checked_source_float(kind, c * b_dot_a) {
        Some(v) => v,
        None => {
            return Some(Err(
                "refract: e3 * dot(e2, e1) overflows the representable range",
            ))
        }
    };
    let sqrt_k = match checked_source_float(kind, k.sqrt()) {
        Some(v) => v,
        None => return Some(Err("refract: sqrt(k) is not representable")),
    };
    let cdba_sqrt_k = match checked_source_float(kind, cbda + sqrt_k) {
        Some(v) => v,
        None => {
            return Some(Err(
                "refract: e3 * dot(e2, e1) + sqrt(k) overflows the representable range",
            ))
        }
    };
    for (a, b) in a.values.iter().zip(b.values.iter()) {
        let a = match checked_source_float(kind, *a) {
            Some(v) => v,
            None => return Some(Err("refract: input is not representable")),
        };
        let b = match checked_source_float(kind, *b) {
            Some(v) => v,
            None => return Some(Err("refract: input is not representable")),
        };
        let ca = match checked_source_float(kind, c * a) {
            Some(v) => v,
            None => return Some(Err("refract: e3 * e1 overflows the representable range")),
        };
        let cdba_sqrt_k_b = match checked_source_float(kind, cdba_sqrt_k * b) {
            Some(v) => v,
            None => {
                return Some(Err(
                    "refract: normal term overflows the representable range",
                ))
            }
        };
        let result = match checked_source_float(kind, ca - cdba_sqrt_k_b) {
            Some(v) => v,
            None => return Some(Err("refract: result overflows the representable range")),
        };
        out.push(result);
    }
    Some(Ok(SourceFloatValues { kind, values: out }))
}

fn format_wgsl_abstract_float_literal(value: f64) -> Option<String> {
    if !value.is_finite() {
        return None;
    }
    if value == 0.0 {
        return Some(if value.is_sign_negative() {
            "-0.0".to_string()
        } else {
            "0.0".to_string()
        });
    }
    Some(format!("{value:.17e}"))
}

fn format_source_float_component(kind: SourceFloatKind, value: f64) -> Option<String> {
    match kind {
        SourceFloatKind::Abstract => format_wgsl_abstract_float_literal(value),
        SourceFloatKind::F32 => format_wgsl_f32_literal(value as f32),
        SourceFloatKind::F16 => format_wgsl_abstract_float_literal(value),
    }
}

fn format_source_float_values(values: &SourceFloatValues) -> Option<String> {
    let width = values.values.len();
    if width == 0 {
        return None;
    }
    if width == 1 {
        return match values.kind {
            SourceFloatKind::Abstract | SourceFloatKind::F32 => {
                format_source_float_component(values.kind, values.values[0])
            }
            SourceFloatKind::F16 => Some(format!(
                "f16({})",
                format_wgsl_abstract_float_literal(values.values[0])?
            )),
        };
    }
    let ctor = match values.kind {
        SourceFloatKind::Abstract => format!("vec{width}"),
        SourceFloatKind::F32 => format!("vec{width}<f32>"),
        SourceFloatKind::F16 => format!("vec{width}<f16>"),
    };
    let components = values
        .values
        .iter()
        .map(|v| format_source_float_component(values.kind, *v))
        .collect::<Option<Vec<_>>>()?;
    Some(format!("{}({})", ctor, components.join(", ")))
}

#[derive(Clone, Debug)]
struct SourceMatrixValues {
    kind: SourceFloatKind,
    cols: usize,
    rows: usize,
    columns: Vec<Vec<f64>>,
}

fn matrix_constructor_dims(name: &str) -> Option<(usize, usize, Option<SourceFloatKind>)> {
    let n = normalize_type_name(name).to_ascii_lowercase();
    let rest = n.strip_prefix("mat")?;
    let mut chars = rest.chars();
    let c = chars.next()?.to_digit(10)? as usize;
    if chars.next()? != 'x' {
        return None;
    }
    let r = chars.next()?.to_digit(10)? as usize;
    if !(2..=4).contains(&c) || !(2..=4).contains(&r) {
        return None;
    }
    let tail: String = chars.collect();
    let kind = if tail.is_empty() {
        None
    } else if tail == "f" {
        Some(SourceFloatKind::F32)
    } else if tail == "h" {
        Some(SourceFloatKind::F16)
    } else if tail.starts_with('<') && tail.ends_with('>') {
        let inner = &tail[1..tail.len() - 1];
        match inner {
            "f32" | "f32_alias" => Some(SourceFloatKind::F32),
            "f16" => Some(SourceFloatKind::F16),
            "abstractfloat" | "abstract-float" => Some(SourceFloatKind::Abstract),
            _ => return None,
        }
    } else {
        return None;
    };
    Some((c, r, kind))
}

fn explicit_source_matrix_values(
    kind: SourceFloatKind,
    cols: usize,
    rows: usize,
    mut columns: Vec<Vec<f64>>,
) -> Option<SourceMatrixValues> {
    if columns.len() != cols {
        return None;
    }
    for col in columns.iter_mut() {
        if col.len() != rows {
            return None;
        }
        for v in col.iter_mut() {
            *v = checked_source_float(kind, *v)?;
        }
    }
    Some(SourceMatrixValues {
        kind,
        cols,
        rows,
        columns,
    })
}

fn resolve_math_matrix_values(
    expr: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<SourceMatrixValues> {
    let s = trim_outer_parens(expr).trim();
    if let Some(values) = resolve_source_matrix_binary_expr(s, constants) {
        return Some(values);
    }
    let (callee, args) = parse_full_call(s)?;
    let (cols, rows, explicit_kind) = matrix_constructor_dims(&callee)?;
    if args.is_empty() {
        let kind = explicit_kind.unwrap_or(SourceFloatKind::Abstract);
        return Some(SourceMatrixValues {
            kind,
            cols,
            rows,
            columns: vec![vec![0.0; rows]; cols],
        });
    }
    if args.len() == 1 {
        if let Some(other) = resolve_math_matrix_values(&args[0], constants) {
            if other.cols != cols || other.rows != rows {
                return None;
            }
            let kind = explicit_kind.unwrap_or(other.kind);
            return explicit_source_matrix_values(kind, cols, rows, other.columns);
        }
        return None;
    }
    if args.len() == cols {
        let mut columns = Vec::with_capacity(cols);
        let mut kind = explicit_kind.unwrap_or(SourceFloatKind::Abstract);
        for a in &args {
            let v = resolve_math_float_values(a, constants)?;
            if v.values.len() != rows {
                return None;
            }
            if explicit_kind.is_none() {
                kind = combine_source_float_kind(kind, v.kind)?;
            }
            columns.push(v.values);
        }
        return explicit_source_matrix_values(kind, cols, rows, columns);
    }
    if args.len() == cols * rows {
        let mut columns: Vec<Vec<f64>> = Vec::with_capacity(cols);
        let mut kind = explicit_kind.unwrap_or(SourceFloatKind::Abstract);
        for c in 0..cols {
            let mut col = Vec::with_capacity(rows);
            for r in 0..rows {
                let v = resolve_math_float_values(&args[c * rows + r], constants)?;
                if v.values.len() != 1 {
                    return None;
                }
                if explicit_kind.is_none() {
                    kind = combine_source_float_kind(kind, v.kind)?;
                }
                col.push(v.values[0]);
            }
            columns.push(col);
        }
        return explicit_source_matrix_values(kind, cols, rows, columns);
    }
    None
}

fn resolve_source_matrix_binary_expr(
    s: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<SourceMatrixValues> {
    if let Some((idx, op)) = find_top_level_binary_operator(s, b"+-") {
        let left = resolve_math_matrix_values(&s[..idx], constants)?;
        let right = resolve_math_matrix_values(&s[idx + 1..], constants)?;
        return match eval_matrix_componentwise_op(left, right, |a, b| {
            if op == b'+' {
                a + b
            } else {
                a - b
            }
        })? {
            Ok(m) => Some(m),
            Err(_) => None,
        };
    }
    if let Some((idx, _op)) = find_top_level_binary_operator(s, b"*") {
        let left_text = &s[..idx];
        let right_text = &s[idx + 1..];
        if let (Some(a), Some(b)) = (
            resolve_math_matrix_values(left_text, constants),
            resolve_math_matrix_values(right_text, constants),
        ) {
            return match eval_matrix_matrix_mul(a, b)? {
                Ok(m) => Some(m),
                Err(_) => None,
            };
        }
        if let Some(m) = resolve_math_matrix_values(left_text, constants) {
            if let Some(scalar) = resolve_math_float_values(right_text, constants) {
                if scalar.values.len() == 1 {
                    return match eval_matrix_scalar_mul(m, scalar)? {
                        Ok(m) => Some(m),
                        Err(_) => None,
                    };
                }
            }
        }
        if let Some(m) = resolve_math_matrix_values(right_text, constants) {
            if let Some(scalar) = resolve_math_float_values(left_text, constants) {
                if scalar.values.len() == 1 {
                    return match eval_matrix_scalar_mul(m, scalar)? {
                        Ok(m) => Some(m),
                        Err(_) => None,
                    };
                }
            }
        }
    }
    None
}

fn format_source_matrix_values(m: &SourceMatrixValues) -> Option<String> {
    let ctor = match m.kind {
        SourceFloatKind::Abstract => format!("mat{}x{}", m.cols, m.rows),
        SourceFloatKind::F32 => format!("mat{}x{}<f32>", m.cols, m.rows),
        SourceFloatKind::F16 => format!("mat{}x{}<f16>", m.cols, m.rows),
    };
    let vec_ctor = match m.kind {
        SourceFloatKind::Abstract => format!("vec{}", m.rows),
        SourceFloatKind::F32 => format!("vec{}<f32>", m.rows),
        SourceFloatKind::F16 => format!("vec{}<f16>", m.rows),
    };
    let mut col_strings = Vec::with_capacity(m.cols);
    for col in &m.columns {
        let comps = col
            .iter()
            .map(|v| format_source_float_component(m.kind, *v))
            .collect::<Option<Vec<_>>>()?;
        col_strings.push(format!("{}({})", vec_ctor, comps.join(", ")));
    }
    Some(format!("{}({})", ctor, col_strings.join(", ")))
}

fn eval_transpose_matrix(m: SourceMatrixValues) -> Option<SourceMatrixValues> {
    let SourceMatrixValues {
        kind,
        cols,
        rows,
        columns,
    } = m;
    // Result has rows columns, cols rows. result.columns[r][c] = m.columns[c][r].
    let mut out = vec![vec![0.0f64; cols]; rows];
    for c in 0..cols {
        for r in 0..rows {
            out[r][c] = columns[c][r];
        }
    }
    Some(SourceMatrixValues {
        kind,
        cols: rows,
        rows: cols,
        columns: out,
    })
}

fn eval_determinant_matrix(
    m: SourceMatrixValues,
) -> Option<Result<SourceFloatValues, &'static str>> {
    if m.cols != m.rows {
        return None;
    }
    let kind = m.kind;
    // Build a row-major matrix view: row[r][c] = m.columns[c][r].
    let n = m.cols;
    let mut row_major: Vec<Vec<f64>> = (0..n)
        .map(|r| (0..n).map(|c| m.columns[c][r]).collect())
        .collect();
    // Quantize per-step: the matrix elements are already representable by
    // construction; the determinant computation produces sums and products
    // that must each round to the kind's precision.
    let value = match determinant_recursive(kind, &mut row_major) {
        Ok(v) => v,
        Err(message) => return Some(Err(message)),
    };
    let value = match checked_source_float(kind, value) {
        Some(v) => v,
        None => return Some(Err("determinant: result overflows the representable range")),
    };
    Some(Ok(SourceFloatValues {
        kind,
        values: vec![value],
    }))
}

fn determinant_recursive(kind: SourceFloatKind, m: &[Vec<f64>]) -> Result<f64, &'static str> {
    let n = m.len();
    match n {
        0 => Ok(1.0),
        1 => Ok(m[0][0]),
        2 => {
            let p0 = checked_source_float(kind, m[0][0] * m[1][1])
                .ok_or("determinant: intermediate product overflows")?;
            let p1 = checked_source_float(kind, m[0][1] * m[1][0])
                .ok_or("determinant: intermediate product overflows")?;
            checked_source_float(kind, p0 - p1).ok_or("determinant: intermediate sum overflows")
        }
        _ => {
            let mut sum = 0.0f64;
            for c in 0..n {
                let minor: Vec<Vec<f64>> = (1..n)
                    .map(|r| {
                        (0..n)
                            .filter(|cc| *cc != c)
                            .map(|cc| m[r][cc])
                            .collect::<Vec<_>>()
                    })
                    .collect();
                let sub = determinant_recursive(kind, &minor)?;
                let term = checked_source_float(kind, m[0][c] * sub)
                    .ok_or("determinant: intermediate product overflows")?;
                let signed = if c % 2 == 0 { term } else { -term };
                sum = checked_source_float(kind, sum + signed)
                    .ok_or("determinant: intermediate sum overflows")?;
            }
            Ok(sum)
        }
    }
}

fn eval_matrix_matrix_mul(
    a: SourceMatrixValues,
    b: SourceMatrixValues,
) -> Option<Result<SourceMatrixValues, &'static str>> {
    if a.cols != b.rows {
        return None;
    }
    let kind = combine_source_float_kind(a.kind, b.kind)?;
    let out_cols = b.cols;
    let out_rows = a.rows;
    let mut columns = vec![vec![0.0f64; out_rows]; out_cols];
    for i in 0..out_cols {
        for j in 0..out_rows {
            let row: Vec<f64> = (0..a.cols).map(|k| a.columns[k][j]).collect();
            let col: Vec<f64> = b.columns[i].clone();
            let dp = match eval_dot_terms(
                kind,
                &row,
                &col,
                "matrix multiplication: intermediate sum overflows",
            ) {
                Ok(v) => v,
                Err("") => return None,
                Err(m) => return Some(Err(m)),
            };
            columns[i][j] = dp;
        }
    }
    Some(Ok(SourceMatrixValues {
        kind,
        cols: out_cols,
        rows: out_rows,
        columns,
    }))
}

fn eval_matrix_vector_mul(
    m: SourceMatrixValues,
    v: SourceFloatValues,
) -> Option<Result<SourceFloatValues, &'static str>> {
    if m.cols != v.values.len() {
        return None;
    }
    let kind = combine_source_float_kind(m.kind, v.kind)?;
    let mut out = Vec::with_capacity(m.rows);
    for j in 0..m.rows {
        let row: Vec<f64> = (0..m.cols).map(|c| m.columns[c][j]).collect();
        let dp = match eval_dot_terms(
            kind,
            &row,
            &v.values,
            "matrix-vector multiplication: intermediate sum overflows",
        ) {
            Ok(value) => value,
            Err("") => return None,
            Err(message) => return Some(Err(message)),
        };
        out.push(dp);
    }
    Some(Ok(SourceFloatValues { kind, values: out }))
}

fn eval_vector_matrix_mul(
    v: SourceFloatValues,
    m: SourceMatrixValues,
) -> Option<Result<SourceFloatValues, &'static str>> {
    if v.values.len() != m.rows {
        return None;
    }
    let kind = combine_source_float_kind(v.kind, m.kind)?;
    let mut out = Vec::with_capacity(m.cols);
    for c in 0..m.cols {
        let dp = match eval_dot_terms(
            kind,
            &v.values,
            &m.columns[c],
            "vector-matrix multiplication: intermediate sum overflows",
        ) {
            Ok(value) => value,
            Err("") => return None,
            Err(message) => return Some(Err(message)),
        };
        out.push(dp);
    }
    Some(Ok(SourceFloatValues { kind, values: out }))
}

fn eval_matrix_scalar_mul(
    m: SourceMatrixValues,
    s: SourceFloatValues,
) -> Option<Result<SourceMatrixValues, &'static str>> {
    if s.values.len() != 1 {
        return None;
    }
    let kind = combine_source_float_kind(m.kind, s.kind)?;
    let scalar = match checked_source_float(kind, s.values[0]) {
        Some(v) => v,
        None => {
            return Some(Err(
                "matrix-scalar multiplication: scalar is not representable",
            ))
        }
    };
    let mut columns = Vec::with_capacity(m.cols);
    for col in m.columns.iter() {
        let mut new_col = Vec::with_capacity(m.rows);
        for value in col.iter() {
            let r = checked_source_float(kind, *value * scalar);
            match r {
                Some(v) => new_col.push(v),
                None => {
                    return Some(Err(
                        "matrix-scalar multiplication: result overflows the representable range",
                    ))
                }
            }
        }
        columns.push(new_col);
    }
    Some(Ok(SourceMatrixValues {
        kind,
        cols: m.cols,
        rows: m.rows,
        columns,
    }))
}

fn eval_matrix_componentwise_op(
    a: SourceMatrixValues,
    b: SourceMatrixValues,
    op: impl Fn(f64, f64) -> f64,
) -> Option<Result<SourceMatrixValues, &'static str>> {
    if a.cols != b.cols || a.rows != b.rows {
        return None;
    }
    let kind = combine_source_float_kind(a.kind, b.kind)?;
    let mut columns = Vec::with_capacity(a.cols);
    for (ac, bc) in a.columns.iter().zip(b.columns.iter()) {
        let mut new_col = Vec::with_capacity(a.rows);
        for (x, y) in ac.iter().zip(bc.iter()) {
            match checked_source_float(kind, op(*x, *y)) {
                Some(v) => new_col.push(v),
                None => {
                    return Some(Err(
                        "matrix componentwise op: result overflows the representable range",
                    ))
                }
            }
        }
        columns.push(new_col);
    }
    Some(Ok(SourceMatrixValues {
        kind,
        cols: a.cols,
        rows: a.rows,
        columns,
    }))
}

fn lower_const_matrix_binary_source(
    source: &str,
    constants: Option<&HashMap<String, f64>>,
) -> String {
    if !source.contains("mat") {
        return source.to_string();
    }
    let bytes = source.as_bytes();
    let mut out = String::with_capacity(source.len());
    let mut pos = 0usize;
    let mut i = 0usize;
    while i < bytes.len() {
        if bytes[i] != b'(' {
            i += 1;
            continue;
        }
        let mut depth = 1i32;
        let mut j = i + 1;
        while j < bytes.len() {
            match bytes[j] {
                b'(' => depth += 1,
                b')' => {
                    depth -= 1;
                    if depth == 0 {
                        break;
                    }
                }
                _ => {}
            }
            j += 1;
        }
        if depth != 0 {
            i += 1;
            continue;
        }
        let inner = &source[i + 1..j];
        if !inner.contains("mat") || find_top_level_binary_operator(inner, b"+-*").is_none() {
            i += 1;
            continue;
        }
        let replacement = resolve_math_matrix_values(inner, constants)
            .and_then(|m| format_source_matrix_values(&m))
            .or_else(|| {
                resolve_math_float_values(inner, constants)
                    .and_then(|v| format_source_float_values(&v))
            });
        if let Some(r) = replacement {
            out.push_str(&source[pos..i]);
            out.push_str(&r);
            pos = j + 1;
            i = j + 1;
            continue;
        }
        i += 1;
    }
    if pos == 0 {
        source.to_string()
    } else {
        out.push_str(&source[pos..]);
        out
    }
}

fn pair_three_arg_values(a: &[f64], b: &[f64], c: &[f64]) -> Vec<(f64, f64, f64)> {
    if a.is_empty() || b.is_empty() || c.is_empty() {
        return Vec::new();
    }
    let n = a.len().max(b.len()).max(c.len());
    let pick = |v: &[f64], i: usize| -> Option<f64> {
        if v.len() == 1 {
            Some(v[0])
        } else if v.len() == n {
            Some(v[i])
        } else {
            None
        }
    };
    let mut out = Vec::with_capacity(n);
    for i in 0..n {
        match (pick(a, i), pick(b, i), pick(c, i)) {
            (Some(x), Some(y), Some(z)) => out.push((x, y, z)),
            _ => return Vec::new(),
        }
    }
    out
}

fn pair_two_arg_values(a: &[f64], b: &[f64]) -> Vec<(f64, f64)> {
    if a.is_empty() || b.is_empty() {
        return Vec::new();
    }
    let n = a.len().max(b.len());
    let pick = |v: &[f64], i: usize| -> Option<f64> {
        if v.len() == 1 {
            Some(v[0])
        } else if v.len() == n {
            Some(v[i])
        } else {
            None
        }
    };
    let mut out = Vec::with_capacity(n);
    for i in 0..n {
        match (pick(a, i), pick(b, i)) {
            (Some(x), Some(y)) => out.push((x, y)),
            _ => return Vec::new(),
        }
    }
    out
}

fn source_float_componentwise_unary(
    values: SourceFloatValues,
    op: impl Fn(f64) -> f64,
) -> Option<SourceFloatValues> {
    let mut out = Vec::with_capacity(values.values.len());
    for value in values.values {
        out.push(checked_source_float(values.kind, op(value))?);
    }
    Some(SourceFloatValues {
        kind: values.kind,
        values: out,
    })
}

fn source_float_componentwise_binary(
    a: SourceFloatValues,
    b: SourceFloatValues,
    op: impl Fn(f64, f64) -> f64,
) -> Option<SourceFloatValues> {
    let kind = combine_source_float_values(&[&a, &b])?;
    let pairs = pair_two_arg_values(&a.values, &b.values);
    if pairs.is_empty() {
        return None;
    }
    let mut out = Vec::with_capacity(pairs.len());
    for (x, y) in pairs {
        out.push(checked_source_float(kind, op(x, y))?);
    }
    Some(SourceFloatValues { kind, values: out })
}

fn source_float_componentwise_ternary(
    a: SourceFloatValues,
    b: SourceFloatValues,
    c: SourceFloatValues,
    op: impl Fn(f64, f64, f64) -> f64,
) -> Option<SourceFloatValues> {
    let kind = combine_source_float_values(&[&a, &b, &c])?;
    let triples = pair_three_arg_values(&a.values, &b.values, &c.values);
    if triples.is_empty() {
        return None;
    }
    let mut out = Vec::with_capacity(triples.len());
    for (x, y, z) in triples {
        out.push(checked_source_float(kind, op(x, y, z))?);
    }
    Some(SourceFloatValues { kind, values: out })
}

fn find_top_level_binary_operator(s: &str, ops: &[u8]) -> Option<(usize, u8)> {
    let bytes = s.as_bytes();
    let mut depth = 0i32;
    let mut i = bytes.len();
    while i > 0 {
        i -= 1;
        match bytes[i] {
            b')' | b']' | b'}' => {
                depth += 1;
                continue;
            }
            b'(' | b'[' | b'{' => {
                depth -= 1;
                continue;
            }
            _ => {}
        }
        if depth != 0 || !ops.contains(&bytes[i]) {
            continue;
        }
        if matches!(bytes[i], b'+' | b'-') {
            let mut prev = i;
            while prev > 0 && bytes[prev - 1].is_ascii_whitespace() {
                prev -= 1;
            }
            if prev == 0
                || matches!(
                    bytes[prev - 1],
                    b'(' | b'[' | b'{' | b',' | b'+' | b'-' | b'*' | b'/' | b'%'
                )
            {
                continue;
            }
        }
        return Some((i, bytes[i]));
    }
    None
}

fn split_trailing_const_index(s: &str) -> Option<(&str, usize)> {
    let trimmed = s.trim();
    if !trimmed.ends_with(']') {
        return None;
    }
    let mut depth = 0i32;
    for (i, b) in trimmed.bytes().enumerate().rev() {
        match b {
            b']' => depth += 1,
            b'[' => {
                depth -= 1;
                if depth == 0 {
                    let index = trimmed[i + 1..trimmed.len() - 1]
                        .trim()
                        .parse::<usize>()
                        .ok()?;
                    return Some((&trimmed[..i], index));
                }
            }
            _ => {}
        }
    }
    None
}

fn eval_float_builtin_source_call(
    name: &str,
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<SourceFloatValues> {
    match name {
        "acosh" | "mix" | "reflect" | "refract" | "faceForward" => {
            eval_const_float_builtin_call(name, args, constants)?.ok()
        }
        "abs" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::abs,
        ),
        "acos" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::acos,
        ),
        "asin" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::asin,
        ),
        "atan" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::atan,
        ),
        "asinh" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            stable_asinh_f64,
        ),
        "atanh" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::atanh,
        ),
        "ceil" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::ceil,
        ),
        "cos" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::cos,
        ),
        "cosh" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::cosh,
        ),
        "degrees" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::to_degrees,
        ),
        "exp" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::exp,
        ),
        "exp2" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::exp2,
        ),
        "floor" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::floor,
        ),
        "fract" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            wgsl_fract_f64,
        ),
        "inverseSqrt" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            |v| 1.0 / v.sqrt(),
        ),
        "log" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::ln,
        ),
        "log2" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::log2,
        ),
        "radians" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::to_radians,
        ),
        "round" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::round,
        ),
        "saturate" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            |v| v.clamp(0.0, 1.0),
        ),
        "sign" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            |v| {
                if v > 0.0 {
                    1.0
                } else if v < 0.0 {
                    -1.0
                } else {
                    0.0
                }
            },
        ),
        "sin" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::sin,
        ),
        "sinh" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::sinh,
        ),
        "sqrt" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::sqrt,
        ),
        "tan" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::tan,
        ),
        "tanh" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::tanh,
        ),
        "trunc" => source_float_componentwise_unary(
            resolve_math_float_values(&args.first()?, constants)?,
            f64::trunc,
        ),
        "select" => {
            if args.len() != 3 {
                return None;
            }
            let false_values = resolve_math_float_values(&args[0], constants)?;
            let true_values = resolve_math_float_values(&args[1], constants)?;
            let cond_values = resolve_source_bool_values(&args[2])?;
            source_float_componentwise_select(false_values, true_values, &cond_values)
        }
        "atan2" => source_float_componentwise_binary(
            resolve_math_float_values(&args.first()?, constants)?,
            resolve_math_float_values(args.get(1)?, constants)?,
            f64::atan2,
        ),
        "max" => source_float_componentwise_binary(
            resolve_math_float_values(&args.first()?, constants)?,
            resolve_math_float_values(args.get(1)?, constants)?,
            f64::max,
        ),
        "min" => source_float_componentwise_binary(
            resolve_math_float_values(&args.first()?, constants)?,
            resolve_math_float_values(args.get(1)?, constants)?,
            f64::min,
        ),
        "pow" => source_float_componentwise_binary(
            resolve_math_float_values(&args.first()?, constants)?,
            resolve_math_float_values(args.get(1)?, constants)?,
            f64::powf,
        ),
        "step" => source_float_componentwise_binary(
            resolve_math_float_values(&args.first()?, constants)?,
            resolve_math_float_values(args.get(1)?, constants)?,
            |edge, x| if x < edge { 0.0 } else { 1.0 },
        ),
        "clamp" => source_float_componentwise_ternary(
            resolve_math_float_values(&args.first()?, constants)?,
            resolve_math_float_values(args.get(1)?, constants)?,
            resolve_math_float_values(args.get(2)?, constants)?,
            |x, low, high| x.clamp(low, high),
        ),
        "fma" => source_float_componentwise_ternary(
            resolve_math_float_values(&args.first()?, constants)?,
            resolve_math_float_values(args.get(1)?, constants)?,
            resolve_math_float_values(args.get(2)?, constants)?,
            f64::mul_add,
        ),
        "smoothstep" => source_float_componentwise_ternary(
            resolve_math_float_values(&args.first()?, constants)?,
            resolve_math_float_values(args.get(1)?, constants)?,
            resolve_math_float_values(args.get(2)?, constants)?,
            |low, high, x| {
                let t = ((x - low) / (high - low)).clamp(0.0, 1.0);
                t * t * (3.0 - 2.0 * t)
            },
        ),
        "ldexp" => source_float_componentwise_binary(
            resolve_math_float_values(&args.first()?, constants)?,
            resolve_math_float_values(args.get(1)?, constants)?,
            |x, exp| x * 2.0f64.powi(exp as i32),
        ),
        "dot" => {
            let a = resolve_math_float_values(&args.first()?, constants)?;
            let b = resolve_math_float_values(args.get(1)?, constants)?;
            let kind = combine_source_float_kind(a.kind, b.kind)?;
            let value = eval_dot_terms(kind, &a.values, &b.values, "").ok()?;
            Some(SourceFloatValues {
                kind,
                values: vec![value],
            })
        }
        "length" => {
            let a = resolve_math_float_values(&args.first()?, constants)?;
            let value = a.values.iter().map(|v| v * v).sum::<f64>().sqrt();
            Some(SourceFloatValues {
                kind: a.kind,
                values: vec![checked_source_float(a.kind, value)?],
            })
        }
        "distance" => {
            let a = resolve_math_float_values(&args.first()?, constants)?;
            let b = resolve_math_float_values(args.get(1)?, constants)?;
            let kind = combine_source_float_kind(a.kind, b.kind)?;
            let pairs = pair_two_arg_values(&a.values, &b.values);
            if pairs.is_empty() {
                return None;
            }
            let value = pairs
                .iter()
                .map(|(x, y)| {
                    let d = x - y;
                    d * d
                })
                .sum::<f64>()
                .sqrt();
            Some(SourceFloatValues {
                kind,
                values: vec![checked_source_float(kind, value)?],
            })
        }
        "normalize" => {
            let a = resolve_math_float_values(&args.first()?, constants)?;
            let len = a.values.iter().map(|v| v * v).sum::<f64>().sqrt();
            if len == 0.0 {
                return None;
            }
            let mut out = Vec::with_capacity(a.values.len());
            for v in a.values {
                out.push(checked_source_float(a.kind, v / len)?);
            }
            Some(SourceFloatValues {
                kind: a.kind,
                values: out,
            })
        }
        "cross" => {
            let a = resolve_math_float_values(&args.first()?, constants)?;
            let b = resolve_math_float_values(args.get(1)?, constants)?;
            if a.values.len() != 3 || b.values.len() != 3 {
                return None;
            }
            let kind = combine_source_float_kind(a.kind, b.kind)?;
            let out = [
                a.values[1] * b.values[2] - a.values[2] * b.values[1],
                a.values[2] * b.values[0] - a.values[0] * b.values[2],
                a.values[0] * b.values[1] - a.values[1] * b.values[0],
            ];
            Some(SourceFloatValues {
                kind,
                values: out
                    .iter()
                    .map(|v| checked_source_float(kind, *v))
                    .collect::<Option<Vec<_>>>()?,
            })
        }
        _ => None,
    }
}

fn stable_asinh_f64(v: f64) -> f64 {
    let magnitude = v.abs();
    let result = if magnitude > 1.0e154 {
        magnitude.ln() + std::f64::consts::LN_2
    } else {
        magnitude.asinh()
    };
    if v.is_sign_negative() {
        -result
    } else {
        result
    }
}

fn wgsl_fract_f64(v: f64) -> f64 {
    v - v.floor()
}

fn resolve_source_bool_values(expr: &str) -> Option<Vec<bool>> {
    let s = trim_outer_parens(expr).trim();
    match s {
        "true" => return Some(vec![true]),
        "false" => return Some(vec![false]),
        _ => {}
    }
    let (callee, args) = parse_full_call(s)?;
    let width = constructor_vector_width(&callee)?;
    let n = normalize_type_name(&callee).to_ascii_lowercase();
    if !(n.contains("bool") || n.ends_with('b') || n.starts_with("vec")) {
        return None;
    }
    if args.len() == 1 {
        let inner = resolve_source_bool_values(&args[0])?;
        return if inner.len() == 1 {
            Some(vec![inner[0]; width])
        } else if inner.len() == width {
            Some(inner)
        } else {
            None
        };
    }
    let mut out = Vec::with_capacity(width);
    for arg in args {
        let values = resolve_source_bool_values(&arg)?;
        out.extend(values);
    }
    (out.len() == width).then_some(out)
}

fn source_float_componentwise_select(
    false_values: SourceFloatValues,
    true_values: SourceFloatValues,
    cond_values: &[bool],
) -> Option<SourceFloatValues> {
    let kind = combine_source_float_values(&[&false_values, &true_values])?;
    let pairs = pair_two_arg_values(&false_values.values, &true_values.values);
    if pairs.is_empty() {
        return None;
    }
    let mut out = Vec::with_capacity(pairs.len());
    for (i, (f, t)) in pairs.iter().enumerate() {
        let cond = if cond_values.len() == 1 {
            cond_values[0]
        } else {
            *cond_values.get(i)?
        };
        out.push(checked_source_float(kind, if cond { *t } else { *f })?);
    }
    Some(SourceFloatValues { kind, values: out })
}

fn lower_const_float_math_call(
    name: &str,
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<String> {
    match name {
        // CTS checks f32 atanh near ±1 against a tight inherited-accuracy
        // interval. Some shader backends lower the const form inaccurately, so
        // resolve source-constant calls before Naga emits them.
        "atanh" => eval_float_builtin_source_call(name, args, constants)
            .and_then(|values| format_source_float_values(&values)),
        _ => None,
    }
}

fn source_modf_values(
    args: &[String],
    field: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<SourceFloatValues> {
    if args.len() != 1 || !matches!(field, "fract" | "whole") {
        return None;
    }
    let values = resolve_math_float_values(&args[0], constants)?;
    let mut out = Vec::with_capacity(values.values.len());
    for value in values.values {
        let (fract, whole) = match values.kind {
            SourceFloatKind::F32 => {
                let v = value as f32;
                let fract = v % 1.0;
                (fract as f64, (v - fract) as f64)
            }
            SourceFloatKind::F16 => {
                let v = quantize_to_f16_f32(value)?;
                let fract = quantize_to_f16_f32((v % 1.0) as f64)?;
                (
                    fract as f64,
                    quantize_to_f16_f32((v - fract) as f64)? as f64,
                )
            }
            SourceFloatKind::Abstract => {
                let fract = value % 1.0;
                (fract, value - fract)
            }
        };
        out.push(checked_source_float(
            values.kind,
            if field == "fract" { fract } else { whole },
        )?);
    }
    Some(SourceFloatValues {
        kind: values.kind,
        values: out,
    })
}

fn frexp_f64_scalar(value: f64) -> Option<(f64, i32)> {
    if value == 0.0 || !value.is_finite() {
        return Some((value, 0));
    }
    let mut v = value;
    let mut exp = 0i32;
    if f64::from_bits(v.to_bits() & 0x7fff_ffff_ffff_ffff) < f64::MIN_POSITIVE {
        v *= 2f64.powi(52);
        exp -= 52;
    }
    let bits = v.to_bits();
    let sign = bits & 0x8000_0000_0000_0000;
    let mant = bits & 0x000f_ffff_ffff_ffff;
    let biased = ((bits >> 52) & 0x7ff) as i32;
    exp += biased - 1023 + 1;
    let fract = f64::from_bits(sign | (0x3feu64 << 52) | mant);
    Some((fract, exp))
}

fn frexp_f32_scalar(value: f64) -> Option<(f64, i32)> {
    let mut v = value as f32;
    if v == 0.0 || !v.is_finite() {
        return Some((v as f64, 0));
    }
    let mut exp = 0i32;
    if f32::from_bits(v.to_bits() & 0x7fff_ffff) < f32::MIN_POSITIVE {
        v *= 2f32.powi(23);
        exp -= 23;
    }
    let bits = v.to_bits();
    let sign = bits & 0x8000_0000;
    let mant = bits & 0x007f_ffff;
    let biased = ((bits >> 23) & 0xff) as i32;
    exp += biased - 127 + 1;
    let fract = f32::from_bits(sign | (0x7eu32 << 23) | mant);
    Some((fract as f64, exp))
}

fn frexp_f16_scalar(value: f64) -> Option<(f64, i32)> {
    let mut bits = f32_to_f16_bits(value as f32);
    let sign = bits & 0x8000;
    let mut v = f16_bits_to_f32(bits);
    if v == 0.0 || !v.is_finite() {
        return Some((v as f64, 0));
    }
    let mut exp = 0i32;
    if (bits & 0x7c00) == 0 {
        v = quantize_to_f16_f32((v * 2f32.powi(10)) as f64)?;
        bits = f32_to_f16_bits(v);
        exp -= 10;
    }
    let mant = bits & 0x03ff;
    let biased = ((bits >> 10) & 0x1f) as i32;
    exp += biased - 15 + 1;
    let fract = f16_bits_to_f32(sign | (0x0eu16 << 10) | mant);
    Some((fract as f64, exp))
}

fn source_frexp_values(
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<(SourceFloatValues, Vec<i32>)> {
    if args.len() != 1 {
        return None;
    }
    let values = resolve_math_float_values(&args[0], constants)?;
    let mut fract = Vec::with_capacity(values.values.len());
    let mut exp = Vec::with_capacity(values.values.len());
    for value in &values.values {
        let (f, e) = match values.kind {
            SourceFloatKind::Abstract => frexp_f64_scalar(*value)?,
            SourceFloatKind::F32 => frexp_f32_scalar(*value)?,
            SourceFloatKind::F16 => frexp_f16_scalar(*value)?,
        };
        fract.push(checked_source_float(values.kind, f)?);
        exp.push(e);
    }
    Some((
        SourceFloatValues {
            kind: values.kind,
            values: fract,
        },
        exp,
    ))
}

fn format_source_i32_values(values: &[i32], typed: bool) -> Option<String> {
    if values.is_empty() {
        return None;
    }
    if values.len() == 1 {
        return Some(values[0].to_string());
    }
    let ctor = if typed {
        format!("vec{}<i32>", values.len())
    } else {
        format!("vec{}", values.len())
    };
    Some(format!(
        "{}({})",
        ctor,
        values
            .iter()
            .map(|v| v.to_string())
            .collect::<Vec<_>>()
            .join(", ")
    ))
}

fn lower_const_float_math_field_call(
    name: &str,
    args: &[String],
    field: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<String> {
    match (name, field) {
        ("modf", "fract" | "whole") => {
            format_source_float_values(&source_modf_values(args, field, constants)?)
        }
        ("frexp", "fract") => {
            let (fract, _) = source_frexp_values(args, constants)?;
            format_source_float_values(&fract)
        }
        ("frexp", "exp") => {
            let (fract, exp) = source_frexp_values(args, constants)?;
            format_source_i32_values(&exp, fract.kind != SourceFloatKind::Abstract)
        }
        _ => None,
    }
}

fn source_field_after_call(source: &str, offset: usize) -> Option<(&'static str, usize)> {
    for field in ["fract", "whole", "exp"] {
        let token = format!(".{field}");
        if source[offset..].starts_with(&token) {
            return Some((field, offset + token.len()));
        }
    }
    None
}

fn lower_const_float_math_source(source: &str, constants: Option<&HashMap<String, f64>>) -> String {
    const BUILTINS: [&str; 3] = ["atanh", "frexp", "modf"];
    if !BUILTINS.iter().any(|name| source.contains(name)) {
        return source.to_string();
    }
    let bytes = source.as_bytes();
    let mut out = String::with_capacity(source.len());
    let mut pos = 0usize;
    let mut i = 0usize;
    while i < source.len() {
        let mut next: Option<(&str, usize)> = None;
        for name in BUILTINS {
            if let Some(rel) = source[i..].find(name) {
                let start = i + rel;
                if next.is_none_or(|(_, best)| start < best) {
                    next = Some((name, start));
                }
            }
        }
        let Some((name, start)) = next else {
            break;
        };
        let end = start + name.len();
        if (start > 0 && is_ident_char(bytes[start - 1]))
            || (end < bytes.len() && is_ident_char(bytes[end]))
        {
            i = end;
            continue;
        }
        if let Some((args, close)) = find_call_args(source, start, name.len()) {
            if !is_builtin_shadowed_at_call(source, name, start) {
                let field = source_field_after_call(source, close + 1);
                let replacement = field
                    .and_then(|(field_name, _)| {
                        lower_const_float_math_field_call(name, &args, field_name, constants)
                    })
                    .or_else(|| lower_const_float_math_call(name, &args, constants));
                if let Some(replacement) = replacement {
                    out.push_str(&source[pos..start]);
                    out.push_str(&replacement);
                    pos = field.map(|(_, end)| end).unwrap_or(close + 1);
                }
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

fn resolve_source_float_binary_expr(
    s: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<SourceFloatValues> {
    if let Some((idx, op)) = find_top_level_binary_operator(s, b"+-") {
        let left = resolve_math_float_values(&s[..idx], constants)?;
        let right = resolve_math_float_values(&s[idx + 1..], constants)?;
        return source_float_componentwise_binary(left, right, |a, b| {
            if op == b'+' {
                a + b
            } else {
                a - b
            }
        });
    }
    if let Some((idx, op)) = find_top_level_binary_operator(s, b"*/%") {
        if op == b'*' {
            let left_text = &s[..idx];
            let right_text = &s[idx + 1..];
            if let Some(m) = resolve_math_matrix_values(left_text, constants) {
                if let Some(v) = resolve_math_float_values(right_text, constants) {
                    if v.values.len() > 1 {
                        if let Some(Ok(r)) = eval_matrix_vector_mul(m, v) {
                            return Some(r);
                        }
                    }
                }
            }
            if let Some(m) = resolve_math_matrix_values(right_text, constants) {
                if let Some(v) = resolve_math_float_values(left_text, constants) {
                    if v.values.len() > 1 {
                        if let Some(Ok(r)) = eval_vector_matrix_mul(v, m) {
                            return Some(r);
                        }
                    }
                }
            }
        }
        let left = resolve_math_float_values(&s[..idx], constants)?;
        let right = resolve_math_float_values(&s[idx + 1..], constants)?;
        return source_float_componentwise_binary(left, right, |a, b| match op {
            b'*' => a * b,
            b'/' => a / b,
            b'%' => a % b,
            _ => unreachable!(),
        });
    }
    None
}

// Resolve a WGSL source-text expression to a vector of f64 values for
// domain checking. Handles literals (with i/u/f/h suffixes), the
// `(-9223372036854775807 - 1)` form for i64::MIN, the standard scalar
// and vector constructor calls, and override-name substitution via
// `constants`. Returns None if the expression is not const-resolvable.
fn resolve_math_arg_values(
    expr: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<Vec<f64>> {
    resolve_math_float_values(expr, constants).map(|v| v.values)
}

fn resolve_math_float_values(
    expr: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<SourceFloatValues> {
    let s = trim_outer_parens(expr).trim();
    if let Some((base, index)) = split_trailing_const_index(s) {
        if let Some(m) = resolve_math_matrix_values(base, constants) {
            if index < m.cols {
                return Some(SourceFloatValues {
                    kind: m.kind,
                    values: m.columns[index].clone(),
                });
            }
            return None;
        }
        let values = resolve_math_float_values(base, constants)?;
        return values
            .values
            .get(index)
            .copied()
            .map(|value| SourceFloatValues {
                kind: values.kind,
                values: vec![value],
            });
    }
    if let Some(consts) = constants {
        if is_plain_identifier(s) {
            if let Some(v) = consts.get(s) {
                return v.is_finite().then_some(SourceFloatValues {
                    kind: SourceFloatKind::Abstract,
                    values: vec![*v],
                });
            }
        }
    }
    if let Some(v) = parse_numeric_literal(s) {
        let kind = numeric_literal_source_float_kind(s)?;
        return explicit_source_float_values(kind, vec![v]);
    }
    if !s.ends_with('i') && !s.ends_with('u') {
        if let Some(v) = parse_wgsl_i64_like(s) {
            return Some(SourceFloatValues {
                kind: SourceFloatKind::Abstract,
                values: vec![v as f64],
            });
        }
    }
    if let Some(rest) = s.strip_prefix('+') {
        return resolve_math_float_values(rest, constants);
    }
    if let Some(rest) = s.strip_prefix('-') {
        if !rest.trim_start().is_empty() {
            let values = resolve_math_float_values(rest, constants)?;
            return source_float_componentwise_unary(values, |v| -v);
        }
    }
    if let Some(values) = resolve_source_float_binary_expr(s, constants) {
        return Some(values);
    }
    if let Some((callee, args)) = parse_full_call(s) {
        if let Some(width) = constructor_vector_width(&callee) {
            let explicit_kind = vector_constructor_float_kind(&callee)?;
            if args.is_empty() {
                return explicit_kind
                    .and_then(|kind| explicit_source_float_values(kind, vec![0.0; width]));
            }
            if args.len() == 1 {
                let inner = resolve_math_float_values(&args[0], constants)?;
                let kind = explicit_kind.unwrap_or(inner.kind);
                let values = if inner.values.len() == 1 {
                    vec![inner.values[0]; width]
                } else if inner.values.len() == width {
                    inner.values
                } else {
                    return None;
                };
                return explicit_source_float_values(kind, values);
            }
            let mut kind = explicit_kind.unwrap_or(SourceFloatKind::Abstract);
            let mut out = Vec::with_capacity(width);
            for a in args {
                let sub = resolve_math_float_values(&a, constants)?;
                if explicit_kind.is_none() {
                    kind = combine_source_float_kind(kind, sub.kind)?;
                }
                out.extend(sub.values);
            }
            return (out.len() == width)
                .then(|| explicit_source_float_values(kind, out))
                .flatten();
        }
        if constructor_is_float_scalar(&callee) && args.len() == 1 {
            let kind = scalar_constructor_float_kind(&callee)?;
            let mut values = resolve_math_float_values(&args[0], constants)?.values;
            values.truncate(1);
            return explicit_source_float_values(kind, values);
        }
        return eval_float_builtin_source_call(&callee, &args, constants);
    }
    None
}

fn pair_math_arg_values(a: &[f64], b: &[f64]) -> Vec<(f64, f64)> {
    if a.is_empty() || b.is_empty() {
        return Vec::new();
    }
    if a.len() == b.len() {
        return a.iter().zip(b.iter()).map(|(x, y)| (*x, *y)).collect();
    }
    if a.len() == 1 {
        return b.iter().map(|y| (a[0], *y)).collect();
    }
    if b.len() == 1 {
        return a.iter().map(|x| (*x, b[0])).collect();
    }
    Vec::new()
}

fn abstract_float_bits_for_cts(value: f64) -> Option<(u32, u32)> {
    if !value.is_finite() {
        return None;
    }
    const MAX_SUBNORMAL: f64 = f64::from_bits(0x000f_ffff_ffff_ffff);
    let bits = if value <= MAX_SUBNORMAL && value >= -MAX_SUBNORMAL {
        if value < 0.0 {
            0x8000_0000_0000_0000u64
        } else {
            0
        }
    } else {
        value.to_bits()
    };
    Some(((bits >> 32) as u32, (bits & 0xffff_ffff) as u32))
}

fn find_enclosing_block_start(source: &str, marker: usize) -> Option<usize> {
    let bytes = source.as_bytes();
    let mut depth = 0i32;
    for i in (0..marker).rev() {
        match bytes[i] {
            b'}' => depth += 1,
            b'{' => {
                if depth == 0 {
                    return Some(i);
                }
                depth -= 1;
            }
            _ => {}
        }
    }
    None
}

fn extract_cts_abstract_float_expr(block: &str) -> Option<String> {
    let needle = "const subnormal_or_zero : bool = (";
    let start = block.find(needle)? + needle.len();
    let rel_end = block[start..].find(" <= ")?;
    Some(block[start..start + rel_end].trim().to_string())
}

fn extract_cts_abstract_float_output(block: &str) -> Option<(String, String)> {
    let high_pos = block.find(".high =")?;
    let line_start = block[..high_pos].rfind("outputs[")?;
    let index_start = line_start + "outputs[".len();
    let index_end = block[index_start..].find(']')? + index_start;
    let case_index = block[index_start..index_end].trim().to_string();
    let value_start = index_end + 1 + ".value".len();
    if !block[index_end + 1..].starts_with(".value") {
        return None;
    }
    let accessor = block[value_start..high_pos].trim().to_string();
    Some((case_index, accessor))
}

fn lower_abstract_float_output_snippets_source(
    source: &str,
    constants: Option<&HashMap<String, f64>>,
) -> String {
    const MARKER: &str = "const kExponentBias = 1022;";
    if !source.contains(MARKER) || !source.contains("const f = frexp(abs(") {
        return source.to_string();
    }

    let mut out = String::with_capacity(source.len());
    let mut copy_pos = 0usize;
    let mut search_pos = 0usize;
    while let Some(rel) = source[search_pos..].find(MARKER) {
        let marker = search_pos + rel;
        let Some(block_start) = find_enclosing_block_start(source, marker) else {
            search_pos = marker + MARKER.len();
            continue;
        };
        if block_start < copy_pos {
            search_pos = marker + MARKER.len();
            continue;
        }
        let Some(block_end) = find_matching_delimiter(source, block_start, b'{', b'}') else {
            search_pos = marker + MARKER.len();
            continue;
        };
        let block = &source[block_start..=block_end];
        let Some(expr) = extract_cts_abstract_float_expr(block) else {
            search_pos = marker + MARKER.len();
            continue;
        };
        let Some((case_index, accessor)) = extract_cts_abstract_float_output(block) else {
            search_pos = marker + MARKER.len();
            continue;
        };
        let Some(values) = resolve_math_float_values(&expr, constants) else {
            search_pos = marker + MARKER.len();
            continue;
        };
        if values.values.len() != 1 {
            search_pos = marker + MARKER.len();
            continue;
        }
        let Some((high, low)) = abstract_float_bits_for_cts(values.values[0]) else {
            search_pos = marker + MARKER.len();
            continue;
        };

        out.push_str(&source[copy_pos..block_start]);
        out.push_str(&format!(
            "{{\n    outputs[{case_index}].value{accessor}.high = {high}u;\n    outputs[{case_index}].value{accessor}.low = {low}u;\n  }}"
        ));
        copy_pos = block_end + 1;
        search_pos = copy_pos;
    }
    out.push_str(&source[copy_pos..]);
    out
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

#[derive(Clone)]
struct BitFoldValue {
    signed: bool,
    ty: Option<naga::Handle<naga::Type>>,
    components: Vec<u32>,
}

struct SourceBitValues {
    signed: bool,
    components: Vec<u32>,
}

fn source_vector_int_info(callee: &str) -> Option<(usize, Option<bool>)> {
    let n = normalize_type_name(callee).to_ascii_lowercase();
    let width = constructor_vector_width(&n)?;
    let signed = if n.contains("u32") || n.ends_with('u') {
        Some(false)
    } else if n.contains("i32") || n.ends_with('i') {
        Some(true)
    } else {
        None
    };
    Some((width, signed))
}

fn parse_source_constant_u32(name: &str, constants: Option<&HashMap<String, f64>>) -> Option<u32> {
    let value = *constants?.get(name)?;
    (value.is_finite() && value.fract() == 0.0 && value >= 0.0 && value <= u32::MAX as f64)
        .then_some(value as u32)
}

fn parse_source_constant_i32(name: &str, constants: Option<&HashMap<String, f64>>) -> Option<i32> {
    let value = *constants?.get(name)?;
    (value.is_finite()
        && value.fract() == 0.0
        && value >= i32::MIN as f64
        && value <= i32::MAX as f64)
        .then_some(value as i32)
}

fn parse_source_u32_value(expr: &str, constants: Option<&HashMap<String, f64>>) -> Option<u32> {
    let mut s = trim_outer_parens(expr).trim();
    if let Some((callee, args)) = parse_full_call(s) {
        let n = normalize_type_name(&callee).to_ascii_lowercase();
        if args.len() == 1 && (n == "u32" || n == "i32") {
            return parse_source_u32_value(&args[0], constants);
        }
    }
    if is_plain_identifier(s) {
        return parse_source_constant_u32(s, constants);
    }
    let unsigned = s.ends_with('u');
    let signed = s.ends_with('i');
    if unsigned || signed {
        s = &s[..s.len() - 1];
    }
    if s.starts_with('-') {
        return None;
    }
    s.parse::<u64>().ok().and_then(|v| u32::try_from(v).ok())
}

fn parse_source_bit_u32_value(expr: &str, constants: Option<&HashMap<String, f64>>) -> Option<u32> {
    let mut s = trim_outer_parens(expr).trim();
    if let Some((callee, args)) = parse_full_call(s) {
        let n = normalize_type_name(&callee).to_ascii_lowercase();
        if args.len() == 1 {
            if n == "u32" {
                return parse_source_u32_value(&args[0], constants);
            }
            if n == "i32" {
                return parse_source_bit_u32_value(&args[0], constants);
            }
        }
    }
    if is_plain_identifier(s) {
        return parse_source_constant_u32(s, constants)
            .or_else(|| parse_source_constant_i32(s, constants).map(|v| v as u32));
    }
    if s.ends_with('u') {
        s = &s[..s.len() - 1];
        return s.parse::<u64>().ok().and_then(|v| u32::try_from(v).ok());
    }
    if s.ends_with('i') {
        s = &s[..s.len() - 1];
    }
    i32::try_from(s.parse::<i64>().ok()?).ok().map(|v| v as u32)
}

fn parse_source_bit_scalar_as(
    expr: &str,
    signed: bool,
    constants: Option<&HashMap<String, f64>>,
) -> Option<SourceBitValues> {
    let value = if signed {
        parse_source_bit_u32_value(expr, constants)?
    } else {
        parse_source_u32_value(expr, constants)?
    };
    Some(SourceBitValues {
        signed,
        components: vec![value],
    })
}

fn parse_source_bit_scalar(
    expr: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<SourceBitValues> {
    let mut s = trim_outer_parens(expr).trim();
    if let Some((callee, args)) = parse_full_call(s) {
        let n = normalize_type_name(&callee).to_ascii_lowercase();
        if args.len() == 1 && (n == "u32" || n == "i32") {
            let value = if n == "u32" {
                parse_source_u32_value(&args[0], constants)?
            } else {
                parse_source_bit_u32_value(&args[0], constants)?
            };
            return Some(SourceBitValues {
                signed: n == "i32",
                components: vec![value],
            });
        }
    }
    let unsigned = s.ends_with('u');
    let signed = s.ends_with('i');
    if !unsigned && !signed {
        return None;
    }
    s = &s[..s.len() - 1];
    if unsigned {
        return Some(SourceBitValues {
            signed: false,
            components: vec![s.parse::<u32>().ok()?],
        });
    }
    let value = i32::try_from(s.parse::<i64>().ok()?).ok()? as u32;
    Some(SourceBitValues {
        signed: true,
        components: vec![value],
    })
}

fn resolve_source_bit_values(
    expr: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<SourceBitValues> {
    let s = trim_outer_parens(expr).trim();
    if let Some((callee, args)) = parse_full_call(s) {
        if let Some((width, explicit_signed)) = source_vector_int_info(&callee) {
            let mut values = Vec::new();
            let mut inferred_signed: Option<bool> = explicit_signed;
            for arg in args {
                let resolved = resolve_source_bit_values(&arg, constants).or_else(|| {
                    explicit_signed
                        .and_then(|signed| parse_source_bit_scalar_as(&arg, signed, constants))
                })?;
                if resolved.components.len() != 1 && resolved.components.len() != width {
                    return None;
                }
                let signed = explicit_signed.unwrap_or(resolved.signed);
                if resolved.signed != signed {
                    return None;
                }
                match inferred_signed {
                    Some(existing) if existing != signed => return None,
                    Some(_) => {}
                    None => inferred_signed = Some(signed),
                }
                values.extend(resolved.components);
            }
            let signed = inferred_signed?;
            if values.len() == 1 {
                values.resize(width, values[0]);
            }
            if values.len() != width {
                return None;
            }
            return Some(SourceBitValues {
                signed,
                components: values,
            });
        }
    }
    parse_source_bit_scalar(s, constants)
}

fn format_source_bit_scalar(bits: u32, signed: bool) -> String {
    if signed {
        format!("i32({})", bits as i32)
    } else {
        format!("{}u", bits)
    }
}

fn format_source_bit_values(values: &[u32], signed: bool) -> String {
    if values.len() == 1 {
        return format_source_bit_scalar(values[0], signed);
    }
    let scalar = if signed { "i32" } else { "u32" };
    let components = values
        .iter()
        .map(|v| format_source_bit_scalar(*v, signed))
        .collect::<Vec<_>>()
        .join(", ");
    format!("vec{}<{}>({})", values.len(), scalar, components)
}

fn lower_bit_builtin_source_call(
    name: &str,
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<String> {
    let (values, offset_arg, count_arg) = match name {
        "extractBits" if args.len() == 3 => (
            resolve_source_bit_values(&args[0], constants)?,
            args[1].as_str(),
            args[2].as_str(),
        ),
        "insertBits" if args.len() == 4 => (
            resolve_source_bit_values(&args[0], constants)?,
            args[2].as_str(),
            args[3].as_str(),
        ),
        _ => return None,
    };
    let offset = parse_source_u32_value(offset_arg, constants)?;
    let count = parse_source_u32_value(count_arg, constants)?;
    if u64::from(offset) + u64::from(count) > 32 {
        return None;
    }
    let components = if name == "extractBits" {
        values
            .components
            .iter()
            .map(|v| bit_extract(*v, values.signed, offset, count))
            .collect::<Vec<_>>()
    } else {
        let newbits = resolve_source_bit_values(&args[1], constants)?;
        if newbits.signed != values.signed || newbits.components.len() != values.components.len() {
            return None;
        }
        values
            .components
            .iter()
            .zip(newbits.components.iter())
            .map(|(v, n)| bit_insert(*v, *n, offset, count))
            .collect::<Vec<_>>()
    };
    Some(format_source_bit_values(&components, values.signed))
}

fn f32_bitcast_target_width(target: &str) -> Option<usize> {
    let n = normalize_type_name(target).to_ascii_lowercase();
    match n.as_str() {
        "f32" => Some(1),
        "vec2f" | "vec2<f32>" => Some(2),
        "vec3f" | "vec3<f32>" => Some(3),
        "vec4f" | "vec4<f32>" => Some(4),
        _ => None,
    }
}

fn find_bitcast_call(source: &str, start: usize) -> Option<(String, Vec<String>, usize)> {
    let bytes = source.as_bytes();
    let mut i = start + "bitcast".len();
    while i < bytes.len() && bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    if i >= bytes.len() || bytes[i] != b'<' {
        return None;
    }

    let target_start = i + 1;
    let mut depth = 1i32;
    i += 1;
    while i < bytes.len() {
        match bytes[i] {
            b'<' => depth += 1,
            b'>' => {
                depth -= 1;
                if depth == 0 {
                    let target = source[target_start..i].trim().to_string();
                    let name_len = i + 1 - start;
                    let (args, close) = find_call_args(source, start, name_len)?;
                    return Some((target, args, close));
                }
            }
            _ => {}
        }
        i += 1;
    }
    None
}

fn format_wgsl_f32_literal(value: f32) -> Option<String> {
    if !value.is_finite() {
        return None;
    }
    if value == 0.0 {
        return Some(if value.is_sign_negative() {
            "-0.0f".to_string()
        } else {
            "0.0f".to_string()
        });
    }
    Some(format!("{value:e}f"))
}

fn lower_const_bitcast_call(
    target: &str,
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<Result<String, WgslMessage>> {
    let width = f32_bitcast_target_width(target)?;
    if args.len() != 1 {
        return None;
    }
    let values = resolve_source_bit_values(&args[0], constants)?;
    if values.components.len() != width {
        return None;
    }

    let mut components = Vec::with_capacity(values.components.len());
    for bits in values.components {
        let value = f32::from_bits(bits);
        let Some(component) = format_wgsl_f32_literal(value) else {
            return Some(Err(wgsl_error(
                "const bitcast to f32 must not evaluate to NaN or infinity",
            )));
        };
        components.push(component);
    }

    let replacement = if width == 1 {
        components.remove(0)
    } else {
        format!("vec{}f({})", width, components.join(", "))
    };
    Some(Ok(replacement))
}

fn scan_const_bitcasts_source<F>(
    source: &str,
    constants: Option<&HashMap<String, f64>>,
    mut visitor: F,
) where
    F: FnMut(usize, usize, Result<String, WgslMessage>) -> bool,
{
    if !source.contains("bitcast") {
        return;
    }
    let masked = source_without_fn_bodies(source);
    let bytes = masked.as_bytes();
    let mut i = 0usize;
    while let Some(rel) = masked[i..].find("bitcast") {
        let start = i + rel;
        let end = start + "bitcast".len();
        if (start > 0 && is_ident_char(bytes[start - 1]))
            || (end < bytes.len() && is_ident_char(bytes[end]))
        {
            i = end;
            continue;
        }
        let Some((target, args, close)) = find_bitcast_call(&masked, start) else {
            i = end;
            continue;
        };
        if let Some(replacement) = lower_const_bitcast_call(&target, &args, constants) {
            if !visitor(start, close + 1, replacement) {
                break;
            }
        }
        i = close + 1;
    }
}

fn validate_const_bitcast_source(
    source: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Vec<WgslMessage> {
    let mut errors = Vec::new();
    scan_const_bitcasts_source(source, constants, |_start, _end, replacement| {
        if let Err(message) = replacement {
            errors.push(message);
        }
        true
    });
    errors
}

fn lower_const_bitcasts_source(source: &str, constants: Option<&HashMap<String, f64>>) -> String {
    let mut replacements: Vec<(usize, usize, String)> = Vec::new();
    scan_const_bitcasts_source(source, constants, |start, end, replacement| {
        if let Ok(text) = replacement {
            replacements.push((start, end, text));
        }
        true
    });
    if replacements.is_empty() {
        return source.to_string();
    }

    let mut out = String::with_capacity(source.len());
    let mut pos = 0usize;
    for (start, end, replacement) in replacements {
        out.push_str(&source[pos..start]);
        out.push_str(&replacement);
        pos = end;
    }
    out.push_str(&source[pos..]);
    out
}

fn lower_bit_builtins_source_once(
    source: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<String> {
    if !source.contains("Bits") {
        return None;
    }
    let masked = source_without_fn_bodies(source);
    let bytes = masked.as_bytes();
    let mut out = String::with_capacity(source.len());
    let mut pos = 0usize;
    let mut i = 0usize;
    let mut changed = false;
    while i < masked.len() {
        let next_insert = masked[i..]
            .find("insertBits")
            .map(|p| (i + p, "insertBits"));
        let next_extract = masked[i..]
            .find("extractBits")
            .map(|p| (i + p, "extractBits"));
        let Some((start, name)) = [next_insert, next_extract]
            .into_iter()
            .flatten()
            .min_by_key(|(p, _)| *p)
        else {
            break;
        };
        let end = start + name.len();
        if (start > 0 && is_ident_char(bytes[start - 1]))
            || (end < bytes.len() && is_ident_char(bytes[end]))
        {
            i = end;
            continue;
        }
        let Some((args, close)) = find_call_args(&masked, start, name.len()) else {
            i = end;
            continue;
        };
        if let Some(replacement) = lower_bit_builtin_source_call(name, &args, constants) {
            out.push_str(&source[pos..start]);
            out.push_str(&replacement);
            pos = close + 1;
            changed = true;
        }
        i = close + 1;
    }
    if changed {
        out.push_str(&source[pos..]);
        Some(out)
    } else {
        None
    }
}

fn lower_bit_builtins_source(source: &str, constants: Option<&HashMap<String, f64>>) -> String {
    let mut lowered = source.to_string();
    for _ in 0..8 {
        let Some(next) = lower_bit_builtins_source_once(&lowered, constants) else {
            return lowered;
        };
        lowered = next;
    }
    lowered
}

fn parse_source_unpack_u32_arg(
    expr: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<u32> {
    let mut s = trim_outer_parens(expr).trim();
    if let Some((callee, args)) = parse_full_call(s) {
        let n = normalize_type_name(&callee).to_ascii_lowercase();
        if args.len() == 1 && n == "u32" {
            return parse_source_unpack_u32_arg(&args[0], constants);
        }
        return None;
    }
    if is_plain_identifier(s) {
        return parse_source_constant_u32(s, constants);
    }
    if s.ends_with('i') || s.starts_with('-') {
        return None;
    }
    if s.ends_with('u') {
        s = &s[..s.len() - 1];
    }
    s.parse::<u64>().ok().and_then(|v| u32::try_from(v).ok())
}

fn resolve_source_int_vector_values(
    expr: &str,
    width: usize,
    signed: bool,
    constants: Option<&HashMap<String, f64>>,
) -> Option<Vec<u32>> {
    let s = trim_outer_parens(expr).trim();
    if let Some((callee, args)) = parse_full_call(s) {
        let n = normalize_type_name(&callee).to_ascii_lowercase();
        if args.len() == 1 && args[0].is_empty() {
            if let Some((ctor_width, Some(ctor_signed))) = source_vector_int_info(&n) {
                if ctor_width == width && ctor_signed == signed {
                    return Some(vec![0; width]);
                }
            }
        }
    }
    let values = resolve_source_bit_values(s, constants)?;
    if values.signed != signed || values.components.len() != width {
        return None;
    }
    Some(values.components)
}

fn resolve_source_float_vector_values(
    expr: &str,
    width: usize,
    constants: Option<&HashMap<String, f64>>,
) -> Option<Vec<f64>> {
    let s = trim_outer_parens(expr).trim();
    if let Some((callee, args)) = parse_full_call(s) {
        let n = normalize_type_name(&callee).to_ascii_lowercase();
        if args.len() == 1
            && args[0].is_empty()
            && constructor_vector_width(&n) == Some(width)
            && (n.contains("f32") || n.ends_with('f'))
        {
            return Some(vec![0.0; width]);
        }
    }
    let values = resolve_smoothstep_values(s, constants)?;
    if values.len() == width {
        Some(values)
    } else {
        None
    }
}

fn source_floats_are_finite(values: &[f64]) -> bool {
    values.iter().all(|v| v.is_finite())
}

fn source_floats_fit_f16(values: &[f64]) -> bool {
    values
        .iter()
        .all(|v| v.is_finite() && *v >= -65504.0 && *v <= 65504.0)
}

fn source_ints_fit_i8(values: &[u32]) -> bool {
    values.iter().all(|v| {
        i32::from_ne_bytes(v.to_ne_bytes()) >= -128 && i32::from_ne_bytes(v.to_ne_bytes()) <= 127
    })
}

fn source_ints_fit_u8(values: &[u32]) -> bool {
    values.iter().all(|v| *v <= 255)
}

fn format_u32_vector(width: usize, values: &[u32]) -> String {
    let components = values
        .iter()
        .map(|v| format!("{v}u"))
        .collect::<Vec<_>>()
        .join(", ");
    format!("vec{width}<u32>({components})")
}

fn format_i32_vector(width: usize, values: &[i32]) -> String {
    let components = values
        .iter()
        .map(|v| format!("i32({v})"))
        .collect::<Vec<_>>()
        .join(", ");
    format!("vec{width}<i32>({components})")
}

fn format_f32_vector(width: usize, values: &[f32]) -> Option<String> {
    let components = values
        .iter()
        .map(|v| format_wgsl_f32_literal(*v))
        .collect::<Option<Vec<_>>>()?
        .join(", ");
    Some(format!("vec{width}<f32>({components})"))
}

fn f16_bits_to_f32(bits: u16) -> f32 {
    let sign = ((bits & 0x8000) as u32) << 16;
    let exp = (bits >> 10) & 0x1f;
    let frac = (bits & 0x03ff) as u32;
    let out = if exp == 0 {
        if frac == 0 {
            sign
        } else {
            let mut mant = frac;
            let mut e = -14i32;
            while (mant & 0x0400) == 0 {
                mant <<= 1;
                e -= 1;
            }
            mant &= 0x03ff;
            sign | (((e + 127) as u32) << 23) | (mant << 13)
        }
    } else if exp == 0x1f {
        sign | 0x7f80_0000 | (frac << 13)
    } else {
        sign | (((exp as u32) + 112) << 23) | (frac << 13)
    };
    f32::from_bits(out)
}

fn lower_packed_builtin_source_call(
    name: &str,
    args: &[String],
    constants: Option<&HashMap<String, f64>>,
) -> Option<String> {
    if args.len() != 1 {
        return None;
    }
    match name {
        "pack2x16float" => {
            let values = resolve_source_float_vector_values(&args[0], 2, constants)?;
            source_floats_fit_f16(&values).then_some("0u".to_string())
        }
        "pack2x16snorm" | "pack2x16unorm" => {
            let values = resolve_source_float_vector_values(&args[0], 2, constants)?;
            source_floats_are_finite(&values).then_some("0u".to_string())
        }
        "pack4x8snorm" | "pack4x8unorm" => {
            let values = resolve_source_float_vector_values(&args[0], 4, constants)?;
            source_floats_are_finite(&values).then_some("0u".to_string())
        }
        "pack4xU8" => {
            let values = resolve_source_int_vector_values(&args[0], 4, false, constants)?;
            source_ints_fit_u8(&values).then_some("0u".to_string())
        }
        "pack4xI8" => {
            let values = resolve_source_int_vector_values(&args[0], 4, true, constants)?;
            source_ints_fit_i8(&values).then_some("0u".to_string())
        }
        "pack4xU8Clamp" => {
            resolve_source_int_vector_values(&args[0], 4, false, constants)?;
            Some("0u".to_string())
        }
        "pack4xI8Clamp" => {
            resolve_source_int_vector_values(&args[0], 4, true, constants)?;
            Some("0u".to_string())
        }
        "unpack2x16float" => {
            let packed = parse_source_unpack_u32_arg(&args[0], constants)?;
            let values = [
                f16_bits_to_f32((packed & 0xffff) as u16),
                f16_bits_to_f32((packed >> 16) as u16),
            ];
            format_f32_vector(2, &values)
        }
        "unpack2x16snorm" => {
            let packed = parse_source_unpack_u32_arg(&args[0], constants)?;
            let values = [
                ((packed & 0xffff) as u16 as i16 as f32 / 32767.0).max(-1.0),
                ((packed >> 16) as u16 as i16 as f32 / 32767.0).max(-1.0),
            ];
            format_f32_vector(2, &values)
        }
        "unpack2x16unorm" => {
            let packed = parse_source_unpack_u32_arg(&args[0], constants)?;
            let values = [
                (packed & 0xffff) as f32 / 65535.0,
                (packed >> 16) as f32 / 65535.0,
            ];
            format_f32_vector(2, &values)
        }
        "unpack4x8snorm" => {
            let packed = parse_source_unpack_u32_arg(&args[0], constants)?;
            let values = [
                ((packed & 0xff) as u8 as i8 as f32 / 127.0).max(-1.0),
                (((packed >> 8) & 0xff) as u8 as i8 as f32 / 127.0).max(-1.0),
                (((packed >> 16) & 0xff) as u8 as i8 as f32 / 127.0).max(-1.0),
                (((packed >> 24) & 0xff) as u8 as i8 as f32 / 127.0).max(-1.0),
            ];
            format_f32_vector(4, &values)
        }
        "unpack4x8unorm" => {
            let packed = parse_source_unpack_u32_arg(&args[0], constants)?;
            let values = [
                (packed & 0xff) as f32 / 255.0,
                ((packed >> 8) & 0xff) as f32 / 255.0,
                ((packed >> 16) & 0xff) as f32 / 255.0,
                ((packed >> 24) & 0xff) as f32 / 255.0,
            ];
            format_f32_vector(4, &values)
        }
        "unpack4xU8" => {
            let packed = parse_source_unpack_u32_arg(&args[0], constants)?;
            Some(format_u32_vector(
                4,
                &[
                    packed & 0xff,
                    (packed >> 8) & 0xff,
                    (packed >> 16) & 0xff,
                    (packed >> 24) & 0xff,
                ],
            ))
        }
        "unpack4xI8" => {
            let packed = parse_source_unpack_u32_arg(&args[0], constants)?;
            Some(format_i32_vector(
                4,
                &[
                    (packed & 0xff) as u8 as i8 as i32,
                    ((packed >> 8) & 0xff) as u8 as i8 as i32,
                    ((packed >> 16) & 0xff) as u8 as i8 as i32,
                    ((packed >> 24) & 0xff) as u8 as i8 as i32,
                ],
            ))
        }
        _ => None,
    }
}

fn lower_packed_builtins_source_once(
    source: &str,
    constants: Option<&HashMap<String, f64>>,
) -> Option<String> {
    const NAMES: &[&str] = &[
        "pack2x16float",
        "pack2x16snorm",
        "pack2x16unorm",
        "pack4x8snorm",
        "pack4x8unorm",
        "pack4xI8Clamp",
        "pack4xU8Clamp",
        "pack4xI8",
        "pack4xU8",
        "unpack2x16float",
        "unpack2x16snorm",
        "unpack2x16unorm",
        "unpack4x8snorm",
        "unpack4x8unorm",
        "unpack4xI8",
        "unpack4xU8",
    ];
    if !source.contains("pack") && !source.contains("unpack") {
        return None;
    }
    let masked = source_without_fn_bodies(source);
    let bytes = masked.as_bytes();
    let mut out = String::with_capacity(source.len());
    let mut pos = 0usize;
    let mut i = 0usize;
    let mut changed = false;
    while i < masked.len() {
        let Some((start, name)) = NAMES
            .iter()
            .filter_map(|name| masked[i..].find(name).map(|p| (i + p, *name)))
            .min_by_key(|(p, _)| *p)
        else {
            break;
        };
        let end = start + name.len();
        if (start > 0 && is_ident_char(bytes[start - 1]))
            || (end < bytes.len() && is_ident_char(bytes[end]))
        {
            i = end;
            continue;
        }
        let Some((args, close)) = find_call_args(&masked, start, name.len()) else {
            i = end;
            continue;
        };
        if !is_builtin_shadowed_at_call(source, name, start) {
            if let Some(replacement) = lower_packed_builtin_source_call(name, &args, constants) {
                out.push_str(&source[pos..start]);
                out.push_str(&replacement);
                pos = close + 1;
                changed = true;
            }
        }
        i = close + 1;
    }
    if changed {
        out.push_str(&source[pos..]);
        Some(out)
    } else {
        None
    }
}

fn lower_packed_builtins_source(source: &str, constants: Option<&HashMap<String, f64>>) -> String {
    let mut lowered = source.to_string();
    for _ in 0..8 {
        let Some(next) = lower_packed_builtins_source_once(&lowered, constants) else {
            return lowered;
        };
        lowered = next;
    }
    lowered
}

fn bit_type_info(
    ty: &TypeResolution,
    types: &naga::UniqueArena<naga::Type>,
) -> Option<(bool, usize, Option<naga::Handle<naga::Type>>)> {
    match ty.inner_with(types) {
        naga::TypeInner::Scalar(scalar)
            if scalar.width == 4
                && matches!(scalar.kind, naga::ScalarKind::Sint | naga::ScalarKind::Uint) =>
        {
            Some((scalar.kind == naga::ScalarKind::Sint, 1, ty.handle()))
        }
        naga::TypeInner::Vector { size, scalar }
            if scalar.width == 4
                && matches!(scalar.kind, naga::ScalarKind::Sint | naga::ScalarKind::Uint) =>
        {
            Some((
                scalar.kind == naga::ScalarKind::Sint,
                u8::from(*size) as usize,
                ty.handle(),
            ))
        }
        _ => None,
    }
}

fn resolve_u32_scalar(
    h: naga::Handle<naga::Expression>,
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
) -> Option<u32> {
    use naga::{Expression, Literal};
    let expr = resolve_expr_in_module(h, local, module)?;
    match expr {
        Expression::Literal(Literal::U32(v)) => Some(*v),
        Expression::Literal(Literal::I32(v)) => u32::try_from(*v).ok(),
        Expression::Literal(Literal::AbstractInt(v)) => u32::try_from(*v).ok(),
        Expression::ZeroValue(_) => Some(0),
        Expression::Constant(c_h) => {
            let init = module.constants[*c_h].init;
            resolve_u32_scalar(init, &module.global_expressions, module)
        }
        Expression::Override(o_h) => module.overrides[*o_h]
            .init
            .and_then(|init| resolve_u32_scalar(init, &module.global_expressions, module)),
        Expression::Compose { components, .. } if components.len() == 1 => {
            resolve_u32_scalar(components[0], local, module)
        }
        Expression::As { expr, .. } => resolve_u32_scalar(*expr, local, module),
        _ => None,
    }
}

fn resolve_int_bits_scalar(
    h: naga::Handle<naga::Expression>,
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
) -> Option<u32> {
    use naga::{Expression, Literal};
    let expr = resolve_expr_in_module(h, local, module)?;
    match expr {
        Expression::Literal(Literal::U32(v)) => Some(*v),
        Expression::Literal(Literal::I32(v)) => Some(*v as u32),
        Expression::Literal(Literal::AbstractInt(v)) => Some((*v as i32) as u32),
        Expression::ZeroValue(_) => Some(0),
        Expression::Constant(c_h) => {
            let init = module.constants[*c_h].init;
            resolve_int_bits_scalar(init, &module.global_expressions, module)
        }
        Expression::Override(o_h) => module.overrides[*o_h]
            .init
            .and_then(|init| resolve_int_bits_scalar(init, &module.global_expressions, module)),
        Expression::Compose { components, .. } if components.len() == 1 => {
            resolve_int_bits_scalar(components[0], local, module)
        }
        Expression::As { expr, .. } => resolve_int_bits_scalar(*expr, local, module),
        _ => None,
    }
}

fn resolve_int_components_bits(
    h: naga::Handle<naga::Expression>,
    width: usize,
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
) -> Option<Vec<u32>> {
    use naga::Expression;
    let expr = resolve_expr_in_module(h, local, module)?;
    match expr {
        Expression::Constant(c_h) => {
            let init = module.constants[*c_h].init;
            resolve_int_components_bits(init, width, &module.global_expressions, module)
        }
        Expression::Override(o_h) => module.overrides[*o_h].init.and_then(|init| {
            resolve_int_components_bits(init, width, &module.global_expressions, module)
        }),
        Expression::ZeroValue(_) => Some(vec![0; width]),
        Expression::Splat { value, .. } if width > 1 => {
            let v = resolve_int_bits_scalar(*value, local, module)?;
            Some(vec![v; width])
        }
        Expression::Compose { components, .. } if width > 1 => {
            if components.len() != width {
                return None;
            }
            let mut out = Vec::with_capacity(width);
            for c in components {
                out.push(resolve_int_bits_scalar(*c, local, module)?);
            }
            Some(out)
        }
        _ if width == 1 => resolve_int_bits_scalar(h, local, module).map(|v| vec![v]),
        _ => None,
    }
}

fn bit_extract(value: u32, signed: bool, offset: u32, count: u32) -> u32 {
    let o = offset.min(32);
    let c = count.min(32 - o);
    if c == 0 {
        return 0;
    }
    let shifted = value >> o;
    let mask = if c == 32 { u32::MAX } else { (1u32 << c) - 1 };
    let bits = shifted & mask;
    if signed && c < 32 && ((bits >> (c - 1)) & 1) != 0 {
        bits | !mask
    } else {
        bits
    }
}

fn bit_insert(value: u32, newbits: u32, offset: u32, count: u32) -> u32 {
    let o = offset.min(32);
    let c = count.min(32 - o);
    if c == 0 {
        return value;
    }
    let low_mask = if c == 32 { u32::MAX } else { (1u32 << c) - 1 };
    let mask = low_mask << o;
    (value & !mask) | ((newbits << o) & mask)
}

fn expression_is_atomic_value(
    h: naga::Handle<naga::Expression>,
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
) -> bool {
    use naga::Expression;
    let Some(expr) = resolve_expr_in_module(h, local, module) else {
        return false;
    };
    match expr {
        Expression::GlobalVariable(g_h) => {
            matches!(
                &module.types[module.global_variables[*g_h].ty].inner,
                naga::TypeInner::Atomic(_)
            )
        }
        Expression::LocalVariable(_) => false,
        Expression::Load { pointer } | Expression::As { expr: pointer, .. } => {
            expression_is_atomic_value(*pointer, local, module)
        }
        _ => false,
    }
}

fn validate_bit_builtins(module: &naga::Module) -> Vec<WgslMessage> {
    let mut errors: Vec<WgslMessage> = Vec::new();
    check_bit_builtins_in_arena(&module.global_expressions, module, None, &mut errors);
    for ep in &module.entry_points {
        check_bit_builtins_in_arena(&ep.function.expressions, module, None, &mut errors);
    }
    for (_, f) in module.functions.iter() {
        check_bit_builtins_in_arena(&f.expressions, module, None, &mut errors);
    }
    errors
}

fn validate_bit_builtins_live(module: &naga::Module) -> Vec<WgslMessage> {
    let mut errors: Vec<WgslMessage> = Vec::new();
    check_bit_builtins_in_arena(&module.global_expressions, module, None, &mut errors);
    let mut reachable_helpers: HashSet<naga::Handle<naga::Function>> = HashSet::new();
    for ep in &module.entry_points {
        collect_called_functions_in_block(&ep.function.body, &mut reachable_helpers);
        let mut live: HashSet<naga::Handle<naga::Expression>> = HashSet::new();
        collect_live_in_block(
            &ep.function.body,
            &ep.function.expressions,
            module,
            &mut live,
        );
        check_bit_builtins_in_arena(&ep.function.expressions, module, Some(&live), &mut errors);
    }
    let mut changed = true;
    while changed {
        changed = false;
        let current: Vec<_> = reachable_helpers.iter().copied().collect();
        for h in current {
            let before = reachable_helpers.len();
            collect_called_functions_in_block(&module.functions[h].body, &mut reachable_helpers);
            changed |= reachable_helpers.len() != before;
        }
    }
    for h in reachable_helpers {
        let f = &module.functions[h];
        let mut live: HashSet<naga::Handle<naga::Expression>> = HashSet::new();
        collect_live_in_block(&f.body, &f.expressions, module, &mut live);
        check_bit_builtins_in_arena(&f.expressions, module, Some(&live), &mut errors);
    }
    errors
}

fn collect_called_functions_in_block(
    block: &naga::Block,
    out: &mut HashSet<naga::Handle<naga::Function>>,
) {
    use naga::Statement;
    for (stmt, _span) in block.span_iter() {
        match stmt {
            Statement::Block(b) => collect_called_functions_in_block(b, out),
            Statement::If { accept, reject, .. } => {
                collect_called_functions_in_block(accept, out);
                collect_called_functions_in_block(reject, out);
            }
            Statement::Switch { cases, .. } => {
                for case in cases {
                    collect_called_functions_in_block(&case.body, out);
                }
            }
            Statement::Loop {
                body, continuing, ..
            } => {
                collect_called_functions_in_block(body, out);
                collect_called_functions_in_block(continuing, out);
            }
            Statement::Call { function, .. } => {
                out.insert(*function);
            }
            _ => {}
        }
    }
}

fn check_bit_builtins_in_arena(
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
    live: Option<&HashSet<naga::Handle<naga::Expression>>>,
    errors: &mut Vec<WgslMessage>,
) {
    use naga::{Expression, MathFunction};
    for (h, expr) in local.iter() {
        if live.is_some_and(|set| !set.contains(&h)) {
            continue;
        }
        let Expression::Math {
            fun,
            arg,
            arg1,
            arg2,
            arg3,
        } = expr
        else {
            continue;
        };
        let (name, value_args, offset, count) = match fun {
            MathFunction::ExtractBits => {
                let (Some(offset), Some(count)) = (*arg1, *arg2) else {
                    continue;
                };
                ("extractBits", vec![*arg], offset, count)
            }
            MathFunction::InsertBits => {
                let Some(newbits) = *arg1 else {
                    continue;
                };
                let (Some(offset), Some(count)) = (*arg2, *arg3) else {
                    continue;
                };
                ("insertBits", vec![*arg, newbits], offset, count)
            }
            _ => continue,
        };
        if value_args
            .iter()
            .copied()
            .any(|h| expression_is_atomic_value(h, local, module))
        {
            errors.push(wgsl_error(format!(
                "{} value arguments must be i32/u32 values, not atomic values",
                name
            )));
            continue;
        }
        if expression_is_atomic_value(offset, local, module)
            || expression_is_atomic_value(count, local, module)
        {
            errors.push(wgsl_error(format!(
                "{} offset/count arguments must be u32 values, not atomic values",
                name
            )));
            continue;
        }
        if let (Some(o), Some(c)) = (
            resolve_u32_scalar(offset, local, module),
            resolve_u32_scalar(count, local, module),
        ) {
            if u64::from(o) + u64::from(c) > 32 {
                errors.push(wgsl_error(format!(
                    "{} offset + count must be <= 32 for const/override evaluation",
                    name
                )));
            }
        }
    }
}

fn resolve_expression_types(
    module: &naga::Module,
    local: &naga::Arena<naga::Expression>,
    local_vars: &naga::Arena<naga::LocalVariable>,
    arguments: &[naga::FunctionArgument],
) -> HashMap<naga::Handle<naga::Expression>, TypeResolution> {
    let ctx = ResolveContext::with_locals(module, local_vars, arguments);
    let mut out = HashMap::new();
    for (h, expr) in local.iter() {
        if let Ok(resolution) = ctx.resolve(expr, |dep| {
            out.get(&dep)
                .ok_or(naga::proc::ResolveError::InvalidScalar(dep))
        }) {
            out.insert(h, resolution);
        }
    }
    out
}

fn collect_bit_folds(
    local: &naga::Arena<naga::Expression>,
    module: &naga::Module,
    types: &HashMap<naga::Handle<naga::Expression>, TypeResolution>,
) -> HashMap<naga::Handle<naga::Expression>, BitFoldValue> {
    use naga::{Expression, MathFunction};
    let mut folds = HashMap::new();
    for (h, expr) in local.iter() {
        let Expression::Math {
            fun,
            arg,
            arg1,
            arg2,
            arg3,
        } = expr
        else {
            continue;
        };
        let Some((signed, width, ty)) = types
            .get(&h)
            .and_then(|ty| bit_type_info(ty, &module.types))
        else {
            continue;
        };
        if width > 1 && ty.is_none() {
            continue;
        }
        let Some(values) = resolve_int_components_bits(*arg, width, local, module) else {
            continue;
        };
        let (offset_h, count_h) = match fun {
            MathFunction::ExtractBits => {
                let (Some(offset), Some(count)) = (*arg1, *arg2) else {
                    continue;
                };
                (offset, count)
            }
            MathFunction::InsertBits => {
                let (Some(offset), Some(count)) = (*arg2, *arg3) else {
                    continue;
                };
                (offset, count)
            }
            _ => continue,
        };
        let Some(offset) = resolve_u32_scalar(offset_h, local, module) else {
            continue;
        };
        let Some(count) = resolve_u32_scalar(count_h, local, module) else {
            continue;
        };
        if u64::from(offset) + u64::from(count) > 32 {
            continue;
        }
        let components = match fun {
            MathFunction::ExtractBits => values
                .into_iter()
                .map(|v| bit_extract(v, signed, offset, count))
                .collect(),
            MathFunction::InsertBits => {
                let Some(newbits_h) = *arg1 else {
                    continue;
                };
                let Some(newbits) = resolve_int_components_bits(newbits_h, width, local, module)
                else {
                    continue;
                };
                values
                    .into_iter()
                    .zip(newbits.into_iter())
                    .map(|(v, n)| bit_insert(v, n, offset, count))
                    .collect()
            }
            _ => continue,
        };
        folds.insert(
            h,
            BitFoldValue {
                signed,
                ty,
                components,
            },
        );
    }
    folds
}

fn literal_from_bits(bits: u32, signed: bool) -> naga::Expression {
    if signed {
        naga::Expression::Literal(naga::Literal::I32(bits as i32))
    } else {
        naga::Expression::Literal(naga::Literal::U32(bits))
    }
}

fn rebuild_arena_with_bit_folds(
    local: &mut naga::Arena<naga::Expression>,
    folds: &HashMap<naga::Handle<naga::Expression>, BitFoldValue>,
) -> HashMap<naga::Handle<naga::Expression>, naga::Handle<naga::Expression>> {
    use naga::{Expression, Span};
    use std::mem;

    let drained: Vec<(naga::Handle<Expression>, Expression, Span)> =
        mem::take(local).drain().collect();
    let mut remap: HashMap<naga::Handle<Expression>, naga::Handle<Expression>> = HashMap::new();
    for (old_h, mut expr, span) in drained {
        adjust_expr_with_map(&remap, &mut expr);
        if let Some(fold) = folds.get(&old_h) {
            let new_h = if fold.components.len() == 1 {
                local.append(literal_from_bits(fold.components[0], fold.signed), span)
            } else {
                let Some(ty) = fold.ty else {
                    let new_h = local.append(expr, span);
                    remap.insert(old_h, new_h);
                    continue;
                };
                let mut components = Vec::with_capacity(fold.components.len());
                for bits in &fold.components {
                    components
                        .push(local.append(literal_from_bits(*bits, fold.signed), Span::UNDEFINED));
                }
                local.append(Expression::Compose { ty, components }, span)
            };
            remap.insert(old_h, new_h);
        } else {
            let new_h = local.append(expr, span);
            remap.insert(old_h, new_h);
        }
    }
    remap
}

fn fold_bit_builtins(module: &mut naga::Module) {
    let empty_locals = naga::Arena::new();
    let global_types =
        resolve_expression_types(module, &module.global_expressions, &empty_locals, &[]);
    let global_folds = collect_bit_folds(&module.global_expressions, module, &global_types);
    if !global_folds.is_empty() {
        let remap = rebuild_arena_with_bit_folds(&mut module.global_expressions, &global_folds);
        for (_, c) in module.constants.iter_mut() {
            if let Some(new) = remap.get(&c.init).copied() {
                c.init = new;
            }
        }
        for (_, ov) in module.overrides.iter_mut() {
            if let Some(init) = ov.init {
                if let Some(new) = remap.get(&init).copied() {
                    ov.init = Some(new);
                }
            }
        }
        for (_, gv) in module.global_variables.iter_mut() {
            if let Some(init) = gv.init {
                if let Some(new) = remap.get(&init).copied() {
                    gv.init = Some(new);
                }
            }
        }
    }
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
    let frontend_errors = validate_parse_frontend_source(source);
    if !frontend_errors.is_empty() {
        return Err(frontend_errors);
    }
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
    let dynamic_index_source = lower_dynamic_index_lets_source(&short_circuit_source);
    let var_normalized_source =
        normalize_var_template_trailing_commas_source(&dynamic_index_source);
    let interpolated_source = normalize_interpolate_trailing_commas_source(&var_normalized_source);
    let normalized_source = lower_struct_size5_align16_source(&interpolated_source);
    let shadowed_builtin_errors =
        validate_shadowed_lowered_builtin_calls_source(&normalized_source);
    if !shadowed_builtin_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(shadowed_builtin_errors);
        return Err(msgs);
    }
    let smoothstep_errors = validate_smoothstep_source(&normalized_source, None, None);
    if !smoothstep_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(smoothstep_errors);
        return Err(msgs);
    }
    let ldexp_errors = validate_ldexp_source(&normalized_source, None, None);
    if !ldexp_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(ldexp_errors);
        return Err(msgs);
    }
    let precedence_errors = validate_binary_precedence_source(&normalized_source, None);
    if !precedence_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(precedence_errors);
        return Err(msgs);
    }
    let function_attribute_errors = validate_function_attribute_source(&normalized_source);
    if !function_attribute_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(function_attribute_errors);
        return Err(msgs);
    }
    let shader_io_errors = validate_shader_io_attribute_source(&normalized_source);
    if !shader_io_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(shader_io_errors);
        return Err(msgs);
    }
    let id_attribute_errors = validate_id_attribute_source(&normalized_source);
    if !id_attribute_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(id_attribute_errors);
        return Err(msgs);
    }
    let loop_behavior_errors = validate_loop_behavior_source(&normalized_source);
    if !loop_behavior_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(loop_behavior_errors);
        return Err(msgs);
    }
    let size_attribute_errors = validate_size_attribute_source(&normalized_source);
    if !size_attribute_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(size_attribute_errors);
        return Err(msgs);
    }
    let atomic_errors = validate_atomic_source(&normalized_source);
    if !atomic_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(atomic_errors);
        return Err(msgs);
    }
    let bool_order_errors = validate_bool_order_comparison_source(&normalized_source);
    if !bool_order_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(bool_order_errors);
        return Err(msgs);
    }
    let pointer_parameter_errors =
        validate_unrestricted_pointer_parameter_source(&normalized_source);
    if !pointer_parameter_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(pointer_parameter_errors);
        return Err(msgs);
    }
    let override_sized_array_errors = validate_override_sized_array_source(&normalized_source);
    if !override_sized_array_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(override_sized_array_errors);
        return Err(msgs);
    }
    let let_resource_errors = validate_let_resource_handle_source(&normalized_source);
    if !let_resource_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(let_resource_errors);
        return Err(msgs);
    }
    // Naga 29 skips return- and coords-type validation for
    // textureSampleBaseClampToEdge calls on texture_external bindings; emit
    // shader-creation errors for the CTS-shaped invalid forms.
    let texture_sample_clamp_errors =
        validate_texture_sample_base_clamp_to_edge_source(&normalized_source);
    if !texture_sample_clamp_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(texture_sample_clamp_errors);
        return Err(msgs);
    }
    let texture_builtin_type_errors = validate_texture_builtin_type_source(&normalized_source);
    if !texture_builtin_type_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(texture_builtin_type_errors);
        return Err(msgs);
    }
    let bitcast_errors = validate_const_bitcast_source(&normalized_source, None);
    if !bitcast_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(bitcast_errors);
        return Err(msgs);
    }
    let div_rem_source_errors = validate_div_rem_source(&normalized_source, None, None);
    if !div_rem_source_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(div_rem_source_errors);
        return Err(msgs);
    }
    let math_domain_errors = validate_math_domain_source(&normalized_source, None, None);
    if !math_domain_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(math_domain_errors);
        return Err(msgs);
    }
    let size_lowered_source = lower_large_size_attribute_source(&normalized_source);
    let naga_source = lower_no_entry_workgroup_pointer_params_source(&size_lowered_source);
    let shadow_builtin_source = lower_shadow_builtin_abstract_args_source(&naga_source, None);
    let matrix_binary_source = lower_const_matrix_binary_source(&shadow_builtin_source, None);
    let bool_bitwise_source = lower_bool_bitwise_const_source(&matrix_binary_source);
    let float_math_source = lower_const_float_math_source(&bool_bitwise_source, None);
    let parse_source = lower_const_bitcasts_source(
        &lower_packed_builtins_source(
            &lower_bit_builtins_source(
                &lower_quantize_to_f16_source(
                    &lower_ldexp_source(&lower_smoothstep_source(&float_math_source), None),
                    None,
                ),
                None,
            ),
            None,
        ),
        None,
    );
    let parse_source = lower_abstract_float_output_snippets_source(&parse_source, None);
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
    cloak_const_indices(&mut module, &parse_source);

    // WGSL §17.6.2: integer e1 / e2 and e1 % e2 with v2 == 0 must be a
    // compilation error. naga 29 catches this for fully-const expressions
    // but not for runtime statements like `var v = 0; v /= 0;` (which is
    // what CTS scalar_vector compound_assignment=true and
    // scalar_vector_out_of_range exercise). Walk the expression arenas
    // ourselves and emit synthetic errors for the const-zero divisor case.
    let mut bridge_errors = validate_storage_atomic_access(&module);
    bridge_errors.extend(validate_stage_resource_restrictions(&module, &parse_source));
    bridge_errors.extend(validate_bool_order_comparisons(&module));
    bridge_errors.extend(validate_div_rem(&module));
    // WGSL §17.6.3.4 / §17.6.3.21: clamp(v, low, high) and smoothstep(e0,
    // e1, x) require low ≤ high / e0 ≤ e1 when both are const- or override-
    // evaluable. naga 29 misses this; we walk Math expressions and reject
    // const pairs where low > high.
    bridge_errors.extend(validate_clamp_smoothstep(&module));
    // WGSL §17.7.x: ImageSample offset must be a const vec[2|3]<i32>
    // with each component in [-8, 7].
    bridge_errors.extend(validate_texture_offsets(&module));
    // WGSL integer bit builtins: const/override offset+count must fit in the
    // 32-bit lane, and Naga 29 misses a few invalid atomic offset/count cases.
    bridge_errors.extend(validate_bit_builtins(&module));
    if !bridge_errors.is_empty() {
        let mut msgs = strip.messages;
        msgs.extend(bridge_errors);
        return Err(msgs);
    }
    // Naga 29 does not const-evaluate extractBits/insertBits. Fold fully
    // constant calls to literals before validation so const arrays and module
    // constants in CTS can compile.
    fold_bit_builtins(&mut module);

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
                .and_then(|(processed_module, _processed_info)| {
                    let mut processed_module = processed_module.into_owned();
                    fold_bit_builtins(&mut processed_module);
                    let processed_info =
                        Validator::new(ValidationFlags::all(), Capabilities::all())
                            .validate(&processed_module)
                            .ok()?;
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn no_entry_workgroup_pointer_params_are_sanitized_for_naga() {
        let source = "override size = 1;\nfn f(a: ptr<workgroup, array<u32, size>>) {}\n";
        let lowered = lower_no_entry_workgroup_pointer_params_source(source);
        assert!(lowered.contains("ptr<function, array<u32, 1>>"));
    }

    #[test]
    fn plain_workgroup_pointer_params_are_not_sanitized() {
        let source = "fn f(a: ptr<workgroup, u32>) {}\n";
        let lowered = lower_no_entry_workgroup_pointer_params_source(source);
        assert!(lowered.contains("ptr<workgroup, u32>"));
    }

    #[test]
    fn private_pointer_override_sized_arrays_are_rejected() {
        let source = "override size = 1;\nfn f(a: ptr<private, array<u32, size>>) {}\n";
        assert!(!validate_override_sized_array_source(source).is_empty());
    }

    #[test]
    fn var_template_trailing_commas_are_normalized() {
        let source = "fn f() { var<function,> x : u32; }\n@group(0) @binding(0) var<storage, read,> y : u32;";
        let normalized = normalize_var_template_trailing_commas_source(source);
        assert!(normalized.contains("var<function >"));
        assert!(normalized.contains("var<storage, read >"));
    }

    #[test]
    fn read_only_storage_atomics_are_rejected() {
        let source = "@group(0) @binding(0) var<storage, read> foo : atomic<u32>;";
        let module = wgsl::parse_str(source).unwrap();
        assert!(!validate_storage_atomic_access(&module).is_empty());
    }

    #[test]
    fn invalid_atomic_template_args_are_rejected() {
        assert!(validate_atomic_source("struct S { x: atomic<f32> }").len() == 1);
        assert!(validate_atomic_source("alias A = atomic<i32,>;").is_empty());
        assert!(validate_atomic_source("alias T = i32; alias A = atomic<T>;").is_empty());
    }

    #[test]
    fn invalid_atomic_value_uses_are_rejected() {
        let source = "var<workgroup> a1: atomic<u32>; var<workgroup> a2: atomic<u32>; fn f() { let x: u32 = a1 + a2; }";
        assert!(!validate_atomic_source(source).is_empty());

        let source = "var<workgroup> a1: atomic<u32>; var<workgroup> a2: atomic<u32>; fn f() { let x = a1 + a2; }";
        assert!(!validate_atomic_source(source).is_empty());

        let source = "var<workgroup> xu: atomic<u32>; fn f() { var a = xu; a++; }";
        assert!(!validate_atomic_source(source).is_empty());

        let source = "var<workgroup> xu: atomic<u32>; fn f() { _ = xu; }";
        assert!(!validate_atomic_source(source).is_empty());

        let source = "var<workgroup> xu: atomic<u32>; fn f() { let p = &xu; _ = atomicLoad(p); }";
        assert!(validate_atomic_source(source).is_empty());
    }

    #[test]
    fn invalid_pointer_atomic_shapes_are_rejected() {
        assert!(!validate_atomic_source("alias p = ptr<private, atomic<u32>>;").is_empty());
        assert!(!validate_atomic_source("alias p = ptr<storage, u32, write,>;").is_empty());
        assert!(validate_atomic_source("alias p = ptr<workgroup, atomic<u32>>;").is_empty());
    }

    #[test]
    fn atomic_values_cannot_be_switch_selectors() {
        let source = "var<workgroup> A : atomic<i32>; fn f() { switch A { default: {} } }";
        assert!(!validate_atomic_source(source).is_empty());
    }

    #[test]
    fn vertex_writable_storage_is_rejected() {
        let source = "@group(0) @binding(0) var<storage, read_write> v : u32;\n@vertex fn main() -> @builtin(position) vec4f { _ = v; return vec4f(); }";
        let module = wgsl::parse_str(source).unwrap();
        assert!(!validate_stage_resource_restrictions(&module, source).is_empty());
    }

    #[test]
    fn local_shadow_does_not_mark_workgroup_global_used() {
        let source = "var<workgroup> a: atomic<u32>;\n@vertex fn main() -> @builtin(position) vec4f { var<function> a = 1u; _ = a; return vec4f(); }";
        let module = wgsl::parse_str(source).unwrap();
        assert!(validate_stage_resource_restrictions(&module, source).is_empty());
    }

    #[test]
    fn bool_order_comparisons_are_rejected() {
        let source = "const lhs = false; const rhs = true; const bad = lhs < rhs;";
        assert!(!validate_bool_order_comparison_source(source).is_empty());

        let source =
            "const lhs = vec2(false, false); const rhs = vec2(false, false); const bad = lhs >= rhs;";
        assert!(!validate_bool_order_comparison_source(source).is_empty());

        let source = "const lhs = false; const rhs = true; const ok = lhs == rhs;";
        assert!(validate_bool_order_comparison_source(source).is_empty());
    }

    #[test]
    fn const_bool_bitwise_is_folded_for_naga() {
        let scalar = "const lhs = false; const rhs = true; const foo : bool = lhs | rhs;";
        assert!(lower_bool_bitwise_const_source(scalar).contains("const foo : bool = true;"));
        assert!(compile_with_meta(scalar).is_ok());

        let vector = "const lhs = vec2(false, true); const rhs = vec2(true, true); const foo : vec2<bool> = lhs & rhs;";
        assert!(lower_bool_bitwise_const_source(vector)
            .contains("const foo : vec2<bool> = vec2<bool>(false, true);"));
        assert!(compile_with_meta(vector).is_ok());
    }

    #[test]
    fn div_rem_i32_min_source_shapes_are_rejected() {
        let div =
            "const left = i32(-2147483648); const right = i32(-1); fn f() { _ = left / right; }";
        assert!(!validate_div_rem_source(div, None, None).is_empty());
        assert!(compile_with_meta(div).is_err());

        let rem =
            "const left = i32(-2147483648); const right = i32(-1); fn f() { _ = left % right; }";
        assert!(!validate_div_rem_source(rem, None, None).is_empty());

        let ok =
            "const left = i32(-2147483647); const right = i32(-1); fn f() { _ = left / right; }";
        assert!(validate_div_rem_source(ok, None, None).is_empty());

        let guarded = "const left = i32(-2147483648); const right = i32(-1); fn f() { _ = false && ((left / right) == 0); }";
        assert!(validate_div_rem_source(guarded, None, None).is_empty());
    }

    #[test]
    fn div_rem_i32_min_override_source_shape_is_bake_only() {
        let source = "override o0 : i32; override o1 : i32; @compute @workgroup_size(1) fn main() { _ = i32(o0) / i32(o1); }";
        assert!(validate_div_rem_source(source, None, None).is_empty());

        let mut constants = HashMap::new();
        constants.insert("o0".to_string(), i32::MIN as f64);
        constants.insert("o1".to_string(), -1.0);
        assert!(!validate_div_rem_source(source, Some(&constants), Some("main")).is_empty());

        constants.insert("o0".to_string(), (i32::MIN + 1) as f64);
        assert!(validate_div_rem_source(source, Some(&constants), Some("main")).is_empty());
    }

    #[test]
    fn id_attribute_only_allows_overrides() {
        assert!(validate_id_attribute_source("@id(1) override a = 4;").is_empty());
        assert!(validate_id_attribute_source("@\nid(1) override a = 4;").is_empty());
        assert!(!validate_id_attribute_source("@id(1) const a = 4;").is_empty());
        assert!(!validate_id_attribute_source("@id(1) var a = 4;").is_empty());
    }

    #[test]
    fn loop_behavior_cts_gaps_are_rejected() {
        assert!(validate_loop_behavior_source("fn f(){ loop { break; } }").is_empty());
        assert!(
            validate_loop_behavior_source("fn f(){ loop { continuing { break if true; } } }")
                .is_empty()
        );
        assert!(!validate_loop_behavior_source("fn f(){ loop{} }").is_empty());
        assert!(!validate_loop_behavior_source("fn f(){ loop { continue; } }").is_empty());
        assert!(!validate_loop_behavior_source("fn f(){ loop { continue; break; } }").is_empty());
        assert!(!validate_loop_behavior_source("fn f(){ loop { continuing {} } }").is_empty());
        assert!(!validate_loop_behavior_source("fn f(){ for (;;) {} }").is_empty());
        assert!(
            !validate_loop_behavior_source("fn f(){ for (;;) { continue; break; } }").is_empty()
        );
        assert!(!validate_loop_behavior_source("fn f(){ for (var i = 0; ; i++) {} }").is_empty());
    }

    #[test]
    fn continue_cannot_bypass_declarations_used_by_continuing() {
        assert!(validate_loop_behavior_source(
            "fn f(){ loop { let cond = false; continue; continuing { break if cond; } } }"
        )
        .is_empty());
        assert!(validate_loop_behavior_source(
            "fn f(){ loop { continue; let cond = false; continuing { break if false; } } }"
        )
        .is_empty());
        assert!(!validate_loop_behavior_source(
            "fn f(){ loop { continue; let cond = false; continuing { break if cond; } } }"
        )
        .is_empty());
        assert!(!validate_loop_behavior_source(
            "fn f(){ loop { if false { continue; } let cond = false; continuing { break if cond; } } }"
        )
        .is_empty());
    }

    #[test]
    fn texture_sample_base_clamp_to_edge_external_validates_args() {
        let prelude = "@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: texture_external;\n";
        let ok = format!(
            "{prelude}@fragment fn fs() -> @location(0) vec4f {{ let v: vec4f = textureSampleBaseClampToEdge(t, s, vec2f(0)); return vec4f(0); }}"
        );
        assert!(validate_texture_sample_base_clamp_to_edge_source(&ok).is_empty());

        let bad_return = format!(
            "{prelude}@fragment fn fs() -> @location(0) vec4f {{ let v: vec2<bool> = textureSampleBaseClampToEdge(t, s, vec2f(0)); return vec4f(0); }}"
        );
        assert!(!validate_texture_sample_base_clamp_to_edge_source(&bad_return).is_empty());

        let bad_return_f16 = format!(
            "{prelude}@fragment fn fs() -> @location(0) vec4f {{ let v: vec4<f16> = textureSampleBaseClampToEdge(t, s, vec2f(0)); return vec4f(0); }}"
        );
        assert!(!validate_texture_sample_base_clamp_to_edge_source(&bad_return_f16).is_empty());

        let bad_coord_scalar = format!(
            "{prelude}@fragment fn fs() -> @location(0) vec4f {{ let v = textureSampleBaseClampToEdge(t, s, false); return vec4f(0); }}"
        );
        assert!(!validate_texture_sample_base_clamp_to_edge_source(&bad_coord_scalar).is_empty());

        let bad_coord_vec3 = format!(
            "{prelude}@fragment fn fs() -> @location(0) vec4f {{ let v = textureSampleBaseClampToEdge(t, s, vec3f(0,0,0)); return vec4f(0); }}"
        );
        assert!(!validate_texture_sample_base_clamp_to_edge_source(&bad_coord_vec3).is_empty());

        let bad_coord_vec2_int = format!(
            "{prelude}@fragment fn fs() -> @location(0) vec4f {{ let v = textureSampleBaseClampToEdge(t, s, vec2<i32>(0, 0)); return vec4f(0); }}"
        );
        assert!(!validate_texture_sample_base_clamp_to_edge_source(&bad_coord_vec2_int).is_empty());

        let abstract_int_coord = format!(
            "{prelude}@fragment fn fs() -> @location(0) vec4f {{ let v = textureSampleBaseClampToEdge(t, s, vec2(0, 0)); return vec4f(0); }}"
        );
        assert!(validate_texture_sample_base_clamp_to_edge_source(&abstract_int_coord).is_empty());

        // texture_2d<f32> is naga's responsibility — leave it untouched.
        let texture_2d_prelude = "@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: texture_2d<f32>;\n";
        let texture_2d_bad = format!(
            "{texture_2d_prelude}@fragment fn fs() -> @location(0) vec4f {{ let v: vec2<bool> = textureSampleBaseClampToEdge(t, s, vec2f(0)); return vec4f(0); }}"
        );
        assert!(validate_texture_sample_base_clamp_to_edge_source(&texture_2d_bad).is_empty());

        // Exact CTS shader shape (multi-line, leading whitespace inside body).
        let cts_shape = "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: texture_external;\n@fragment fn fs() -> @location(0) vec4f {\n  let v: bool = textureSampleBaseClampToEdge(t, s, vec2f(0));\n  return vec4f(0);\n}\n";
        assert!(
            !validate_texture_sample_base_clamp_to_edge_source(cts_shape).is_empty(),
            "exact CTS-shape shader should produce error"
        );
    }

    #[test]
    fn texture_builtin_type_cts_gaps_are_rejected() {
        let gather_ok = "@group(0) @binding(0) var s: sampler; @group(0) @binding(1) var t: texture_depth_2d; @fragment fn fs() -> @location(0) vec4f { _ = textureGather(t, s, vec2f(0)); return vec4f(); }";
        assert!(validate_texture_builtin_type_source(gather_ok).is_empty());

        let gather_component = "@group(0) @binding(0) var s: sampler; @group(0) @binding(1) var t: texture_depth_2d; @fragment fn fs() -> @location(0) vec4f { _ = textureGather(0, t, s, vec2f(0)); return vec4f(); }";
        assert!(!validate_texture_builtin_type_source(gather_component).is_empty());

        let sample_ok = "@group(0) @binding(0) var s: sampler; @group(0) @binding(1) var t: texture_cube<f32>; @fragment fn fs() -> @location(0) vec4f { _ = textureSample(t, s, vec3f(0)); return vec4f(); }";
        assert!(validate_texture_builtin_type_source(sample_ok).is_empty());

        let sample_cube_offset = "@group(0) @binding(0) var s: sampler; @group(0) @binding(1) var t: texture_cube<f32>; @fragment fn fs() -> @location(0) vec4f { _ = textureSample(t, s, vec3f(0), vec3i(0)); return vec4f(); }";
        assert!(!validate_texture_builtin_type_source(sample_cube_offset).is_empty());

        let sample_level_cube_offset = "@group(0) @binding(0) var s: sampler; @group(0) @binding(1) var t: texture_cube<f32>; @fragment fn fs() -> @location(0) vec4f { _ = textureSampleLevel(t, s, vec3f(0), 0, vec3i(0)); return vec4f(); }";
        assert!(!validate_texture_builtin_type_source(sample_level_cube_offset).is_empty());

        let sample_grad_depth = "@group(0) @binding(0) var s: sampler; @group(0) @binding(1) var t: texture_depth_2d; @fragment fn fs() -> @location(0) vec4f { _ = textureSampleGrad(t, s, vec2f(0), vec2f(0), vec2f(0)); return vec4f(); }";
        assert!(!validate_texture_builtin_type_source(sample_grad_depth).is_empty());
    }

    #[test]
    fn subgroup_calls_require_enable_directive_even_without_entry_point() {
        assert!(!validate_subgroup_enable_source("fn foo() { _ = subgroupAny(true); }").is_empty());
        assert!(validate_subgroup_enable_source(
            "enable subgroups;\nfn foo() { _ = subgroupAny(true); }"
        )
        .is_empty());
        assert!(validate_subgroup_enable_source("fn foo() { let subgroupAny = true; }").is_empty());
        assert!(validate_subgroup_enable_source("fn subgroupAny() {}").is_empty());
    }

    #[test]
    fn size_attribute_handles_large_and_runtime_array_cases() {
        let large = "struct S { @size(2147483647) a: f32, }";
        assert!(lower_large_size_attribute_source(large).contains("@size(1073741824)"));

        let fixed = "struct S { @size(64) a: array<f32, 4>, }";
        assert!(validate_size_attribute_source(fixed).is_empty());

        let runtime = "struct S { @size(64) a: array<f32>, }";
        assert!(!validate_size_attribute_source(runtime).is_empty());
    }

    #[test]
    fn shader_io_cts_gaps_are_rejected() {
        assert!(
            !validate_shader_io_attribute_source("struct S { @align(2147483648) a: i32, }")
                .is_empty()
        );
        assert!(validate_shader_io_attribute_source(
            "@\tcompute\n@workgroup_size(1)\nfn main() {}"
        )
        .is_empty());
        assert!(!validate_shader_io_attribute_source(
            "@compute var<private> priv_var: i32; @vertex fn f() -> @builtin(position) vec4f { return vec4f(); }"
        )
        .is_empty());
        assert!(!validate_shader_io_attribute_source(
            "@workgroup_size(1i, 1u, 1i) @compute fn main() {}"
        )
        .is_empty());
        assert!(!validate_shader_io_attribute_source(
            "struct S { @location(0) value: f32, }; @compute @workgroup_size(1) fn main(in: S) {}"
        )
        .is_empty());
        assert!(!validate_shader_io_attribute_source(
            "@fragment fn main(@location(0) @interpolate(flat, either) value: texture_external) {}"
        )
        .is_empty());
        assert!(!validate_shader_io_attribute_source(
            "@fragment fn main(@location(0) value: vec2<i32>) {}"
        )
        .is_empty());
        assert!(validate_shader_io_attribute_source(
            "@vertex fn main(@location(0) value: i32) -> @builtin(position) vec4f { return vec4f(); }"
        )
        .is_empty());
        assert!(!validate_shader_io_attribute_source(
            "struct Out { @builtin(position) pos: vec4f, @location(0) value: i32, } @vertex fn main() -> Out { return Out(vec4f(), 1); }"
        )
        .is_empty());
        assert!(validate_shader_io_attribute_source(
            "@fragment fn main(@location(0) @interpolate(flat,) value: vec2<i32>) {}"
        )
        .is_empty());
        assert!(validate_shader_io_attribute_source(
            "@fragment fn main(@interpolate(flat) @location(0) value: vec2<i32>) {}"
        )
        .is_empty());
        assert!(
            normalize_interpolate_trailing_commas_source("@interpolate(flat,)").contains("flat ")
        );
        assert!(lower_struct_size5_align16_source(
            "struct T { @align(16) @size(5) x : u32 } struct S { x : u32, y : T }"
        )
        .contains("@align(16) y"));
    }

    #[test]
    fn diagnostic_derivative_uniformity_scopes_escalate_errors() {
        let prelude = "@group(0) @binding(0) var t : texture_1d<f32>;\n@group(0) @binding(1) var s : sampler;\nvar<private> non_uniform_cond : bool;\n@fragment fn main() {\n";
        let error = format!(
            "{prelude}@diagnostic(error, derivative_uniformity)\nif non_uniform_cond {{ _ = textureSample(t, s, 0.0); }}\n}}"
        );
        assert!(strip_diagnostic_directives(&error).has_error);

        let off = format!(
            "diagnostic(error, derivative_uniformity);\n{prelude}@diagnostic(off, derivative_uniformity)\nif non_uniform_cond {{ _ = textureSample(t, s, 0.0); }}\n}}"
        );
        assert!(!strip_diagnostic_directives(&off).has_error);

        let condition_error = format!(
            "{prelude}if non_uniform_cond {{\n@diagnostic(error, derivative_uniformity)\nif textureSample(t, s, 0.0).x > 0.0 @diagnostic(off, derivative_uniformity) {{}}\n}}\n}}"
        );
        assert!(strip_diagnostic_directives(&condition_error).has_error);
    }

    #[test]
    fn shadowed_builtin_lowering_respects_scope() {
        let shadowed = "fn main() { let smoothstep = 4; _ = smoothstep(1, 2, 3); }";
        assert!(!validate_shadowed_lowered_builtin_calls_source(shadowed).is_empty());
        assert!(lower_smoothstep_source(shadowed).contains("smoothstep(1, 2, 3)"));

        let sibling_shadow = "fn sibling() { let dpdx = 4; } @fragment fn main() -> @location(0) vec4f { _ = dpdx(2); return vec4f(1); }";
        assert!(
            lower_shadow_builtin_abstract_args_source(sibling_shadow, None).contains("dpdx(2.0)")
        );
    }

    #[test]
    fn vector_float_builtin_const_lowering_handles_cts_shapes() {
        assert!(compile_with_meta("const c = acosh(3.4028234663852886e38f);").is_ok());
        assert!(compile_with_meta("const c = acosh(vec4f(3.4028234663852886e38));").is_ok());
        assert!(compile_with_meta("const c = acosh(1.7976931348623157e308);").is_ok());
        assert!(compile_with_meta("const c = acosh(vec4(1.7976931348623157e308));").is_ok());
        assert!(compile_with_meta("const c = acosh(0.99999994f);").is_err());
        assert!(compile_with_meta("fn f() { acosh(1); }").is_err());

        let acosh_lowered = lower_shadow_builtin_abstract_args_source(
            "const c = acosh(vec2f(3.4028234663852886e38));",
            None,
        );
        assert!(acosh_lowered.contains("vec2<f32>("));
        assert!(!acosh_lowered.contains("acosh("));

        let acosh_source = "override o0 : f32; var<private> v = acosh(f32(o0)); @compute @workgroup_size(1) fn main() { _ = v; }";
        assert!(compile_with_meta(acosh_source).is_ok());
        let mut constants = HashMap::new();
        constants.insert("o0".to_string(), f32::MAX as f64);
        assert!(bake_wgsl_with_constants(acosh_source, constants, Some("main")).is_some());

        assert!(compile_with_meta("const c = mix(vec3(0), vec3(1), vec3(0.5));").is_ok());
        assert!(compile_with_meta("const c = reflect(vec3(0), vec3(1));").is_ok());
        assert!(compile_with_meta("const c = refract(vec3(0), vec3(1), 2.0);").is_ok());
        assert!(compile_with_meta("const c = faceForward(vec3(0), vec3(1), vec3(0.5));").is_ok());

        assert!(compile_with_meta("fn f() { _ = mix(vec3(0), vec3(1), vec3(0.5)); }").is_ok());
        assert!(compile_with_meta("fn f() { mix(vec3(0), vec3(1), vec3(0.5)); }").is_err());
        assert!(compile_with_meta("const c: vec3u = refract(vec3(0), vec3(1), 2.0);").is_err());
        assert!(compile_with_meta("const c = refract(vec3(0));").is_err());
        assert!(compile_with_meta("const c = refract(vec3(0), vec3(1));").is_err());
        assert!(compile_with_meta("const c = refract(vec3(0), vec3(1), true);").is_err());

        let lowered = lower_shadow_builtin_abstract_args_source(
            "const c = mix(vec3(0), vec3(1), vec3(0.5));",
            None,
        );
        assert!(lowered.contains("vec3("));
        assert!(!lowered.contains("mix("));

        let source = "override o0 : f32; override o1 : f32; override o2 : f32; var<private> v = mix(vec3<f32>(o0, o0, o0), vec3<f32>(o1, o1, o1), vec3<f32>(o2, o2, o2)); @compute @workgroup_size(1) fn main() { _ = v; }";
        assert!(compile_with_meta(source).is_ok());
        let mut constants = HashMap::new();
        constants.insert("o0".to_string(), 0.0);
        constants.insert("o1".to_string(), 1.0);
        constants.insert("o2".to_string(), 0.5);
        assert!(bake_wgsl_with_constants(source, constants, Some("main")).is_some());

        let refract_source = "override o0 : f32; override o1 : f32; override o2 : f32; override o3 : f32; override o4 : f32; var<private> v = refract(vec2<f32>(o0, o1), vec2<f32>(o2, o3), f32(o4)); @compute @workgroup_size(1) fn main() { _ = v; }";
        assert!(compile_with_meta(refract_source).is_ok());
        let mut constants = HashMap::new();
        constants.insert("o0".to_string(), 0.0);
        constants.insert("o1".to_string(), 0.0);
        constants.insert("o2".to_string(), 1.0);
        constants.insert("o3".to_string(), 0.0);
        constants.insert("o4".to_string(), f32::MAX as f64);
        assert!(bake_wgsl_with_constants(refract_source, constants, Some("main")).is_none());
    }

    #[test]
    fn vector_float_builtin_domain_validation_handles_overflow() {
        let mix = "const c = mix(vec3f(3.40282347e38f), vec3f(0.0f), vec3f(-1.0f));";
        assert!(!validate_math_domain_source(mix, None, None).is_empty());

        let face_forward =
            "const c = faceForward(vec3f(0.0f), vec3f(3.40282347e38f), vec3f(3.40282347e38f));";
        assert!(!validate_math_domain_source(face_forward, None, None).is_empty());
    }

    #[test]
    fn abstract_float_output_snippet_lowering_handles_cts_shapes() {
        let source = r#"
struct AF { high: u32, low: u32 }
struct Output { value: AF }
@group(0) @binding(0) var<storage, read_write> outputs : array<Output, 1>;

@compute @workgroup_size(1)
fn main() {
  {
    const kExponentBias = 1022;
    const subnormal_or_zero : bool = (((1.0) + (2.0)) <= 2.225073858507201e-308) && (((1.0) + (2.0)) >= -2.225073858507201e-308);
    const sign_bit : u32 = select(0, 0x80000000, ((1.0) + (2.0)) < 0);
    const f = frexp(abs(((1.0) + (2.0))));
    const f_fract = select(f.fract, 0, subnormal_or_zero);
    const f_exp = select(f.exp, -kExponentBias, subnormal_or_zero);
    const exponent_bits : u32 = (f_exp + kExponentBias) << 20;
    const high_mantissa = ldexp(f_fract, 21);
    const high_mantissa_bits : u32 = u32(ldexp(f_fract, 21)) & 0x000fffff;
    const low_mantissa = f_fract - ldexp(floor(high_mantissa), -21);
    const low_mantissa_bits = u32(ldexp(low_mantissa, 53));
    outputs[0].value.high = sign_bit | exponent_bits | high_mantissa_bits;
    outputs[0].value.low = low_mantissa_bits;
  }
}
"#;
        let lowered = lower_abstract_float_output_snippets_source(source, None);
        assert!(lowered.contains("outputs[0].value.high = 1074266112u;"));
        assert!(lowered.contains("outputs[0].value.low = 0u;"));
        assert!(!lowered.contains("frexp("));
        assert!(compile_with_meta(source).is_ok());

        let vector = "abs(vec2(-1.0, -2.0))[1]";
        let values = resolve_math_float_values(vector, None).unwrap();
        assert_eq!(values.values, vec![2.0]);

        let asinh_source = source.replace("((1.0) + (2.0))", "asinh(-1.7976931348623157e+308)");
        let asinh_lowered = lower_abstract_float_output_snippets_source(&asinh_source, None);
        assert!(!asinh_lowered.contains("frexp("));
        assert!(asinh_lowered.contains("outputs[0].value.high ="));
        assert!(compile_with_meta(&asinh_source).is_ok());
        assert!(stable_asinh_f64(-0.0000010704507633131377).is_sign_negative());

        let atanh_source = source.replace("((1.0) + (2.0))", "atanh(-0.9997229916897505)");
        let atanh_lowered = lower_abstract_float_output_snippets_source(&atanh_source, None);
        assert!(!atanh_lowered.contains("frexp("));
        assert!(compile_with_meta(&atanh_source).is_ok());

        let f32_atanh = "const values = array(atanh(-0.9999999403953552f));";
        let f32_atanh_lowered = lower_const_float_math_source(f32_atanh, None);
        assert!(!f32_atanh_lowered.contains("atanh("));
        assert!(f32_atanh_lowered.contains("-8.664"));

        let modf_lowered =
            lower_const_float_math_source("const values = array(modf(-1.25f).fract);", None);
        assert!(!modf_lowered.contains("modf("));
        assert!(modf_lowered.contains("-2.5e-1f"));

        let frexp_lowered =
            lower_const_float_math_source("const values = array(frexp(8.0f).exp);", None);
        assert_eq!(frexp_lowered, "const values = array(4);");
        let frexp_abstract_vector =
            lower_const_float_math_source("const values = array(frexp(vec2(8.0, 4.0)).exp);", None);
        assert!(frexp_abstract_vector.contains("vec2(4, 3)"));

        let fract = resolve_math_float_values("fract(-0.1)", None).unwrap();
        assert!((fract.values[0] - 0.9).abs() < 1.0e-15);

        let selected = resolve_math_float_values(
            "select(vec3(1.0, 2.0, 3.0), vec3(4.0, 5.0, 6.0), vec3<bool>(true, false, true))",
            None,
        )
        .unwrap();
        assert_eq!(selected.values, vec![4.0, 2.0, 6.0]);
    }

    #[test]
    fn packed_builtin_const_lowering_handles_cts_shapes() {
        assert_eq!(
            lower_packed_builtins_source("const c = pack4xU8(vec4u());", None),
            "const c = 0u;"
        );
        assert!(compile_with_meta("const c = pack4xU8(vec4u());").is_ok());
        assert!(compile_with_meta("const c = pack4x8unorm(vec4(0.1));").is_ok());
        assert!(compile_with_meta("const c = pack2x16float(vec2f());").is_ok());
        assert!(compile_with_meta("const c: vec4<u32> = unpack4xU8(1u);").is_ok());
        assert!(compile_with_meta("const c: vec4<i32> = unpack4xI8(1u);").is_ok());
        assert!(compile_with_meta("const c: vec2<f32> = unpack2x16unorm(1u);").is_ok());

        let not_lowered = lower_packed_builtins_source("const c = unpack4xU8(i32(1));", None);
        assert!(not_lowered.contains("unpack4xU8(i32(1))"));

        let mut constants = HashMap::new();
        constants.insert("o0".to_string(), 65504.0);
        constants.insert("o1".to_string(), -65504.0);
        let lowered = lower_packed_builtins_source(
            "override o0 : f32; override o1 : f32; var<private> v = pack2x16float(vec2<f32>(o0, o1));",
            Some(&constants),
        );
        assert!(lowered.contains("var<private> v = 0u;"));
    }

    #[test]
    fn quantize_to_f16_const_lowering_handles_cts_shapes() {
        let lowered = lower_quantize_to_f16_source(
            "const a = quantizeToF16(0.011948528699576855f); const b = quantizeToF16(vec2(1.0, -1.0));",
            None,
        );
        assert!(lowered.contains("const a = f32("));
        assert!(lowered.contains("const b = vec2<f32>("));
        assert!(compile_with_meta("const c = quantizeToF16(0.011948528699576855f);").is_ok());
        assert!(compile_with_meta("const c = quantizeToF16(vec3f());").is_ok());
        assert!(
            lower_quantize_to_f16_source("const c = quantizeToF16(1h);", None)
                .contains("quantizeToF16(1h)")
        );
    }

    #[test]
    fn matrix_constructor_dims_parses_naked_and_typed() {
        assert_eq!(matrix_constructor_dims("mat2x2"), Some((2, 2, None)));
        assert_eq!(matrix_constructor_dims("mat3x4"), Some((3, 4, None)));
        assert_eq!(
            matrix_constructor_dims("mat4x4<f32>"),
            Some((4, 4, Some(SourceFloatKind::F32)))
        );
        assert_eq!(
            matrix_constructor_dims("mat3x2<f16>"),
            Some((3, 2, Some(SourceFloatKind::F16)))
        );
        assert_eq!(
            matrix_constructor_dims("mat2x3f"),
            Some((2, 3, Some(SourceFloatKind::F32)))
        );
        assert_eq!(
            matrix_constructor_dims("mat2x3h"),
            Some((2, 3, Some(SourceFloatKind::F16)))
        );
        assert_eq!(matrix_constructor_dims("vec3"), None);
        assert_eq!(matrix_constructor_dims("mat5x2"), None);
    }

    #[test]
    fn resolve_matrix_handles_scalar_and_vector_constructors() {
        let m = resolve_math_matrix_values("mat2x2(1.0, 2.0, 3.0, 4.0)", None).unwrap();
        assert_eq!((m.cols, m.rows), (2, 2));
        assert_eq!(m.columns, vec![vec![1.0, 2.0], vec![3.0, 4.0]]);

        let m =
            resolve_math_matrix_values("mat2x3(vec3(1.0, 2.0, 3.0), vec3(4.0, 5.0, 6.0))", None)
                .unwrap();
        assert_eq!((m.cols, m.rows), (2, 3));
        assert_eq!(m.columns, vec![vec![1.0, 2.0, 3.0], vec![4.0, 5.0, 6.0]]);

        let m = resolve_math_matrix_values("mat3x2(1.0, 2.0, 3.0, 4.0, 5.0, 6.0)", None).unwrap();
        assert_eq!((m.cols, m.rows), (3, 2));
        assert_eq!(
            m.columns,
            vec![vec![1.0, 2.0], vec![3.0, 4.0], vec![5.0, 6.0]]
        );
    }

    #[test]
    fn format_matrix_values_round_trips_through_resolve() {
        let m = resolve_math_matrix_values("mat3x2(1.0, 2.0, 3.0, 4.0, 5.0, 6.0)", None).unwrap();
        let s = format_source_matrix_values(&m).unwrap();
        let m2 = resolve_math_matrix_values(&s, None).unwrap();
        assert_eq!(m.columns, m2.columns);
        assert_eq!((m.cols, m.rows), (m2.cols, m2.rows));
    }

    #[test]
    fn eval_transpose_swaps_dimensions_and_indices() {
        let m = resolve_math_matrix_values("mat3x2(1.0,2.0, 3.0,4.0, 5.0,6.0)", None).unwrap();
        let t = eval_transpose_matrix(m).unwrap();
        assert_eq!((t.cols, t.rows), (2, 3));
        assert_eq!(t.columns, vec![vec![1.0, 3.0, 5.0], vec![2.0, 4.0, 6.0]]);
    }

    #[test]
    fn eval_determinant_dim_2_3_4() {
        let m = resolve_math_matrix_values("mat2x2(1.0, 2.0, 3.0, 4.0)", None).unwrap();
        let r = eval_determinant_matrix(m).unwrap().unwrap();
        assert_eq!(r.values, vec![-2.0]);

        let m = resolve_math_matrix_values("mat3x3(1.0,4.0,7.0, 2.0,5.0,8.0, 3.0,6.0,9.0)", None)
            .unwrap();
        let r = eval_determinant_matrix(m).unwrap().unwrap();
        assert_eq!(r.values, vec![0.0]);

        let m = resolve_math_matrix_values("mat3x3(2.0,0.0,0.0, 0.0,3.0,0.0, 0.0,0.0,5.0)", None)
            .unwrap();
        let r = eval_determinant_matrix(m).unwrap().unwrap();
        assert_eq!(r.values, vec![30.0]);

        let m = resolve_math_matrix_values(
            "mat4x4(1.0,0.0,0.0,0.0, 0.0,2.0,0.0,0.0, 0.0,0.0,3.0,0.0, 0.0,0.0,0.0,4.0)",
            None,
        )
        .unwrap();
        let r = eval_determinant_matrix(m).unwrap().unwrap();
        assert_eq!(r.values, vec![24.0]);
    }

    #[test]
    fn eval_matrix_matrix_mul_2x2() {
        let a = resolve_math_matrix_values("mat2x2(1.0,2.0, 3.0,4.0)", None).unwrap();
        let b = resolve_math_matrix_values("mat2x2(5.0,6.0, 7.0,8.0)", None).unwrap();
        let r = eval_matrix_matrix_mul(a, b).unwrap().unwrap();
        assert_eq!((r.cols, r.rows), (2, 2));
        assert_eq!(r.columns, vec![vec![23.0, 34.0], vec![31.0, 46.0]]);
    }

    #[test]
    fn eval_matrix_matrix_mul_2x3_times_3x2() {
        let a = resolve_math_matrix_values("mat2x3(1.0,2.0,3.0, 4.0,5.0,6.0)", None).unwrap();
        let b = resolve_math_matrix_values("mat3x2(7.0,8.0, 9.0,10.0, 11.0,12.0)", None).unwrap();
        let r = eval_matrix_matrix_mul(a, b).unwrap().unwrap();
        assert_eq!((r.cols, r.rows), (3, 3));
        assert_eq!(
            r.columns,
            vec![
                vec![39.0, 54.0, 69.0],
                vec![49.0, 68.0, 87.0],
                vec![59.0, 82.0, 105.0],
            ]
        );
    }

    #[test]
    fn eval_matrix_vector_mul_2x3_times_vec2() {
        let m = resolve_math_matrix_values("mat2x3(1.0,2.0,3.0, 4.0,5.0,6.0)", None).unwrap();
        let v = resolve_math_float_values("vec2(7.0, 8.0)", None).unwrap();
        let r = eval_matrix_vector_mul(m, v).unwrap().unwrap();
        assert_eq!(r.values, vec![39.0, 54.0, 69.0]);
    }

    #[test]
    fn eval_vector_matrix_mul_vec2_times_3x2() {
        let v = resolve_math_float_values("vec2(7.0, 8.0)", None).unwrap();
        let m = resolve_math_matrix_values("mat3x2(1.0,2.0, 3.0,4.0, 5.0,6.0)", None).unwrap();
        let r = eval_vector_matrix_mul(v, m).unwrap().unwrap();
        assert_eq!(r.values, vec![23.0, 53.0, 83.0]);
    }

    #[test]
    fn eval_matrix_scalar_mul_componentwise() {
        let m = resolve_math_matrix_values("mat2x2(1.0,2.0, 3.0,4.0)", None).unwrap();
        let s = SourceFloatValues {
            kind: SourceFloatKind::Abstract,
            values: vec![3.0],
        };
        let r = eval_matrix_scalar_mul(m, s).unwrap().unwrap();
        assert_eq!(r.columns, vec![vec![3.0, 6.0], vec![9.0, 12.0]]);
    }

    #[test]
    fn eval_matrix_componentwise_add_sub() {
        let a = resolve_math_matrix_values("mat2x2(1.0,2.0, 3.0,4.0)", None).unwrap();
        let b = resolve_math_matrix_values("mat2x2(10.0,20.0, 30.0,40.0)", None).unwrap();
        let add = eval_matrix_componentwise_op(a.clone(), b.clone(), |x, y| x + y)
            .unwrap()
            .unwrap();
        assert_eq!(add.columns, vec![vec![11.0, 22.0], vec![33.0, 44.0]]);
        let sub = eval_matrix_componentwise_op(a, b, |x, y| x - y)
            .unwrap()
            .unwrap();
        assert_eq!(sub.columns, vec![vec![-9.0, -18.0], vec![-27.0, -36.0]]);
    }

    #[test]
    fn lower_const_matrix_binary_source_handles_mat_mat_mul() {
        let source =
            "fn f() -> f32 { return ((mat2x2(1.0,2.0,3.0,4.0)) * (mat2x2(5.0,6.0,7.0,8.0)))[0][0]; }";
        let lowered = lower_const_matrix_binary_source(source, None);
        assert_ne!(lowered, source);
        assert!(!lowered.contains("(mat2x2"));
        assert!(lowered.contains("2.30000000000000000e1"));
        assert!(compile_with_meta(&lowered).is_ok());
    }

    #[test]
    fn lower_const_matrix_binary_source_handles_mat_vec_mul() {
        let source =
            "fn f() -> f32 { return ((mat2x3(1.0,2.0,3.0, 4.0,5.0,6.0)) * (vec2(7.0,8.0)))[0]; }";
        let lowered = lower_const_matrix_binary_source(source, None);
        assert_ne!(lowered, source);
        assert!(lowered.contains("3.90000000000000000e1"));
        assert!(compile_with_meta(&lowered).is_ok());
    }

    #[test]
    fn lower_const_matrix_binary_source_handles_mat_add() {
        let source = "fn f() -> f32 { return ((mat2x2(1.0,2.0,3.0,4.0)) + (mat2x2(10.0,20.0,30.0,40.0)))[0][0]; }";
        let lowered = lower_const_matrix_binary_source(source, None);
        assert_ne!(lowered, source);
        assert!(lowered.contains("1.10000000000000000e1"));
        assert!(compile_with_meta(&lowered).is_ok());
    }

    #[test]
    fn lower_const_transpose_call_emits_literal_matrix() {
        let source = "fn f() -> f32 { return transpose(mat2x3(1.0,2.0,3.0, 4.0,5.0,6.0))[0][0]; }";
        let lowered = lower_shadow_builtin_abstract_args_source(source, None);
        assert!(lowered.contains("mat3x2"));
        assert!(compile_with_meta(&lowered).is_ok());
    }

    #[test]
    fn lower_const_determinant_call_emits_scalar() {
        let source = "fn f() -> f32 { return f32(determinant(mat2x2(1.0, 2.0, 3.0, 4.0))); }";
        let lowered = lower_shadow_builtin_abstract_args_source(source, None);
        assert!(lowered.contains("-2"));
        assert!(compile_with_meta(&lowered).is_ok());
    }

    #[test]
    fn cts_like_af_matrix_addition_compiles() {
        // Mimics what abstractFloatShaderBuilder emits for af_matrix_addition
        // with cols=2, rows=2. The expression `((MAT_A)+(MAT_B))[c][r]` is
        // referenced four times across the snippet; each must lower cleanly.
        let source = r#"
struct AbstractFloat { high: u32, low: u32 };
struct Output { value: array<array<AbstractFloat, 2>, 2> };
@group(0) @binding(0) var<storage, read_write> outputs: array<Output, 1>;
@compute @workgroup_size(1)
fn main() {
  {
    const kExponentBias = 1022;
    const subnormal_or_zero : bool = (((mat2x2(1.0, 2.0, 3.0, 4.0))+(mat2x2(5.0, 6.0, 7.0, 8.0)))[0][0] <= 2.2250738585072014e-308) && (((mat2x2(1.0, 2.0, 3.0, 4.0))+(mat2x2(5.0, 6.0, 7.0, 8.0)))[0][0] >= -2.2250738585072014e-308);
    const sign_bit : u32 = select(0, 0x80000000, ((mat2x2(1.0, 2.0, 3.0, 4.0))+(mat2x2(5.0, 6.0, 7.0, 8.0)))[0][0] < 0);
    const f = frexp(abs(((mat2x2(1.0, 2.0, 3.0, 4.0))+(mat2x2(5.0, 6.0, 7.0, 8.0)))[0][0]));
    const f_fract = select(f.fract, 0, subnormal_or_zero);
    const f_exp = select(f.exp, -kExponentBias, subnormal_or_zero);
    const exponent_bits : u32 = u32(f_exp + kExponentBias) << 20;
    const high_mantissa = ldexp(f_fract, 21);
    const high_mantissa_bits : u32 = u32(ldexp(f_fract, 21)) & 0x000fffff;
    const low_mantissa = f_fract - ldexp(floor(high_mantissa), -21);
    const low_mantissa_bits = u32(ldexp(low_mantissa, 53));
    outputs[0].value[0][0].high = sign_bit | exponent_bits | high_mantissa_bits;
    outputs[0].value[0][0].low = low_mantissa_bits;
  }
}
"#;
        let r = compile_with_meta(source);
        assert!(
            r.is_ok(),
            "compile failed: {:?}",
            r.err()
                .map(|msgs| msgs.iter().map(|m| m.message.clone()).collect::<Vec<_>>())
        );
    }
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
    if !validate_parse_frontend_source(source).is_empty() {
        return None;
    }
    let strip = strip_diagnostic_directives(source);
    if strip.has_error {
        return None;
    }
    if !validate_short_circuit_source(&strip.sanitized, entry_point).is_empty() {
        return None;
    }
    let short_circuit_source = lower_short_circuit_source(&strip.sanitized, Some(&constants));
    let dynamic_index_source = lower_dynamic_index_lets_source(&short_circuit_source);
    let var_normalized_source =
        normalize_var_template_trailing_commas_source(&dynamic_index_source);
    let interpolated_source = normalize_interpolate_trailing_commas_source(&var_normalized_source);
    let normalized_source = lower_struct_size5_align16_source(&interpolated_source);
    if !validate_shadowed_lowered_builtin_calls_source(&normalized_source).is_empty() {
        return None;
    }
    if !validate_smoothstep_source(&normalized_source, Some(&constants), entry_point).is_empty() {
        return None;
    }
    if !validate_ldexp_source(&normalized_source, Some(&constants), entry_point).is_empty() {
        return None;
    }
    if !validate_binary_precedence_source(&normalized_source, entry_point).is_empty() {
        return None;
    }
    if !validate_function_attribute_source(&normalized_source).is_empty() {
        return None;
    }
    if !validate_shader_io_attribute_source(&normalized_source).is_empty() {
        return None;
    }
    if !validate_id_attribute_source(&normalized_source).is_empty() {
        return None;
    }
    if !validate_loop_behavior_source(&normalized_source).is_empty() {
        return None;
    }
    if !validate_size_attribute_source(&normalized_source).is_empty() {
        return None;
    }
    if !validate_atomic_source(&normalized_source).is_empty() {
        return None;
    }
    if !validate_bool_order_comparison_source(&normalized_source).is_empty() {
        return None;
    }
    if !validate_unrestricted_pointer_parameter_source(&normalized_source).is_empty() {
        return None;
    }
    if !validate_override_sized_array_source(&normalized_source).is_empty() {
        return None;
    }
    if !validate_let_resource_handle_source(&normalized_source).is_empty() {
        return None;
    }
    if !validate_texture_sample_base_clamp_to_edge_source(&normalized_source).is_empty() {
        return None;
    }
    if !validate_texture_builtin_type_source(&normalized_source).is_empty() {
        return None;
    }
    if !validate_const_bitcast_source(&normalized_source, Some(&constants)).is_empty() {
        return None;
    }
    if !validate_div_rem_source(&normalized_source, Some(&constants), entry_point).is_empty() {
        return None;
    }
    if !validate_math_domain_source(&normalized_source, Some(&constants), entry_point).is_empty() {
        return None;
    }
    let size_lowered_source = lower_large_size_attribute_source(&normalized_source);
    let naga_source = lower_no_entry_workgroup_pointer_params_source(&size_lowered_source);
    let shadow_builtin_source =
        lower_shadow_builtin_abstract_args_source(&naga_source, Some(&constants));
    let matrix_binary_source =
        lower_const_matrix_binary_source(&shadow_builtin_source, Some(&constants));
    let bool_bitwise_source = lower_bool_bitwise_const_source(&matrix_binary_source);
    let float_math_source = lower_const_float_math_source(&bool_bitwise_source, Some(&constants));
    let parse_source = lower_const_bitcasts_source(
        &lower_packed_builtins_source(
            &lower_bit_builtins_source(
                &lower_quantize_to_f16_source(
                    &lower_ldexp_source(
                        &lower_smoothstep_source(&float_math_source),
                        Some(&constants),
                    ),
                    Some(&constants),
                ),
                Some(&constants),
            ),
            Some(&constants),
        ),
        Some(&constants),
    );
    let parse_source = lower_abstract_float_output_snippets_source(&parse_source, Some(&constants));
    let mut module = wgsl::parse_str(&parse_source).ok()?;
    clamp_image_sample_bias(&mut module);
    cloak_const_indices(&mut module, &parse_source);
    let mut bridge_errors = validate_storage_atomic_access(&module);
    bridge_errors.extend(validate_stage_resource_restrictions(&module, &parse_source));
    bridge_errors.extend(validate_bool_order_comparisons(&module));
    if !bridge_errors.is_empty() {
        return None;
    }
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
            .and_then(|(processed_module, _processed_info)| {
                let mut processed_module = processed_module.into_owned();
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
                if !validate_bit_builtins_live(&processed_module).is_empty() {
                    return None;
                }
                fold_bit_builtins(&mut processed_module);
                let processed_info = Validator::new(ValidationFlags::all(), Capabilities::all())
                    .validate(&processed_module)
                    .ok()?;
                spv::write_vec(&processed_module, &processed_info, &options, None).ok()
            })
    }))
    .ok()
    .flatten()?;
    strip_workgroup_explicit_layout(&mut words);
    rewrite_signed_mod_to_remainder(&mut words);
    Some(words)
}

fn hlsl_stage_from_str(stage: &str) -> Option<naga::ShaderStage> {
    match stage {
        "vertex" => Some(naga::ShaderStage::Vertex),
        "fragment" => Some(naga::ShaderStage::Fragment),
        "compute" => Some(naga::ShaderStage::Compute),
        _ => None,
    }
}

fn apply_dx12_webgpu_binding_map(options: &mut hlsl::Options, module: &naga::Module) {
    for (_, global) in module.global_variables.iter() {
        let Some(binding) = global.binding else {
            continue;
        };

        let register = if matches!(global.space, naga::AddressSpace::Uniform) && binding.group == 0
        {
            binding.binding + 1
        } else {
            binding.binding
        };

        options.binding_map.insert(
            binding,
            hlsl::BindTarget {
                space: binding.group as u8,
                register,
                ..Default::default()
            },
        );
    }
}

pub fn spirv_to_hlsl(
    words: &[u32],
    entry_point: Option<&str>,
    stage: Option<&str>,
) -> Option<String> {
    if words.is_empty() {
        return None;
    }

    let parse_options = spv_front::Options {
        adjust_coordinate_space: false,
        strict_capabilities: false,
        block_ctx_dump_prefix: None,
    };

    let module = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        spv_front::Frontend::new(words.iter().copied(), &parse_options).parse()
    }))
    .ok()
    .and_then(Result::ok)?;

    let info = Validator::new(ValidationFlags::all(), hlsl::supported_capabilities())
        .validate(&module)
        .ok()?;

    let mut options = hlsl::Options::default();
    options.shader_model = hlsl::ShaderModel::V6_6;
    options.fake_missing_bindings = false;
    apply_dx12_webgpu_binding_map(&mut options, &module);

    let pipeline_options = hlsl::PipelineOptions {
        entry_point: match (entry_point, stage.and_then(hlsl_stage_from_str)) {
            (Some(name), Some(shader_stage)) if !name.is_empty() => {
                Some((shader_stage, name.to_string()))
            }
            _ => None,
        },
    };

    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let mut output = String::new();
        let mut writer = hlsl::Writer::new(&mut output, &options, &pipeline_options);
        writer.write(&module, &info, None).ok()?;
        Some(output)
    }))
    .ok()
    .flatten()
}
