//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

namespace Vulkan::HostPasses {

class NisPass {
public:
    struct Settings {
        bool enable{false};
        float sharpness{0.5f}; // 0.0 to 1.0
    };

    void Create(vk::Device device, VmaAllocator allocator, u32 num_images);

    vk::ImageView Render(vk::CommandBuffer cmdbuf, vk::ImageView input, vk::Extent2D input_size,
                         vk::Extent2D output_size, Settings settings, bool hdr);

private:
    struct Img {
        u8 id{};
        bool dirty{true};

        VideoCore::UniqueImage output_image;
        vk::UniqueImageView output_image_view;
    };

    void ResizeAndInvalidate(u32 width, u32 height);
    void CreateImages(Img& img) const;
    void CreateCoefficientsTextures();
    void UploadCoefficients(vk::CommandBuffer cmdbuf);

    vk::Device device{};
    VmaAllocator allocator{};
    u32 num_images{};

    vk::UniqueDescriptorSetLayout descriptor_set_layout{};
    vk::UniqueSampler sampler{};
    vk::UniquePipelineLayout pipeline_layout{};
    vk::UniquePipeline scaler_pipeline{};

    // UBO buffer for NIS configuration
    vk::Buffer config_buffer{};
    VmaAllocation config_buffer_allocation{};
    void* config_buffer_mapped{};

    // Coefficient textures required by NIS
    VideoCore::UniqueImage coef_scale_image;
    vk::UniqueImageView coef_scale_image_view;
    VideoCore::UniqueImage coef_usm_image;
    vk::UniqueImageView coef_usm_image_view;
    bool coefficients_created{false};
    bool coefficients_uploaded{false};

    vk::Extent2D cur_size{};
    u32 cur_image{};
    std::vector<Img> available_imgs;
};

} // namespace Vulkan::HostPasses

