//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

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

    // Get current jitter offset for projection matrix application (without advancing index)
    // Returns jitter in [-0.5, 0.5] range as required by XeSS
    // This should be called before Render() to get the jitter that will be used for that frame
    void GetCurrentJitter(float& jitter_x, float& jitter_y, u32 input_width, u32 input_height) const;

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
};

} // namespace Vulkan::HostPasses

