#include "core/Window.h"

#include <stdexcept>

namespace vt {

Window::Window(int width, int height, const std::string &title) {
    if (!glfwInit()) {
        throw std::runtime_error("glfwInit failed");
    }
    if (!glfwVulkanSupported()) {
        glfwTerminate();
        throw std::runtime_error("No Vulkan loader found by GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, onFramebufferResize);
    glfwSetKeyCallback(window_, onKey);
}

Window::~Window() {
    if (window_) {
        glfwDestroyWindow(window_);
    }
    glfwTerminate();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window_) != 0;
}

void Window::pollEvents() const {
    glfwPollEvents();
}

void Window::framebufferSize(int &width, int &height) const {
    glfwGetFramebufferSize(window_, &width, &height);
}

bool Window::consumeResized() {
    const bool was = resized_;
    resized_ = false;
    return was;
}

void Window::waitWhileMinimized() const {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window_, &width, &height);
        glfwWaitEvents();
    }
}

void Window::onFramebufferResize(GLFWwindow *window, int, int) {
    auto *self = static_cast<Window *>(glfwGetWindowUserPointer(window));
    if (self) {
        self->resized_ = true;
    }
}

void Window::onKey(GLFWwindow *window, int key, int, int action, int) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

} // namespace vt
