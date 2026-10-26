# AdrenoMetal

Metal frontend through metal2vulkan and Turnip.

The frontend uses metal2vulkan and Turnip with the kernel GPU interface and selected OS ABI. GPU composition has been verified on sheng with A740.

The board selects its `NativeAdrenoABI.h` directory for both Turnip and the plugin.
Query version 1 uses 16 indirect buffers per submission; version 2 uses 128.
The diagnostic mailbox uses the 16-buffer record accepted by both versions.

The frontend caches device answers with arguments, primes shaders between animation
bursts, and disables encoder replay logs because it does not rebind no-copy buffers.
Padded render targets alias their backing rows only when 16x4 tile stores fit the row
pitch, the height is divisible by four, and the final row remains inside the buffer.
The host-import release acknowledgement retains the caller's pages until the kernel
has removed their GPU mapping.

`NATIVE_ADRENO_FRAME_TRACE` and `NATIVE_ADRENO_PIPELINE_JOURNAL` default to zero.
The periodic in-memory totals remain available; each error log prints at most 32
errors per process. Detailed trace and pipeline files require an explicit build
override. The optional `NB_SAMPLER` also needs frame tracing to export its samples.

Build and interface details: [documentation](https://unixnu.com/drivers/adreno-metal). Component sources and dependencies: [catalog](https://unixnu.com/reference/components).
