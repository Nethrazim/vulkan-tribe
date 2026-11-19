#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

namespace vt {

class VulkanContext;

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;

    static VkVertexInputBindingDescription bindingDescription();
    static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions();
};

// Device-local vertex and index buffers uploaded once through a staging buffer.
class Mesh {
public:
    Mesh(VulkanContext &context,
         const std::vector<Vertex> &vertices,
         const std::vector<std::uint32_t> &indices);
    ~Mesh();

    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;

    void bind(VkCommandBuffer commandBuffer) const;
    void draw(VkCommandBuffer commandBuffer) const;

    std::uint32_t indexCount() const { return indexCount_; }

    // A unit cube: 24 vertices so each face gets its own normal and UVs.
    static Mesh *createCube(VulkanContext &context);

private:
    template <class T>
    void upload(const std::vector<T> &data,
                VkBufferUsageFlags usage,
                VkBuffer &buffer,
                VkDeviceMemory &memory);

    VulkanContext &context_;

    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory_ = VK_NULL_HANDLE;
    std::uint32_t indexCount_ = 0;
};

} // namespace vt
