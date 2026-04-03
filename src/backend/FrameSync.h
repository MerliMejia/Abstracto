#pragma once

#include "DeviceContext.h"
#include <cassert>
#include <vector>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

class FrameSync {
public:
  void create(DeviceContext &deviceContext, size_t swapchainImageCount,
              uint32_t framesInFlight) {
    presentCompleteSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();
    maxFramesInFlight = framesInFlight;
    frameIndex = 0;

    for (size_t i = 0; i < swapchainImageCount; i++) {
      renderFinishedSemaphores.emplace_back(deviceContext.deviceHandle(),
                                            vk::SemaphoreCreateInfo());
    }

    for (uint32_t i = 0; i < maxFramesInFlight; i++) {
      presentCompleteSemaphores.emplace_back(deviceContext.deviceHandle(),
                                             vk::SemaphoreCreateInfo());
      inFlightFences.emplace_back(
          deviceContext.deviceHandle(),
          vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    }
  }

  uint32_t currentFrameIndex() const { return frameIndex; }
  vk::raii::Semaphore &presentCompleteSemaphore() {
    return presentCompleteSemaphores[frameIndex];
  }
  vk::raii::Semaphore &renderFinishedSemaphore(uint32_t imageIndex) {
    return renderFinishedSemaphores[imageIndex];
  }
  vk::raii::Fence &inFlightFence() { return inFlightFences[frameIndex]; }
  void advance() { frameIndex = (frameIndex + 1) % maxFramesInFlight; }

private:
  std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
  std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
  std::vector<vk::raii::Fence> inFlightFences;
  uint32_t frameIndex = 0;
  uint32_t maxFramesInFlight = 1;
};
