# AdrenoMetal

Metal frontend through metal2vulkan and Turnip.

`unixnu-sm-t733` contains submission, synchronization and memory optimizations
with the system SDR format policy observer. `unixnu-sm-t733-apple3` adds the
Apple3 frontend, compiler and format paths on the same optimization base.

The frontend uses metal2vulkan and Turnip with the kernel GPU interface and selected OS ABI. GPU composition has been verified on sheng with A740.

The SM-T733 bridge targets metal2vulkan development commit
`624bab3918de5b978e1101517abca2b4fa426565`. Its frontend incorporates the import,
heap sizing and command completion fixes from AdrenoMetal
`547312cf58156f7abe5f83a3376a14f5cb157884`. The ordered runtime patch list is
`bridge/patches/series`.

The runtime sends Metal command buffer and encoder identities, render attachment
actions, changed texture bindings and actual draw pipeline identities to Turnip's
bounded GPU capture buffer. Deferred draws retain their Metal identities until
their Vulkan commands are recorded. The driver owns capture control and export;
the fixed metadata layout is documented in [bridge/TRACE_METADATA.md](bridge/TRACE_METADATA.md).

The runtime keeps separate buffer and image memory pools for each Vulkan memory
type. Pool growth is serialized independently of existing allocations and
releases. An allocator that waited for growth searches the pool again before
requesting another block; optional reserve work skips a pool already growing.
The bridge's ordered runtime patches apply this behavior after the upstream
pool growth changes.

The memory patches select cached coherent device-local memory for Private resources
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
and the existing telemetry report.

Each no-copy buffer creation compares the current VM objects and offsets behind
its address range with the preceding import. A remapped range tells the runtime
to import the current pages. IOSurface imports remain reusable while their
surface has another local owner; command completion, subsequent imports and
memory pressure release the retained buffers. The import table stores the
surface object retained by that buffer.

Heap size queries use the runtime's device allocation layout. Texture descriptors
carry framebuffer-only, drawable and rotation fields into that layout and the
created texture. Pipeline creation uses the runtime's exact wait policy. Failed
command buffers report Metal error codes and release their runtime resources
when frontend completion is delivered.

Build and interface details: [documentation](https://unixnu.com/drivers/adreno-metal). Component sources and dependencies: [catalog](https://unixnu.com/reference/components).
