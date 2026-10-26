# Data A Live Metal runtime patches

These patches come from <https://github.com/ShueiTsuki/data-a-live>, revision
`a4173581595e63808a1c8f0742e7da838ecf27f6`, under `tools/metal/m2v/`.
Their original mail headers retain their author and source commit. The adjoining
`LICENSE` preserves the MIT license and copyright notice.

| Patch | Source commit | Change |
| --- | --- | --- |
| 0001 | `d748038a02629494c41b237f6f9c34488175c1ac` | Cache immutable final render passes by their attachment operations and shape. |
| 0002 | `438ab5f8556cd86fd17d8484e87c22c07d5b3846` | Store small push fields and viewport state inline; avoid per-draw temporary lists. |
| 0004 | `82475a44b8849b4cf9f2da9807ac5a50e69676d5` | Retain draw-command storage in the render encoder. |
| 0005 | `7d46073e8279fee5168a5b57d331c7aba4c9d7a6` | Reuse resource-use and sampler lists; store one tracker use inline. |
| 0007 | `4c8632fc16f4fcc9ed5d9ab93fc9bf56ff9bb44d` | Use the runtime's existing KeyMap for resource keys. |
| 0008 | `11e1050ecd79760499a2e15277af48fa42a749ef` | Omit the initial clear of a two-plane shadow image which is fully populated before its first read. |

The build script applies these after the native bridge's patches. Source patch
0003 duplicates the native padded-row alias policy. Source patch 0006 changes
the no-copy rebind contract and is not part of this sequence.
