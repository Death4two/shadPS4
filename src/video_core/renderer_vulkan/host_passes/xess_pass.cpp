//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/config.h"
#include "common/logging/log.h"
#include "core/debug_state.h"
#include "video_core/renderer_vulkan/host_passes/xess_pass.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

#include <vk_mem_alloc.h>
#include <xess/xess.h>
#include <xess/xess_vk.h>

namespace Vulkan::HostPasses {

namespace {
// Generate Halton sequence for jitter
float GetCorput(u32 index, u32 base) {
    float result = 0.0f;
    float bk = 1.0f;

    while (index > 0) {
        bk /= static_cast<float>(base);
        result += static_cast<float>(index % base) * bk;
        index /= base;
    }

    return result;
}

std::vector<std::pair<float, float>> GenerateHalton(u32 base1, u32 base2, u32 start_index,
                                                     u32 count, float offset1 = -0.5f,
                                                     float offset2 = -0.5f) {
    std::vector<std::pair<float, float>> result;
    result.reserve(count);

    for (u32 a = start_index; a < count + start_index; ++a) {
        result.emplace_back(GetCorput(a, base1) + offset1, GetCorput(a, base2) + offset2);
    }
    return result;
}
} // namespace

void XessPass::Create(vk::Device device, VmaAllocator allocator, vk::Instance instance,
                      vk::PhysicalDevice physical_device, u32 num_images) {
    this->device = device;
    this->allocator = allocator;
    this->instance = instance;
    this->physical_device = physical_device;
    this->num_images = num_images;

    // Generate Halton sequence for jitter (using bases 2 and 3, as recommended)
    halton_sequence = GenerateHalton(2, 3, 1, kJitterSequenceLength);

    available_imgs.resize(num_images);
    for (u32 i = 0; i < num_images; ++i) {
        auto& img = available_imgs[i];
        img.id = static_cast<u8>(i);
        img.output_image = VideoCore::UniqueImage(device, allocator);
    }
}

void XessPass::Destroy() {
    if (xess_context) {
        xessDestroyContext(xess_context);
        xess_context = nullptr;
    }
    xess_initialized = false;
}

void XessPass::CreateDummyTextures(vk::CommandBuffer cmdbuf) {
    if (dummy_velocity_image_view && dummy_depth_image_view) {
        return; // Already created
    }

    constexpr vk::Extent2D dummy_size{1, 1};

    // Create dummy velocity texture (RG16F - motion vectors)
    {
        const vk::ImageCreateInfo image_info{
            .imageType = vk::ImageType::e2D,
            .format = vk::Format::eR16G16Sfloat,
            .extent{
                .width = dummy_size.width,
                .height = dummy_size.height,
                .depth = 1,
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            .initialLayout = vk::ImageLayout::eUndefined,
        };

        dummy_velocity_image = VideoCore::UniqueImage(device, allocator);
        dummy_velocity_image.Create(image_info);
        SetObjectName(device, static_cast<vk::Image>(dummy_velocity_image),
                      "XeSS Dummy Velocity Image");

        const vk::ImageViewCreateInfo view_info{
            .image = dummy_velocity_image,
            .viewType = vk::ImageViewType::e2D,
            .format = vk::Format::eR16G16Sfloat,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        dummy_velocity_image_view =
            Check<"create xess dummy velocity image view">(device.createImageViewUnique(view_info));
        SetObjectName(device, dummy_velocity_image_view.get(), "XeSS Dummy Velocity ImageView");

        // Transition to shader read optimal
        const vk::ImageSubresourceRange subresource{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = 1,
        };
        const vk::ImageMemoryBarrier2 barrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eNone,
            .srcAccessMask = vk::AccessFlagBits2::eNone,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = dummy_velocity_image,
            .subresourceRange = subresource,
        };
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
    }

    // Create dummy depth texture (R32Sfloat - depth)
    {
        const vk::ImageCreateInfo image_info{
            .imageType = vk::ImageType::e2D,
            .format = vk::Format::eR32Sfloat,
            .extent{
                .width = dummy_size.width,
                .height = dummy_size.height,
                .depth = 1,
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            .initialLayout = vk::ImageLayout::eUndefined,
        };

        dummy_depth_image = VideoCore::UniqueImage(device, allocator);
        dummy_depth_image.Create(image_info);
        SetObjectName(device, static_cast<vk::Image>(dummy_depth_image), "XeSS Dummy Depth Image");

        const vk::ImageViewCreateInfo view_info{
            .image = dummy_depth_image,
            .viewType = vk::ImageViewType::e2D,
            .format = vk::Format::eR32Sfloat,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        dummy_depth_image_view =
            Check<"create xess dummy depth image view">(device.createImageViewUnique(view_info));
        SetObjectName(device, dummy_depth_image_view.get(), "XeSS Dummy Depth ImageView");

        // Transition to shader read optimal
        const vk::ImageSubresourceRange subresource{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = 1,
        };
        const vk::ImageMemoryBarrier2 barrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eNone,
            .srcAccessMask = vk::AccessFlagBits2::eNone,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = dummy_depth_image,
            .subresourceRange = subresource,
        };
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
    }
}

void XessPass::InitializeXess(vk::Extent2D output_size, Settings settings) {
    if (xess_initialized && xess_output_size == output_size) {
        return; // Already initialized with correct size
    }

    // Validate output size
    if (output_size.width == 0 || output_size.height == 0) {
        LOG_ERROR(Render_Vulkan, "Invalid XeSS output size: {}x{}", output_size.width,
                  output_size.height);
        return;
    }

    // Destroy old context if it exists
    if (xess_context) {
        xessDestroyContext(xess_context);
        xess_context = nullptr;
    }

    // Create XeSS context
    xess_result_t result = xessVKCreateContext(
        static_cast<VkInstance>(instance), static_cast<VkPhysicalDevice>(physical_device),
        static_cast<VkDevice>(device), &xess_context);
    if (result != XESS_RESULT_SUCCESS) {
        LOG_ERROR(Render_Vulkan, "Failed to create XeSS context: {}", static_cast<int>(result));
        xess_context = nullptr;
        return;
    }

    // Validate quality setting
    const xess_quality_settings_t quality_setting =
        static_cast<xess_quality_settings_t>(settings.quality_mode);
    if (quality_setting < XESS_QUALITY_SETTING_ULTRA_PERFORMANCE ||
        quality_setting > XESS_QUALITY_SETTING_AA) {
        LOG_ERROR(Render_Vulkan, "Invalid XeSS quality setting: {}",
                  static_cast<int>(settings.quality_mode));
        xessDestroyContext(xess_context);
        xess_context = nullptr;
        return;
    }

    // Initialize XeSS - use C-style initialization to match the sample
    xess_vk_init_params_t init_params = {
        /* Output width and height */
        {output_size.width, output_size.height},
        /* Quality setting */
        quality_setting,
        /* Initialization flags - we don't have high-res motion vectors */
        XESS_INIT_FLAG_NONE,
        /* Node mask for internally created resources on multi-adapter systems */
        0,
        /* Node visibility mask for internally created resources on multi-adapter systems */
        0,
        /* Optional externally allocated buffer memory for XeSS */
        VK_NULL_HANDLE,
        /* Offset in the externally allocated memory for temporary buffer storage */
        0,
        /* Optional externally allocated texture memory for XeSS */
        VK_NULL_HANDLE,
        /* Offset in the externally allocated memory for temporary texture storage */
        0,
        /* Optional pipeline cache */
        VK_NULL_HANDLE,
    };

    result = xessVKInit(xess_context, &init_params);
    if (result != XESS_RESULT_SUCCESS) {
        LOG_ERROR(Render_Vulkan,
                  "Failed to initialize XeSS: {} (output: {}x{}, quality: {}, flags: {})",
                  static_cast<int>(result), output_size.width, output_size.height,
                  static_cast<int>(quality_setting), init_params.initFlags);
        xessDestroyContext(xess_context);
        xess_context = nullptr;
        return;
    }

    // Set velocity scale to 0.0 to minimize impact of zero motion vectors
    // This helps prevent artifacts when using dummy motion vectors
    xessSetVelocityScale(xess_context, 0.0f, 0.0f);

    xess_initialized = true;
    xess_output_size = output_size;
}

void XessPass::GetJitterOffset(float& jitter_x, float& jitter_y, u32 width, u32 height) {
    if (halton_sequence.empty()) {
        jitter_x = 0.0f;
        jitter_y = 0.0f;
        return;
    }

    const auto& [halton_x, halton_y] = halton_sequence[jitter_index % halton_sequence.size()];
    jitter_x = halton_x;
    jitter_y = halton_y;
    jitter_index = (jitter_index + 1) % halton_sequence.size();
}

vk::ImageView XessPass::Render(vk::CommandBuffer cmdbuf, vk::ImageView input, vk::Image input_image,
                                vk::Extent2D input_size, vk::Extent2D output_size,
                                Settings settings, bool hdr) {
    if (!settings.enable) {
        DebugState.is_using_xess = false;
        return input;
    }
    if (input_size.width >= output_size.width && input_size.height >= output_size.height) {
        DebugState.is_using_xess = false;
        return input;
    }

    DebugState.is_using_xess = true;

    // Initialize XeSS if needed
    InitializeXess(output_size, settings);

    if (!xess_context || !xess_initialized) {
        LOG_WARNING(Render_Vulkan, "XeSS not available, falling back to input");
        return input;
    }

    // Create dummy textures if needed
    CreateDummyTextures(cmdbuf);

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
            .pLabelName = "Host/XeSS",
        });
    }

    // Get jitter offset
    float jitter_x, jitter_y;
    GetJitterOffset(jitter_x, jitter_y, input_size.width, input_size.height);

    // Prepare XeSS execute parameters
    xess_vk_execute_params_t exec_params{};
    exec_params.colorTexture.imageView = static_cast<VkImageView>(input);
    exec_params.colorTexture.image = static_cast<VkImage>(input_image);
    exec_params.colorTexture.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    exec_params.colorTexture.width = input_size.width;
    exec_params.colorTexture.height = input_size.height;
    exec_params.colorTexture.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    // Use dummy velocity and depth textures
    exec_params.velocityTexture.imageView =
        static_cast<VkImageView>(dummy_velocity_image_view.get());
    exec_params.velocityTexture.image = static_cast<VkImage>(dummy_velocity_image.image);
    exec_params.velocityTexture.format = VK_FORMAT_R16G16_SFLOAT;
    exec_params.velocityTexture.width = 1;
    exec_params.velocityTexture.height = 1;
    exec_params.velocityTexture.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    exec_params.depthTexture.imageView = static_cast<VkImageView>(dummy_depth_image_view.get());
    exec_params.depthTexture.image = static_cast<VkImage>(dummy_depth_image.image);
    exec_params.depthTexture.format = VK_FORMAT_R32_SFLOAT;
    exec_params.depthTexture.width = 1;
    exec_params.depthTexture.height = 1;
    exec_params.depthTexture.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    exec_params.outputTexture.imageView = static_cast<VkImageView>(img.output_image_view.get());
    exec_params.outputTexture.image = static_cast<VkImage>(img.output_image.image);
    exec_params.outputTexture.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    exec_params.outputTexture.width = width;
    exec_params.outputTexture.height = height;
    exec_params.outputTexture.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    exec_params.jitterOffsetX = jitter_x;
    exec_params.jitterOffsetY = jitter_y;
    exec_params.exposureScale = 1.0f;
    exec_params.resetHistory = 0; // Don't reset history
    exec_params.inputWidth = input_size.width;
    exec_params.inputHeight = input_size.height;

    // Transition output image to general layout for XeSS
    constexpr vk::ImageSubresourceRange simple_subresource = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = 1,
    };

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

    // Execute XeSS
    xess_result_t result = xessVKExecute(xess_context, static_cast<VkCommandBuffer>(cmdbuf),
                                          &exec_params);
    if (result != XESS_RESULT_SUCCESS) {
        LOG_ERROR(Render_Vulkan, "XeSS execute failed: {}", static_cast<int>(result));
        if (Config::getVkHostMarkersEnabled()) {
            cmdbuf.endDebugUtilsLabelEXT();
        }
        return input;
    }

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

void XessPass::ResizeAndInvalidate(u32 width, u32 height) {
    this->cur_size = vk::Extent2D{
        .width = width,
        .height = height,
    };
    for (u32 i = 0; i < num_images; ++i) {
        available_imgs[i].dirty = true;
    }
}

void XessPass::CreateImages(Img& img) const {
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
    SetObjectName(device, static_cast<vk::Image>(img.output_image), "XeSS Output Image #{}",
                  img.id);

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
        Check<"create xess output image view">(device.createImageViewUnique(image_view_create_info));
    SetObjectName(device, img.output_image_view.get(), "XeSS Output ImageView #{}", img.id);
}

} // namespace Vulkan::HostPasses
