#pragma once

#include "AppWindow.h"
#include "DeviceContext.h"
#include "SurfaceContext.h"
#include <algorithm>
#include <cassert>
#include <ranges>
#include <vector>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

class SwapchainContext {
public:
  void create(AppWindow &appWindow, SurfaceContext &surfaceContext,
              DeviceContext &deviceContext) {
    auto surfaceCapabilities =
        deviceContext.physicalDeviceHandle().getSurfaceCapabilitiesKHR(
            *surfaceContext.surfaceHandle());
    extent = chooseSwapExtent(appWindow, surfaceCapabilities);
    surfaceFormat = chooseSwapSurfaceFormat(
        deviceContext.physicalDeviceHandle().getSurfaceFormatsKHR(
            *surfaceContext.surfaceHandle()));
    vk::SwapchainCreateInfoKHR swapChainCreateInfo{
        .surface = *surfaceContext.surfaceHandle(),
        .minImageCount = chooseSwapMinImageCount(surfaceCapabilities),
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = chooseSwapPresentMode(
            deviceContext.physicalDeviceHandle().getSurfacePresentModesKHR(
                *surfaceContext.surfaceHandle())),
        .clipped = true};

    swapchain =
        vk::raii::SwapchainKHR(deviceContext.deviceHandle(), swapChainCreateInfo);
    images = swapchain.getImages();
    createImageViews(deviceContext);
  }

  void recreate(AppWindow &appWindow, SurfaceContext &surfaceContext,
                DeviceContext &deviceContext) {
    cleanup();
    create(appWindow, surfaceContext, deviceContext);
  }

  void cleanup() {
    imageViews.clear();
    images.clear();
    swapchain = nullptr;
  }

  vk::raii::SwapchainKHR &swapchainHandle() { return swapchain; }
  const vk::raii::SwapchainKHR &swapchainHandle() const { return swapchain; }
  std::vector<vk::Image> &swapchainImages() { return images; }
  const std::vector<vk::Image> &swapchainImages() const { return images; }
  std::vector<vk::raii::ImageView> &swapchainImageViews() { return imageViews; }
  const std::vector<vk::raii::ImageView> &swapchainImageViews() const {
    return imageViews;
  }
  vk::SurfaceFormatKHR &surfaceFormatInfo() { return surfaceFormat; }
  const vk::SurfaceFormatKHR &surfaceFormatInfo() const { return surfaceFormat; }
  vk::Extent2D extent2D() const { return extent; }
  size_t imageCount() const { return images.size(); }

private:
  void createImageViews(DeviceContext &deviceContext) {
    assert(imageViews.empty());

    vk::ImageViewCreateInfo imageViewCreateInfo{
        .viewType = vk::ImageViewType::e2D,
        .format = surfaceFormat.format,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    for (auto &image : images) {
      imageViewCreateInfo.image = image;
      imageViews.emplace_back(deviceContext.deviceHandle(), imageViewCreateInfo);
    }
  }

  static uint32_t
  chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &surfaceCapabilities) {
    auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    if ((0 < surfaceCapabilities.maxImageCount) &&
        (surfaceCapabilities.maxImageCount < minImageCount)) {
      minImageCount = surfaceCapabilities.maxImageCount;
    }
    return minImageCount;
  }

  static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(
      const std::vector<vk::SurfaceFormatKHR> &availableFormats) {
    assert(!availableFormats.empty());
    const auto formatIt =
        std::ranges::find_if(availableFormats, [](const auto &format) {
          return format.format == vk::Format::eB8G8R8A8Srgb &&
                 format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        });
    return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
  }

  static vk::PresentModeKHR chooseSwapPresentMode(
      const std::vector<vk::PresentModeKHR> &availablePresentModes) {
    assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) {
      return presentMode == vk::PresentModeKHR::eFifo;
    }));
    return std::ranges::any_of(availablePresentModes,
                               [](const vk::PresentModeKHR value) {
                                 return vk::PresentModeKHR::eMailbox == value;
                               })
               ? vk::PresentModeKHR::eMailbox
               : vk::PresentModeKHR::eFifo;
  }

  static vk::Extent2D chooseSwapExtent(
      AppWindow &appWindow, const vk::SurfaceCapabilitiesKHR &capabilities) {
    if (capabilities.currentExtent.width != 0xFFFFFFFF) {
      return capabilities.currentExtent;
    }

    WindowSize size = appWindow.framebufferSize();
    return {
        std::clamp(size.width, capabilities.minImageExtent.width,
                   capabilities.maxImageExtent.width),
        std::clamp(size.height, capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height)};
  }

  vk::raii::SwapchainKHR swapchain = nullptr;
  std::vector<vk::Image> images;
  vk::SurfaceFormatKHR surfaceFormat{};
  vk::Extent2D extent{};
  std::vector<vk::raii::ImageView> imageViews;
};
