//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/config.h"
#include "common/logging/log.h"
#include "core/debug_state.h"
#include "video_core/renderer_vulkan/host_passes/xess_pass.h"
#include "video_core/renderer_vulkan/vk_platform.h"

#include <vk_mem_alloc.h>

#ifdef XESS_AVAILABLE
#include <xess/xess.h>
#include <xess/xess_vk.h>
#endif

namespace Vulkan::HostPasses {

#ifdef XESS_AVAILABLE
static xess_quality_settings_t MapQualityMode(XeSSQualityMode mode) {
    switch (mode) {
    case XeSSQualityMode::UltraPerformance:
        return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
    case XeSSQualityMode::Performance:
        return XESS_QUALITY_SETTING_PERFORMANCE;
    case XeSSQualityMode::Balanced:
        return XESS_QUALITY_SETTING_BALANCED;
    case XeSSQualityMode::Quality:
        return XESS_QUALITY_SETTING_QUALITY;
    case XeSSQualityMode::UltraQuality:
        return XESS_QUALITY_SETTING_ULTRA_QUALITY;
    case XeSSQualityMode::NativeAA:
    default:
        return XESS_QUALITY_SETTING_AA;
    }
}

static const char* XeSSResultToString(xess_result_t result) {
    switch (result) {
    case XESS_RESULT_SUCCESS:
        return "Success";
    case XESS_RESULT_WARNING_OLD_DRIVER:
        return "Warning: Old driver";
    case XESS_RESULT_WARNING_NONEXISTING_FOLDER:
        return "Warning: Non-existing folder";
    case XESS_RESULT_ERROR_UNSUPPORTED_DEVICE:
        return "Unsupported device";
    case XESS_RESULT_ERROR_UNSUPPORTED_DRIVER:
        return "Unsupported driver";
    case XESS_RESULT_ERROR_UNINITIALIZED:
        return "Uninitialized";
    case XESS_RESULT_ERROR_INVALID_ARGUMENT:
        return "Invalid argument";
    case XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY:
        return "Out of memory";
    case XESS_RESULT_ERROR_DEVICE:
        return "Device error";
    case XESS_RESULT_ERROR_NOT_IMPLEMENTED:
        return "Not implemented";
    case XESS_RESULT_ERROR_INVALID_CONTEXT:
        return "Invalid context";
    case XESS_RESULT_ERROR_OPERATION_IN_PROGRESS:
        return "Operation in progress";
    case XESS_RESULT_ERROR_UNSUPPORTED:
        return "Unsupported";
    case XESS_RESULT_ERROR_CANT_LOAD_LIBRARY:
        return "Cannot load library";
    default:
        return "Unknown error";
    }
}
#endif

void XeSSPass::Create(vk::Device device, VmaAllocator allocator,
                      vk::PhysicalDevice physical_device, vk::Instance vulkan_instance,
                      u32 num_images) {
    this->device = device;
    this->allocator = allocator;
    this->physical_device = physical_device;
    this->vulkan_instance = vulkan_instance;

    LOG_INFO(Render_Vulkan, "Initializing XeSS pass");

#ifdef XESS_AVAILABLE
    // Get XeSS version
    xess_version_t version{};
    xess_result_t result = xessGetVersion(&version);
    if (result == XESS_RESULT_SUCCESS) {
        version_string = fmt::format("{}.{}.{}", version.major, version.minor, version.patch);
        LOG_INFO(Render_Vulkan, "XeSS SDK version: {}", version_string);
    }

    // Create XeSS context for Vulkan
    xess_context_handle_t context = nullptr;
    result = xessVKCreateContext(static_cast<VkInstance>(vulkan_instance),
                                 static_cast<VkPhysicalDevice>(physical_device),
                                 static_cast<VkDevice>(device), &context);

    if (result != XESS_RESULT_SUCCESS) {
        LOG_WARNING(Render_Vulkan, "Failed to create XeSS context: {} ({})",
                    XeSSResultToString(result), static_cast<int>(result));
        is_available = false;
        xess_context = nullptr;
    } else {
        xess_context = context;
        is_available = true;
        context_created = true;
        LOG_INFO(Render_Vulkan, "XeSS context created successfully");

        // Check for optimal driver
        result = xessIsOptimalDriver(context);
        if (result == XESS_RESULT_WARNING_OLD_DRIVER) {
            LOG_WARNING(Render_Vulkan, "XeSS: Using an older driver, performance may be degraded");
        }
    }
#else
    is_available = false;
    context_created = false;
    version_string = "SDK Not Available";
    LOG_INFO(Render_Vulkan, "XeSS SDK not compiled in");
#endif

    output_images.resize(num_images);
    for (u32 i = 0; i < num_images; ++i) {
        output_images[i].id = static_cast<u8>(i);
        output_images[i].image = VideoCore::UniqueImage(device, allocator);
    }

    LOG_INFO(Render_Vulkan, "XeSS pass initialized (available: {}, version: {})", is_available,
             version_string);
}

void XeSSPass::Destroy() {
#ifdef XESS_AVAILABLE
    if (context_created && xess_context) {
        xessDestroyContext(static_cast<xess_context_handle_t>(xess_context));
        xess_context = nullptr;
        context_created = false;
    }
#endif
    output_images.clear();
}

vk::ImageView XeSSPass::Render(vk::CommandBuffer cmdbuf, vk::Image color_image,
                               vk::ImageView color_view, vk::Format color_format,
                               vk::Extent2D input_size, vk::Extent2D output_size, Settings settings,
                               float delta_time, bool reset) {
    if (!settings.enable || !is_available) {
        DebugState.is_using_xess = false;
        return color_view; // Pass-through if disabled or unavailable
    }

    // Skip if input is already at or above output resolution
    if (input_size.width >= output_size.width && input_size.height >= output_size.height) {
        DebugState.is_using_xess = false;
        return color_view;
    }

    DebugState.is_using_xess = true;

#ifdef XESS_AVAILABLE
    auto context = static_cast<xess_context_handle_t>(xess_context);

    // Check if we need to reinitialize XeSS (resolution or quality changed)
    bool needs_init = !initialized_for_resolution ||
                      current_output_size != output_size ||
                      current_quality_mode != settings.quality_mode;

    if (needs_init) {
        // Initialize XeSS for the new resolution/quality
        // Note: Without real motion vectors from the game, XeSS will blur during movement
        // Using low-res motion vectors (default) may work slightly better with zero vectors
        xess_vk_init_params_t init_params{};
        init_params.outputResolution = {output_size.width, output_size.height};
        init_params.qualitySetting = MapQualityMode(settings.quality_mode);
        // Use auto exposure - motion vectors are at input resolution (low-res) by default
        // This works better than HIGH_RES_MV when providing zero motion vectors
        init_params.initFlags = XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;
        init_params.creationNodeMask = 1;
        init_params.visibleNodeMask = 1;
        init_params.tempBufferHeap = VK_NULL_HANDLE;
        init_params.tempTextureHeap = VK_NULL_HANDLE;
        init_params.pipelineCache = VK_NULL_HANDLE;

        xess_result_t result = xessVKInit(context, &init_params);
        if (result != XESS_RESULT_SUCCESS) {
            LOG_ERROR(Render_Vulkan, "Failed to initialize XeSS: {} ({})",
                      XeSSResultToString(result), static_cast<int>(result));
            initialized_for_resolution = false;
            DebugState.is_using_xess = false;
            return color_view;
        }

        // Set velocity scale to input resolution (low-res motion vectors)
        xessSetVelocityScale(context, static_cast<float>(input_size.width),
                             static_cast<float>(input_size.height));

        current_output_size = output_size;
        current_quality_mode = settings.quality_mode;
        initialized_for_resolution = true;

        // Get the expected scale factor for this quality mode
        const char* quality_name = "Unknown";
        float expected_scale = 1.0f;
        switch (settings.quality_mode) {
        case XeSSQualityMode::UltraPerformance:
            quality_name = "Ultra Performance";
            expected_scale = 3.0f;
            break;
        case XeSSQualityMode::Performance:
            quality_name = "Performance";
            expected_scale = 2.0f;
            break;
        case XeSSQualityMode::Balanced:
            quality_name = "Balanced";
            expected_scale = 1.7f;
            break;
        case XeSSQualityMode::Quality:
            quality_name = "Quality";
            expected_scale = 1.5f;
            break;
        case XeSSQualityMode::UltraQuality:
            quality_name = "Ultra Quality";
            expected_scale = 1.3f;
            break;
        case XeSSQualityMode::NativeAA:
            quality_name = "Native AA";
            expected_scale = 1.0f;
            break;
        }

        float actual_scale = static_cast<float>(output_size.width) / input_size.width;
        LOG_INFO(Render_Vulkan,
                 "XeSS initialized: {}x{} -> {}x{}, Quality: {} (expected {:.1f}x, actual {:.2f}x)",
                 input_size.width, input_size.height, output_size.width, output_size.height,
                 quality_name, expected_scale, actual_scale);
    }
    
    // Safety check - if initialization failed previously, don't try to execute
    if (!initialized_for_resolution) {
        DebugState.is_using_xess = false;
        return color_view;
    }

    // Resize output if needed
    if (output_size != current_output_size) {
        ResizeOutput(output_size);
    }

    auto& output = output_images[current_output];
    if (output.dirty) {
        CreateOutputImages(output, output_size);
    }

    // Create dummy motion vectors at input resolution (low-res, default)
    CreateDummyMotionVectors(input_size);

    current_output = (current_output + 1) % output_images.size();
    last_delta_time = delta_time;
    frame_index++;

    if (Config::getVkHostMarkersEnabled()) {
        cmdbuf.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
            .pLabelName = "Host/XeSS",
        });
    }

    constexpr vk::ImageSubresourceRange simple_subresource = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = 1,
    };

    // Transition output image to general for compute write
    const auto output_barrier = vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = output.image,
        .subresourceRange = simple_subresource,
    };

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &output_barrier,
    });

    // Transition motion vectors to transfer dst for clearing
    const auto mv_clear_barrier = vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eNone,
        .srcAccessMask = vk::AccessFlagBits2::eNone,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .image = motion_vector_image,
        .subresourceRange = simple_subresource,
    };

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &mv_clear_barrier,
    });

    // Clear motion vectors to zero (no motion)
    const vk::ClearColorValue zero_motion{std::array{0.0f, 0.0f, 0.0f, 0.0f}};
    cmdbuf.clearColorImage(motion_vector_image, vk::ImageLayout::eTransferDstOptimal,
                           zero_motion, simple_subresource);

    // Transition motion vectors to shader read
    const auto mv_read_barrier = vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .image = motion_vector_image,
        .subresourceRange = simple_subresource,
    };

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &mv_read_barrier,
    });

    // Prepare XeSS execution parameters
    xess_vk_execute_params_t exec_params{};
    memset(&exec_params, 0, sizeof(exec_params));
    
    // Color input - need both image and imageView
    exec_params.colorTexture.imageView = static_cast<VkImageView>(color_view);
    exec_params.colorTexture.image = static_cast<VkImage>(color_image);
    exec_params.colorTexture.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    exec_params.colorTexture.format = static_cast<VkFormat>(color_format);
    exec_params.colorTexture.width = input_size.width;
    exec_params.colorTexture.height = input_size.height;

    // Motion vectors (dummy zero-motion at input resolution - low-res)
    exec_params.velocityTexture.imageView = static_cast<VkImageView>(motion_vector_view.get());
    exec_params.velocityTexture.image = static_cast<VkImage>(static_cast<vk::Image>(motion_vector_image));
    exec_params.velocityTexture.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    exec_params.velocityTexture.format = VK_FORMAT_R16G16_SFLOAT;
    exec_params.velocityTexture.width = input_size.width;
    exec_params.velocityTexture.height = input_size.height;

    // Output
    exec_params.outputTexture.imageView = static_cast<VkImageView>(output.image_view.get());
    exec_params.outputTexture.image = static_cast<VkImage>(static_cast<vk::Image>(output.image));
    exec_params.outputTexture.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    exec_params.outputTexture.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    exec_params.outputTexture.width = output_size.width;
    exec_params.outputTexture.height = output_size.height;

    // Execution parameters
    exec_params.jitterOffsetX = 0.0f; // No jitter available from game
    exec_params.jitterOffsetY = 0.0f;
    exec_params.exposureScale = 1.0f;
    exec_params.resetHistory = (reset || frame_index == 1) ? 1 : 0;
    exec_params.inputWidth = input_size.width;
    exec_params.inputHeight = input_size.height;

    xess_result_t result = xessVKExecute(context, static_cast<VkCommandBuffer>(cmdbuf), &exec_params);
    if (result != XESS_RESULT_SUCCESS) {
        LOG_ERROR(Render_Vulkan, "XeSS execution failed: {} ({})",
                  XeSSResultToString(result), static_cast<int>(result));
        if (Config::getVkHostMarkersEnabled()) {
            cmdbuf.endDebugUtilsLabelEXT();
        }
        DebugState.is_using_xess = false;
        return color_view; // Fall back to input on failure
    }

    // Transition output image to shader read optimal
    const auto post_barrier = vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .image = output.image,
        .subresourceRange = simple_subresource,
    };

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &post_barrier,
    });

    if (Config::getVkHostMarkersEnabled()) {
        cmdbuf.endDebugUtilsLabelEXT();
    }

    return output.image_view.get();
#else
    // XeSS not available, pass through
    DebugState.is_using_xess = false;
    return color_view;
#endif
}

vk::Extent2D XeSSPass::GetRenderResolution(vk::Extent2D display_size, XeSSQualityMode mode) {
    float scale;
    switch (mode) {
    case XeSSQualityMode::UltraPerformance:
        scale = 3.0f;
        break;
    case XeSSQualityMode::Performance:
        scale = 2.0f;
        break;
    case XeSSQualityMode::Balanced:
        scale = 1.7f;
        break;
    case XeSSQualityMode::Quality:
        scale = 1.5f;
        break;
    case XeSSQualityMode::UltraQuality:
        scale = 1.3f;
        break;
    case XeSSQualityMode::NativeAA:
    default:
        scale = 1.0f;
        break;
    }

    return vk::Extent2D{
        .width = static_cast<u32>(display_size.width / scale),
        .height = static_cast<u32>(display_size.height / scale),
    };
}

void XeSSPass::CreateOutputImages(OutputImage& img, vk::Extent2D size) {
    img.dirty = false;

    // Reset the image by assigning a fresh UniqueImage (destroys the old one)
    img.image = VideoCore::UniqueImage(device, allocator);
    img.image_view.reset();

    const vk::ImageCreateInfo image_info{
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .extent{
            .width = size.width,
            .height = size.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled |
                 vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eColorAttachment,
        .initialLayout = vk::ImageLayout::eUndefined,
    };

    img.image.Create(image_info);
    SetObjectName(device, static_cast<vk::Image>(img.image), "XeSS Output #{}", img.id);

    const vk::ImageViewCreateInfo view_info{
        .image = img.image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = 1,
        },
    };

    img.image_view = Check<"XeSS output view">(device.createImageViewUnique(view_info));
    SetObjectName(device, img.image_view.get(), "XeSS Output View #{}", img.id);
}

void XeSSPass::ResizeOutput(vk::Extent2D size) {
    current_output_size = size;
    for (auto& img : output_images) {
        img.dirty = true;
    }
}

#ifdef XESS_AVAILABLE
void XeSSPass::CreateDummyMotionVectors(vk::Extent2D size) {
    if (motion_vectors_initialized && motion_vector_size == size) {
        return; // Already created at this size
    }

    // Reset the image
    motion_vector_image = VideoCore::UniqueImage(device, allocator);
    motion_vector_view.reset();

    // Create RG16F image for motion vectors (2 components: X and Y motion)
    // Needs TransferDst for clearing to zero
    const vk::ImageCreateInfo image_info{
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR16G16Sfloat,
        .extent{
            .width = size.width,
            .height = size.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                 vk::ImageUsageFlagBits::eStorage,
        .initialLayout = vk::ImageLayout::eUndefined,
    };

    motion_vector_image.Create(image_info);
    SetObjectName(device, static_cast<vk::Image>(motion_vector_image), "XeSS Dummy Motion Vectors");

    const vk::ImageViewCreateInfo view_info{
        .image = motion_vector_image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR16G16Sfloat,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = 1,
        },
    };

    motion_vector_view = Check<"XeSS MV view">(device.createImageViewUnique(view_info));
    SetObjectName(device, motion_vector_view.get(), "XeSS Dummy Motion Vectors View");

    motion_vector_size = size;
    motion_vectors_initialized = true;

    LOG_INFO(Render_Vulkan, "Created XeSS dummy motion vector texture: {}x{}", size.width, size.height);
}
#else
void XeSSPass::CreateDummyMotionVectors(vk::Extent2D) {}
#endif

} // namespace Vulkan::HostPasses
