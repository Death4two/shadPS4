//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/config.h"
#include "video_core/host_shaders/nis_comp.h"
#include "video_core/host_shaders/nis/NIS_Config.h"
#include "video_core/renderer_vulkan/host_passes/nis_pass.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

#include <vk_mem_alloc.h>

#include "core/debug_state.h"

namespace Vulkan::HostPasses {

static constexpr u32 kNisBlockWidth = 32;
static constexpr u32 kNisBlockHeight = 24;
static constexpr u32 kNisThreadGroupSize = 256;

void NisPass::Create(vk::Device device, VmaAllocator allocator, u32 num_images) {
    this->device = device;
    this->allocator = allocator;
    this->num_images = num_images;

    sampler = Check<"create nis sampler">(device.createSamplerUnique(vk::SamplerCreateInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxAnisotropy = 1.0f,
        .minLod = -1000.0f,
        .maxLod = 1000.0f,
    }));

    // 6 bindings: UBO, sampler, input texture, output image, coef_scaler, coef_usm
    std::array<vk::DescriptorSetLayoutBinding, 6> layoutBindings{{
        {
            .binding = 0,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
        {
            .binding = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
            .pImmutableSamplers = &sampler.get(),
        },
        {
            .binding = 2,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
        {
            .binding = 3,
            .descriptorType = vk::DescriptorType::eStorageImage,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
        {
            .binding = 4,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
        {
            .binding = 5,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
    }};

    descriptor_set_layout =
        Check<"create nis descriptor set layout">(device.createDescriptorSetLayoutUnique({
            .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor,
            .bindingCount = static_cast<u32>(layoutBindings.size()),
            .pBindings = layoutBindings.data(),
        }));

    const auto& cs_module =
        Compile(HostShaders::NIS_COMP, vk::ShaderStageFlagBits::eCompute, device, {});
    ASSERT(cs_module);
    SetObjectName(device, cs_module, "nis.comp");

    pipeline_layout = Check<"nis pipeline layout">(device.createPipelineLayoutUnique({
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_set_layout.get(),
    }));
    SetObjectName(device, pipeline_layout.get(), "nis pipeline layout");

    const vk::ComputePipelineCreateInfo pinfo{
        .stage{
            .stage = vk::ShaderStageFlagBits::eCompute,
            .module = cs_module,
            .pName = "main",
        },
        .layout = pipeline_layout.get(),
    };
    scaler_pipeline =
        Check<"nis scaler compute pipeline">(device.createComputePipelineUnique({}, pinfo));
    SetObjectName(device, scaler_pipeline.get(), "nis scaler pipeline");

    device.destroyShaderModule(cs_module);

    // Create UBO buffer for NISConfig (256-byte aligned)
    const vk::BufferCreateInfo buffer_ci{
        .size = sizeof(NISConfig),
        .usage = vk::BufferUsageFlagBits::eUniformBuffer,
    };

    const VmaAllocationCreateInfo alloc_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VkBuffer unsafe_buffer{};
    VmaAllocationInfo allocation_info{};
    const auto buffer_ci_unsafe = static_cast<VkBufferCreateInfo>(buffer_ci);
    const auto result = vmaCreateBuffer(allocator, &buffer_ci_unsafe, &alloc_info, &unsafe_buffer,
                                        &config_buffer_allocation, &allocation_info);
    ASSERT(result == VK_SUCCESS);
    config_buffer = vk::Buffer{unsafe_buffer};
    config_buffer_mapped = allocation_info.pMappedData;
    SetObjectName(device, config_buffer, "NIS Config UBO");

    available_imgs.resize(num_images);
    for (u32 i = 0; i < num_images; ++i) {
        auto& img = available_imgs[i];
        img.id = static_cast<u8>(i);
        img.output_image = VideoCore::UniqueImage(device, allocator);
    }
}

void NisPass::CreateCoefficientsTextures() {
    if (coefficients_created) {
        return; // Already created
    }
    coefficients_created = true;

    // Create coefficient textures (kPhaseCount x kFilterSize/4 = 64x2 for RGBA32F)
    const vk::ImageCreateInfo coef_image_info{
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR32G32B32A32Sfloat,
        .extent{
            .width = kFilterSize / 4, // 8 floats / 4 components = 2 pixels
            .height = kPhaseCount,    // 64 phases
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        .initialLayout = vk::ImageLayout::eUndefined,
    };

    coef_scale_image = VideoCore::UniqueImage(device, allocator);
    coef_scale_image.Create(coef_image_info);
    SetObjectName(device, static_cast<vk::Image>(coef_scale_image), "NIS Coef Scale Image");

    coef_usm_image = VideoCore::UniqueImage(device, allocator);
    coef_usm_image.Create(coef_image_info);
    SetObjectName(device, static_cast<vk::Image>(coef_usm_image), "NIS Coef USM Image");

    const vk::ImageViewCreateInfo view_info{
        .image = coef_scale_image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR32G32B32A32Sfloat,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    coef_scale_image_view =
        Check<"create nis coef scale image view">(device.createImageViewUnique(view_info));
    SetObjectName(device, coef_scale_image_view.get(), "NIS Coef Scale ImageView");

    auto usm_view_info = view_info;
    usm_view_info.image = coef_usm_image;
    coef_usm_image_view =
        Check<"create nis coef usm image view">(device.createImageViewUnique(usm_view_info));
    SetObjectName(device, coef_usm_image_view.get(), "NIS Coef USM ImageView");
}

void NisPass::UploadCoefficients(vk::CommandBuffer cmdbuf) {
    if (coefficients_uploaded) {
        return;
    }

    // Create staging buffer for coefficient data
    const size_t coef_data_size = kPhaseCount * kFilterSize * sizeof(float);
    const vk::BufferCreateInfo staging_ci{
        .size = coef_data_size * 2, // Both coef_scale and coef_usm
        .usage = vk::BufferUsageFlagBits::eTransferSrc,
    };

    const VmaAllocationCreateInfo staging_alloc_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VkBuffer staging_buffer_raw{};
    VmaAllocation staging_allocation{};
    VmaAllocationInfo staging_info{};
    const auto staging_ci_unsafe = static_cast<VkBufferCreateInfo>(staging_ci);
    vmaCreateBuffer(allocator, &staging_ci_unsafe, &staging_alloc_info, &staging_buffer_raw,
                    &staging_allocation, &staging_info);
    vk::Buffer staging_buffer{staging_buffer_raw};

    // Copy coefficient data to staging buffer
    auto* staging_data = static_cast<float*>(staging_info.pMappedData);
    std::memcpy(staging_data, coef_scale, coef_data_size);
    std::memcpy(staging_data + kPhaseCount * kFilterSize, coef_usm, coef_data_size);

    vmaFlushAllocation(allocator, staging_allocation, 0, VK_WHOLE_SIZE);

    constexpr vk::ImageSubresourceRange subresource{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = 1,
    };

    // Transition coefficient images to transfer dst
    const std::array pre_barriers{
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eNone,
            .srcAccessMask = vk::AccessFlagBits2::eNone,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .image = coef_scale_image,
            .subresourceRange = subresource,
        },
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eNone,
            .srcAccessMask = vk::AccessFlagBits2::eNone,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .image = coef_usm_image,
            .subresourceRange = subresource,
        },
    };
    cmdbuf.pipelineBarrier2({
        .imageMemoryBarrierCount = pre_barriers.size(),
        .pImageMemoryBarriers = pre_barriers.data(),
    });

    // Copy buffer to images
    const vk::BufferImageCopy copy_region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .layerCount = 1,
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {kFilterSize / 4, kPhaseCount, 1},
    };
    cmdbuf.copyBufferToImage(staging_buffer, coef_scale_image,
                             vk::ImageLayout::eTransferDstOptimal, copy_region);

    const vk::BufferImageCopy usm_copy_region{
        .bufferOffset = coef_data_size,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .layerCount = 1,
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {kFilterSize / 4, kPhaseCount, 1},
    };
    cmdbuf.copyBufferToImage(staging_buffer, coef_usm_image, vk::ImageLayout::eTransferDstOptimal,
                             usm_copy_region);

    // Transition coefficient images to shader read optimal
    const std::array post_barriers{
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = coef_scale_image,
            .subresourceRange = subresource,
        },
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = coef_usm_image,
            .subresourceRange = subresource,
        },
    };
    cmdbuf.pipelineBarrier2({
        .imageMemoryBarrierCount = post_barriers.size(),
        .pImageMemoryBarriers = post_barriers.data(),
    });

    // Schedule staging buffer destruction (defer to avoid destroying while in use)
    // For simplicity, we'll destroy it immediately after the command buffer completes
    // In a production implementation, you'd want to defer this properly
    // For now, we leak this buffer slightly - TODO: proper cleanup
    coefficients_uploaded = true;
}

vk::ImageView NisPass::Render(vk::CommandBuffer cmdbuf, vk::ImageView input,
                              vk::Extent2D input_size, vk::Extent2D output_size, Settings settings,
                              bool hdr) {
    if (!settings.enable) {
        DebugState.is_using_nis = false;
        return input;
    }
    if (input_size.width >= output_size.width && input_size.height >= output_size.height) {
        DebugState.is_using_nis = false;
        return input;
    }

    DebugState.is_using_nis = true;

    // Ensure coefficient textures are created and uploaded
    CreateCoefficientsTextures();
    UploadCoefficients(cmdbuf);

    if (output_size != cur_size) {
        ResizeAndInvalidate(output_size.width, output_size.height);
    }
    auto [width, height] = cur_size;

    auto& img = available_imgs[cur_image];
    if (++cur_image >= available_imgs.size()) {
        cur_image = 0;
    }

    if (img.dirty) {
        CreateImages(img);
    }

    if (Config::getVkHostMarkersEnabled()) {
        cmdbuf.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
            .pLabelName = "Host/NIS",
        });
    }

    // Calculate dispatch size
    const u32 dispatch_x = (width + kNisBlockWidth - 1) / kNisBlockWidth;
    const u32 dispatch_y = (height + kNisBlockHeight - 1) / kNisBlockHeight;

    constexpr vk::ImageSubresourceRange simple_subresource = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = 1,
    };

    // Transition output image to general for compute write
    const std::array enter_barrier{
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eGeneral,
            .image = img.output_image,
            .subresourceRange = simple_subresource,
        },
    };
    cmdbuf.pipelineBarrier2({
        .imageMemoryBarrierCount = enter_barrier.size(),
        .pImageMemoryBarriers = enter_barrier.data(),
    });

    // Configure NIS and update UBO
    NISConfig config{};
    NVScalerUpdateConfig(config, settings.sharpness, 0, 0, input_size.width, input_size.height,
                         input_size.width, input_size.height, 0, 0, output_size.width,
                         output_size.height, output_size.width, output_size.height,
                         hdr ? NISHDRMode::Linear : NISHDRMode::None);

    // Copy config to mapped UBO
    std::memcpy(config_buffer_mapped, &config, sizeof(NISConfig));
    vmaFlushAllocation(allocator, config_buffer_allocation, 0, sizeof(NISConfig));

    // Bind pipeline
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, scaler_pipeline.get());

    // Prepare descriptor writes
    const vk::DescriptorBufferInfo buffer_info{
        .buffer = config_buffer,
        .offset = 0,
        .range = sizeof(NISConfig),
    };

    const std::array<vk::DescriptorImageInfo, 5> img_info{{
        {
            .sampler = sampler.get(),
        },
        {
            .imageView = input,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        },
        {
            .imageView = img.output_image_view.get(),
            .imageLayout = vk::ImageLayout::eGeneral,
        },
        {
            .imageView = coef_scale_image_view.get(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        },
        {
            .imageView = coef_usm_image_view.get(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        },
    }};

    const std::array<vk::WriteDescriptorSet, 6> set_writes{{
        {
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo = &buffer_info,
        },
        {
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .pImageInfo = &img_info[0],
        },
        {
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .pImageInfo = &img_info[1],
        },
        {
            .dstBinding = 3,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageImage,
            .pImageInfo = &img_info[2],
        },
        {
            .dstBinding = 4,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .pImageInfo = &img_info[3],
        },
        {
            .dstBinding = 5,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .pImageInfo = &img_info[4],
        },
    }};

    // Push descriptors and dispatch
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, pipeline_layout.get(), 0,
                                set_writes);
    cmdbuf.dispatch(dispatch_x, dispatch_y, 1);

    // Transition output image to shader read optimal
    const std::array return_barrier{
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = img.output_image,
            .subresourceRange = simple_subresource,
        },
    };
    cmdbuf.pipelineBarrier2({
        .imageMemoryBarrierCount = return_barrier.size(),
        .pImageMemoryBarriers = return_barrier.data(),
    });

    if (Config::getVkHostMarkersEnabled()) {
        cmdbuf.endDebugUtilsLabelEXT();
    }

    return img.output_image_view.get();
}

void NisPass::ResizeAndInvalidate(u32 width, u32 height) {
    this->cur_size = vk::Extent2D{
        .width = width,
        .height = height,
    };
    for (u32 i = 0; i < num_images; ++i) {
        available_imgs[i].dirty = true;
    }
}

void NisPass::CreateImages(Img& img) const {
    img.dirty = false;

    // Reset the image by assigning a fresh UniqueImage (destroys the old one)
    img.output_image = VideoCore::UniqueImage(device, allocator);
    img.output_image_view.reset();

    vk::ImageCreateInfo image_create_info{
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .extent{
            .width = cur_size.width,
            .height = cur_size.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled |
                 vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eColorAttachment,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    img.output_image.Create(image_create_info);
    SetObjectName(device, static_cast<vk::Image>(img.output_image), "NIS Output Image #{}", img.id);

    vk::ImageViewCreateInfo image_view_create_info{
        .image = img.output_image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    img.output_image_view =
        Check<"create nis output image view">(device.createImageViewUnique(image_view_create_info));
    SetObjectName(device, img.output_image_view.get(), "NIS Output ImageView #{}", img.id);
}

} // namespace Vulkan::HostPasses
