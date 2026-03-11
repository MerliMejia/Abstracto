#pragma once

#include "../backend/DeviceContext.h"
#include "../backend/SwapchainContext.h"
#include "../renderable/DescriptorBindings.h"
#include "../renderable/Mesh.h"
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

class RenderPass;

struct RenderItem {
  Mesh *mesh = nullptr;
  DescriptorBindings *descriptorBindings = nullptr;
  const RenderPass *targetPass = nullptr;
};

struct RenderPassContext {
  vk::raii::CommandBuffer &commandBuffer;
  SwapchainContext &swapchainContext;
  uint32_t frameIndex = 0;
  uint32_t imageIndex = 0;
};

class RenderPass {
public:
  virtual ~RenderPass() = default;

  virtual void initialize(DeviceContext &deviceContext,
                          SwapchainContext &swapchainContext) = 0;
  virtual void recreate(DeviceContext &deviceContext,
                        SwapchainContext &swapchainContext) = 0;
  virtual void record(const RenderPassContext &context,
                      const std::vector<RenderItem> &renderItems) = 0;
  virtual vk::raii::DescriptorSetLayout *descriptorSetLayout() {
    return nullptr;
  }
};
