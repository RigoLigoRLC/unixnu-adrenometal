# Static textures with argument-buffer samplers

The iPadOS 17 Apple3 path accepts a statically bound texture with a sampler loaded from an argument buffer. The selector retains the texture handle's candidate switch and absent-resource arms. Fragment samples calculate implicit gradients before that switch; LOD queries use the existing bounded candidate prelude. XR sampling receives the same hoisted gradients.

`Source::SlotHeapSampler` identifies the texture slot, sampler handle, comparison use, cube-gradient use, and raw-view choice. The compiler deduplicates sampler arrays by `SamplerUse`, sharing the descriptor class and sampler metadata with argument-buffer textures. The module requires runtime descriptor arrays, partially bound descriptors, nonuniform sampled-image indexing, and sampled-image update-after-bind support.

Each static sampled image has a separate `SampledTexture` binding. Its descriptor preserves the sampled texture's shape even when a read of the same cube uses a 2D array of faces. Raw sRGB sampling selects the UNORM view. The emitter loads the sampler array element from the handle table, combines it with the sampled image, and uses the static texture's conversion, swizzle, and offset metadata. Dynamic texel offsets retain Metal's low-bit interpretation.

XR runtime sampler slots select anisotropic tap loops only in the specialization variant whose bound sampler has anisotropy. Argument-buffer samplers retain their runtime metadata branch. Raw heap cubes use unsigned 2D-array views for face fetches.

The selector and declarations are recorded in patch 0013. Patch 0018 contains native image and sampler emission, raw heap cube declarations, separate sampled-image bindings, and inline filtering for static textures with argument-buffer samplers. Graphical bindings and cache encoding are recorded in patch 0015; sampler metadata and format admission are recorded in patch 0017. `scripts/build-adreno-metal-bridge.py` applies these patches before compiling the bridge.
