#pragma once
#include "AppWindow.h"
#include "InstanceContext.h"
#include <stdexcept>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

class SurfaceContext {
public:
  void create(AppWindow &appWindow, InstanceContext &instanceContext) {
    VkSurfaceKHR _surface;
    if (glfwCreateWindowSurface(*instanceContext.instanceHandle(),
                                appWindow.handle(), nullptr, &_surface) != 0) {
      throw std::runtime_error("failed to create window surface!");
    }
    surface = vk::raii::SurfaceKHR(instanceContext.instanceHandle(), _surface);
  }
  vk::raii::SurfaceKHR &surfaceHandle() { return surface; }
  const vk::raii::SurfaceKHR &surfaceHandle() const { return surface; }

private:
  vk::raii::SurfaceKHR surface = nullptr;
};
