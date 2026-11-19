#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace vt {

class VulkanContext;
class Window;

// Swapchain images, the depth buffer, the render pass and the framebuffers.
// These all depend on the window size, so they are rebuilt together.
class Swapchain {
public:
    Swapchain(VulkanContext &context, Window &window);
    ~Swapchain();

    Swapchain(const Swapchain &) = delete;
    Swapchain &operator=(const Swapchain &) = delete;

    // Tears down and rebuilds everything for the window's current size.
    // The render pass is preserved because the formats do not change.
    void recreate();

    VkSwapchainKHR handle() const { return swapchain_; }
    VkRenderPass renderPass() const { return renderPass_; }
    VkExtent2D extent() const { return extent_; }
    VkFormat imageFormat() const { return imageFormat_; }
    std::uint32_t imageCount() const { return static_cast<std::uint32_t>(images_.size()); }
    VkFramebuffer framebuffer(std::uint32_t index) const { return framebuffers_[index]; }
    float aspectRatio() const;

private:
    void createSwapchain();
    void createImageViews();
    void createDepthResources();
    void createRenderPass();
    void createFramebuffers();
    void destroySizeDependentResources();

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &available) const;
    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR> &available) const;
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR &capabilities) const;

    VulkanContext &context_;
    Window &window_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFormat imageFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};

    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    std::vector<VkFramebuffer> framebuffers_;

    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
};

} // namespace vt
