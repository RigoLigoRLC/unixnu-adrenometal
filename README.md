# AdrenoMetal

Metal frontend through metal2vulkan and Turnip.

`unixnu-sm-t733` contains submission, synchronization and memory optimizations
with the system SDR format policy observer. `unixnu-sm-t733-apple3` adds the
Apple3 frontend, compiler and format paths on the same optimization base.

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

Build and interface details: [documentation](https://unixnu.com/drivers/adreno-metal). Component sources and dependencies: [catalog](https://unixnu.com/reference/components).
