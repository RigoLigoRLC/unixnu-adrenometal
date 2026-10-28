# AdrenoMetal

Metal frontend through metal2vulkan and Turnip.

`unixnu-sm-t733` contains submission, synchronization and memory optimizations
with the system SDR format policy observer. `unixnu-sm-t733-apple3` adds the
Apple3 frontend, compiler and format paths on the same optimization base.

[Tessellation and indirect command execution](TESSELLATION_AND_ICB_2026-10-28.md)
describes draw-supplied control-point counts, valid empty index ranges, and render
submissions that wait for GPU-written ICB ranges while retaining attachments and visibility queries.

The frontend uses metal2vulkan and Turnip with the kernel GPU interface and selected OS ABI. GPU composition has been verified on sheng with A740.

The runtime keeps separate buffer and image memory pools for each Vulkan memory
type. Pool growth is serialized independently of existing allocations and
releases. An allocator that waited for growth searches the pool again before
requesting another block; optional reserve work skips a pool already growing.
The bridge applies this behavior from `bridge/patches/0010-coordinate-memory-pool-growth.patch`.

The same patch selects cached coherent device-local memory for Private resources
and heaps on unified-memory GPUs. Shared storage, including WriteCombined hints,
also prefers cached coherent memory. Memoryless attachments prefer lazy memory,
then cached coherent memory when their backing is allocated eagerly.

The frontend queries Turnip's imported-memory visibility support once per device.
Hardware-coherent submissions retain resource owners without collecting IOSurface
cache-flush ranges. Backends that require frontend cache operations retain that
path.

Resource declarations retain the frontend owners and pass the resource handles,
usage and render stages to the runtime's existing residency tables. Render
barriers and fence stages reach the runtime through the C ABI. Ordinary draws
keep the render pass's attachment ordering; serial compute barriers add no
dependency beyond the dispatch ordering already recorded by the runtime.

The frontend accepts serial and concurrent compute encoders through both
`computeCommandEncoderWithDispatchType:` and `computeCommandEncoderWithDescriptor:`.
The encoder reports its selected dispatch type. Concurrent scope and resource
barriers use the runtime's compute dependencies between the affected dispatches.

The synchronization patch records whether each allocation has a CPU consumer in
its resource identity. Encoder summaries accumulate the actual access stages and
writes to that storage. Submission makes completed GPU writes visible to those
accesses and exports only the host-visible writes to the CPU. Resource
initialization and staged host transfers keep their complete export dependency.
Fixed counters are available through `native_bridge_metal_synchronization_stats`
and the existing telemetry report. Render pass splits are part of patch 0006;
the remaining synchronization changes are in patch 0011.

On A6xx, Turnip uses its texel-buffer-to-3D-image conversion for long texture
buffers. The shader reads the descriptor's length and texel offset from memory,
using the descriptor-set addresses uploaded with its constants. The conversion
covers fetches, storage reads and writes, atomics and length queries, and the
driver exposes the resulting element limit to metal2vulkan. The implementation
is recorded in `patches/mesa/turnip-a6xx-large-texel-buffers.patch` in the core.

The frontend extension connects argument encoders, indirect draw and dispatch,
tessellation draws, indirect command buffers, resolve attachments and sample
positions to the runtime. Command buffers retain the objects used by those
commands. Render pipeline descriptors carry their tessellation and indirect
command settings into compilation.

Patch 0013 samples public XR textures in 1D, 2D, arrays, 3D and cube shapes through
their raw integer texels. It follows metal2vulkan commits `f29872bd`, `b0b61ac1`
and `d01ed34d`. Public XR multisample resolves read integer sample codes through a
shader, copying the attachment to an input-capable image when required. XR blend
combinations outside the fast contraction model use the rounded software model
with the requested factors, operations, write mask and source outputs. The
runtime announces the corresponding precision notes for the blend, derivative
level selection, and BGRA alpha or sRGB resolves. Resolve routing and these
notifications share patch 0012 with depth/stencil resolve.

The sampler selects minification and magnification filters from the raw LOD,
places dynamic offsets through the existing address calculation, and applies
both LOD clamp endpoints before mip filtering. Cube taps fold across neighboring
faces. Anisotropic filtering uses at most 16 taps; 2D reuses the general sampler's
footprint calculation. Other mip and cube combinations filter decoded float
texels and record `ExtendedRangeSampling`. Sampler slots use specialization
constants; argument-buffer samplers read filter, clamp and anisotropy fields
from the metadata table. Heap textures use the same address and filter helpers
with the raw texel class selected by their resource handle.

Patch 0014 decodes PVRTC RGB/RGBA two-bit and four-bit textures on the GPU into
cached RGBA8 images. Each written mip level and layer is refreshed after CPU
uploads or GPU blits; sampling uses its linear or sRGB view. Decode strips keep
scratch within 32 MiB plus alignment and include the wrapped neighboring block
rows. The shader follows the MIT-licensed PowerVR Native_SDK decoder. The runtime
build compiles the checked-in GLSL with the host compiler selected by
`M2V_GLSL_COMPILER`; the core build selects its existing Mesa glslang executable.

Build and interface details: [documentation](https://unixnu.com/drivers/adreno-metal). Component sources and dependencies: [catalog](https://unixnu.com/reference/components).
