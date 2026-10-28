/* SPDX-License-Identifier: MIT */

// This file shares the frontend's translation unit and object wrappers. The
// Rust extension owns validation and command recording in metal2vulkan.
static void nbExtendedRequire(bool success, const char *operation) {
    if (!success) {
        NSString *reason = [NSString stringWithFormat:@"%s: %@", operation, lastError()];
        noteProblem(reason);
        [NSException raise:NSInvalidArgumentException format:@"%@", reason];
    }
}

static void nbExtendedClass(id object, Class expected, bool nullable) {
    if ((!object && !nullable) || (object && ![object isKindOfClass:expected])) {
        [NSException raise:NSInvalidArgumentException format:@"Expected %@ from this Metal device", NSStringFromClass(expected)];
    }
}

static bool nbExtendedRetain(NBEncoder *encoder, id object, Class expected, bool nullable) {
    if ((!object && !nullable) || (object && ![object isKindOfClass:expected])) {
        [encoder.commandBuffer refuse:[NSString stringWithFormat:@"Expected %@ from this Metal device", NSStringFromClass(expected)]];
        return false;
    }
    if (object) {
        [encoder.commandBuffer.resourceOwners addObject:object];
    }
    return true;
}

@interface NBIndirectCommandBuffer : NBObject
@property(nonatomic) NmbIndirectCommandBuffer *handle;
// Each command keeps the objects named by its current bindings and draw.
@property(nonatomic,strong) NSMutableDictionary<NSNumber *, NSMutableDictionary<NSString *, id> *> *commandOwners;
- (void)keep:(id)object command:(NSUInteger)index key:(NSString *)key;
- (void)forget:(NSRange)range;
@end

@interface NBIndirectCommand : NBObject
@property(nonatomic) NmbIndirectCommand *handle;
@property(nonatomic,strong) NBIndirectCommandBuffer *parent;
@property(nonatomic) NSUInteger index;
@end

@interface NBIndirectComputeCommand : NBIndirectCommand
@end
@interface NBIndirectRenderCommand : NBIndirectCommand
@end

@implementation NBIndirectCommandBuffer
- (instancetype)init {
    self = [super init];
    if (self) {
        _commandOwners = [NSMutableDictionary new];
    }
    return self;
}
- (void)dealloc {
    nmb_indirect_buffer_release(_handle);
}
- (uint64_t)info:(NSUInteger)index {
    uint64_t info[5] = {};
    nbExtendedRequire(nmb_indirect_buffer_info(_handle, info), "indirect buffer properties");
    return info[index];
}
- (NSUInteger)size {
    return [self info:0];
}
- (MTLResourceID)gpuResourceID {
    return MTLResourceID{[self info:1]};
}
- (NSUInteger)allocatedSize {
    return [self info:2];
}
- (MTLResourceOptions)resourceOptions {
    return (MTLResourceOptions)[self info:3];
}
- (MTLStorageMode)storageMode {
    return (MTLStorageMode)(([self info:3] & MTLResourceStorageModeMask) >> MTLResourceStorageModeShift);
}
- (MTLCPUCacheMode)cpuCacheMode {
    return (MTLCPUCacheMode)(([self info:3] & MTLResourceCPUCacheModeMask) >> MTLResourceCPUCacheModeShift);
}
- (MTLHazardTrackingMode)hazardTrackingMode {
    return (MTLHazardTrackingMode)(([self info:3] & MTLResourceHazardTrackingModeMask) >> MTLResourceHazardTrackingModeShift);
}
- (NSUInteger)resourceIndex {
    return [self info:4];
}
- (id<MTLHeap>)heap {
    return nil;
}
- (NSUInteger)heapOffset {
    return 0;
}
- (BOOL)isAliasable {
    return NO;
}
- (MTLPurgeableState)setPurgeableState:(MTLPurgeableState)state {
    uint64_t result = nmb_indirect_buffer_purgeable(_handle, state);
    nbExtendedRequire(result != 0, "indirect buffer purgeable state");
    if (state == MTLPurgeableStateEmpty) {
        @synchronized(self) {
            [_commandOwners removeAllObjects];
        }
    }
    return (MTLPurgeableState)result;
}
- (void)keep:(id)object command:(NSUInteger)index key:(NSString *)key {
    @synchronized(self) {
        NSMutableDictionary *owners = _commandOwners[@(index)];
        if (!owners && object) {
            owners = [NSMutableDictionary new];
            _commandOwners[@(index)] = owners;
        }
        if (object) {
            owners[key] = object;
        } else {
            [owners removeObjectForKey:key];
        }
    }
}
- (void)forget:(NSRange)range {
    @synchronized(self) {
        for (NSNumber *index in [_commandOwners allKeys]) {
            if (index.unsignedLongLongValue >= range.location && index.unsignedLongLongValue - range.location < range.length) {
                [_commandOwners removeObjectForKey:index];
            }
        }
    }
}
- (void)resetWithRange:(NSRange)range {
    nbExtendedRequire(nmb_indirect_buffer_reset(_handle, range.location, range.length), "reset indirect command buffer");
    [self forget:range];
}
- (id<MTLIndirectComputeCommand>)indirectComputeCommandAtIndex:(NSUInteger)index {
    NmbIndirectCommand *handle = nmb_indirect_command_new(_handle, index, true);
    nbExtendedRequire(handle != nullptr, "indirect compute command");
    NBIndirectComputeCommand *command = [NBIndirectComputeCommand new];
    command.handle = handle;
    command.parent = self;
    command.index = index;
    return (id<MTLIndirectComputeCommand>)command;
}
- (id<MTLIndirectRenderCommand>)indirectRenderCommandAtIndex:(NSUInteger)index {
    NmbIndirectCommand *handle = nmb_indirect_command_new(_handle, index, false);
    nbExtendedRequire(handle != nullptr, "indirect render command");
    NBIndirectRenderCommand *command = [NBIndirectRenderCommand new];
    command.handle = handle;
    command.parent = self;
    command.index = index;
    return (id<MTLIndirectRenderCommand>)command;
}
@end

static NmbIndirectCommandBuffer *nbExtendedIndirectHandle(id resource) {
    if ([resource isKindOfClass:[NBIndirectCommandBuffer class]]) {
        return [(NBIndirectCommandBuffer *)resource handle];
    }
    return nullptr;
}

@implementation NBIndirectCommand
- (void)dealloc {
    nmb_indirect_command_release(_handle);
}
- (void)reset {
    nbExtendedRequire(nmb_indirect_command_control(_handle, 0), "reset indirect command");
    [_parent forget:NSMakeRange(_index, 1)];
}
- (void)setBarrier {
    nbExtendedRequire(nmb_indirect_command_control(_handle, 1), "indirect command barrier");
}
- (void)clearBarrier {
    nbExtendedRequire(nmb_indirect_command_control(_handle, 2), "clear indirect command barrier");
}
- (void)buffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset index:(NSUInteger)index stage:(uint32_t)stage {
    nbExtendedClass(buffer, [NBBuffer class], true);
    nbExtendedRequire(nmb_indirect_command_buffer(_handle, stage, [(NBBuffer *)buffer handle], offset, index), "indirect command buffer binding");
    [_parent keep:buffer command:_index key:[NSString stringWithFormat:@"buffer-%u-%lu", stage, (unsigned long)index]];
}
@end

@implementation NBIndirectComputeCommand
- (void)setComputePipelineState:(id<MTLComputePipelineState>)pipeline {
    nbExtendedClass(pipeline, [NBComputePipeline class], false);
    nbExtendedRequire(nmb_indirect_command_pipeline(self.handle, [(NBComputePipeline *)pipeline handle]), "indirect compute pipeline");
    [self.parent keep:pipeline command:self.index key:@"pipeline"];
}
- (void)setKernelBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index {
    [self buffer:buffer offset:offset index:index stage:2];
}
- (void)setKernelBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset attributeStride:(NSUInteger)stride atIndex:(NSUInteger)index {
    nbExtendedClass(buffer, [NBBuffer class], false);
    nbExtendedRequire(nmb_indirect_command_buffer_stride(self.handle, 2, [(NBBuffer *)buffer handle], offset, stride, index), "indirect kernel buffer stride");
    [self.parent keep:buffer command:self.index key:[NSString stringWithFormat:@"buffer-2-%lu", (unsigned long)index]];
}
- (void)concurrentDispatchThreadgroups:(MTLSize)grid threadsPerThreadgroup:(MTLSize)group {
    uint64_t g[] = {grid.width, grid.height, grid.depth};
    uint64_t t[] = {group.width, group.height, group.depth};
    nbExtendedRequire(nmb_indirect_command_dispatch(self.handle, g, t, false), "indirect threadgroup dispatch");
}
- (void)concurrentDispatchThreads:(MTLSize)grid threadsPerThreadgroup:(MTLSize)group {
    uint64_t g[] = {grid.width, grid.height, grid.depth};
    uint64_t t[] = {group.width, group.height, group.depth};
    nbExtendedRequire(nmb_indirect_command_dispatch(self.handle, g, t, true), "indirect thread dispatch");
}
- (void)setThreadgroupMemoryLength:(NSUInteger)length atIndex:(NSUInteger)index {
    nbExtendedRequire(nmb_indirect_command_memory(self.handle, length, index), "indirect threadgroup memory");
}
- (void)setStageInRegion:(MTLRegion)region {
    uint64_t origin[] = {region.origin.x, region.origin.y, region.origin.z};
    uint64_t size[] = {region.size.width, region.size.height, region.size.depth};
    nbExtendedRequire(nmb_indirect_command_region(self.handle, origin, size), "indirect stage region");
}
@end

@implementation NBIndirectRenderCommand
- (void)setRenderPipelineState:(id<MTLRenderPipelineState>)pipeline {
    nbExtendedClass(pipeline, [NBRenderPipeline class], false);
    nbExtendedRequire(nmb_indirect_command_pipeline(self.handle, [(NBRenderPipeline *)pipeline handle]), "indirect render pipeline");
    [self.parent keep:pipeline command:self.index key:@"pipeline"];
}
- (void)setVertexBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index {
    [self buffer:buffer offset:offset index:index stage:0];
}
- (void)setVertexBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset attributeStride:(NSUInteger)stride atIndex:(NSUInteger)index {
    nbExtendedClass(buffer, [NBBuffer class], false);
    nbExtendedRequire(nmb_indirect_command_buffer_stride(self.handle, 0, [(NBBuffer *)buffer handle], offset, stride, index), "indirect vertex buffer stride");
    [self.parent keep:buffer command:self.index key:[NSString stringWithFormat:@"buffer-0-%lu", (unsigned long)index]];
}
- (void)setFragmentBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index {
    [self buffer:buffer offset:offset index:index stage:1];
}
- (void)drawPrimitives:(MTLPrimitiveType)primitive vertexStart:(NSUInteger)start vertexCount:(NSUInteger)count instanceCount:(NSUInteger)instances baseInstance:(NSUInteger)base {
    nbExtendedRequire(nmb_indirect_command_draw(self.handle, primitive, start, count, instances, base), "indirect primitive draw");
}
- (void)drawIndexedPrimitives:(MTLPrimitiveType)primitive indexCount:(NSUInteger)count indexType:(MTLIndexType)type indexBuffer:(id<MTLBuffer>)buffer indexBufferOffset:(NSUInteger)offset instanceCount:(NSUInteger)instances baseVertex:(NSInteger)vertex baseInstance:(NSUInteger)base {
    nbExtendedClass(buffer, [NBBuffer class], false);
    nbExtendedRequire(nmb_indirect_command_draw_indexed(self.handle, primitive, count, type, [(NBBuffer *)buffer handle], offset, instances, vertex, base), "indirect indexed draw");
    [self.parent keep:buffer command:self.index key:@"indices"];
}
- (void)patches:(NmbPatchDraw *)draw patchBuffer:(id<MTLBuffer>)patch controlBuffer:(id<MTLBuffer>)control factors:(id<MTLBuffer>)factors {
    nbExtendedClass(patch, [NBBuffer class], true);
    nbExtendedClass(control, [NBBuffer class], true);
    nbExtendedClass(factors, [NBBuffer class], false);
    draw->patch_indices = [(NBBuffer *)patch handle];
    draw->control_indices = [(NBBuffer *)control handle];
    draw->factors = [(NBBuffer *)factors handle];
    nbExtendedRequire(nmb_indirect_command_patches(self.handle, draw), "indirect patch draw");
    [self.parent keep:patch command:self.index key:@"patch-indices"];
    [self.parent keep:control command:self.index key:@"control-indices"];
    [self.parent keep:factors command:self.index key:@"factors"];
}
- (void)drawPatches:(NSUInteger)points patchStart:(NSUInteger)start patchCount:(NSUInteger)count patchIndexBuffer:(id<MTLBuffer>)patch patchIndexBufferOffset:(NSUInteger)patchOffset instanceCount:(NSUInteger)instances baseInstance:(NSUInteger)base tessellationFactorBuffer:(id<MTLBuffer>)factors tessellationFactorBufferOffset:(NSUInteger)factorOffset tessellationFactorBufferInstanceStride:(NSUInteger)stride {
    NmbPatchDraw draw = {};
    draw.control_points = points; draw.start = start; draw.count = count;
    draw.instances = instances; draw.base_instance = base;
    draw.patch_offset = patchOffset; draw.factor_offset = factorOffset; draw.factor_stride = stride;
    [self patches:&draw patchBuffer:patch controlBuffer:nil factors:factors];
}
- (void)drawIndexedPatches:(NSUInteger)points patchStart:(NSUInteger)start patchCount:(NSUInteger)count patchIndexBuffer:(id<MTLBuffer>)patch patchIndexBufferOffset:(NSUInteger)patchOffset controlPointIndexBuffer:(id<MTLBuffer>)control controlPointIndexBufferOffset:(NSUInteger)controlOffset instanceCount:(NSUInteger)instances baseInstance:(NSUInteger)base tessellationFactorBuffer:(id<MTLBuffer>)factors tessellationFactorBufferOffset:(NSUInteger)factorOffset tessellationFactorBufferInstanceStride:(NSUInteger)stride {
    NmbPatchDraw draw = {};
    draw.control_points = points; draw.start = start; draw.count = count;
    draw.instances = instances; draw.base_instance = base; draw.indexed = true;
    draw.patch_offset = patchOffset; draw.control_offset = controlOffset;
    draw.factor_offset = factorOffset; draw.factor_stride = stride;
    [self patches:&draw patchBuffer:patch controlBuffer:control factors:factors];
}
@end

@interface NBArgumentEncoder : NBObject
@property(nonatomic) NmbArgumentEncoder *handle;
@property(nonatomic,strong) id<MTLBuffer> target;
@end

static id<MTLArgumentEncoder> nbArgumentEncoder(NmbArgumentEncoder *handle) {
    if (!handle) {
        noteProblem([NSString stringWithFormat:@"argument encoder: %@", lastError()]);
        return nil;
    }
    NBArgumentEncoder *encoder = [NBArgumentEncoder new];
    encoder.handle = handle;
    return (id<MTLArgumentEncoder>)encoder;
}

static const void *nbArgumentHandle(id object, uint32_t kind) {
    if (!object) {
        return nullptr;
    }
    switch (kind) {
        case NMB_ARGUMENT_BUFFER:
            nbExtendedClass(object, [NBBuffer class], false);
            return [(NBBuffer *)object handle];
        case NMB_ARGUMENT_TEXTURE:
            nbExtendedClass(object, [NBTexture class], false);
            return [(NBTexture *)object handle];
        case NMB_ARGUMENT_SAMPLER:
            nbExtendedClass(object, [NBSampler class], false);
            return [(NBSampler *)object handle];
        case NMB_ARGUMENT_COMPUTE_PIPELINE:
            nbExtendedClass(object, [NBComputePipeline class], false);
            return [(NBComputePipeline *)object handle];
        case NMB_ARGUMENT_RENDER_PIPELINE:
            nbExtendedClass(object, [NBRenderPipeline class], false);
            return [(NBRenderPipeline *)object handle];
        case NMB_ARGUMENT_INDIRECT_BUFFER:
            nbExtendedClass(object, [NBIndirectCommandBuffer class], false);
            return [(NBIndirectCommandBuffer *)object handle];
        default:
            [NSException raise:NSInvalidArgumentException format:@"Unknown argument resource kind"];
            return nullptr;
    }
}

@implementation NBArgumentEncoder
- (void)dealloc {
    nmb_argument_encoder_release(_handle);
}
- (NSUInteger)encodedLength {
    return nmb_argument_encoder_length(_handle);
}
- (NSUInteger)alignment {
    return nmb_argument_encoder_alignment(_handle);
}
- (void)setArgumentBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset {
    [self setArgumentBuffer:buffer startOffset:offset arrayElement:0];
}
- (void)setArgumentBuffer:(id<MTLBuffer>)buffer startOffset:(NSUInteger)offset arrayElement:(NSUInteger)element {
    nbExtendedClass(buffer, [NBBuffer class], true);
    nbExtendedRequire(nmb_argument_encoder_target(_handle, [(NBBuffer *)buffer handle], offset, element), "argument buffer target");
    _target = buffer;
}
- (void)resources:(const id __unsafe_unretained *)resources offsets:(const NSUInteger *)offsets range:(NSRange)range kind:(uint32_t)kind {
    if (range.length && (!resources || (kind == NMB_ARGUMENT_BUFFER && !offsets))) {
        [NSException raise:NSInvalidArgumentException format:@"Null argument resource array"];
    }
    std::vector<const void *> handles;
    std::vector<uint64_t> positions;
    handles.reserve(range.length);
    if (offsets) {
        positions.reserve(range.length);
    }
    for (NSUInteger i = 0; i < range.length; ++i) {
        handles.push_back(nbArgumentHandle(resources[i], kind));
        if (offsets) {
            positions.push_back(offsets[i]);
        }
    }
    nbExtendedRequire(nmb_argument_encoder_resources(_handle, kind, handles.data(), positions.data(), range.location, range.length), "argument resources");
}
- (void)setBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index {
    const void *handle = nbArgumentHandle(buffer, NMB_ARGUMENT_BUFFER);
    uint64_t position = offset;
    nbExtendedRequire(nmb_argument_encoder_resources(_handle, NMB_ARGUMENT_BUFFER, &handle, &position, index, 1), "argument buffer");
}
- (void)setBuffers:(const id<MTLBuffer> __unsafe_unretained *)buffers offsets:(const NSUInteger *)offsets withRange:(NSRange)range {
    [self resources:(const id __unsafe_unretained *)buffers offsets:offsets range:range kind:NMB_ARGUMENT_BUFFER];
}
- (void)setTexture:(id<MTLTexture>)texture atIndex:(NSUInteger)index {
    const void *handle = nbArgumentHandle(texture, NMB_ARGUMENT_TEXTURE);
    nbExtendedRequire(nmb_argument_encoder_resources(_handle, NMB_ARGUMENT_TEXTURE, &handle, nullptr, index, 1), "argument texture");
}
- (void)setTextures:(const id<MTLTexture> __unsafe_unretained *)textures withRange:(NSRange)range {
    [self resources:(const id __unsafe_unretained *)textures offsets:nullptr range:range kind:NMB_ARGUMENT_TEXTURE];
}
- (void)setSamplerState:(id<MTLSamplerState>)sampler atIndex:(NSUInteger)index {
    const void *handle = nbArgumentHandle(sampler, NMB_ARGUMENT_SAMPLER);
    nbExtendedRequire(nmb_argument_encoder_resources(_handle, NMB_ARGUMENT_SAMPLER, &handle, nullptr, index, 1), "argument sampler");
}
- (void)setSamplerStates:(const id<MTLSamplerState> __unsafe_unretained *)samplers withRange:(NSRange)range {
    [self resources:(const id __unsafe_unretained *)samplers offsets:nullptr range:range kind:NMB_ARGUMENT_SAMPLER];
}
- (void)setComputePipelineState:(id<MTLComputePipelineState>)pipeline atIndex:(NSUInteger)index {
    const void *handle = nbArgumentHandle(pipeline, NMB_ARGUMENT_COMPUTE_PIPELINE);
    nbExtendedRequire(nmb_argument_encoder_resources(_handle, NMB_ARGUMENT_COMPUTE_PIPELINE, &handle, nullptr, index, 1), "argument compute pipeline");
}
- (void)setComputePipelineStates:(const id<MTLComputePipelineState> __unsafe_unretained *)pipelines withRange:(NSRange)range {
    [self resources:(const id __unsafe_unretained *)pipelines offsets:nullptr range:range kind:NMB_ARGUMENT_COMPUTE_PIPELINE];
}
- (void)setRenderPipelineState:(id<MTLRenderPipelineState>)pipeline atIndex:(NSUInteger)index {
    const void *handle = nbArgumentHandle(pipeline, NMB_ARGUMENT_RENDER_PIPELINE);
    nbExtendedRequire(nmb_argument_encoder_resources(_handle, NMB_ARGUMENT_RENDER_PIPELINE, &handle, nullptr, index, 1), "argument render pipeline");
}
- (void)setRenderPipelineStates:(const id<MTLRenderPipelineState> __unsafe_unretained *)pipelines withRange:(NSRange)range {
    [self resources:(const id __unsafe_unretained *)pipelines offsets:nullptr range:range kind:NMB_ARGUMENT_RENDER_PIPELINE];
}
- (void)setIndirectCommandBuffer:(id<MTLIndirectCommandBuffer>)buffer atIndex:(NSUInteger)index {
    const void *handle = nbArgumentHandle(buffer, NMB_ARGUMENT_INDIRECT_BUFFER);
    nbExtendedRequire(nmb_argument_encoder_resources(_handle, NMB_ARGUMENT_INDIRECT_BUFFER, &handle, nullptr, index, 1), "argument indirect command buffer");
}
- (void)setIndirectCommandBuffers:(const id<MTLIndirectCommandBuffer> __unsafe_unretained *)buffers withRange:(NSRange)range {
    [self resources:(const id __unsafe_unretained *)buffers offsets:nullptr range:range kind:NMB_ARGUMENT_INDIRECT_BUFFER];
}
- (void *)constantDataAtIndex:(NSUInteger)index {
    void *data = nmb_argument_encoder_constant_data(_handle, index);
    nbExtendedRequire(data != nullptr, "argument constant data");
    return data;
}
- (id<MTLArgumentEncoder>)newArgumentEncoderForBufferAtIndex:(NSUInteger)index {
    return nbArgumentEncoder(nmb_argument_encoder_nested(_handle, index));
}
@end

@implementation NBFunction (NBExtendedFunction)
- (id<MTLArgumentEncoder>)newArgumentEncoderWithBufferIndex:(NSUInteger)index {
    return nbArgumentEncoder(nmb_function_argument_encoder(self.handle, index));
}
- (MTLPatchType)patchType {
    uint64_t kind = 0;
    int64_t count = -1;
    nbExtendedRequire(nmb_function_patch_info(self.handle, &kind, &count), "function patch type");
    return (MTLPatchType)kind;
}
- (NSInteger)patchControlPointCount {
    uint64_t kind = 0;
    int64_t count = -1;
    nbExtendedRequire(nmb_function_patch_info(self.handle, &kind, &count), "function patch control points");
    return count;
}
@end

@implementation NBDeviceMethods (NBExtendedDevice)
- (id<MTLArgumentEncoder>)newArgumentEncoderWithArguments:(NSArray<MTLArgumentDescriptor *> *)arguments {
    std::vector<NmbArgumentDescriptor> descriptors;
    descriptors.reserve(arguments.count);
    for (MTLArgumentDescriptor *argument in arguments) {
        descriptors.push_back({(uint64_t)argument.dataType, argument.index, argument.arrayLength,
            (uint64_t)argument.access, (uint64_t)argument.textureType, argument.constantBlockAlignment});
    }
    return nbArgumentEncoder(nmb_argument_encoder_new(gDevice, descriptors.data(), descriptors.size()));
}
- (id<MTLIndirectCommandBuffer>)newIndirectCommandBufferWithDescriptor:(MTLIndirectCommandBufferDescriptor *)descriptor maxCommandCount:(NSUInteger)count options:(MTLResourceOptions)options {
    NmbIndirectCommandBufferDescriptor d = {(uint64_t)descriptor.commandTypes, (uint64_t)descriptor.inheritPipelineState,
        (uint64_t)descriptor.inheritBuffers, descriptor.maxVertexBufferBindCount, descriptor.maxFragmentBufferBindCount,
        descriptor.maxKernelBufferBindCount, descriptor.maxKernelThreadgroupMemoryBindCount, (uint64_t)descriptor.supportDynamicAttributeStride};
    NmbIndirectCommandBuffer *handle = nmb_indirect_buffer_new(gDevice, &d, count, options);
    if (!handle) {
        noteProblem([NSString stringWithFormat:@"indirect command buffer: %@", lastError()]);
        return nil;
    }
    NBIndirectCommandBuffer *buffer = [NBIndirectCommandBuffer new];
    buffer.handle = handle;
    return (id<MTLIndirectCommandBuffer>)buffer;
}
@end

static bool nbExtendedRenderPipeline(MTLRenderPipelineDescriptor *descriptor, NmbExtendedRenderPipelineDescriptor *extended, NSError **error) {
    uint64_t patchType = 0;
    int64_t points = -1;
    if (!nmb_function_patch_info([(NBFunction *)descriptor.vertexFunction handle], &patchType, &points)) {
        if (error) {
            *error = nbError(lastError());
        }
        return false;
    }
    extended->indirect = descriptor.supportIndirectCommandBuffers;
    extended->tessellation = patchType != MTLPatchTypeNone;
    extended->max_factor = descriptor.maxTessellationFactor;
    extended->partition = descriptor.tessellationPartitionMode;
    extended->factor_format = descriptor.tessellationFactorFormat;
    extended->factor_step = descriptor.tessellationFactorStepFunction;
    extended->winding = descriptor.tessellationOutputWindingOrder;
    extended->control_point_index_type = descriptor.tessellationControlPointIndexType;
    extended->factor_scale = descriptor.tessellationFactorScaleEnabled;
    return true;
}

static bool nbExtendedRenderPass(MTLRenderPassDescriptor *descriptor, NmbExtendedRenderPassDescriptor *extended, NBCommandBuffer *commandBuffer) {
    auto attachment = [&](MTLRenderPassAttachmentDescriptor *source, NmbExtendedPassAttachment &target) {
        target.store_options = source.storeActionOptions;
        if (source.resolveTexture) {
            if (![(id)source.resolveTexture isKindOfClass:[NBTexture class]]) {
                [commandBuffer refuse:@"foreign resolve texture"];
                return false;
            }
            NBTexture *texture = (NBTexture *)source.resolveTexture;
            [commandBuffer.resourceOwners addObject:texture];
            if (!gDeviceHostCoherent && texture.surface) {
                [commandBuffer.surfaces addObject:texture];
            }
            target.resolve_texture = texture.handle;
        }
        target.resolve_level = source.resolveLevel;
        target.resolve_slice = source.resolveSlice;
        target.resolve_depth_plane = source.resolveDepthPlane;
        return true;
    };
    for (unsigned i = 0; i < 8; ++i) {
        if (!attachment(descriptor.colorAttachments[i], extended->colors[i])) {
            return false;
        }
    }
    if (!attachment(descriptor.depthAttachment, extended->depth) || !attachment(descriptor.stencilAttachment, extended->stencil)) {
        return false;
    }
    extended->depth_filter = descriptor.depthAttachment.depthResolveFilter;
    extended->stencil_filter = descriptor.stencilAttachment.stencilResolveFilter;
    NSUInteger count = [descriptor getSamplePositions:nullptr count:0];
    if (count > 16) {
        [commandBuffer refuse:@"render pass has more than 16 sample positions"];
        return false;
    }
    MTLSamplePosition positions[16] = {};
    [descriptor getSamplePositions:positions count:count];
    extended->sample_count = count;
    for (NSUInteger i = 0; i < count; ++i) {
        extended->positions[i] = {positions[i].x, positions[i].y};
    }
    return true;
}

@implementation NBEncoder (NBExtendedExecution)
- (void)executeCommandsInBuffer:(id<MTLIndirectCommandBuffer>)buffer withRange:(NSRange)range {
    if (!nbExtendedRetain(self, buffer, [NBIndirectCommandBuffer class], false)) {
        return;
    }
    [self check:nmb_encoder_execute_indirect(self.handle, [(NBIndirectCommandBuffer *)buffer handle], range.location, range.length, nullptr, 0) what:"executeCommandsInBuffer"];
}
- (void)executeCommandsInBuffer:(id<MTLIndirectCommandBuffer>)buffer indirectBuffer:(id<MTLBuffer>)ranges indirectBufferOffset:(NSUInteger)offset {
    if (!nbExtendedRetain(self, buffer, [NBIndirectCommandBuffer class], false) || !nbExtendedRetain(self, ranges, [NBBuffer class], false)) {
        return;
    }
    [self check:nmb_encoder_execute_indirect(self.handle, [(NBIndirectCommandBuffer *)buffer handle], 0, 0, [(NBBuffer *)ranges handle], offset) what:"executeCommandsInBuffer:indirectBuffer"];
}
@end

@implementation NBComputeEncoder (NBExtendedCompute)
- (void)dispatchThreadgroupsWithIndirectBuffer:(id<MTLBuffer>)buffer indirectBufferOffset:(NSUInteger)offset threadsPerThreadgroup:(MTLSize)group {
    if (!nbExtendedRetain(self, buffer, [NBBuffer class], false)) {
        return;
    }
    uint64_t threads[] = {group.width, group.height, group.depth};
    gDispatches++;
    self.commandBuffer->encodedWork.dispatches++;
    [self check:nmb_compute_dispatch_indirect(self.handle, [(NBBuffer *)buffer handle], offset, threads) what:"dispatchThreadgroupsWithIndirectBuffer"];
}
@end

@implementation NBRenderEncoder (NBExtendedRender)
- (void)drawPrimitives:(MTLPrimitiveType)primitive indirectBuffer:(id<MTLBuffer>)buffer indirectBufferOffset:(NSUInteger)offset {
    if (!nbExtendedRetain(self, buffer, [NBBuffer class], false)) {
        return;
    }
    gDraws++;
    self.commandBuffer->encodedWork.draws++;
    [self check:nmb_render_draw_indirect(self.handle, primitive, [(NBBuffer *)buffer handle], offset) what:"drawPrimitives:indirectBuffer"];
}
- (void)drawIndexedPrimitives:(MTLPrimitiveType)primitive indexType:(MTLIndexType)type indexBuffer:(id<MTLBuffer>)indices indexBufferOffset:(NSUInteger)indexOffset indirectBuffer:(id<MTLBuffer>)arguments indirectBufferOffset:(NSUInteger)argumentOffset {
    if (!nbExtendedRetain(self, indices, [NBBuffer class], false) || !nbExtendedRetain(self, arguments, [NBBuffer class], false)) {
        return;
    }
    gDraws++;
    self.commandBuffer->encodedWork.draws++;
    [self check:nmb_render_draw_indexed_indirect(self.handle, primitive, type, [(NBBuffer *)indices handle], indexOffset, [(NBBuffer *)arguments handle], argumentOffset) what:"drawIndexedPrimitives:indirectBuffer"];
}
- (void)setTessellationFactorBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset instanceStride:(NSUInteger)stride {
    if (!nbExtendedRetain(self, buffer, [NBBuffer class], true)) {
        return;
    }
    [self check:nmb_render_tessellation_buffer(self.handle, [(NBBuffer *)buffer handle], offset, stride) what:"setTessellationFactorBuffer"];
}
- (void)setTessellationFactorScale:(float)scale {
    [self check:nmb_render_tessellation_scale(self.handle, scale) what:"setTessellationFactorScale"];
}
- (void)patches:(NmbPatchDraw *)draw patchBuffer:(id<MTLBuffer>)patch controlBuffer:(id<MTLBuffer>)control indirectBuffer:(id<MTLBuffer>)indirect {
    if (!nbExtendedRetain(self, patch, [NBBuffer class], true) ||
        !nbExtendedRetain(self, control, [NBBuffer class], !draw->indexed) ||
        !nbExtendedRetain(self, indirect, [NBBuffer class], true)) {
        return;
    }
    draw->patch_indices = [(NBBuffer *)patch handle];
    draw->control_indices = [(NBBuffer *)control handle];
    draw->indirect = [(NBBuffer *)indirect handle];
    gDraws++;
    self.commandBuffer->encodedWork.draws++;
    [self check:nmb_render_patches(self.handle, draw) what:"drawPatches"];
}
- (void)drawPatches:(NSUInteger)points patchStart:(NSUInteger)start patchCount:(NSUInteger)count patchIndexBuffer:(id<MTLBuffer>)patch patchIndexBufferOffset:(NSUInteger)patchOffset instanceCount:(NSUInteger)instances baseInstance:(NSUInteger)base {
    NmbPatchDraw draw = {};
    draw.control_points = points; draw.start = start; draw.count = count;
    draw.instances = instances; draw.base_instance = base; draw.patch_offset = patchOffset;
    [self patches:&draw patchBuffer:patch controlBuffer:nil indirectBuffer:nil];
}
- (void)drawPatches:(NSUInteger)points patchIndexBuffer:(id<MTLBuffer>)patch patchIndexBufferOffset:(NSUInteger)patchOffset indirectBuffer:(id<MTLBuffer>)indirect indirectBufferOffset:(NSUInteger)indirectOffset {
    if (!nbExtendedRetain(self, indirect, [NBBuffer class], false)) {
        return;
    }
    NmbPatchDraw draw = {};
    draw.control_points = points; draw.patch_offset = patchOffset; draw.indirect_offset = indirectOffset;
    [self patches:&draw patchBuffer:patch controlBuffer:nil indirectBuffer:indirect];
}
- (void)drawIndexedPatches:(NSUInteger)points patchStart:(NSUInteger)start patchCount:(NSUInteger)count patchIndexBuffer:(id<MTLBuffer>)patch patchIndexBufferOffset:(NSUInteger)patchOffset controlPointIndexBuffer:(id<MTLBuffer>)control controlPointIndexBufferOffset:(NSUInteger)controlOffset instanceCount:(NSUInteger)instances baseInstance:(NSUInteger)base {
    NmbPatchDraw draw = {};
    draw.control_points = points; draw.start = start; draw.count = count;
    draw.instances = instances; draw.base_instance = base; draw.indexed = true;
    draw.patch_offset = patchOffset; draw.control_offset = controlOffset;
    [self patches:&draw patchBuffer:patch controlBuffer:control indirectBuffer:nil];
}
- (void)drawIndexedPatches:(NSUInteger)points patchIndexBuffer:(id<MTLBuffer>)patch patchIndexBufferOffset:(NSUInteger)patchOffset controlPointIndexBuffer:(id<MTLBuffer>)control controlPointIndexBufferOffset:(NSUInteger)controlOffset indirectBuffer:(id<MTLBuffer>)indirect indirectBufferOffset:(NSUInteger)indirectOffset {
    if (!nbExtendedRetain(self, indirect, [NBBuffer class], false)) {
        return;
    }
    NmbPatchDraw draw = {};
    draw.control_points = points; draw.indexed = true; draw.patch_offset = patchOffset;
    draw.control_offset = controlOffset; draw.indirect_offset = indirectOffset;
    [self patches:&draw patchBuffer:patch controlBuffer:control indirectBuffer:indirect];
}
- (void)setColorStoreActionOptions:(MTLStoreActionOptions)options atIndex:(NSUInteger)index {
    [self check:nmb_render_store_options(self.handle, 0, index, options) what:"setColorStoreActionOptions"];
}
- (void)setDepthStoreActionOptions:(MTLStoreActionOptions)options {
    [self check:nmb_render_store_options(self.handle, 1, 0, options) what:"setDepthStoreActionOptions"];
}
- (void)setStencilStoreActionOptions:(MTLStoreActionOptions)options {
    [self check:nmb_render_store_options(self.handle, 2, 0, options) what:"setStencilStoreActionOptions"];
}
@end

@implementation NBBlitEncoder (NBExtendedBlit)
- (void)copyIndirectCommandBuffer:(id<MTLIndirectCommandBuffer>)source sourceRange:(NSRange)range destination:(id<MTLIndirectCommandBuffer>)destination destinationIndex:(NSUInteger)index {
    if (!nbExtendedRetain(self, source, [NBIndirectCommandBuffer class], false) || !nbExtendedRetain(self, destination, [NBIndirectCommandBuffer class], false)) {
        return;
    }
    NBIndirectCommandBuffer *from = (NBIndirectCommandBuffer *)source;
    NBIndirectCommandBuffer *to = (NBIndirectCommandBuffer *)destination;
    if (![self check:nmb_blit_indirect_copy(self.handle, from.handle, range.location, range.length, to.handle, index) what:"copyIndirectCommandBuffer"]) {
        return;
    }
    // Resource ownership follows the copied records and survives the source ICB.
    NSMutableDictionary *owners = [NSMutableDictionary new];
    @synchronized(from) {
        for (NSNumber *command in from.commandOwners) {
            NSUInteger location = command.unsignedIntegerValue;
            if (location >= range.location && location - range.location < range.length) {
                owners[@(index + location - range.location)] = [from.commandOwners[command] mutableCopy];
            }
        }
    }
    @synchronized(to) {
        [to forget:NSMakeRange(index, range.length)];
        [to.commandOwners addEntriesFromDictionary:owners];
    }
}
- (void)resetCommandsInBuffer:(id<MTLIndirectCommandBuffer>)buffer withRange:(NSRange)range {
    if (!nbExtendedRetain(self, buffer, [NBIndirectCommandBuffer class], false)) {
        return;
    }
    [self check:nmb_blit_indirect_reset(self.handle, [(NBIndirectCommandBuffer *)buffer handle], range.location, range.length, false) what:"resetCommandsInBuffer"];
}
- (void)optimizeIndirectCommandBuffer:(id<MTLIndirectCommandBuffer>)buffer withRange:(NSRange)range {
    if (!nbExtendedRetain(self, buffer, [NBIndirectCommandBuffer class], false)) {
        return;
    }
    [self check:nmb_blit_indirect_reset(self.handle, [(NBIndirectCommandBuffer *)buffer handle], range.location, range.length, true) what:"optimizeIndirectCommandBuffer"];
}
@end
