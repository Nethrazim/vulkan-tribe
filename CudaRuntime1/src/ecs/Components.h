#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

namespace vt {

struct Transform {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // w, x, y, z
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    glm::mat4 matrix() const {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
        model *= glm::mat4_cast(rotation);
        return glm::scale(model, scale);
    }
};

// Spins the Transform around a local axis every frame.
struct Rotator {
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float radiansPerSecond{1.0f};
};

// Indices into the renderer's mesh and texture tables.
struct MeshRenderer {
    std::uint32_t mesh{0};
    std::uint32_t texture{0};
};

struct Camera {
    float fovY{glm::radians(55.0f)};
    float nearZ{0.1f};
    float farZ{100.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};

} // namespace vt
