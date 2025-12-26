//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/config.h"
<<<<<<< Updated upstream
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
=======
#include "video_core/renderer_vulkan/host_passes/xess_pass.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

#include <vk_mem_alloc.h>
#include <xess/xess.h>

#include "core/debug_state.h"

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
>>>>>>> Stashed changes
    }

    DebugState.is_using_xess = true;

<<<<<<< Updated upstream
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

=======
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

>>>>>>> Stashed changes
    if (Config::getVkHostMarkersEnabled()) {
        cmdbuf.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
            .pLabelName = "Host/XeSS",
        });
    }

<<<<<<< Updated upstream
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
=======
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
>>>>>>> Stashed changes
    exec_params.colorTexture.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
<<<<<<< Updated upstream
    exec_params.colorTexture.format = static_cast<VkFormat>(color_format);
    exec_params.colorTexture.width = input_size.width;
    exec_params.colorTexture.height = input_size.height;

    // Motion vectors (dummy zero-motion at input resolution - low-res)
    exec_params.velocityTexture.imageView = static_cast<VkImageView>(motion_vector_view.get());
    exec_params.velocityTexture.image = static_cast<VkImage>(static_cast<vk::Image>(motion_vector_image));
=======

    // Use dummy velocity and depth textures
    exec_params.velocityTexture.imageView =
        static_cast<VkImageView>(dummy_velocity_image_view.get());
    exec_params.velocityTexture.image = static_cast<VkImage>(dummy_velocity_image.image);
    exec_params.velocityTexture.format = VK_FORMAT_R16G16_SFLOAT;
    exec_params.velocityTexture.width = 1;
    exec_params.velocityTexture.height = 1;
>>>>>>> Stashed changes
    exec_params.velocityTexture.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
<<<<<<< Updated upstream
    exec_params.velocityTexture.format = VK_FORMAT_R16G16_SFLOAT;
    exec_params.velocityTexture.width = input_size.width;
    exec_params.velocityTexture.height = input_size.height;

    // Output
    exec_params.outputTexture.imageView = static_cast<VkImageView>(output.image_view.get());
    exec_params.outputTexture.image = static_cast<VkImage>(static_cast<vk::Image>(output.image));
=======

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
>>>>>>> Stashed changes
    exec_params.outputTexture.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
<<<<<<< Updated upstream
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
=======

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
>>>>>>> Stashed changes
    });

    if (Config::getVkHostMarkersEnabled()) {
        cmdbuf.endDebugUtilsLabelEXT();
    }

<<<<<<< Updated upstream
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
=======
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
>>>>>>> Stashed changes
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled |
                 vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eColorAttachment,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
<<<<<<< Updated upstream

    img.image.Create(image_info);
    SetObjectName(device, static_cast<vk::Image>(img.image), "XeSS Output #{}", img.id);

    const vk::ImageViewCreateInfo view_info{
        .image = img.image,
=======
    img.output_image.Create(image_create_info);
    SetObjectName(device, static_cast<vk::Image>(img.output_image), "XeSS Output Image #{}",
                  img.id);

    vk::ImageViewCreateInfo image_view_create_info{
        .image = img.output_image,
>>>>>>> Stashed changes
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
<<<<<<< Updated upstream

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
=======
    img.output_image_view =
        Check<"create xess output image view">(device.createImageViewUnique(image_view_create_info));
    SetObjectName(device, img.output_image_view.get(), "XeSS Output ImageView #{}", img.id);
}

} // namespace Vulkan::HostPasses

>>>>>>> Stashed changes
