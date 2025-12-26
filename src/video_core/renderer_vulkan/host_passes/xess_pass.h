//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

namespace Vulkan::HostPasses {

/// XeSS Quality Modes
enum class XeSSQualityMode : u32 {
    UltraPerformance = 0, // ~3.0x scale
    Performance = 1,      // ~2.0x scale
    Balanced = 2,         // ~1.7x scale
    Quality = 3,          // ~1.5x scale
    UltraQuality = 4,     // ~1.3x scale
    NativeAA = 5,         // 1.0x scale (anti-aliasing only)
};

/// Intel XeSS (Xe Super Sampling) Upscaling Pass
/// Uses AI-powered temporal upscaling for enhanced image quality
class XeSSPass {
public:
    struct Settings {
        bool enable{false};
        XeSSQualityMode quality_mode{XeSSQualityMode::Balanced};
        // Note: XeSS doesn't have a sharpness parameter - it's a neural network upscaler
        // The quality mode affects which AI model is used and expected upscale ratio
    };

    /// Initialize XeSS context and resources
    void Create(vk::Device device, VmaAllocator allocator, vk::PhysicalDevice physical_device,
                vk::Instance vulkan_instance, u32 num_images);

    /// Destroy XeSS context and resources
    void Destroy();

    /// Perform XeSS upscaling
    /// @param cmdbuf Command buffer to record commands
    /// @param color_image Input color image (rendered at lower resolution)
    /// @param color_view Input color image view
    /// @param color_format Format of the input color image
    /// @param input_size Size of input image
    /// @param output_size Desired output size
    /// @param settings XeSS settings
    /// @param delta_time Time since last frame in seconds
    /// @param reset Reset temporal history (on scene change, camera cut, etc.)
    /// @return Upscaled image view
    vk::ImageView Render(vk::CommandBuffer cmdbuf, vk::Image color_image, vk::ImageView color_view,
                         vk::Format color_format, vk::Extent2D input_size, vk::Extent2D output_size,
                         Settings settings, float delta_time, bool reset = false);

    /// Get the render resolution for a given display resolution and quality mode
    static vk::Extent2D GetRenderResolution(vk::Extent2D display_size, XeSSQualityMode mode);

    /// Check if XeSS is available on this device
    bool IsAvailable() const {
        return is_available;
    }

    /// Get the XeSS version string
    const char* GetVersionString() const {
        return version_string.c_str();
    }

private:
    struct OutputImage {
        u8 id{};
        bool dirty{true};
        VideoCore::UniqueImage image;
        vk::UniqueImageView image_view;
    };

    void CreateOutputImages(OutputImage& img, vk::Extent2D size);
    void CreateDummyMotionVectors(vk::Extent2D size);
    void ResizeOutput(vk::Extent2D size);

    vk::Device device{};
    VmaAllocator allocator{};
    vk::PhysicalDevice physical_device{};
    vk::Instance vulkan_instance{};

    bool is_available{false};
    bool context_created{false};
    std::string version_string{"Unknown"};

    // XeSS context handle (opaque, will be xess_context_handle_t)
    void* xess_context{nullptr};

    // Output images for double-buffering
    std::vector<OutputImage> output_images;
    u32 current_output{0};
    vk::Extent2D current_output_size{};

    // Dummy motion vector texture (zero motion for when game doesn't provide MV)
    VideoCore::UniqueImage motion_vector_image;
    vk::UniqueImageView motion_vector_view;
    vk::Extent2D motion_vector_size{};
    bool motion_vectors_initialized{false};

    // Timing for motion estimation
    float last_delta_time{0.016f}; // Default 60fps
    u32 frame_index{0};

    // Initialization state tracking
    bool initialized_for_resolution{false};
    XeSSQualityMode current_quality_mode{XeSSQualityMode::Balanced};
};

} // namespace Vulkan::HostPasses

