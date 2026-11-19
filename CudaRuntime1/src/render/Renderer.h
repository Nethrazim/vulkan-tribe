#pragma once

#include "ecs/ECS.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace vt {

class Mesh;
class Swapchain;
class Texture;
class VulkanContext;
class Window;

// Matches the CameraBlock uniform in cube.vert.
struct CameraUniforms {
    glm::mat4 view;
    glm::mat4 proj;
};

// Matches the PushBlock push constant in cube.vert.
struct ModelPushConstant {
    glm::mat4 model;
};

// Owns the graphics pipeline, per-frame resources and the mesh/texture tables,
// and draws every entity that has a Transform plus a MeshRenderer.
class Renderer {
public:
    static constexpr std::uint32_t kFramesInFlight = 2;
    static constexpr std::uint32_t kMaxTextures = 16;

    Renderer(VulkanContext &context,
             Swapchain &swapchain,
             Window &window,
             const std::filesystem::path &shaderDirectory);
    ~Renderer();

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    // Takes ownership. Returns the id to store in a MeshRenderer component.
    std::uint32_t registerMesh(Mesh *mesh);
    std::uint32_t registerTexture(Texture *texture);

    void drawFrame(Registry &registry);

    // Blocks until the GPU is idle; call before destroying scene resources.
    void waitIdle() const;

private:
    void createDescriptorSetLayout();
    void createPipeline(const std::filesystem::path &shaderDirectory);
    void createUniformBuffers();
    void createDescriptorPool();
    void createCommandBuffers();
    void createSyncObjects();

    void allocateDescriptorSetsForTexture(std::uint32_t textureId);
    void updateCameraUniforms(Registry &registry, std::uint32_t frame);
    void recordCommands(VkCommandBuffer commandBuffer,
                        std::uint32_t imageIndex,
                        Registry &registry,
                        std::uint32_t frame);

    VkShaderModule createShaderModule(const std::filesystem::path &path) const;

    VulkanContext &context_;
    Swapchain &swapchain_;
    Window &window_;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    std::vector<std::unique_ptr<Mesh>> meshes_;
    std::vector<std::unique_ptr<Texture>> textures_;

    // [frame][textureId]
    std::vector<std::vector<VkDescriptorSet>> descriptorSets_;

    std::vector<VkBuffer> uniformBuffers_;
    std::vector<VkDeviceMemory> uniformMemory_;
    std::vector<void *> uniformMapped_;

    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkSemaphore> imageAvailable_;
    std::vector<VkSemaphore> renderFinished_; // one per swapchain image
    std::vector<VkFence> inFlight_;

    std::uint32_t currentFrame_ = 0;
};

} // namespace vt
