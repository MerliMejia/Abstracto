#pragma once
#include <cstdint>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>

struct WindowSize {
  uint32_t width;
  uint32_t height;
};

class AppWindow {
public:
  void create(int width, int height, const std::string &title,
              bool maximized = false) {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    if (maximized) {
      glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    }

    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
  }
  void destroy() const {
    glfwDestroyWindow(window);
    glfwTerminate();
  }
  void pollEvents() { glfwPollEvents(); }
  void waitEvents() { glfwWaitEvents(); }
  bool shouldClose() const { return glfwWindowShouldClose(window); }
  GLFWwindow *handle() const { return window; }
  bool consumeResizeFlag() {
    bool resized = framebufferResized;
    framebufferResized = false;
    return resized;
  }
  WindowSize framebufferSize() const {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
  }

private:
  static void framebufferResizeCallback(GLFWwindow *window, int width,
                                        int height) {
    auto app = static_cast<AppWindow *>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
  }
  GLFWwindow *window = nullptr;
  bool framebufferResized = false;
};
