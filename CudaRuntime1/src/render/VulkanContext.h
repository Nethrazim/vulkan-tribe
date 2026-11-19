#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace vt {

class Window;

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;

    bool complete() const { return graphics.has_value() && present.has_value(); }
};

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

// Instance, surface, physical/logical device, queues, command pool, and the
// one-shot transfer helpers everything else is built on.
class VulkanContext {
public:
    explicit VulkanContext(Window &window);
    ~VulkanContext();

    VulkanContext(const VulkanContext &) = delete;
    VulkanContext &operator=(const VulkanContext &) = delete;

    VkInstance instance() const { return instance_; }
    VkSurfaceKHR surface() const { return surface_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkDevice device() const { return device_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    VkQueue presentQueue() const { return presentQueue_; }
    VkCommandPool commandPool() const { return commandPool_; }
    const QueueFamilies &queueFamilies() const { return queueFamilies_; }
    float maxSamplerAnisotropy() const { return maxSamplerAnisotropy_; }
    const char *deviceName() const { return deviceProperties_.deviceName; }

    SwapchainSupport querySwapchainSupport() const;
    std::uint32_t findMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    VkFormat findDepthFormat() const;

    void createBuffer(VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer &buffer,
                      VkDeviceMemory &memory) const;

    void createImage(std::uint32_t width,
                     std::uint32_t height,
                     VkFormat format,
                     VkImageTiling tiling,
                     VkImageUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     VkImage &image,
                     VkDeviceMemory &memory) const;

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect) const;

    VkCommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;

    void copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) const;
    void copyBufferToImage(VkBuffer buffer, VkImage image, std::uint32_t width, std::uint32_t height) const;
    void transitionImageLayout(VkImage image,
                               VkImageLayout oldLayout,
                               VkImageLayout newLayout) const;

private:
    void createInstance();
    void createDebugMessenger();
    void createSurface(Window &window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();

    QueueFamilies findQueueFamilies(VkPhysicalDevice device) const;
    bool isDeviceSuitable(VkPhysicalDevice device) const;
    bool supportsRequiredExtensions(VkPhysicalDevice device) const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    VkPhysicalDeviceProperties deviceProperties_{};
    QueueFamilies queueFamilies_;
    float maxSamplerAnisotropy_ = 1.0f;
    bool validationEnabled_ = false;
};

} // namespace vt
