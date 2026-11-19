#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace vt {

class VulkanContext;

// A sampled 2D RGBA texture: staged into a device-local image, plus its
// view and sampler.
class Texture {
public:
    Texture(VulkanContext &context,
            std::uint32_t width,
            std::uint32_t height,
            const std::vector<std::uint8_t> &rgba);
    ~Texture();

    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;

    VkImageView view() const { return view_; }
    VkSampler sampler() const { return sampler_; }

    // A checkerboard with tinted cells and a dark grid, so the cube's
    // rotation and face orientation are obvious on screen.
    static Texture *createCheckerboard(VulkanContext &context);

private:
    void createSampler();

    VulkanContext &context_;

    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
};

} // namespace vt
