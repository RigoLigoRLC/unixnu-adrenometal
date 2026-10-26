# AdrenoMetal

Metal frontend through metal2vulkan and Turnip.

The frontend uses metal2vulkan and Turnip with the kernel GPU interface and selected OS ABI. GPU composition has been verified on sheng with A740.

The runtime keeps separate buffer and image memory pools for each Vulkan memory
type. Pool growth is serialized independently of existing allocations and
releases. An allocator that waited for growth searches the pool again before
requesting another block; optional reserve work skips a pool already growing.
The bridge applies this behavior from `bridge/patches/0010-coordinate-memory-pool-growth.patch`.

Build and interface details: [documentation](https://unixnu.com/drivers/adreno-metal). Component sources and dependencies: [catalog](https://unixnu.com/reference/components).
