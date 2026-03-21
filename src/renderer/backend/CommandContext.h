#pragma once

#include "DeviceContext.h"
#include <memory>
#include <vector>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

class CommandContext {
public:
  void create(DeviceContext &deviceContext, uint32_t framesInFlight) {
    vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = deviceContext.queueFamilyIndex()};
    commandPool = vk::raii::CommandPool(deviceContext.deviceHandle(), poolInfo);

    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = framesInFlight};
    commandBuffers =
        vk::raii::CommandBuffers(deviceContext.deviceHandle(), allocInfo);
  }

  vk::raii::CommandPool &commandPoolHandle() { return commandPool; }
  const vk::raii::CommandPool &commandPoolHandle() const { return commandPool; }
  vk::raii::CommandBuffer &commandBuffer(uint32_t frameIndex) {
    return commandBuffers[frameIndex];
  }
  const vk::raii::CommandBuffer &commandBuffer(uint32_t frameIndex) const {
    return commandBuffers[frameIndex];
  }

  std::unique_ptr<vk::raii::CommandBuffer>
  beginSingleTimeCommands(DeviceContext &deviceContext) {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1};
    auto commandBuffer = std::make_unique<vk::raii::CommandBuffer>(std::move(
        vk::raii::CommandBuffers(deviceContext.deviceHandle(), allocInfo)
            .front()));

    vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    commandBuffer->begin(beginInfo);
    return commandBuffer;
  }

  void endSingleTimeCommands(DeviceContext &deviceContext,
                             const vk::raii::CommandBuffer &commandBuffer) {
    commandBuffer.end();

    vk::SubmitInfo submitInfo{.commandBufferCount = 1,
                              .pCommandBuffers = &*commandBuffer};
    deviceContext.queueHandle().submit(submitInfo, nullptr);
    deviceContext.queueHandle().waitIdle();
  }

private:
  vk::raii::CommandPool commandPool = nullptr;
  std::vector<vk::raii::CommandBuffer> commandBuffers;
};
