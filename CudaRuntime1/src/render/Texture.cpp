#include "render/Texture.h"

#include "render/VulkanContext.h"

#include <cstring>
#include <stdexcept>
#include <string>

namespace vt {
namespace {

constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_SRGB;

void check(VkResult result, const char *what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed (VkResult " +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
}

} // namespace

Texture::Texture(VulkanContext &context,
                 std::uint32_t width,
                 std::uint32_t height,
                 const std::vector<std::uint8_t> &rgba)
    : context_(context) {
    const VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4;
    if (rgba.size() != size) {
        throw std::runtime_error("Texture pixel buffer has the wrong size");
    }

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    context_.createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          staging, stagingMemory);

    void *mapped = nullptr;
    vkMapMemory(context_.device(), stagingMemory, 0, size, 0, &mapped);
    std::memcpy(mapped, rgba.data(), static_cast<std::size_t>(size));
    vkUnmapMemory(context_.device(), stagingMemory);

    context_.createImage(width, height, kFormat, VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image_, memory_);

    context_.transitionImageLayout(image_, VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    context_.copyBufferToImage(staging, image_, width, height);
    context_.transitionImageLayout(image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(context_.device(), staging, nullptr);
    vkFreeMemory(context_.device(), stagingMemory, nullptr);

    view_ = context_.createImageView(image_, kFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    createSampler();
}

Texture::~Texture() {
    VkDevice device = context_.device();
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler_, nullptr);
    }
    if (view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, view_, nullptr);
    }
    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, image_, nullptr);
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory_, nullptr);
    }
}

void Texture::createSampler() {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.anisotropyEnable = VK_TRUE;
    info.maxAnisotropy = context_.maxSamplerAnisotropy();
    info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    info.unnormalizedCoordinates = VK_FALSE;
    info.compareEnable = VK_FALSE;
    info.compareOp = VK_COMPARE_OP_ALWAYS;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.mipLodBias = 0.0f;
    info.minLod = 0.0f;
    info.maxLod = 0.0f;

    check(vkCreateSampler(context_.device(), &info, nullptr, &sampler_), "vkCreateSampler");
}

Texture *Texture::createCheckerboard(VulkanContext &context) {
    constexpr std::uint32_t kSize = 256;
    constexpr std::uint32_t kCell = 32;

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kSize) * kSize * 4);

    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const bool odd = ((x / kCell) + (y / kCell)) % 2 == 1;

            std::uint8_t r = odd ? 230 : 40;
            std::uint8_t g = odd ? 120 : 60;
            std::uint8_t b = odd ? 60 : 110;

            // Dark grid lines along every cell edge.
            const bool onGrid = (x % kCell) == 0 || (y % kCell) == 0;
            if (onGrid) {
                r = 20;
                g = 20;
                b = 24;
            }

            const std::size_t offset = (static_cast<std::size_t>(y) * kSize + x) * 4;
            pixels[offset + 0] = r;
            pixels[offset + 1] = g;
            pixels[offset + 2] = b;
            pixels[offset + 3] = 255;
        }
    }

    return new Texture(context, kSize, kSize, pixels);
}

} // namespace vt
