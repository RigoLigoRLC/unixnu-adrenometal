# Metal metadata in the GPU capture

The runtime loads `vkNativeAdrenoTraceEnabled(VkDevice) -> VkBool32` and
`vkNativeAdrenoTraceMetal(VkDevice, VkCommandBuffer, uint32_t type,
uint32_t flags, const uint64_t *words, uint32_t count)` through the device
loader. The enabled callback returns the active capture generation, or zero when
metadata admission is closed. Turnip owns the bounded event buffer, capture
window and binary transport.
Each metadata call copies four words. The callback uses source 1, converts the
Vulkan handle to Turnip's recording ID in `command_buffer_id`, and puts its input flags in the high
16 bits of the event flags. Argument keys are `type * 16 + index`, with indices
0 through 3. CPU metadata timestamps use the same monotonic clock as the kernel.

| Type | Name | Word 0 | Word 1 | Word 2 | Word 3 | Input flags |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Metal encoder command buffer | Metal command buffer ID | Encoder ID | Metal queue ID | Encoder kind | 0 |
| 2 | Metal render pass | Encoder ID | Width, height | Layers, samples | Attachment count | 0 initial, 1 final |
| 3 | Metal render attachment | Encoder ID | Texture ID | Width, height | Format and actions | Attachment slot |
| 4 | Metal texture binding | Encoder ID | Texture ID | Width, height | Format, depth | Stage and slot |
| 5 | Metal draw pipeline | Encoder ID | Metal pipeline ID | Vulkan pipeline handle | Draw kind | Draw kind |
| 6 | Metal draw arguments | Encoder ID | Metal pipeline ID | Count | Instances | Draw kind |
| 7 | Metal encoder ended | Metal command buffer ID | Encoder ID | Encoder kind | 0 | 0 |

Each pair stores its first value in bits 0 through 31 and its second value in
bits 32 through 63. Attachment format occupies bits 0 through 31, load action
bits 32 through 39, and store action bits 40 through 47. These formats and actions
are Metal enum values. Attachment slot flags are color indices 0 through 7,
depth `0x100`, or stencil `0x200`; bit 15 marks the final attachment actions.
Texture binding flags contain stage 1 for vertex
or 2 for fragment in the low byte and the binding index shifted left by 8. A zero
texture ID records an unbound texture slot. Texture IDs are process-local runtime
object IDs and stay unique after an object is released.

Encoder kinds are 1 for render, 2 for compute, 3 for blit and 4 for other work.
Draw kinds are 1 for direct, 2 for indexed, 3 for indirect, 4 for indexed indirect,
5 for mesh, 6 for mesh indirect and 7 for mesh indirect count. Direct counts are
vertex or index counts, with the actual instance count. Indirect counts are the
number of indirect records, or the maximum for kind 7, and instances is zero.
Mesh kind 5 stores group counts X and Y as a pair in Count and Z in Instances.

The command buffer ID is allocated once when the Metal command buffer is made.
Encoder IDs come from the runtime's existing encoder allocation counter. The
encoder end records each primary Vulkan command buffer it produced. Actual draw
recording also records the primary or secondary Vulkan command buffer, including
draws deferred until a pipeline finishes compiling and draws recorded again
inside a split pass. Each draw records the runtime pipeline identity and the
actual Vulkan pipeline handle selected for that draw. The draw's existing Bound
object remembers which encoder and pipeline identities were emitted for its
Vulkan command buffer and capture generation. It emits an encoder mapping once
and a pipeline identity when either the Metal or Vulkan pipeline changes. Every
actual draw still emits its arguments.

A render encoder reports its current pass extent and attachment actions before
its first draw in each capture generation, and the final actions at its end.
The initial description remains available when capture stops inside the encoder.
Vertex and fragment texture setters record changed bindings after the runtime
accepts them. Pass and texture records may have a zero Vulkan command buffer
while the runtime holds the render pass separately; their encoder ID supplies the
relationship. Turnip supplies command buffer execution, submission and completion
relationships, and GPU timestamps for the actual driver work. Turnip allocates
each recording ID at `BeginCommandBuffer`; its secondary execution event links
the primary recording ID to the secondary recording ID. Captures can begin
within an encoder, so metadata preceding the capture window may be absent.

## Buffer to image copies

The runtime's Vulkan copy wrapper assigns a process-local operation ID immediately
before each actual buffer-to-image command. The driver attaches that ID to the
copy's existing GPU timestamps. These records are CPU metadata. A fixed thread
local context carries a shadow operation's cause through its synchronous helpers.
The context is restored when that operation returns.

| Type | Name | Word 0 | Word 1 | Word 2 | Word 3 |
| --- | --- | --- | --- | --- | --- |
| 8 | Copy operation | Operation ID | Source VkBuffer | Destination VkImage | Region count |
| 9 | Copy region | Operation ID | Buffer offset | Row length, image height | Width, height |
| 10 | Copy subresource | Operation ID | Aspect mask, mip level | Base layer, layer count | Depth, signed Z offset |
| 11 | Copy offset | Operation ID | Signed X, Y offsets | Row stride bytes | Origin, caller source line |
| 12 | Texture image | Texture object ID | VkImage | Backing VkBuffer | Refusal code |
| 13 | Image layout | VkImage | Metal row pitch | Vulkan row pitch | Required image bytes |
| 14 | In-place reason | VkImage | Byte offset, original byte length | First eight UTF-8 bytes | Next eight UTF-8 bytes |

Types 9 through 11 use the zero-based region index as their flags. At most 32
regions are described. Type 8 flags contain origin in bits 0 through 7, shadow
cause in bits 8 through 14, and bit 15 when regions were truncated. Origin is
1 for shadow synchronization, 2 for a Metal blit path, 3 for a host texture
upload, 4 for initial contents or a deferred host upload, and 5 for another
runtime copy. Type 11 records the static caller's source line in the high word.
A missing operation ID means the source was not associated.

Shadow causes are 0 when unrecorded; 1 for the encoder's first synchronization;
2 when a command needs texels outside the rectangle held by the shadow; 3 after
another shadow writes the backing; 4 after a merge of two writes to the backing;
5 after another command writes the backing; 6 after a rectangular write whose
path or layout prevents retaining the rest of the shadow; 7 after writes that
Metal discards leave the shadow different from its backing; and 8 when the copy
covers the rectangle bounding changed texels.

Type 12 flags currently identify storage kind 1, a shadow. Refusal codes are
0 for an unrecorded classification; 1 for the shadow policy; 2 for the render
target policy; 3 for the shader write policy; 4 for a pending import check;
5 for a failed check; 6 for an unperformed check; 7 for overlapping rows;
8 for detached bytes; 9 for an unsupported image; 10 for its pitch; 11 for
memory alignment, size or type; 12 for the dma-buf source; 13 for an unsupported
dma-buf image; and 14 for a deliberate device copy of logged host pages.
Type 14 preserves the allocation's actual explanation, including refusals whose
classification is unrecorded. Each chunk has 16 bytes in little-endian words;
at most 256 bytes are sent, and the original byte length exposes truncation.

The first shadow texture object supplies the identity shared by its copies and
views. Types 12 through 14 are emitted once per object and capture generation
when it is used, before the operation record. Zero Vulkan pitch or required size
means that value was not retained. Zero row stride in type 11 means the copy's
destination format must supply its texel size; the runtime computes it only for
a known uncompressed color shadow image. The producer retains its fixed capacity
and performs transport on its existing worker.
