//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

namespace Vulkan::HostPasses {

/// FSR 2 Quality Modes
enum class Fsr2QualityMode : u32 {
    UltraPerformance = 0, // 3.0x scale
    Performance = 1,      // 2.0x scale
    Balanced = 2,         // 1.7x scale
    Quality = 3,          // 1.5x scale
    NativeAA = 4,         // 1.0x scale (anti-aliasing only)
};

/// FSR 2 Temporal Upscaling Pass
/// Requires: Color, Depth, Motion Vectors
class Fsr2Pass {
public:
    struct Settings {
        bool enable{false};
        Fsr2QualityMode quality_mode{Fsr2QualityMode::Balanced};
        float sharpness{0.5f}; // 0.0 to 1.0
        bool hdr{false};
    };

    /// Initialize FSR 2 context and resources
    void Create(vk::Device device, VmaAllocator allocator, vk::PhysicalDevice physical_device,
                u32 max_render_width, u32 max_render_height, u32 max_display_width,
                u32 max_display_height);

    /// Destroy FSR 2 context
    void Destroy();

    /// Perform temporal upscaling
    /// @param cmdbuf Command buffer to record commands
    /// @param color Input color image (rendered at lower resolution)
    /// @param depth Input depth image (same resolution as color)
    /// @param motion_vectors Per-pixel motion vectors (same resolution as color)
    /// @param input_size Size of input images
    /// @param output_size Desired output size
    /// @param settings FSR 2 settings
    /// @param delta_time Time since last frame in seconds
    /// @param reset Reset temporal history (on scene change, camera cut, etc.)
    /// @return Upscaled image view
    vk::ImageView Render(vk::CommandBuffer cmdbuf, vk::ImageView color, vk::ImageView depth,
                         vk::ImageView motion_vectors, vk::Extent2D input_size,
                         vk::Extent2D output_size, Settings settings, float delta_time,
                         bool reset = false);

    /// Get the render resolution for a given display resolution and quality mode
    static vk::Extent2D GetRenderResolution(vk::Extent2D display_size, Fsr2QualityMode mode);

    /// Check if FSR 2 is available on this device
    bool IsAvailable() const {
        return is_available;
    }

private:
    struct OutputImage {
        u8 id{};
        bool dirty{true};
        VideoCore::UniqueImage image;
        vk::UniqueImageView image_view;
    };

    void CreateOutputImages(OutputImage& img, vk::Extent2D size);
    void ResizeOutput(vk::Extent2D size);

    vk::Device device{};
    VmaAllocator allocator{};
    bool is_available{false};
    bool context_created{false};

    // FSR 2 context handle (opaque, will be FfxFsr2Context*)
    void* fsr2_context{nullptr};

    // Output images (double buffered)
    vk::Extent2D output_size{};
    u32 current_output{0};
    std::vector<OutputImage> output_images;

    // Jitter sequence for temporal accumulation
    u32 jitter_index{0};
    static constexpr u32 kJitterPhaseCount = 32;
};

/// Optical Flow Pass - Generates motion vectors from consecutive frames
class OpticalFlowPass {
public:
    struct Settings {
        bool enable{true};
        bool high_quality{true}; // Use higher quality estimation
    };

    /// Initialize optical flow context
    void Create(vk::Device device, VmaAllocator allocator, u32 max_width, u32 max_height);

    /// Destroy optical flow context
    void Destroy();

    /// Generate motion vectors from two consecutive frames
    /// @param cmdbuf Command buffer
    /// @param previous_frame Previous frame color
    /// @param current_frame Current frame color
    /// @param size Frame dimensions
    /// @param settings Optical flow settings
    /// @return Motion vector image view (RG16F format, pixel displacement in pixels)
    vk::ImageView GenerateMotionVectors(vk::CommandBuffer cmdbuf, vk::ImageView previous_frame,
                                        vk::ImageView current_frame, vk::Extent2D size,
                                        Settings settings);

    bool IsAvailable() const {
        return is_available;
    }

private:
    struct MotionVectorImage {
        VideoCore::UniqueImage image;
        vk::UniqueImageView image_view;
    };

    void CreateMotionVectorImages(vk::Extent2D size);

    vk::Device device{};
    VmaAllocator allocator{};
    bool is_available{false};
    bool context_created{false};

    // Optical flow context (opaque, will be FfxOpticalflowContext*)
    void* optical_flow_context{nullptr};

    // Motion vector output
    vk::Extent2D current_size{};
    u32 current_image{0};
    std::vector<MotionVectorImage> motion_vector_images;
};

/// Depth Estimation Pass - Generates depth from a single color image using AI
class DepthEstimationPass {
public:
    enum class ModelSize : u32 {
        Small = 0,  // Fastest, ~3-5ms on RTX 5090
        Base = 1,   // Balanced, ~6-10ms on RTX 5090
        Large = 2,  // Best quality, ~15-25ms on RTX 5090
    };

    struct Settings {
        bool enable{true};
        ModelSize model_size{ModelSize::Base};
        bool async{true};           // Run depth estimation asynchronously
        float resolution_scale{1.0f}; // Scale factor for depth estimation (0.5 = half res)
        u32 estimation_rate{1};     // Estimate every N frames (1 = every frame)
    };

    /// Initialize depth estimation model
    void Create(vk::Device device, VmaAllocator allocator, vk::PhysicalDevice physical_device,
                u32 max_width, u32 max_height, ModelSize model_size);

    /// Destroy depth estimation context
    void Destroy();

    /// Estimate depth from a color image
    /// @param cmdbuf Command buffer
    /// @param color Input color image
    /// @param size Image dimensions
    /// @param settings Depth estimation settings
    /// @return Depth image view (R32F format, linear depth 0-1)
    vk::ImageView EstimateDepth(vk::CommandBuffer cmdbuf, vk::ImageView color, vk::Extent2D size,
                                Settings settings);

    /// Get the last estimated depth (for async mode)
    vk::ImageView GetCachedDepth() const;

    bool IsAvailable() const {
        return is_available;
    }

    /// Check if async depth estimation is complete
    bool IsAsyncComplete() const;

private:
    struct DepthImage {
        VideoCore::UniqueImage image;
        vk::UniqueImageView image_view;
    };

    void CreateDepthImages(vk::Extent2D size);
    void LoadModel(ModelSize model_size);

    vk::Device device{};
    VmaAllocator allocator{};
    bool is_available{false};
    bool model_loaded{false};

    // Inference context (will be ONNX Runtime or TensorRT session)
    void* inference_session{nullptr};

    // Depth output
    vk::Extent2D current_size{};
    u32 current_image{0};
    u32 frame_counter{0};
    std::vector<DepthImage> depth_images;

    // Async execution
    vk::UniqueFence async_fence;
    bool async_pending{false};
};

} // namespace Vulkan::HostPasses

