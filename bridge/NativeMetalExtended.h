/* SPDX-License-Identifier: MIT */
#ifndef NATIVE_METAL_EXTENDED_H
#define NATIVE_METAL_EXTENDED_H

/* NativeMetalObjects.h includes this file after its opaque object declarations. */
typedef struct NmbArgumentEncoder NmbArgumentEncoder;
typedef struct NmbIndirectCommandBuffer NmbIndirectCommandBuffer;
typedef struct NmbIndirectCommand NmbIndirectCommand;

typedef struct NmbArgumentDescriptor {
    uint64_t data_type, index, array_length, access, texture_type, constant_alignment;
} NmbArgumentDescriptor;

typedef struct NmbExtendedRenderPipelineDescriptor {
    uint64_t indirect, tessellation, max_factor, partition, factor_format;
    uint64_t factor_step, winding, control_point_index_type, factor_scale;
} NmbExtendedRenderPipelineDescriptor;

typedef struct NmbExtendedPassAttachment {
    NmbTexture *resolve_texture;
    uint64_t resolve_level, resolve_slice, resolve_depth_plane, store_options;
} NmbExtendedPassAttachment;

typedef struct NmbSamplePosition {
    float x, y;
} NmbSamplePosition;

/* The output contains count positions for a supported texture sample count. */
bool nmb_device_default_sample_positions(const NmbDevice *, NmbSamplePosition *, uint64_t count);

typedef struct NmbExtendedRenderPassDescriptor {
    NmbExtendedPassAttachment colors[8], depth, stencil;
    uint64_t depth_filter, stencil_filter, sample_count;
    NmbSamplePosition positions[16];
} NmbExtendedRenderPassDescriptor;

typedef struct NmbIndirectCommandBufferDescriptor {
    uint64_t command_types, inherit_pipeline, inherit_buffers;
    uint64_t vertex_buffers, fragment_buffers, kernel_buffers, kernel_memory, dynamic_stride;
} NmbIndirectCommandBufferDescriptor;

/* NmbPatchDraw carries both ordinary encoder and ICB patch arguments. The ICB
 * uses factors; an ordinary encoder uses its current tessellation factor buffer. */
typedef struct NmbPatchDraw {
    uint64_t control_points, start, count, instances, base_instance;
    NmbBuffer *patch_indices;
    uint64_t patch_offset;
    NmbBuffer *control_indices;
    uint64_t control_offset;
    NmbBuffer *indirect;
    uint64_t indirect_offset;
    NmbBuffer *factors;
    uint64_t factor_offset, factor_stride;
    bool indexed;
} NmbPatchDraw;

enum {
    NMB_ARGUMENT_BUFFER = 0, NMB_ARGUMENT_TEXTURE = 1, NMB_ARGUMENT_SAMPLER = 2,
    NMB_ARGUMENT_COMPUTE_PIPELINE = 3, NMB_ARGUMENT_RENDER_PIPELINE = 4,
    NMB_ARGUMENT_INDIRECT_BUFFER = 5
};

NmbArgumentEncoder *nmb_argument_encoder_new(NmbDevice *, const NmbArgumentDescriptor *, size_t);
NmbArgumentEncoder *nmb_function_argument_encoder(NmbFunction *, uint64_t);
NmbArgumentEncoder *nmb_argument_encoder_nested(NmbArgumentEncoder *, uint64_t);
void nmb_argument_encoder_release(NmbArgumentEncoder *);
uint64_t nmb_argument_encoder_length(const NmbArgumentEncoder *);
uint64_t nmb_argument_encoder_alignment(const NmbArgumentEncoder *);
bool nmb_argument_encoder_target(NmbArgumentEncoder *, NmbBuffer *, uint64_t, uint64_t);
bool nmb_argument_encoder_resources(NmbArgumentEncoder *, uint32_t, const void *const *, const uint64_t *, uint64_t, size_t);
void *nmb_argument_encoder_constant_data(NmbArgumentEncoder *, uint64_t);
bool nmb_function_patch_info(const NmbFunction *, uint64_t *, int64_t *);

NmbIndirectCommandBuffer *nmb_indirect_buffer_new(NmbDevice *, const NmbIndirectCommandBufferDescriptor *, uint64_t, uint64_t);
void nmb_indirect_buffer_release(NmbIndirectCommandBuffer *);
/* size, resource ID, allocation bytes, resource options, resource index */
bool nmb_indirect_buffer_info(const NmbIndirectCommandBuffer *, uint64_t out[5]);
uint64_t nmb_indirect_buffer_purgeable(NmbIndirectCommandBuffer *, uint64_t);
bool nmb_indirect_buffer_reset(NmbIndirectCommandBuffer *, uint64_t, uint64_t);
NmbIndirectCommand *nmb_indirect_command_new(NmbIndirectCommandBuffer *, uint64_t, bool);
void nmb_indirect_command_release(NmbIndirectCommand *);
bool nmb_indirect_command_pipeline(NmbIndirectCommand *, const void *);
/* stage: 0 vertex, 1 fragment, 2 kernel */
bool nmb_indirect_command_buffer(NmbIndirectCommand *, uint32_t, NmbBuffer *, uint64_t, uint64_t);
bool nmb_indirect_command_buffer_stride(NmbIndirectCommand *, uint32_t, NmbBuffer *, uint64_t, uint64_t, uint64_t);
/* action: 0 reset, 1 setBarrier, 2 clearBarrier */
bool nmb_indirect_command_control(NmbIndirectCommand *, uint32_t);
bool nmb_indirect_command_dispatch(NmbIndirectCommand *, const uint64_t grid[3], const uint64_t group[3], bool);
bool nmb_indirect_command_memory(NmbIndirectCommand *, uint64_t, uint64_t);
bool nmb_indirect_command_region(NmbIndirectCommand *, const uint64_t origin[3], const uint64_t size[3]);
bool nmb_indirect_command_draw(NmbIndirectCommand *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
bool nmb_indirect_command_draw_indexed(NmbIndirectCommand *, uint64_t, uint64_t, uint64_t, NmbBuffer *, uint64_t, uint64_t, int64_t, uint64_t);
bool nmb_indirect_command_patches(NmbIndirectCommand *, const NmbPatchDraw *);

bool nmb_compute_dispatch_indirect(NmbEncoder *, NmbBuffer *, uint64_t, const uint64_t group[3]);
bool nmb_render_draw_indirect(NmbEncoder *, uint64_t, NmbBuffer *, uint64_t);
bool nmb_render_draw_indexed_indirect(NmbEncoder *, uint64_t, uint64_t, NmbBuffer *, uint64_t, NmbBuffer *, uint64_t);
bool nmb_render_tessellation_buffer(NmbEncoder *, NmbBuffer *, uint64_t, uint64_t);
bool nmb_render_tessellation_scale(NmbEncoder *, float);
bool nmb_render_patches(NmbEncoder *, const NmbPatchDraw *);
bool nmb_encoder_execute_indirect(NmbEncoder *, NmbIndirectCommandBuffer *, uint64_t, uint64_t, NmbBuffer *, uint64_t);
bool nmb_blit_indirect_copy(NmbEncoder *, NmbIndirectCommandBuffer *, uint64_t, uint64_t, NmbIndirectCommandBuffer *, uint64_t);
bool nmb_blit_indirect_reset(NmbEncoder *, NmbIndirectCommandBuffer *, uint64_t, uint64_t, bool);
/* attachment: 0 color, 1 depth, 2 stencil */
bool nmb_render_store_options(NmbEncoder *, uint32_t, uint64_t, uint64_t);

#endif
