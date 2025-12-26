//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/config.h"
#include "common/logging/log.h"
#include "video_core/renderer_vulkan/host_passes/fsr2_pass.h"
#include "video_core/renderer_vulkan/vk_platform.h"

#include <vk_mem_alloc.h>

// TODO: Include FidelityFX SDK headers when integrated
// #include <FidelityFX/host/ffx_fsr2.h>
// #include <FidelityFX/host/backends/vk/ffx_vk.h>

namespace Vulkan::HostPasses {

//=============================================================================
// FSR 2 Pass Implementation
//=============================================================================

void Fsr2Pass::Create(vk::Device device, VmaAllocator allocator,
                      vk::PhysicalDevice physical_device, u32 max_render_width,
                      u32 max_render_height, u32 max_display_width, u32 max_display_height) {
    this->device = device;
    this->allocator = allocator;

    LOG_INFO(Render_Vulkan, "Initializing FSR 2 pass (max render: {}x{}, max display: {}x{})",
             max_render_width, max_render_height, max_display_width, max_display_height);

    // TODO: Initialize FidelityFX FSR 2 context
    // FfxFsr2ContextDescription fsr2_desc{};
    // fsr2_desc.flags = FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE |
    //                   FFX_FSR2_ENABLE_DEPTH_INVERTED |
    //                   FFX_FSR2_ENABLE_AUTO_EXPOSURE;
    // fsr2_desc.maxRenderSize.width = max_render_width;
    // fsr2_desc.maxRenderSize.height = max_render_height;
    // fsr2_desc.displaySize.width = max_display_width;
    // fsr2_desc.displaySize.height = max_display_height;
    // fsr2_desc.backendInterface = ffxGetInterfaceVK(...);
    //
    // FfxErrorCode error = ffxFsr2ContextCreate(&fsr2_context, &fsr2_desc);
    // is_available = (error == FFX_OK);

    // For now, mark as unavailable until SDK is integrated
    is_available = false;
    context_created = false;

    // Pre-allocate output images
    output_images.resize(2);
    for (u32 i = 0; i < output_images.size(); ++i) {
        output_images[i].id = static_cast<u8>(i);
        output_images[i].image = VideoCore::UniqueImage(device, allocator);
    }

    LOG_INFO(Render_Vulkan, "FSR 2 pass initialized (available: {})", is_available);
}

void Fsr2Pass::Destroy() {
    if (context_created && fsr2_context) {
        // TODO: Destroy FidelityFX context
        // ffxFsr2ContextDestroy(reinterpret_cast<FfxFsr2Context*>(fsr2_context));
        fsr2_context = nullptr;
        context_created = false;
    }
    output_images.clear();
}

vk::ImageView Fsr2Pass::Render(vk::CommandBuffer cmdbuf, vk::ImageView color, vk::ImageView depth,
                               vk::ImageView motion_vectors, vk::Extent2D input_size,
                               vk::Extent2D output_size, Settings settings, float delta_time,
                               bool reset) {
    if (!settings.enable || !is_available) {
        return color; // Pass through
    }

    // Resize output if needed
    if (output_size != this->output_size) {
        ResizeOutput(output_size);
    }

    auto& output = output_images[current_output];
    current_output = (current_output + 1) % output_images.size();

    if (output.dirty) {
        CreateOutputImages(output, output_size);
    }

    // Calculate jitter offset for temporal accumulation
    const float jitter_x = GetJitterOffset(jitter_index, kJitterPhaseCount, input_size.width);
    const float jitter_y = GetJitterOffset(jitter_index + 1, kJitterPhaseCount, input_size.height);
    jitter_index = (jitter_index + 1) % kJitterPhaseCount;

    // TODO: Dispatch FSR 2
    // FfxFsr2DispatchDescription dispatch_desc{};
    // dispatch_desc.commandList = ffxGetCommandListVK(cmdbuf);
    // dispatch_desc.color = ffxGetResourceVK(color, ...);
    // dispatch_desc.depth = ffxGetResourceVK(depth, ...);
    // dispatch_desc.motionVectors = ffxGetResourceVK(motion_vectors, ...);
    // dispatch_desc.output = ffxGetResourceVK(output.image, ...);
    // dispatch_desc.jitterOffset.x = jitter_x;
    // dispatch_desc.jitterOffset.y = jitter_y;
    // dispatch_desc.renderSize.width = input_size.width;
    // dispatch_desc.renderSize.height = input_size.height;
    // dispatch_desc.frameTimeDelta = delta_time * 1000.0f; // ms
    // dispatch_desc.reset = reset;
    // dispatch_desc.sharpness = settings.sharpness;
    //
    // ffxFsr2ContextDispatch(&fsr2_context, &dispatch_desc);

    return output.image_view.get();
}

vk::Extent2D Fsr2Pass::GetRenderResolution(vk::Extent2D display_size, Fsr2QualityMode mode) {
    float scale = 1.0f;
    switch (mode) {
    case Fsr2QualityMode::UltraPerformance:
        scale = 3.0f;
        break;
    case Fsr2QualityMode::Performance:
        scale = 2.0f;
        break;
    case Fsr2QualityMode::Balanced:
        scale = 1.7f;
        break;
    case Fsr2QualityMode::Quality:
        scale = 1.5f;
        break;
    case Fsr2QualityMode::NativeAA:
        scale = 1.0f;
        break;
    }

    return vk::Extent2D{
        .width = static_cast<u32>(display_size.width / scale),
        .height = static_cast<u32>(display_size.height / scale),
    };
}

void Fsr2Pass::CreateOutputImages(OutputImage& img, vk::Extent2D size) {
    img.dirty = false;
    img.image = VideoCore::UniqueImage(device, allocator);

    const vk::ImageCreateInfo image_ci{
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
        .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                 vk::ImageUsageFlagBits::eTransferSrc,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    img.image.Create(image_ci);
    SetObjectName(device, static_cast<vk::Image>(img.image), "FSR2 Output #{}", img.id);

    const vk::ImageViewCreateInfo view_ci{
        .image = img.image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    img.image_view = Check<"FSR2 output view">(device.createImageViewUnique(view_ci));
}

void Fsr2Pass::ResizeOutput(vk::Extent2D size) {
    this->output_size = size;
    for (auto& img : output_images) {
        img.dirty = true;
    }
}

// Helper function to compute Halton jitter sequence
static float GetJitterOffset(u32 index, u32 phase_count, u32 dimension) {
    // Halton sequence for jitter
    float result = 0.0f;
    float f = 1.0f;
    u32 i = index % phase_count;
    const u32 base = (dimension == 0) ? 2 : 3;

    while (i > 0) {
        f /= static_cast<float>(base);
        result += f * static_cast<float>(i % base);
        i /= base;
    }

    return result - 0.5f;
}

//=============================================================================
// Optical Flow Pass Implementation
//=============================================================================

void OpticalFlowPass::Create(vk::Device device, VmaAllocator allocator, u32 max_width,
                             u32 max_height) {
    this->device = device;
    this->allocator = allocator;

    LOG_INFO(Render_Vulkan, "Initializing Optical Flow pass (max: {}x{})", max_width, max_height);

    // TODO: Initialize FidelityFX Optical Flow context
    // FfxOpticalflowContextDescription of_desc{};
    // of_desc.backendInterface = ffxGetInterfaceVK(...);
    // of_desc.resolution.width = max_width;
    // of_desc.resolution.height = max_height;
    //
    // FfxErrorCode error = ffxOpticalflowContextCreate(&optical_flow_context, &of_desc);
    // is_available = (error == FFX_OK);

    is_available = false;
    context_created = false;

    motion_vector_images.resize(2);

    LOG_INFO(Render_Vulkan, "Optical Flow pass initialized (available: {})", is_available);
}

void OpticalFlowPass::Destroy() {
    if (context_created && optical_flow_context) {
        // TODO: Destroy FidelityFX context
        // ffxOpticalflowContextDestroy(...)
        optical_flow_context = nullptr;
        context_created = false;
    }
    motion_vector_images.clear();
}

vk::ImageView OpticalFlowPass::GenerateMotionVectors(vk::CommandBuffer cmdbuf,
                                                     vk::ImageView previous_frame,
                                                     vk::ImageView current_frame,
                                                     vk::Extent2D size, Settings settings) {
    if (!settings.enable || !is_available) {
        return {}; // No motion vectors available
    }

    if (size != current_size) {
        CreateMotionVectorImages(size);
    }

    auto& output = motion_vector_images[current_image];
    current_image = (current_image + 1) % motion_vector_images.size();

    // TODO: Dispatch optical flow
    // FfxOpticalflowDispatchDescription dispatch_desc{};
    // dispatch_desc.commandList = ffxGetCommandListVK(cmdbuf);
    // dispatch_desc.color = ffxGetResourceVK(current_frame, ...);
    // dispatch_desc.opticalFlowVector = ffxGetResourceVK(output.image, ...);
    // dispatch_desc.reset = false;
    //
    // ffxOpticalflowContextDispatch(&optical_flow_context, &dispatch_desc);

    return output.image_view.get();
}

void OpticalFlowPass::CreateMotionVectorImages(vk::Extent2D size) {
    current_size = size;

    for (u32 i = 0; i < motion_vector_images.size(); ++i) {
        auto& img = motion_vector_images[i];
        img.image = VideoCore::UniqueImage(device, allocator);

        // Motion vectors stored as RG16F (x, y displacement in pixels)
        const vk::ImageCreateInfo image_ci{
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
            .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
            .initialLayout = vk::ImageLayout::eUndefined,
        };
        img.image.Create(image_ci);
        SetObjectName(device, static_cast<vk::Image>(img.image), "Optical Flow MV #{}", i);

        const vk::ImageViewCreateInfo view_ci{
            .image = img.image,
            .viewType = vk::ImageViewType::e2D,
            .format = vk::Format::eR16G16Sfloat,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        img.image_view = Check<"Optical Flow MV view">(device.createImageViewUnique(view_ci));
    }
}

//=============================================================================
// Depth Estimation Pass Implementation
//=============================================================================

void DepthEstimationPass::Create(vk::Device device, VmaAllocator allocator,
                                 vk::PhysicalDevice physical_device, u32 max_width, u32 max_height,
                                 ModelSize model_size) {
    this->device = device;
    this->allocator = allocator;

    LOG_INFO(Render_Vulkan, "Initializing Depth Estimation pass (max: {}x{}, model: {})", max_width,
             max_height, static_cast<u32>(model_size));

    // TODO: Initialize ONNX Runtime or TensorRT for inference
    // - Load Depth Anything V2 model
    // - Create inference session
    // - Allocate GPU buffers

    is_available = false;
    model_loaded = false;

    depth_images.resize(2);

    // Create async fence
    async_fence = Check<"Depth async fence">(
        device.createFenceUnique({.flags = vk::FenceCreateFlagBits::eSignaled}));

    LOG_INFO(Render_Vulkan, "Depth Estimation pass initialized (available: {})", is_available);
}

void DepthEstimationPass::Destroy() {
    if (model_loaded && inference_session) {
        // TODO: Destroy ONNX/TensorRT session
        inference_session = nullptr;
        model_loaded = false;
    }
    depth_images.clear();
}

vk::ImageView DepthEstimationPass::EstimateDepth(vk::CommandBuffer cmdbuf, vk::ImageView color,
                                                 vk::Extent2D size, Settings settings) {
    if (!settings.enable || !is_available) {
        return {}; // No depth available
    }

    // Check if we should skip this frame (for estimation_rate > 1)
    frame_counter++;
    if (settings.estimation_rate > 1 && (frame_counter % settings.estimation_rate) != 0) {
        return GetCachedDepth();
    }

    if (size != current_size) {
        CreateDepthImages(size);
    }

    auto& output = depth_images[current_image];
    current_image = (current_image + 1) % depth_images.size();

    // TODO: Run depth estimation inference
    // 1. Copy color image to inference input buffer
    // 2. Run ONNX/TensorRT inference
    // 3. Copy output to depth image
    //
    // If async mode:
    // - Submit on separate queue/stream
    // - Return previous frame's depth
    // - Signal fence when complete

    return output.image_view.get();
}

vk::ImageView DepthEstimationPass::GetCachedDepth() const {
    if (depth_images.empty()) {
        return {};
    }
    // Return the most recently completed depth
    const u32 prev = (current_image == 0) ? depth_images.size() - 1 : current_image - 1;
    return depth_images[prev].image_view.get();
}

bool DepthEstimationPass::IsAsyncComplete() const {
    if (!async_pending) {
        return true;
    }
    return device.getFenceStatus(async_fence.get()) == vk::Result::eSuccess;
}

void DepthEstimationPass::CreateDepthImages(vk::Extent2D size) {
    current_size = size;

    for (u32 i = 0; i < depth_images.size(); ++i) {
        auto& img = depth_images[i];
        img.image = VideoCore::UniqueImage(device, allocator);

        // Depth stored as R32F (linear depth 0-1)
        const vk::ImageCreateInfo image_ci{
            .imageType = vk::ImageType::e2D,
            .format = vk::Format::eR32Sfloat,
            .extent{
                .width = size.width,
                .height = size.height,
                .depth = 1,
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                     vk::ImageUsageFlagBits::eTransferDst,
            .initialLayout = vk::ImageLayout::eUndefined,
        };
        img.image.Create(image_ci);
        SetObjectName(device, static_cast<vk::Image>(img.image), "Depth Estimation #{}", i);

        const vk::ImageViewCreateInfo view_ci{
            .image = img.image,
            .viewType = vk::ImageViewType::e2D,
            .format = vk::Format::eR32Sfloat,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        img.image_view = Check<"Depth Estimation view">(device.createImageViewUnique(view_ci));
    }
}

void DepthEstimationPass::LoadModel(ModelSize model_size) {
    // TODO: Load Depth Anything V2 model
    // const char* model_path = nullptr;
    // switch (model_size) {
    // case ModelSize::Small:
    //     model_path = "models/depth_anything_v2_vits.onnx";
    //     break;
    // case ModelSize::Base:
    //     model_path = "models/depth_anything_v2_vitb.onnx";
    //     break;
    // case ModelSize::Large:
    //     model_path = "models/depth_anything_v2_vitl.onnx";
    //     break;
    // }
    //
    // Initialize ONNX Runtime with CUDA/TensorRT EP
    // Ort::SessionOptions session_options;
    // session_options.AppendExecutionProvider_CUDA(...);
    // inference_session = new Ort::Session(env, model_path, session_options);

    model_loaded = false; // Will be true when implemented
}

} // namespace Vulkan::HostPasses

