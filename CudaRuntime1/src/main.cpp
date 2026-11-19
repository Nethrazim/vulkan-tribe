// windows.h must come before GLFW, which otherwise defines APIENTRY itself.
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "core/Window.h"
#include "ecs/Components.h"
#include "ecs/ECS.h"
#include "ecs/Systems.h"
#include "render/Mesh.h"
#include "render/Renderer.h"
#include "render/Swapchain.h"
#include "render/Texture.h"
#include "render/VulkanContext.h"

#include <cstdio>
#include <exception>
#include <filesystem>

namespace {

// Shaders are copied next to the executable, so resolve them from there
// rather than from the current working directory.
std::filesystem::path executableDirectory() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(buffer).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

} // namespace

int main() {
    try {
        vt::Window window(1280, 720, "VulkanTribe - rotating textured cube");
        vt::VulkanContext context(window);
        vt::Swapchain swapchain(context, window);
        vt::Renderer renderer(context, swapchain, window, executableDirectory() / "shaders");

        std::printf("GPU: %s\n", context.deviceName());

        const std::uint32_t cubeMesh = renderer.registerMesh(vt::Mesh::createCube(context));
        const std::uint32_t checkerTexture =
            renderer.registerTexture(vt::Texture::createCheckerboard(context));

        vt::Registry registry;

        const vt::Entity cube = registry.create();
        registry.add<vt::Transform>(cube, vt::Transform{});
        registry.add<vt::MeshRenderer>(cube, vt::MeshRenderer{cubeMesh, checkerTexture});
        registry.add<vt::Rotator>(cube, vt::Rotator{glm::normalize(glm::vec3(0.35f, 1.0f, 0.2f)),
                                                    glm::radians(55.0f)});

        const vt::Entity camera = registry.create();
        vt::Transform cameraTransform{};
        cameraTransform.position = glm::vec3(2.2f, 1.8f, 3.0f);
        registry.add<vt::Transform>(camera, cameraTransform);
        registry.add<vt::Camera>(camera, vt::Camera{});

        std::printf("Running. Press Escape or close the window to exit.\n");

        double previous = glfwGetTime();
        while (!window.shouldClose()) {
            window.pollEvents();

            const double now = glfwGetTime();
            const float delta = static_cast<float>(now - previous);
            previous = now;

            vt::systems::applyRotation(registry, delta);
            renderer.drawFrame(registry);
        }

        renderer.waitIdle();
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "fatal: %s\n", error.what());
        return 1;
    }
}
