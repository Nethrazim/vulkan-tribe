#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>

namespace vt {

// Owns the GLFW library lifetime and the single engine window.
class Window {
public:
    Window(int width, int height, const std::string &title);
    ~Window();

    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    bool shouldClose() const;
    void pollEvents() const;

    GLFWwindow *handle() const { return window_; }

    void framebufferSize(int &width, int &height) const;

    // True once after the framebuffer changed size; clears the flag.
    bool consumeResized();

    // Blocks while the window is minimised (zero-sized framebuffer).
    void waitWhileMinimized() const;

private:
    static void onFramebufferResize(GLFWwindow *window, int width, int height);
    static void onKey(GLFWwindow *window, int key, int scancode, int action, int mods);

    GLFWwindow *window_ = nullptr;
    bool resized_ = false;
};

} // namespace vt
