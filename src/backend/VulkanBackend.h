#pragma once

#include "BackendConfig.h"
#include "CommandContext.h"
#include "DeviceContext.h"
#include "FrameSync.h"
#include "InstanceContext.h"
#include "SurfaceContext.h"
#include "SwapchainContext.h"
#include <cassert>
#include <optional>
#include <stdexcept>

struct FrameState {
  uint32_t frameIndex;
  uint32_t imageIndex;
};

class VulkanBackend {
public:
  void initialize(AppWindow &appWindow, const BackendConfig &backendConfig) {
    config = backendConfig;
    instanceContext.create(config);
    surfaceContext.create(appWindow, instanceContext);
    deviceContext.create(instanceContext, surfaceContext);
    swapchainContext.create(appWindow, surfaceContext, deviceContext);
    commandContext.create(deviceContext, config.maxFramesInFlight);
    frameSync.create(deviceContext, swapchainContext.imageCount(),
                     config.maxFramesInFlight);
  }

  std::optional<FrameState> beginFrame(AppWindow &appWindow) {
    auto fenceResult = deviceContext.deviceHandle().waitForFences(
        *frameSync.inFlightFence(), vk::True, UINT64_MAX);
    if (fenceResult != vk::Result::eSuccess) {
      throw std::runtime_error("failed to wait for fence!");
    }

    auto [result, imageIndex] = swapchainContext.swapchainHandle().acquireNextImage(
        UINT64_MAX, *frameSync.presentCompleteSemaphore(), nullptr);
    if (result == vk::Result::eErrorOutOfDateKHR) {
      return std::nullopt;
    }
    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
      assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
      throw std::runtime_error("failed to acquire swap chain image!");
    }

    deviceContext.deviceHandle().resetFences(*frameSync.inFlightFence());
    commandContext.commandBuffer(frameSync.currentFrameIndex()).reset();

    return FrameState{frameSync.currentFrameIndex(), imageIndex};
  }

  bool endFrame(const FrameState &frameState, AppWindow &appWindow) {
    vk::PipelineStageFlags waitDestinationStageMask(
        vk::PipelineStageFlagBits::eColorAttachmentOutput);
    const vk::SubmitInfo submitInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*frameSync.presentCompleteSemaphore(),
        .pWaitDstStageMask = &waitDestinationStageMask,
        .commandBufferCount = 1,
        .pCommandBuffers = &*commandContext.commandBuffer(frameState.frameIndex),
        .signalSemaphoreCount = 1,
        .pSignalSemaphores =
            &*frameSync.renderFinishedSemaphore(frameState.imageIndex)};
    deviceContext.queueHandle().submit(submitInfo, *frameSync.inFlightFence());

    const vk::PresentInfoKHR presentInfoKHR{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*frameSync.renderFinishedSemaphore(frameState.imageIndex),
        .swapchainCount = 1,
        .pSwapchains = &*swapchainContext.swapchainHandle(),
        .pImageIndices = &frameState.imageIndex};
    vk::Result result = deviceContext.queueHandle().presentKHR(presentInfoKHR);
    bool shouldRecreate =
        (result == vk::Result::eSuboptimalKHR) ||
        (result == vk::Result::eErrorOutOfDateKHR) ||
        appWindow.consumeResizeFlag();
    if (!shouldRecreate) {
      assert(result == vk::Result::eSuccess);
    }

    frameSync.advance();
    return shouldRecreate;
  }

  void recreateSwapchain(AppWindow &appWindow) {
    WindowSize extent = appWindow.framebufferSize();
    while (extent.width == 0 || extent.height == 0) {
      appWindow.waitEvents();
      extent = appWindow.framebufferSize();
    }

    waitIdle();
    swapchainContext.recreate(appWindow, surfaceContext, deviceContext);
    frameSync.create(deviceContext, swapchainContext.imageCount(),
                     config.maxFramesInFlight);
  }

  void waitIdle() { deviceContext.deviceHandle().waitIdle(); }

  DeviceContext &device() { return deviceContext; }
  SwapchainContext &swapchain() { return swapchainContext; }
  CommandContext &commands() { return commandContext; }
  FrameSync &sync() { return frameSync; }

private:
  BackendConfig config;
  InstanceContext instanceContext;
  SurfaceContext surfaceContext;
  DeviceContext deviceContext;
  SwapchainContext swapchainContext;
  CommandContext commandContext;
  FrameSync frameSync;
};
