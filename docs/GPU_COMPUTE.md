# Optional Whisper compute acceleration (GPU-010)

The recognition **Compute device** setting defaults to **Automatic**. Automatic
prefers the first usable Vulkan GPU and retries model initialization on CPU if
Vulkan cannot initialize. **CPU** sets Whisper's `use_gpu=false`, preserving the
existing recognition baseline. **Vulkan** explicitly requests acceleration but
still retries CPU on failure. Both fallback modes expose the reason in the
recognition device status; no modal dialog is required. When multiple GPUs are
discovered, **GPU** selects Automatic or a device name. Device indices are GGML
GPU ordinals, stable within the same enumeration; they are not persistent physical
hardware identifiers across driver changes.

Only Whisper inference moves to the selected backend. Silero VAD always uses
`vadParameters.use_gpu=false`. Model, language, vocabulary grammar, compact
prompt, decoding, endpoint timing, command normalization and parsing are unchanged.

## Build and runtime

CPU-only builds remain the default and require no Vulkan development packages:

```sh
cmake -S . -B build -DTIMMY_BUILD_GUI=OFF -DTIMMY_ENABLE_VULKAN=OFF
cmake --build build
```

On the prepared Windows development machine, use the existing preset plus:

```sh
cmake --preset windows-release -DTIMMY_ENABLE_VULKAN=ON
cmake --build out/build/windows-release
```

Enabling Vulkan requires the SDK/development headers, loader import library,
`glslc` and SPIRV-Headers CMake package. CMake fails explicitly when these are
missing. Other GPU backends remain disabled. The SDK is a **build** requirement;
end users normally only need the graphics driver's Vulkan runtime for acceleration.
No Vulkan SDK development files or loader DLL are bundled by TIMMY.

The Vulkan build stages these libraries next to each executable, including tests:
`whisper`, `ggml`, `ggml-base`, `ggml-cpu` (required), and `ggml-vulkan` (optional).
On Windows these are DLLs. Distribute the staged runtime directory together with
the existing wxWidgets/compiler runtime dependencies. Missing `ggml-vulkan.dll`
or `vulkan-1.dll` causes optional module loading to fail and recognition to use
CPU; neither is an executable import dependency. Required CPU/shared libraries
must remain in the distribution. SDK headers, shader compilers and development
libraries are not runtime dependencies.

## Pinned dependency integration

Whisper.cpp stays at `927cfce34f31707e17f2bff35c349632fb9e2c3a` (v1.9.4),
including GGML 0.23.0. Its GGML registry supports `ggml_backend_load` and generic
device enumeration. However, Whisper directly references CPU symbols such as
`ggml_backend_cpu_buffer_type`; enabling `GGML_BACKEND_DL` globally creates a CPU
MODULE that Whisper cannot link to directly. Therefore TIMMY uses a hybrid:
normal shared Whisper/GGML with CPU linked/registered, then the same upstream
`ggml_add_backend_library` with `GGML_BACKEND_DL=ON` scoped only to Vulkan.
Vulkan is never linked into the executable or required shared libraries.

TIMMY loads only the module adjacent to the executable using `ggml_backend_load`,
not `ggml_backend_load_all` (which searches the working directory and other backend
types). Discovery exposes backend ID, GGML GPU ordinal, device ID, description
and GPU/integrated GPU class. NVIDIA, AMD and Intel use this same generic path.
An explicit CPU start does not load or initialize Vulkan. GUI device discovery
may already have loaded the optional module while populating the device names.

`cmake/WhisperTransactionalInit.cmake` applies a narrow, idempotent source overlay
to the hash-verified pin in the build directory. It makes failed GPU state
initialization return failure rather than silently creating a CPU state; partial
model/state/backend resources unwind before TIMMY retries CPU. This avoids
claiming Vulkan is active when Whisper actually substituted CPU. The overlay
does not change decoding parameters or model semantics. Loaded GGML modules and
their registry/device caches remain process-lived as upstream requires; model,
state and worker resources are released on stop/restart.

## Diagnostics and comparison

The recognition status shows the active device, e.g. `Recognition device:
NVIDIA GeForce RTX 3070 · Vulkan`, or a CPU fallback reason. Headless use supports
`asr listen --compute automatic|cpu|vulkan [--compute-device N]` and prints the
same status.

Benchmark JSONL adds `requested_compute_backend`, `requested_device`,
`active_compute_backend`, `active_device`, `compute_device_index`,
`compute_device_name`, `fallback_used`, `fallback_reason` and `compute_discovery`.
Unreported active diagnostics remain null, including injected backends. These
additive fields retain schema version 2 and the same corpus/outcome meanings;
old rows omit them, and readers must tolerate missing/unknown additive fields.
No-result outcomes also retain compute metadata. Existing inference/audio
durations, real-time factor, queue and dropped-work metrics remain intact.

Compare Base Q5_1 with CPU and Vulkan using identical recognition settings.
Deterministic selection/fallback and JSON tests require no GPU or Vulkan runtime.
Windows hardware validation must check the RTX 3070 name, actual Vulkan status,
explicit CPU, model reload/stop/restart, and startup/fallback with the optional
module removed and on a machine without a Vulkan loader. Do not claim performance
improvement without measured live results. Catastrophic driver/process failures
and upstream fatal assertions cannot be recovered by an in-process fallback;
normal loader, discovery, allocation and initialization failures are handled.
