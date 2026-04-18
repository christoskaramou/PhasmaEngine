# PhasmaWebGPU Code Review Report
**Date:** April 18, 2026
**Scope:** `PhasmaWebGPU/Code/` - Architectural and Implementation Review

## Executive Summary
The `PhasmaWebGPU` subproject is a functional WebGPU-on-Vulkan wrapper, but it contains several critical memory safety and synchronization issues. The most pressing concerns involve Use-After-Free (UAF) vulnerabilities in resource destruction and asynchronous callbacks.

---

## 1. Critical Issues

### 1.1 Immediate Resource Destruction (GPU Race Condition)
**Location:** `BindGroup.cpp`, `Texture.cpp` (Views), `ShaderModule.cpp`, etc.
**Description:** Unlike `Buffer` and `Texture` base objects, auxiliary resources like `BindGroup`, `TextureView`, and `ShaderModule` destroy their underlying Vulkan handles (descriptors, image views, modules) immediately when their reference count hits zero.
**Impact:** If a user submits a command buffer and immediately releases the bind groups used within it, the GPU will attempt to access destroyed descriptor sets, leading to device loss or driver crashes.
**Recommendation:** Implement a deferred deletion queue for all resource types that tracks the `lastUsageSerial` and only destroys handles after the GPU has finished the corresponding work.

### 1.2 Use-After-Free in Async Callbacks
**Location:** `Buffer.cpp` (`wgpuBufferMapAsync`), `Adapter.cpp` (`wgpuAdapterRequestDevice`), `Instance.cpp`.
**Description:** Async operations capture raw `this` pointers (e.g., `WGPUBufferImpl*`) in lambdas passed to the `FutureRegistry`. They do not increment the reference count of the captured object.
**Impact:** If the user releases the object before the callback fires, `ProcessEvents()` will execute the lambda with a dangling pointer.
**Recommendation:** Every lambda captured for an async event must call `AddRef` on its resources and `Release` them inside the lambda body.

---

## 2. High Priority Issues

### 2.1 Resource Leaks on Instance Shutdown
**Location:** `FutureRegistry.cpp` / `Instance.cpp`
**Description:** If a `WGPUInstance` is released while async operations (like `requestDevice`) are pending, the `TrackedFuture` lambdas are destroyed. Any resources newly allocated and captured by value (like a raw `new WGPUDeviceImpl`) are leaked because the lambda never runs to pass ownership to the user or delete the pointer.
**Recommendation:** Use `std::shared_ptr` or unique ownership transfer for objects created during async "request" calls.

### 2.2 Blocking CPU Thread on Release/Destroy
**Location:** `Buffer.cpp` (`wgpuBufferRelease`, `wgpuBufferDestroy`)
**Description:** The implementation calls `sem->WaitTimeout(lastUsage, UINT64_MAX)` to ensure the GPU is done before freeing the buffer.
**Impact:** This turns a `Release()` call into a synchronous, blocking operation that can cause frame hitches on the main thread.
**Recommendation:** Move the resource to a "Pending Deletion" state and let the existing `ReclaimCompleted...` logic handle the cleanup asynchronously.

---

## 3. Efficiency & Architecture

### 3.1 Duplicate Tracking in `RetainedResources`
**Location:** `CommandEncoder.cpp` (`RetainedResources::MergeFrom`)
**Description:** Resources are appended to `std::vector` without checking for duplicates. A single command buffer with hundreds of `setBindGroup` calls will bloat the retention vectors significantly.
**Impact:** Increased memory pressure and slower cleanup during command buffer destruction.
**Recommendation:** Use `std::set` or perform a `std::sort` + `std::unique` pass during `CommandEncoder.finish()`.

### 3.2 Combined Sampler Reflection
**Location:** `Reflect.cpp`
**Description:** SPIR-V reflection treats combined image-samplers as simple textures.
**Impact:** This may cause layout compatibility issues in Vulkan if the shader expects a combined descriptor but the WebGPU layout only provides a sampled image binding.

---

## 4. Incompleteness & Stubs

*   **WGSL Support:** Hardcoded to fail; only SPIR-V is supported.
*   **External Textures:** Validation stubs exist, but the implementation is missing.
*   **Error Handling:** Many validation failures log `PE_WARN` but do not correctly transition objects to an "Invalid" state, which may allow subsequent illegal operations to proceed.

---

## 5. Minor Bugs

*   **Sampler Leak:** `Device.cpp:GetOrCreateExternalTextureSampler` leaks a `pe::Sampler` if called without a valid `bindGroup`.
*   **WaitIdle() Overhead:** `wgpuDeviceRelease` calls `WaitIdle()` on the entire queue, which is far more heavy-handed than waiting for specific submission serials.
