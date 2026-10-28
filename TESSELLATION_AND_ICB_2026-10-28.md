# Tessellation and indirect command execution

This change targets the iPadOS 17 Metal interfaces used by the A642L bridge.

## Implemented paths

- A post-tessellation function with `[[patch(kind)]]` receives the draw's control-point count through `PatchControlPointCount`. Its control and evaluation stages exchange space for 32 elements. The control stage calculates each patch's addresses with the draw's count and fills unused entries with its last valid control point. A declared count keeps the fixed-count path.
- Per-instance patch attributes use `base_instance + (instance_id - base_instance) / step_rate`. Direct patch draws supply the base instance through the existing draw push data.
- Fragment `primitive_id` uses Vulkan's patch primitive identifier behind tessellation. Apple describes this behavior in [Discover Metal enhancements for A14 Bionic](https://developer.apple.com/videos/play/tech-talks/10858/). Primitive ID belongs to Apple7 and is separate from the Apple3 tessellation requirement.
- An indirect indexed draw whose index offset reaches the buffer's end binds a nonempty initialized zero buffer. The existing robust index fetch or GPU bounds pass handles every requested index; Vulkan receives a valid buffer offset and range.
- An ICB execution whose GPU-written range depends on earlier draws ends the preceding render instance. The encoder submits that prefix, copies the execution range, and lets the next host step read the completed copy before recording the continuation. GPU-written ICB records use the same submission boundary. Per-pass storage and memoryless backing images remain retained across the parts; visibility queries continue through the existing borrowed-query records.
- Shadow copies for a pass containing ICB executions are planned at commit using the command buffer's complete residency. The first copies run before attachment preparation, later copies run between the appropriate render instances or tile dispatches, and final copies run after attachment stores, resolves, and visibility results. Resources declared after the execute call are included in both recording and the copy plan.
- A resolving memoryless color attachment receives its queue's retained backing before the first draw binds an input attachment or color-format view. This includes a deferred store action that may resolve. Memoryless sample-plane sources and depth/stencil resolve sources have their backing fixed before copy and resolve planning. Every captured ICB execution layout names those same images, so split instances, sample-plane copies, and XR shader resolves retain their samples across submissions.

## Capability boundaries

Apple3 includes direct tessellation, ordinary indirect draw arguments, and rendering ICBs. Indirect tessellation arguments and tessellation inside ICBs require Apple5; multiview amplification requires Apple6. The compiler continues to require actual tessellation-evaluation subgroup support for SIMD operations. This change does not advertise those later families.

Indirect draws that require CPU base-instance data because of a later-family operation, a multiview limit, or missing Vulkan `drawIndirectFirstInstance` retain their existing explicit refusal. Memoryless colors, XR formats, and combined MSAA store/resolve are included in the implemented Apple3 paths.

## Reproducible inputs

Runtime render changes are stored in `bridge/patches/0006-skip-draws-without-a-pipeline.patch` and `0011-preserve-metal-stages-and-host-visibility.patch`. Compiler changes, the deferred shadow helper, indirect argument copy helper, and zero index source are stored in `0015-tessellation-counts-and-icb-continuations.patch`. The build script registers that patch before `0016-pass-runtime-sampler-maximum-lod.patch`.

The compiler semantics, artifact format, and runtime versions are 1699, 400, and 1212. No tests, builds, device probes, file hashes, or commits were run by the implementing subagent; the coordinating agent owns the unified build.
