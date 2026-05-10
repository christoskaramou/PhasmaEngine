use std::collections::HashMap;
use std::ffi::{c_char, c_void, CStr, CString};
use std::ptr;
use std::slice;

use crate::{
    bake_wgsl_with_constants, compile_with_meta, install_panic_suppression, spirv_to_hlsl,
    BindingRef, EntryPointMeta, OverrideInfo, WgslCompileResult, WgslMessage,
};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NagaBindingView {
    pub group: u32,
    pub binding: u32,
    pub name: *const c_char,
    pub kind: *const c_char,
    pub format: *const c_char,
    pub access: *const c_char,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NagaEntryPointView {
    pub name: *const c_char,
    pub stage: *const c_char,
    pub statically_used: *const NagaBindingView,
    pub statically_used_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NagaOverrideView {
    pub identifier: *const c_char,
    pub name: *const c_char,
    pub type_name: *const c_char,
    pub has_default: bool,
    pub statically_used: bool,
    pub statically_used_by: *const *const c_char,
    pub statically_used_by_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NagaMessageView {
    pub r#type: *const c_char,
    pub message: *const c_char,
    pub line_num: u32,
    pub line_pos: u32,
    pub offset: u32,
    pub length: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NagaConstantKv {
    pub key: *const c_char,
    pub value: f64,
}

struct BindingOwned {
    group: u32,
    binding: u32,
    name: Option<CString>,
    kind: Option<CString>,
    format: Option<CString>,
    access: Option<CString>,
}

struct EntryPointOwned {
    name: CString,
    stage: CString,
    _statically_used: Vec<BindingOwned>,
    statically_used_views: Vec<NagaBindingView>,
}

struct OverrideOwned {
    identifier: CString,
    name: CString,
    type_name: CString,
    has_default: bool,
    statically_used: bool,
    _statically_used_by: Vec<CString>,
    statically_used_by_ptrs: Vec<*const c_char>,
}

struct MessageOwned {
    r#type: CString,
    message: CString,
    line_num: u32,
    line_pos: u32,
    offset: u32,
    length: u32,
}

pub struct NagaCompileResult {
    spirv: Vec<u32>,
    entry_points: Vec<EntryPointOwned>,
    _comparison_samplers: Vec<BindingOwned>,
    comparison_sampler_views: Vec<NagaBindingView>,
    overrides: Vec<OverrideOwned>,
    messages: Vec<MessageOwned>,
}

unsafe extern "C" {
    fn malloc(size: usize) -> *mut c_void;
    fn free(ptr: *mut c_void);
}

fn make_cstring(value: impl Into<String>) -> CString {
    match CString::new(value.into()) {
        Ok(s) => s,
        Err(err) => {
            let bytes: Vec<u8> = err.into_vec().into_iter().filter(|b| *b != 0).collect();
            CString::new(bytes).unwrap_or_else(|_| CString::new("").unwrap())
        }
    }
}

fn make_optional_cstring(value: Option<String>) -> Option<CString> {
    value.and_then(|s| {
        if s.is_empty() {
            None
        } else {
            Some(make_cstring(s))
        }
    })
}

fn opt_ptr(value: &Option<CString>) -> *const c_char {
    value.as_ref().map_or(ptr::null(), |s| s.as_ptr())
}

impl BindingOwned {
    fn from_binding(binding: BindingRef) -> Self {
        Self {
            group: binding.group,
            binding: binding.binding,
            name: make_optional_cstring(binding.name),
            kind: make_optional_cstring(binding.kind),
            format: make_optional_cstring(binding.format),
            access: make_optional_cstring(binding.access),
        }
    }

    fn view(&self) -> NagaBindingView {
        NagaBindingView {
            group: self.group,
            binding: self.binding,
            name: opt_ptr(&self.name),
            kind: opt_ptr(&self.kind),
            format: opt_ptr(&self.format),
            access: opt_ptr(&self.access),
        }
    }
}

impl EntryPointOwned {
    fn from_entry_point(entry_point: EntryPointMeta) -> Self {
        let statically_used: Vec<BindingOwned> = entry_point
            .statically_used
            .into_iter()
            .map(BindingOwned::from_binding)
            .collect();
        let statically_used_views = statically_used.iter().map(BindingOwned::view).collect();
        Self {
            name: make_cstring(entry_point.name),
            stage: make_cstring(entry_point.stage),
            _statically_used: statically_used,
            statically_used_views,
        }
    }

    fn view(&self) -> NagaEntryPointView {
        NagaEntryPointView {
            name: self.name.as_ptr(),
            stage: self.stage.as_ptr(),
            statically_used: if self.statically_used_views.is_empty() {
                ptr::null()
            } else {
                self.statically_used_views.as_ptr()
            },
            statically_used_count: self.statically_used_views.len(),
        }
    }
}

impl OverrideOwned {
    fn from_override(override_info: OverrideInfo) -> Self {
        let statically_used_by: Vec<CString> = override_info
            .statically_used_by
            .into_iter()
            .map(make_cstring)
            .collect();
        let statically_used_by_ptrs = statically_used_by.iter().map(|s| s.as_ptr()).collect();
        Self {
            identifier: make_cstring(override_info.identifier),
            name: make_cstring(override_info.name),
            type_name: make_cstring(override_info.r#type),
            has_default: override_info.has_default,
            statically_used: override_info.statically_used,
            _statically_used_by: statically_used_by,
            statically_used_by_ptrs,
        }
    }

    fn view(&self) -> NagaOverrideView {
        NagaOverrideView {
            identifier: self.identifier.as_ptr(),
            name: self.name.as_ptr(),
            type_name: self.type_name.as_ptr(),
            has_default: self.has_default,
            statically_used: self.statically_used,
            statically_used_by: if self.statically_used_by_ptrs.is_empty() {
                ptr::null()
            } else {
                self.statically_used_by_ptrs.as_ptr()
            },
            statically_used_by_count: self.statically_used_by_ptrs.len(),
        }
    }
}

impl MessageOwned {
    fn from_message(message: WgslMessage) -> Self {
        Self {
            r#type: make_cstring(message.r#type),
            message: make_cstring(message.message),
            line_num: message.line_num,
            line_pos: message.line_pos,
            offset: message.offset,
            length: message.length,
        }
    }

    fn view(&self) -> NagaMessageView {
        NagaMessageView {
            r#type: self.r#type.as_ptr(),
            message: self.message.as_ptr(),
            line_num: self.line_num,
            line_pos: self.line_pos,
            offset: self.offset,
            length: self.length,
        }
    }
}

impl NagaCompileResult {
    fn from_compile_result(result: WgslCompileResult) -> Self {
        let comparison_samplers: Vec<BindingOwned> = result
            .comparison_samplers
            .into_iter()
            .map(BindingOwned::from_binding)
            .collect();
        let comparison_sampler_views = comparison_samplers.iter().map(BindingOwned::view).collect();
        Self {
            spirv: result.spirv.unwrap_or_default(),
            entry_points: result
                .entry_points
                .into_iter()
                .map(EntryPointOwned::from_entry_point)
                .collect(),
            _comparison_samplers: comparison_samplers,
            comparison_sampler_views,
            overrides: result
                .overrides
                .into_iter()
                .map(OverrideOwned::from_override)
                .collect(),
            messages: result
                .messages
                .into_iter()
                .map(MessageOwned::from_message)
                .collect(),
        }
    }

    fn from_messages(messages: Vec<WgslMessage>) -> Self {
        Self {
            spirv: Vec::new(),
            entry_points: Vec::new(),
            _comparison_samplers: Vec::new(),
            comparison_sampler_views: Vec::new(),
            overrides: Vec::new(),
            messages: messages
                .into_iter()
                .map(MessageOwned::from_message)
                .collect(),
        }
    }

    fn from_error(message: &str) -> Self {
        Self::from_messages(vec![WgslMessage {
            r#type: "error".to_string(),
            message: message.to_string(),
            line_num: 0,
            line_pos: 0,
            offset: 0,
            length: 0,
        }])
    }
}

fn source_from_raw<'a>(
    source: *const c_char,
    source_len: usize,
) -> Result<&'a str, NagaCompileResult> {
    if source.is_null() {
        if source_len == 0 {
            return Ok("");
        }
        return Err(NagaCompileResult::from_error("WGSL source pointer is null"));
    }
    let bytes = unsafe { slice::from_raw_parts(source.cast::<u8>(), source_len) };
    std::str::from_utf8(bytes)
        .map_err(|_| NagaCompileResult::from_error("WGSL source is not valid UTF-8"))
}

unsafe fn cstr_to_string(ptr: *const c_char) -> Option<String> {
    if ptr.is_null() {
        return None;
    }
    Some(CStr::from_ptr(ptr).to_string_lossy().into_owned())
}

#[no_mangle]
pub extern "C" fn naga_install_panic_suppression() {
    install_panic_suppression();
}

#[no_mangle]
pub extern "C" fn naga_compile_wgsl(
    source: *const c_char,
    source_len: usize,
) -> *mut NagaCompileResult {
    install_panic_suppression();
    let source = match source_from_raw(source, source_len) {
        Ok(source) => source,
        Err(result) => return Box::into_raw(Box::new(result)),
    };
    let result = match compile_with_meta(source) {
        Ok(result) => NagaCompileResult::from_compile_result(result),
        Err(messages) => NagaCompileResult::from_messages(messages),
    };
    Box::into_raw(Box::new(result))
}

#[no_mangle]
pub extern "C" fn naga_result_free(result: *mut NagaCompileResult) {
    if !result.is_null() {
        unsafe {
            drop(Box::from_raw(result));
        }
    }
}

#[no_mangle]
pub extern "C" fn naga_result_spirv(
    result: *const NagaCompileResult,
    out_word_count: *mut usize,
) -> *const u32 {
    if !out_word_count.is_null() {
        unsafe {
            *out_word_count = 0;
        }
    }
    let result = unsafe { result.as_ref() };
    let Some(result) = result else {
        return ptr::null();
    };
    if result.spirv.is_empty() {
        return ptr::null();
    }
    if !out_word_count.is_null() {
        unsafe {
            *out_word_count = result.spirv.len();
        }
    }
    result.spirv.as_ptr()
}

#[no_mangle]
pub extern "C" fn naga_result_entry_point_count(result: *const NagaCompileResult) -> usize {
    unsafe { result.as_ref() }.map_or(0, |r| r.entry_points.len())
}

#[no_mangle]
pub extern "C" fn naga_result_entry_point(
    result: *const NagaCompileResult,
    i: usize,
    out: *mut NagaEntryPointView,
) {
    if out.is_null() {
        return;
    }
    let view = unsafe { result.as_ref() }
        .and_then(|r| r.entry_points.get(i))
        .map(EntryPointOwned::view)
        .unwrap_or(NagaEntryPointView {
            name: ptr::null(),
            stage: ptr::null(),
            statically_used: ptr::null(),
            statically_used_count: 0,
        });
    unsafe {
        *out = view;
    }
}

#[no_mangle]
pub extern "C" fn naga_result_comparison_sampler_count(result: *const NagaCompileResult) -> usize {
    unsafe { result.as_ref() }.map_or(0, |r| r.comparison_sampler_views.len())
}

#[no_mangle]
pub extern "C" fn naga_result_comparison_sampler(
    result: *const NagaCompileResult,
    i: usize,
    out: *mut NagaBindingView,
) {
    if out.is_null() {
        return;
    }
    let view = unsafe { result.as_ref() }
        .and_then(|r| r.comparison_sampler_views.get(i))
        .copied()
        .unwrap_or(NagaBindingView {
            group: 0,
            binding: 0,
            name: ptr::null(),
            kind: ptr::null(),
            format: ptr::null(),
            access: ptr::null(),
        });
    unsafe {
        *out = view;
    }
}

#[no_mangle]
pub extern "C" fn naga_result_override_count(result: *const NagaCompileResult) -> usize {
    unsafe { result.as_ref() }.map_or(0, |r| r.overrides.len())
}

#[no_mangle]
pub extern "C" fn naga_result_override(
    result: *const NagaCompileResult,
    i: usize,
    out: *mut NagaOverrideView,
) {
    if out.is_null() {
        return;
    }
    let view = unsafe { result.as_ref() }
        .and_then(|r| r.overrides.get(i))
        .map(OverrideOwned::view)
        .unwrap_or(NagaOverrideView {
            identifier: ptr::null(),
            name: ptr::null(),
            type_name: ptr::null(),
            has_default: false,
            statically_used: false,
            statically_used_by: ptr::null(),
            statically_used_by_count: 0,
        });
    unsafe {
        *out = view;
    }
}

#[no_mangle]
pub extern "C" fn naga_result_message_count(result: *const NagaCompileResult) -> usize {
    unsafe { result.as_ref() }.map_or(0, |r| r.messages.len())
}

#[no_mangle]
pub extern "C" fn naga_result_message(
    result: *const NagaCompileResult,
    i: usize,
    out: *mut NagaMessageView,
) {
    if out.is_null() {
        return;
    }
    let view = unsafe { result.as_ref() }
        .and_then(|r| r.messages.get(i))
        .map(MessageOwned::view)
        .unwrap_or(NagaMessageView {
            r#type: ptr::null(),
            message: ptr::null(),
            line_num: 0,
            line_pos: 0,
            offset: 0,
            length: 0,
        });
    unsafe {
        *out = view;
    }
}

#[no_mangle]
pub extern "C" fn naga_bake_with_constants(
    source: *const c_char,
    source_len: usize,
    kvs: *const NagaConstantKv,
    kv_count: usize,
    entry_point: *const c_char,
    out_word_count: *mut usize,
) -> *const u32 {
    install_panic_suppression();
    if !out_word_count.is_null() {
        unsafe {
            *out_word_count = 0;
        }
    }
    let source = match source_from_raw(source, source_len) {
        Ok(source) => source,
        Err(_) => return ptr::null(),
    };
    let constants_slice = if kvs.is_null() || kv_count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(kvs, kv_count) }
    };
    let mut constants = HashMap::new();
    for kv in constants_slice {
        let Some(key) = (unsafe { cstr_to_string(kv.key) }) else {
            continue;
        };
        constants.insert(key, kv.value);
    }
    let entry_point = unsafe { cstr_to_string(entry_point) };
    let Some(words) = bake_wgsl_with_constants(source, constants, entry_point.as_deref()) else {
        return ptr::null();
    };
    if words.is_empty() {
        return ptr::null();
    }

    let byte_count = words.len() * std::mem::size_of::<u32>();
    let dst = unsafe { malloc(byte_count).cast::<u32>() };
    if dst.is_null() {
        return ptr::null();
    }
    unsafe {
        ptr::copy_nonoverlapping(words.as_ptr(), dst, words.len());
        if !out_word_count.is_null() {
            *out_word_count = words.len();
        }
    }
    dst
}

#[no_mangle]
pub extern "C" fn naga_bake_free(spirv: *const u32) {
    if !spirv.is_null() {
        unsafe {
            free(spirv.cast_mut().cast::<c_void>());
        }
    }
}

#[no_mangle]
pub extern "C" fn naga_spirv_to_hlsl(
    words: *const u32,
    word_count: usize,
    entry_point: *const c_char,
    stage: *const c_char,
    out_len: *mut usize,
) -> *const c_char {
    install_panic_suppression();
    if !out_len.is_null() {
        unsafe {
            *out_len = 0;
        }
    }
    if words.is_null() || word_count == 0 {
        return ptr::null();
    }

    let words_slice = unsafe { slice::from_raw_parts(words, word_count) };
    let entry_point = unsafe { cstr_to_string(entry_point) };
    let stage = unsafe { cstr_to_string(stage) };
    let Some(hlsl) = spirv_to_hlsl(words_slice, entry_point.as_deref(), stage.as_deref()) else {
        return ptr::null();
    };

    let bytes = hlsl.as_bytes();
    let dst = unsafe { malloc(bytes.len() + 1).cast::<c_char>() };
    if dst.is_null() {
        return ptr::null();
    }
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), dst.cast::<u8>(), bytes.len());
        *dst.add(bytes.len()) = 0;
        if !out_len.is_null() {
            *out_len = bytes.len();
        }
    }
    dst
}

#[no_mangle]
pub extern "C" fn naga_string_free(text: *const c_char) {
    if !text.is_null() {
        unsafe {
            free(text.cast_mut().cast::<c_void>());
        }
    }
}
