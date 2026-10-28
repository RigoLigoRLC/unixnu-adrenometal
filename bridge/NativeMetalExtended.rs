// SPDX-License-Identifier: MIT

use metal2vulkan::bind::ArgumentDescriptor;
use metal2vulkan::metal::{BindingAccess, IndirectCommandType, TessellationControlPointIndexType,
    TessellationFactorFormat, TessellationFactorStepFunction, TessellationPartitionMode};
use metal2vulkan::pipeline::TessellationDescriptor;
use metal2vulkan_runtime::{ArgumentEncoder, IndirectCommandBuffer, IndirectCommandBufferDescriptor,
    IndirectComputeCommand, IndirectRenderCommand, MultisampleDepthResolveFilter,
    MultisampleStencilResolveFilter, SamplePosition, StoreActionOptions};

pub struct NmbArgumentEncoder(ArgumentEncoder);
pub struct NmbIndirectCommandBuffer(pub IndirectCommandBuffer);

pub enum NmbIndirectCommand {
    Compute(IndirectComputeCommand),
    Render(IndirectRenderCommand),
}

#[repr(C)]
pub struct NmbArgumentDescriptor {
    data_type: u64, index: u64, array_length: u64, access: u64, texture_type: u64,
    constant_alignment: u64,
}

#[repr(C)]
pub struct NmbExtendedRenderPipelineDescriptor {
    indirect: u64, tessellation: u64, max_factor: u64, partition: u64, factor_format: u64,
    factor_step: u64, winding: u64, control_point_index_type: u64, factor_scale: u64,
}

#[repr(C)]
pub struct NmbExtendedPassAttachment {
    resolve_texture: *mut NmbTexture,
    resolve_level: u64, resolve_slice: u64, resolve_depth_plane: u64, store_options: u64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NmbSamplePosition {
    x: f32, y: f32,
}

/// The output holds the Vulkan standard positions in sample-index order.
/// The device must advertise both this sample count and standard locations.
///
/// # Safety
/// `out` points to `count` writable sample positions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nmb_device_default_sample_positions(
    device: *const NmbDevice,
    out: *mut NmbSamplePosition,
    count: u64,
) -> bool {
    guard(false, || {
        if out.is_null() {
            return Err("null sample positions".into());
        }

        let device = &get(device)?.0;
        if !device.properties().supports_texture_sample_count(count).map_err(e("sample count"))? {
            return Err(format!("unsupported sample count {count}"));
        }

        // The runtime retains these handles throughout this read-only query.
        let properties = unsafe {
            let handles = device.raw();
            handles.instance.get_physical_device_properties(handles.physical_device)
        };
        if properties.limits.standard_sample_locations != ash::vk::TRUE {
            return Err("the Vulkan device does not advertise standard sample locations".into());
        }

        let positions: &[(f32, f32)] = match count {
            1 => &[(0.5, 0.5)],
            2 => &[(0.75, 0.75), (0.25, 0.25)],
            4 => &[(0.375, 0.125), (0.875, 0.375), (0.125, 0.625), (0.625, 0.875)],
            _ => return Err(format!("sample count {count} has no Metal standard-position table")),
        };

        for (index, &(x, y)) in positions.iter().enumerate() {
            // The caller supplies count writable entries, matching the selected table.
            unsafe {
                out.add(index).write(NmbSamplePosition { x, y });
            }
        }

        Ok(true)
    })
}

#[repr(C)]
pub struct NmbExtendedRenderPassDescriptor {
    colors: [NmbExtendedPassAttachment; 8], depth: NmbExtendedPassAttachment,
    stencil: NmbExtendedPassAttachment,
    depth_filter: u64, stencil_filter: u64, sample_count: u64,
    positions: [NmbSamplePosition; 16],
}

#[repr(C)]
pub struct NmbIndirectCommandBufferDescriptor {
    command_types: u64, inherit_pipeline: u64, inherit_buffers: u64,
    vertex_buffers: u64, fragment_buffers: u64, kernel_buffers: u64, kernel_memory: u64, dynamic_stride: u64,
}

#[repr(C)]
pub struct NmbPatchDraw {
    control_points: u64, start: u64, count: u64, instances: u64, base_instance: u64,
    patch_indices: *mut NmbBuffer,
    patch_offset: u64,
    control_indices: *mut NmbBuffer,
    control_offset: u64,
    indirect: *mut NmbBuffer,
    indirect_offset: u64,
    factors: *mut NmbBuffer,
    factor_offset: u64, factor_stride: u64,
    indexed: bool,
}

fn extended_u32(value: u64, name: &str) -> Result<u32, String> {
    u32::try_from(value).map_err(|_| format!("{name} exceeds 32 bits"))
}

fn extended_range(start: u64, length: u64) -> Result<std::ops::Range<u64>, String> {
    let end = start.checked_add(length).ok_or("command range overflow")?;
    Ok(start..end)
}

fn extended_buffer<'a>(buffer: *const NmbBuffer) -> Result<Option<&'a Buffer>, String> {
    if buffer.is_null() {
        Ok(None)
    } else {
        Ok(Some(&get(buffer)?.0))
    }
}

fn apply_extended_render_pipeline(out: &mut RenderPipelineDescriptor, d: &NmbExtendedRenderPipelineDescriptor) -> Result<(), String> {
    out.support_indirect_command_buffers = d.indirect != 0;
    if d.tessellation != 0 {
        let mut tess = TessellationDescriptor::default();
        tess.max_tessellation_factor = extended_u32(d.max_factor, "max tessellation factor")?;
        tess.partition_mode = raw::<TessellationPartitionMode>(d.partition, "tessellation partition")?;
        tess.factor_format = raw::<TessellationFactorFormat>(d.factor_format, "tessellation factor format")?;
        tess.factor_step_function = raw::<TessellationFactorStepFunction>(d.factor_step, "tessellation factor step")?;
        tess.output_winding_order = raw::<Winding>(d.winding, "tessellation winding")?;
        tess.control_point_index_type = raw::<TessellationControlPointIndexType>(d.control_point_index_type, "control point index type")?;
        tess.factor_scale_enabled = d.factor_scale != 0;
        out.tessellation = Some(tess);
    }
    Ok(())
}

fn apply_extended_render_pass(pass: &mut RenderPassDescriptor, d: &NmbExtendedRenderPassDescriptor) -> Result<(), String> {
    fn texture(pointer: *const NmbTexture) -> Result<Option<Texture>, String> {
        if pointer.is_null() {
            Ok(None)
        } else {
            Ok(Some(get(pointer)?.0.clone()))
        }
    }

    for (slot, extra) in pass.color_attachments.iter_mut().zip(&d.colors) {
        slot.resolve_texture = texture(extra.resolve_texture)?;
        slot.resolve_level = extra.resolve_level;
        slot.resolve_slice = extra.resolve_slice;
        slot.resolve_depth_plane = extra.resolve_depth_plane;
        slot.store_action_options = StoreActionOptions::from_raw(extra.store_options).map_err(e("color store options"))?;
    }
    let depth = &mut pass.depth_attachment;
    depth.resolve_texture = texture(d.depth.resolve_texture)?;
    depth.resolve_level = d.depth.resolve_level;
    depth.resolve_slice = d.depth.resolve_slice;
    depth.resolve_depth_plane = d.depth.resolve_depth_plane;
    depth.store_action_options = StoreActionOptions::from_raw(d.depth.store_options).map_err(e("depth store options"))?;
    depth.depth_resolve_filter = MultisampleDepthResolveFilter::from_raw(d.depth_filter).map_err(e("depth resolve filter"))?;
    let stencil = &mut pass.stencil_attachment;
    stencil.resolve_texture = texture(d.stencil.resolve_texture)?;
    stencil.resolve_level = d.stencil.resolve_level;
    stencil.resolve_slice = d.stencil.resolve_slice;
    stencil.resolve_depth_plane = d.stencil.resolve_depth_plane;
    stencil.store_action_options = StoreActionOptions::from_raw(d.stencil.store_options).map_err(e("stencil store options"))?;
    stencil.stencil_resolve_filter = MultisampleStencilResolveFilter::from_raw(d.stencil_filter).map_err(e("stencil resolve filter"))?;
    let count = usize::try_from(d.sample_count).map_err(e("sample position count"))?;
    let positions = d.positions.get(..count).ok_or("too many sample positions")?;
    pass.sample_positions = positions.iter().map(|p| SamplePosition::new(p.x, p.y)).collect();
    Ok(())
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_new(device: *mut NmbDevice, descriptors: *const NmbArgumentDescriptor, count: usize) -> *mut NmbArgumentEncoder {
    guard(std::ptr::null_mut(), || {
        if count != 0 && descriptors.is_null() {
            return Err("null argument descriptors".into());
        }
        let descriptors = if count == 0 { &[] } else { unsafe { std::slice::from_raw_parts(descriptors, count) } };
        let mut arguments = Vec::with_capacity(count);
        for d in descriptors {
            let mut argument = ArgumentDescriptor::new(raw::<DataType>(d.data_type, "argument data type")?, extended_u32(d.index, "argument index")?);
            argument.array_length = extended_u32(d.array_length, "argument array length")?;
            argument.access = raw::<BindingAccess>(d.access, "argument access")?;
            argument.texture_type = raw::<TextureType>(d.texture_type, "argument texture type")?;
            argument.constant_block_alignment = extended_u32(d.constant_alignment, "constant block alignment")?;
            arguments.push(argument);
        }
        let encoder = get(device)?.0.new_argument_encoder_with_arguments(&arguments).map_err(e("argument encoder"))?;
        Ok(boxed(NmbArgumentEncoder(encoder)))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_function_argument_encoder(function: *mut NmbFunction, index: u64) -> *mut NmbArgumentEncoder {
    guard(std::ptr::null_mut(), || {
        let encoder = get(function)?.0.new_argument_encoder_with_buffer_index(extended_u32(index, "argument buffer index")?).map_err(e("function argument encoder"))?;
        Ok(boxed(NmbArgumentEncoder(encoder)))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_nested(encoder: *mut NmbArgumentEncoder, index: u64) -> *mut NmbArgumentEncoder {
    guard(std::ptr::null_mut(), || {
        let nested = get(encoder)?.0.new_argument_encoder_for_buffer_at_index(extended_u32(index, "argument index")?).map_err(e("nested argument encoder"))?;
        Ok(boxed(NmbArgumentEncoder(nested)))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_release(encoder: *mut NmbArgumentEncoder) {
    release(encoder)
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_length(encoder: *const NmbArgumentEncoder) -> u64 {
    guard(0, || Ok(get(encoder)?.0.encoded_length()))
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_alignment(encoder: *const NmbArgumentEncoder) -> u64 {
    guard(0, || Ok(get(encoder)?.0.alignment()))
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_target(encoder: *mut NmbArgumentEncoder, buffer: *mut NmbBuffer, offset: u64, element: u64) -> bool {
    guard(false, || {
        get_mut(encoder)?.0.set_argument_buffer_start_offset_array_element(extended_buffer(buffer)?, offset, element).map_err(e("argument buffer target"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_resources(encoder: *mut NmbArgumentEncoder, kind: u32, handles: *const *const c_void, offsets: *const u64, first: u64, count: usize) -> bool {
    guard(false, || {
        if count != 0 && handles.is_null() {
            return Err("null argument resource array".into());
        }
        let handles = if count == 0 { &[] } else { unsafe { std::slice::from_raw_parts(handles, count) } };
        let first = extended_u32(first, "first argument")?;
        let encoder = &get(encoder)?.0;
        macro_rules! references {
            ($ty:ty) => {{
                handles.iter().map(|p| {
                    if p.is_null() { Ok(None) } else { Ok(Some(&get(p.cast::<$ty>())?.0)) }
                }).collect::<Result<Vec<_>, String>>()?
            }};
        }
        match kind {
            0 => {
                if count != 0 && offsets.is_null() {
                    return Err("null argument buffer offsets".into());
                }
                let offsets = if count == 0 { &[] } else { unsafe { std::slice::from_raw_parts(offsets, count) } };
                encoder.set_buffers(&references!(NmbBuffer), offsets, first)
            }
            1 => encoder.set_textures(&references!(NmbTexture), first),
            2 => encoder.set_sampler_states(&references!(NmbSampler), first),
            3 => encoder.set_compute_pipeline_states(&references!(NmbComputePipeline), first),
            4 => encoder.set_render_pipeline_states(&references!(NmbRenderPipeline), first),
            5 => encoder.set_indirect_command_buffers(&references!(NmbIndirectCommandBuffer), first),
            _ => return Err("unknown argument resource kind".into()),
        }.map_err(e("argument resources"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_constant_data(encoder: *mut NmbArgumentEncoder, index: u64) -> *mut c_void {
    guard(std::ptr::null_mut(), || {
        Ok(get(encoder)?.0.constant_data(extended_u32(index, "constant argument index")?).map_err(e("argument constant data"))?.as_ptr().cast())
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_function_patch_info(function: *const NmbFunction, kind: *mut u64, count: *mut i64) -> bool {
    guard(false, || {
        let function = &get(function)?.0;
        *get_mut(kind)? = function.patch_type().map_err(e("patch type"))?.as_raw().into();
        *get_mut(count)? = function.patch_control_point_count().map_err(e("patch control points"))?.map_or(-1, i64::from);
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_buffer_new(device: *mut NmbDevice, descriptor: *const NmbIndirectCommandBufferDescriptor, count: u64, resource_options: u64) -> *mut NmbIndirectCommandBuffer {
    guard(std::ptr::null_mut(), || {
        let d = get(descriptor)?;
        let mut descriptor = IndirectCommandBufferDescriptor::default();
        descriptor.command_types = raw::<IndirectCommandType>(d.command_types, "indirect command types")?;
        descriptor.inherit_pipeline_state = d.inherit_pipeline != 0;
        descriptor.inherit_buffers = d.inherit_buffers != 0;
        descriptor.max_vertex_buffer_bind_count = extended_u32(d.vertex_buffers, "indirect vertex buffers")?;
        descriptor.max_fragment_buffer_bind_count = extended_u32(d.fragment_buffers, "indirect fragment buffers")?;
        descriptor.max_kernel_buffer_bind_count = extended_u32(d.kernel_buffers, "indirect kernel buffers")?;
        descriptor.max_kernel_threadgroup_memory_bind_count = extended_u32(d.kernel_memory, "indirect kernel memory")?;
        descriptor.support_dynamic_attribute_stride = d.dynamic_stride != 0;
        let buffer = get(device)?.0.new_indirect_command_buffer(&descriptor, count, options(resource_options)?).map_err(e("indirect command buffer"))?;
        Ok(boxed(NmbIndirectCommandBuffer(buffer)))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_buffer_release(buffer: *mut NmbIndirectCommandBuffer) {
    release(buffer)
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_buffer_info(buffer: *const NmbIndirectCommandBuffer, out: *mut u64) -> bool {
    guard(false, || {
        if out.is_null() {
            return Err("null indirect buffer info".into());
        }
        let buffer = &get(buffer)?.0;
        let values = [buffer.size(), buffer.gpu_resource_id().0, buffer.allocated_size(), buffer.resource_options().raw(), buffer.resource_index()];
        unsafe { std::ptr::copy_nonoverlapping(values.as_ptr(), out, values.len()) };
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_buffer_purgeable(buffer: *mut NmbIndirectCommandBuffer, state: u64) -> u64 {
    guard(0, || Ok(get(buffer)?.0.set_purgeable_state(PurgeableState::from_raw(state).map_err(e("purgeable state"))?).map_err(e("indirect buffer purgeable"))?.raw()))
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_buffer_reset(buffer: *mut NmbIndirectCommandBuffer, start: u64, length: u64) -> bool {
    guard(false, || {
        get(buffer)?.0.reset_with_range(extended_range(start, length)?).map_err(e("reset indirect command buffer"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_command_new(buffer: *mut NmbIndirectCommandBuffer, index: u64, compute_command: bool) -> *mut NmbIndirectCommand {
    guard(std::ptr::null_mut(), || {
        let buffer = &get(buffer)?.0;
        let command = if compute_command {
            NmbIndirectCommand::Compute(buffer.indirect_compute_command_at(index).map_err(e("indirect compute command"))?)
        } else {
            NmbIndirectCommand::Render(buffer.indirect_render_command_at(index).map_err(e("indirect render command"))?)
        };
        Ok(boxed(command))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_command_release(command: *mut NmbIndirectCommand) {
    release(command)
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_command_pipeline(command: *mut NmbIndirectCommand, pipeline: *const c_void) -> bool {
    guard(false, || {
        match get(command)? {
            NmbIndirectCommand::Compute(c) => c.set_compute_pipeline_state(&get(pipeline.cast::<NmbComputePipeline>())?.0),
            NmbIndirectCommand::Render(r) => r.set_render_pipeline_state(&get(pipeline.cast::<NmbRenderPipeline>())?.0),
        }.map_err(e("indirect command pipeline"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_command_buffer(command: *mut NmbIndirectCommand, stage: u32, buffer: *mut NmbBuffer, offset: u64, index: u64) -> bool {
    guard(false, || {
        let buffer = extended_buffer(buffer)?;
        let index = extended_u32(index, "indirect buffer index")?;
        match (get(command)?, stage) {
            (NmbIndirectCommand::Render(r), 0) => r.set_vertex_buffer(buffer, offset, index),
            (NmbIndirectCommand::Render(r), 1) => r.set_fragment_buffer(buffer, offset, index),
            (NmbIndirectCommand::Compute(c), 2) => c.set_kernel_buffer(buffer, offset, index),
            _ => return Err("indirect buffer stage does not match command".into()),
        }.map_err(e("indirect command buffer binding"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_command_control(command: *mut NmbIndirectCommand, action: u32) -> bool {
    guard(false, || {
        match (get(command)?, action) {
            (NmbIndirectCommand::Compute(c), 0) => c.reset(),
            (NmbIndirectCommand::Compute(c), 1) => c.set_barrier(),
            (NmbIndirectCommand::Compute(c), 2) => c.clear_barrier(),
            (NmbIndirectCommand::Render(r), 0) => r.reset(),
            (NmbIndirectCommand::Render(r), 1) => r.set_barrier(),
            (NmbIndirectCommand::Render(r), 2) => r.clear_barrier(),
            _ => return Err("unknown indirect command action".into()),
        }.map_err(e("indirect command action"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_command_buffer_stride(command: *mut NmbIndirectCommand, stage: u32, buffer: *mut NmbBuffer, offset: u64, stride: u64, index: u64) -> bool {
    guard(false, || {
        let buffer = extended_buffer(buffer)?;
        let index = extended_u32(index, "indirect buffer index")?;
        match (get(command)?, stage) {
            (NmbIndirectCommand::Render(r), 0) => r.set_vertex_buffer_with_stride(buffer, offset, stride, index),
            (NmbIndirectCommand::Compute(c), 2) => c.set_kernel_buffer_with_stride(buffer, offset, stride, index),
            _ => return Err("indirect buffer stride stage does not match command".into()),
        }.map_err(e("indirect command dynamic buffer stride"))?;
        Ok(true)
    })
}

fn extended_compute(command: *const NmbIndirectCommand) -> Result<&'static IndirectComputeCommand, String> {
    match get(command)? {
        NmbIndirectCommand::Compute(c) => Ok(c),
        _ => Err("not an indirect compute command".into()),
    }
}

fn extended_render(command: *const NmbIndirectCommand) -> Result<&'static IndirectRenderCommand, String> {
    match get(command)? {
        NmbIndirectCommand::Render(r) => Ok(r),
        _ => Err("not an indirect render command".into()),
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_command_dispatch(command: *mut NmbIndirectCommand, grid: *const u64, group: *const u64, threads: bool) -> bool {
    guard(false, || {
        let c = extended_compute(command)?;
        if threads {
            c.concurrent_dispatch_threads(size(grid)?, size(group)?)
        } else {
            c.concurrent_dispatch_threadgroups(size(grid)?, size(group)?)
        }.map_err(e("indirect command dispatch"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_command_memory(command: *mut NmbIndirectCommand, length: u64, index: u64) -> bool {
    guard(false, || {
        extended_compute(command)?.set_threadgroup_memory_length(extended_u32(length, "threadgroup memory length")?, extended_u32(index, "threadgroup memory index")?).map_err(e("indirect command threadgroup memory"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_command_region(
    command: *mut NmbIndirectCommand,
    region_origin: *const u64,
    region_size: *const u64,
) -> bool {
    guard(false, || {
        let region = Region {
            origin: origin(region_origin)?,
            size: size(region_size)?,
        };

        extended_compute(command)?
            .set_stage_in_region(region)
            .map_err(e("indirect command stage region"))?;

        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_command_draw(command: *mut NmbIndirectCommand, primitive: u64, start: u64, count: u64, instances: u64, base: u64) -> bool {
    guard(false, || {
        extended_render(command)?.draw_primitives(raw::<PrimitiveType>(primitive, "primitive")?, start, count, instances, base).map_err(e("indirect command draw"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_command_draw_indexed(command: *mut NmbIndirectCommand, primitive: u64, count: u64, index_type: u64, buffer: *mut NmbBuffer, offset: u64, instances: u64, base_vertex: i64, base_instance: u64) -> bool {
    guard(false, || {
        extended_render(command)?.draw_indexed_primitives(raw::<PrimitiveType>(primitive, "primitive")?, count, raw::<IndexType>(index_type, "index type")?, &get(buffer)?.0, offset, instances, base_vertex, base_instance).map_err(e("indirect command indexed draw"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_indirect_command_patches(command: *mut NmbIndirectCommand, draw: *const NmbPatchDraw) -> bool {
    guard(false, || {
        let d = get(draw)?;
        let r = extended_render(command)?;
        let patch = extended_buffer(d.patch_indices)?;
        let factors = &get(d.factors)?.0;
        if d.indexed {
            r.draw_indexed_patches(d.control_points, d.start, d.count, patch, d.patch_offset, extended_buffer(d.control_indices)?, d.control_offset, d.instances, d.base_instance, factors, d.factor_offset, d.factor_stride)
        } else {
            r.draw_patches(d.control_points, d.start, d.count, patch, d.patch_offset, d.instances, d.base_instance, factors, d.factor_offset, d.factor_stride)
        }.map_err(e("indirect command patch draw"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_compute_dispatch_indirect(encoder: *mut NmbEncoder, buffer: *mut NmbBuffer, offset: u64, group: *const u64) -> bool {
    guard(false, || {
        compute(encoder)?.dispatch_threadgroups_indirect(&get(buffer)?.0, offset, size(group)?).map_err(e("indirect dispatch"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_render_draw_indirect(encoder: *mut NmbEncoder, primitive: u64, buffer: *mut NmbBuffer, offset: u64) -> bool {
    guard(false, || {
        render(encoder)?.draw_primitives_indirect(raw::<PrimitiveType>(primitive, "primitive")?, &get(buffer)?.0, offset).map_err(e("indirect draw"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_render_draw_indexed_indirect(encoder: *mut NmbEncoder, primitive: u64, index_type: u64, indices: *mut NmbBuffer, index_offset: u64, arguments: *mut NmbBuffer, argument_offset: u64) -> bool {
    guard(false, || {
        render(encoder)?.draw_indexed_primitives_indirect(raw::<PrimitiveType>(primitive, "primitive")?, raw::<IndexType>(index_type, "index type")?, &get(indices)?.0, index_offset, &get(arguments)?.0, argument_offset).map_err(e("indexed indirect draw"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_render_tessellation_buffer(encoder: *mut NmbEncoder, buffer: *mut NmbBuffer, offset: u64, stride: u64) -> bool {
    guard(false, || {
        render(encoder)?.set_tessellation_factor_buffer(extended_buffer(buffer)?, offset, stride).map_err(e("tessellation factor buffer"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_render_tessellation_scale(encoder: *mut NmbEncoder, scale: f32) -> bool {
    guard(false, || {
        render(encoder)?.set_tessellation_factor_scale(scale);
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_render_patches(encoder: *mut NmbEncoder, draw: *const NmbPatchDraw) -> bool {
    guard(false, || {
        let d = get(draw)?;
        let r = render(encoder)?;
        let points = extended_u32(d.control_points, "patch control points")?;
        let patch = extended_buffer(d.patch_indices)?;
        if !d.indirect.is_null() {
            let arguments = &get(d.indirect)?.0;
            if d.indexed {
                r.draw_indexed_patches_indirect(points, patch, d.patch_offset, &get(d.control_indices)?.0, d.control_offset, arguments, d.indirect_offset)
            } else {
                r.draw_patches_indirect(points, patch, d.patch_offset, arguments, d.indirect_offset)
            }
        } else {
            let start = extended_u32(d.start, "patch start")?;
            let count = extended_u32(d.count, "patch count")?;
            let instances = extended_u32(d.instances, "patch instance count")?;
            let base = extended_u32(d.base_instance, "patch base instance")?;
            if d.indexed {
                r.draw_indexed_patches(points, start, count, patch, d.patch_offset, &get(d.control_indices)?.0, d.control_offset, instances, base)
            } else {
                r.draw_patches(points, start, count, patch, d.patch_offset, instances, base)
            }
        }.map_err(e("patch draw"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_encoder_execute_indirect(encoder: *mut NmbEncoder, buffer: *mut NmbIndirectCommandBuffer, start: u64, length: u64, range_buffer: *mut NmbBuffer, range_offset: u64) -> bool {
    guard(false, || {
        let icb = &get(buffer)?.0;
        let range = extended_range(start, length)?;
        match (get_mut(encoder)?, extended_buffer(range_buffer)?) {
            (NmbEncoder::Compute(c), None) => c.execute_commands_in_buffer(icb, range),
            (NmbEncoder::Render(r), None) => r.execute_commands_in_buffer(icb, range),
            (NmbEncoder::Compute(c), Some(b)) => c.execute_commands_in_buffer_indirect(icb, b, range_offset),
            (NmbEncoder::Render(r), Some(b)) => r.execute_commands_in_buffer_indirect(icb, b, range_offset),
            _ => return Err("indirect execution requires a render or compute encoder".into()),
        }.map_err(e("execute indirect commands"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_blit_indirect_copy(encoder: *mut NmbEncoder, source: *mut NmbIndirectCommandBuffer, start: u64, length: u64, destination: *mut NmbIndirectCommandBuffer, destination_index: u64) -> bool {
    guard(false, || {
        blit(encoder)?.copy_indirect_command_buffer(&get(source)?.0, extended_range(start, length)?, &get(destination)?.0, destination_index).map_err(e("copy indirect commands"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_blit_indirect_reset(encoder: *mut NmbEncoder, buffer: *mut NmbIndirectCommandBuffer, start: u64, length: u64, optimize: bool) -> bool {
    guard(false, || {
        let b = blit(encoder)?;
        let icb = &get(buffer)?.0;
        let range = extended_range(start, length)?;
        if optimize {
            b.optimize_indirect_command_buffer(icb, range)
        } else {
            b.reset_commands_in_buffer(icb, range)
        }.map_err(e("indirect command blit"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_render_store_options(encoder: *mut NmbEncoder, attachment: u32, index: u64, options: u64) -> bool {
    guard(false, || {
        let r = render(encoder)?;
        let options = StoreActionOptions::from_raw(options).map_err(e("store action options"))?;
        match attachment {
            0 => r.set_color_store_action_options(options, extended_u32(index, "color attachment index")?),
            1 => r.set_depth_store_action_options(options),
            2 => r.set_stencil_store_action_options(options),
            _ => return Err("unknown store options attachment".into()),
        }.map_err(e("store action options"))?;
        Ok(true)
    })
}
