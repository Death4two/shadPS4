//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

<<<<<<< Updated upstream
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
=======
#include <xess/xess_vk.h>

namespace Vulkan::HostPasses {

class XessPass {
public:
    enum class QualityMode : u32 {
        UltraPerformance = XESS_QUALITY_SETTING_ULTRA_PERFORMANCE,
        Performance = XESS_QUALITY_SETTING_PERFORMANCE,
        Balanced = XESS_QUALITY_SETTING_BALANCED,
        Quality = XESS_QUALITY_SETTING_QUALITY,
        UltraQuality = XESS_QUALITY_SETTING_ULTRA_QUALITY,
        UltraQualityPlus = XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS,
        NativeAA = XESS_QUALITY_SETTING_AA,
    };

    struct Settings {
        bool enable{false};
        QualityMode quality_mode{QualityMode::Balanced};
    };

    void Create(vk::Device device, VmaAllocator allocator, vk::Instance instance,
                vk::PhysicalDevice physical_device, u32 num_images);

    void Destroy();

    vk::ImageView Render(vk::CommandBuffer cmdbuf, vk::ImageView input, vk::Image input_image,
                         vk::Extent2D input_size, vk::Extent2D output_size, Settings settings,
                         bool hdr);

private:
    struct Img {
        u8 id{};
        bool dirty{true};

        VideoCore::UniqueImage output_image;
        vk::UniqueImageView output_image_view;
    };

    void ResizeAndInvalidate(u32 width, u32 height);
    void CreateImages(Img& img) const;
    void InitializeXess(vk::Extent2D output_size, Settings settings);
    void CreateDummyTextures(vk::CommandBuffer cmdbuf);
    void GetJitterOffset(float& jitter_x, float& jitter_y, u32 width, u32 height);

    vk::Device device{};
    VmaAllocator allocator{};
    vk::Instance instance{};
    vk::PhysicalDevice physical_device{};
    u32 num_images{};

    xess_context_handle_t xess_context{nullptr};
    bool xess_initialized{false};
    vk::Extent2D xess_output_size{};

    // Dummy textures for motion vectors and depth (since we don't have them)
    VideoCore::UniqueImage dummy_velocity_image;
    vk::UniqueImageView dummy_velocity_image_view;
    VideoCore::UniqueImage dummy_depth_image;
    vk::UniqueImageView dummy_depth_image_view;

    vk::Extent2D cur_size{};
    u32 cur_image{};
    std::vector<Img> available_imgs;

    // Jitter state for temporal accumulation
    u32 jitter_index{0};
    static constexpr u32 kJitterSequenceLength = 32;
    std::vector<std::pair<float, float>> halton_sequence;
>>>>>>> Stashed changes
};

} // namespace Vulkan::HostPasses

