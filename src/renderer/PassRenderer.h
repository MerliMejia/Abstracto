#pragma once

#include "RenderPass.h"
#include <memory>
#include <stdexcept>
#include <vector>

class PassRenderer {
public:
  void addPass(std::unique_ptr<RenderPass> renderPass) {
    passes.push_back(std::move(renderPass));
  }

  void initialize(DeviceContext &deviceContext,
                  SwapchainContext &swapchainContext) {
    if (passes.empty()) {
      throw std::runtime_error("PassRenderer requires at least one render pass");
    }

    for (auto &renderPass : passes) {
      renderPass->initialize(deviceContext, swapchainContext);
    }
  }

  void recreate(DeviceContext &deviceContext,
                SwapchainContext &swapchainContext) {
    for (auto &renderPass : passes) {
      renderPass->recreate(deviceContext, swapchainContext);
    }
  }

  void record(vk::raii::CommandBuffer &commandBuffer,
              SwapchainContext &swapchainContext, Mesh &mesh,
              DescriptorBindings &descriptorBindings, uint32_t frameIndex,
              uint32_t imageIndex) {
    std::vector<RenderItem> renderItems = {
        RenderItem{.mesh = &mesh, .descriptorBindings = &descriptorBindings}};
    record(commandBuffer, swapchainContext, renderItems, frameIndex,
           imageIndex);
  }

  void record(vk::raii::CommandBuffer &commandBuffer,
              SwapchainContext &swapchainContext,
              const std::vector<RenderItem> &renderItems, uint32_t frameIndex,
              uint32_t imageIndex) {
    commandBuffer.begin({});
    RenderPassContext context{.commandBuffer = commandBuffer,
                              .swapchainContext = swapchainContext,
                              .frameIndex = frameIndex,
                              .imageIndex = imageIndex};
    for (auto &renderPass : passes) {
      renderPass->record(context, renderItems);
    }
    commandBuffer.end();
  }

  vk::raii::DescriptorSetLayout &descriptorSetLayout() {
    auto *layout = passes.front()->descriptorSetLayout();
    if (layout == nullptr) {
      throw std::runtime_error(
          "First render pass does not expose a descriptor set layout");
    }
    return *layout;
  }

private:
  std::vector<std::unique_ptr<RenderPass>> passes;
};
