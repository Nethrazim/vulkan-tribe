#include "render/Swapchain.h"

#include "core/Window.h"
#include "render/VulkanContext.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace vt {
namespace {

void check(VkResult result, const char *what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed (VkResult " +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
}

} // namespace

Swapchain::Swapchain(VulkanContext &context, Window &window)
    : context_(context), window_(window) {
    depthFormat_ = context_.findDepthFormat();
    createSwapchain();
    createImageViews();
    createDepthResources();
    createRenderPass();
    createFramebuffers();
}

Swapchain::~Swapchain() {
    destroySizeDependentResources();
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(context_.device(), renderPass_, nullptr);
    }
}

void Swapchain::destroySizeDependentResources() {
    VkDevice device = context_.device();

    for (VkFramebuffer framebuffer : framebuffers_) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    framebuffers_.clear();

    if (depthView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depthView_, nullptr);
        depthView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, depthImage_, nullptr);
        depthImage_ = VK_NULL_HANDLE;
    }
    if (depthMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, depthMemory_, nullptr);
        depthMemory_ = VK_NULL_HANDLE;
    }

    for (VkImageView view : imageViews_) {
        vkDestroyImageView(device, view, nullptr);
    }
    imageViews_.clear();
    images_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

void Swapchain::recreate() {
    window_.waitWhileMinimized();
    vkDeviceWaitIdle(context_.device());

    destroySizeDependentResources();

    createSwapchain();
    createImageViews();
    createDepthResources();
    createFramebuffers();
}

VkSurfaceFormatKHR Swapchain::chooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR> &available) const {
    for (const VkSurfaceFormatKHR &format : available) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return available.front();
}

VkPresentModeKHR Swapchain::choosePresentMode(
    const std::vector<VkPresentModeKHR> &available) const {
    for (const VkPresentModeKHR mode : available) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    // FIFO is the only mode guaranteed to exist.
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Swapchain::chooseExtent(const VkSurfaceCapabilitiesKHR &capabilities) const {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    window_.framebufferSize(width, height);

    VkExtent2D extent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);
    return extent;
}

void Swapchain::createSwapchain() {
    const SwapchainSupport support = context_.querySwapchainSupport();
    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
    const VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
    const VkExtent2D extent = chooseExtent(support.capabilities);

    std::uint32_t desiredImages = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 &&
        desiredImages > support.capabilities.maxImageCount) {
        desiredImages = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = context_.surface();
    info.minImageCount = desiredImages;
    info.imageFormat = surfaceFormat.format;
    info.imageColorSpace = surfaceFormat.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.preTransform = support.capabilities.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = VK_NULL_HANDLE;

    const QueueFamilies &families = context_.queueFamilies();
    const std::uint32_t indices[] = {families.graphics.value(), families.present.value()};
    if (indices[0] != indices[1]) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = indices;
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    check(vkCreateSwapchainKHR(context_.device(), &info, nullptr, &swapchain_),
          "vkCreateSwapchainKHR");

    std::uint32_t count = 0;
    vkGetSwapchainImagesKHR(context_.device(), swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(context_.device(), swapchain_, &count, images_.data());

    imageFormat_ = surfaceFormat.format;
    extent_ = extent;
}

void Swapchain::createImageViews() {
    imageViews_.resize(images_.size());
    for (std::size_t i = 0; i < images_.size(); ++i) {
        imageViews_[i] = context_.createImageView(images_[i], imageFormat_,
                                                  VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void Swapchain::createDepthResources() {
    context_.createImage(extent_.width, extent_.height, depthFormat_,
                         VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         depthImage_, depthMemory_);
    depthView_ = context_.createImageView(depthImage_, depthFormat_, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Swapchain::createRenderPass() {
    VkAttachmentDescription color{};
    color.format = imageFormat_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format = depthFormat_;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    const std::array<VkAttachmentDescription, 2> attachments = {color, depth};

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    check(vkCreateRenderPass(context_.device(), &info, nullptr, &renderPass_),
          "vkCreateRenderPass");
}

void Swapchain::createFramebuffers() {
    framebuffers_.resize(imageViews_.size());
    for (std::size_t i = 0; i < imageViews_.size(); ++i) {
        const std::array<VkImageView, 2> attachments = {imageViews_[i], depthView_};

        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = renderPass_;
        info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.width = extent_.width;
        info.height = extent_.height;
        info.layers = 1;

        check(vkCreateFramebuffer(context_.device(), &info, nullptr, &framebuffers_[i]),
              "vkCreateFramebuffer");
    }
}

float Swapchain::aspectRatio() const {
    if (extent_.height == 0) {
        return 1.0f;
    }
    return static_cast<float>(extent_.width) / static_cast<float>(extent_.height);
}

} // namespace vt
