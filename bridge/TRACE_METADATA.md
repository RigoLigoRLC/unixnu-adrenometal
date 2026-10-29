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
