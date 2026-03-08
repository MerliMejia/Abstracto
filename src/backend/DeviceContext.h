#pragma once

#include "InstanceContext.h"
#include "SurfaceContext.h"
#include <cstring>
#include <ranges>
#include <stdexcept>
#include <vector>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

class DeviceContext {
public:
  void create(InstanceContext &instanceContext,
              SurfaceContext &surfaceContext) {
    pickPhysicalDevice(instanceContext, surfaceContext);
    msaaSamples = getMaxUsableSampleCount();
    createLogicalDevice(surfaceContext);
  }
  vk::raii::PhysicalDevice &physicalDeviceHandle() { return physicalDevice; }
  const vk::raii::PhysicalDevice &physicalDeviceHandle() const {
    return physicalDevice;
  }
  vk::raii::Device &deviceHandle() { return device; }
  const vk::raii::Device &deviceHandle() const { return device; }
  vk::raii::Queue &queueHandle() { return queue; }
  const vk::raii::Queue &queueHandle() const { return queue; }
  uint32_t queueFamilyIndex() const { return queueIndex; }
  vk::SampleCountFlagBits msaaSampleCount() const { return msaaSamples; }

  vk::Format findSupportedFormat(const std::vector<vk::Format> &candidates,
                                 vk::ImageTiling tiling,
                                 vk::FormatFeatureFlags features) const {
    for (const auto format : candidates) {
      vk::FormatProperties props = physicalDevice.getFormatProperties(format);

      if (tiling == vk::ImageTiling::eLinear &&
          (props.linearTilingFeatures & features) == features) {
        return format;
      }
      if (tiling == vk::ImageTiling::eOptimal &&
          (props.optimalTilingFeatures & features) == features) {
        return format;
      }
    }

    throw std::runtime_error("failed to find supported format!");
  }

  [[nodiscard]] vk::Format findDepthFormat() const {
    return findSupportedFormat(
        {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint,
         vk::Format::eD24UnormS8Uint},
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment);
  }

  uint32_t findMemoryType(uint32_t typeFilter,
                          vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties =
        physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
      if ((typeFilter & (1 << i)) &&
          (memProperties.memoryTypes[i].propertyFlags & properties) ==
              properties) {
        return i;
      }
    }

    throw std::runtime_error("failed to find suitable memory type!");
  }

private:
  vk::raii::PhysicalDevice physicalDevice = nullptr;
  vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;
  vk::raii::Device device = nullptr;
  uint32_t queueIndex = ~0;
  vk::raii::Queue queue = nullptr;
  std::vector<const char *> requiredDeviceExtension = {
      vk::KHRSwapchainExtensionName,
#if defined(__APPLE__)
      "VK_KHR_portability_subset"
#endif
  };

  void pickPhysicalDevice(InstanceContext &instanceContext,
                          SurfaceContext &surfaceContext) {
    std::vector<vk::raii::PhysicalDevice> devices =
        instanceContext.instanceHandle().enumeratePhysicalDevices();
    const auto devIter = std::ranges::find_if(devices, [&](auto const &device) {
      // Check if the device supports the Vulkan 1.3 API version
      bool supportsVulkan1_3 =
          device.getProperties().apiVersion >= VK_API_VERSION_1_3;

      // Check if any of the queue families support graphics operations
      auto queueFamilies = device.getQueueFamilyProperties();
      bool supportsGraphics =
          std::ranges::any_of(queueFamilies, [](auto const &qfp) {
            return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
          });
      bool supportsPresent = std::ranges::any_of(
          std::views::iota(uint32_t{0},
                           static_cast<uint32_t>(queueFamilies.size())),
          [&](uint32_t qfpIndex) {
            return !!device.getSurfaceSupportKHR(qfpIndex,
                                                 *surfaceContext.surfaceHandle());
          });

      // Check if all required device extensions are available
      auto availableDeviceExtensions =
          device.enumerateDeviceExtensionProperties();
      bool supportsAllRequiredExtensions = std::ranges::all_of(
          requiredDeviceExtension,
          [&availableDeviceExtensions](auto const &requiredDeviceExtension) {
            return std::ranges::any_of(
                availableDeviceExtensions,
                [requiredDeviceExtension](
                    auto const &availableDeviceExtension) {
                  return strcmp(availableDeviceExtension.extensionName,
                                requiredDeviceExtension) == 0;
                });
          });

      auto features = device.template getFeatures2<
          vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features,
          vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
      bool supportsRequiredFeatures =
          features.template get<vk::PhysicalDeviceFeatures2>()
              .features.samplerAnisotropy &&
          features.template get<vk::PhysicalDeviceVulkan13Features>()
              .dynamicRendering &&
          features
              .template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()
              .extendedDynamicState;

      return supportsVulkan1_3 && supportsGraphics && supportsPresent &&
             supportsAllRequiredExtensions && supportsRequiredFeatures;
    });
    if (devIter != devices.end()) {
      physicalDevice = *devIter;
    } else {
      throw std::runtime_error("failed to find a suitable GPU!");
    }
  }

  vk::SampleCountFlagBits getMaxUsableSampleCount() {
    vk::PhysicalDeviceProperties physicalDeviceProperties =
        physicalDevice.getProperties();

    vk::SampleCountFlags counts =
        physicalDeviceProperties.limits.framebufferColorSampleCounts &
        physicalDeviceProperties.limits.framebufferDepthSampleCounts;
    if (counts & vk::SampleCountFlagBits::e64) {
      return vk::SampleCountFlagBits::e64;
    }
    if (counts & vk::SampleCountFlagBits::e32) {
      return vk::SampleCountFlagBits::e32;
    }
    if (counts & vk::SampleCountFlagBits::e16) {
      return vk::SampleCountFlagBits::e16;
    }
    if (counts & vk::SampleCountFlagBits::e8) {
      return vk::SampleCountFlagBits::e8;
    }
    if (counts & vk::SampleCountFlagBits::e4) {
      return vk::SampleCountFlagBits::e4;
    }
    if (counts & vk::SampleCountFlagBits::e2) {
      return vk::SampleCountFlagBits::e2;
    }

    return vk::SampleCountFlagBits::e1;
  }

  void createLogicalDevice(SurfaceContext &surfaceContext) {
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties =
        physicalDevice.getQueueFamilyProperties();

    // get the first index into queueFamilyProperties which supports both
    // graphics and present
    for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size();
         qfpIndex++) {
      if ((queueFamilyProperties[qfpIndex].queueFlags &
           vk::QueueFlagBits::eGraphics) &&
          physicalDevice.getSurfaceSupportKHR(qfpIndex,
                                              *surfaceContext.surfaceHandle())) {
        // found a queue family that supports both graphics and present
        queueIndex = qfpIndex;
        break;
      }
    }
    if (queueIndex == ~0) {
      throw std::runtime_error(
          "Could not find a queue for graphics and present -> terminating");
    }

    // query for Vulkan 1.3 features
    vk::StructureChain<vk::PhysicalDeviceFeatures2,
                       vk::PhysicalDeviceVulkan13Features,
                       vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
        featureChain = {
            {.features = {.samplerAnisotropy =
                              true}}, // vk::PhysicalDeviceFeatures2
            {.synchronization2 = true,
             .dynamicRendering = true}, // vk::PhysicalDeviceVulkan13Features
            {.extendedDynamicState =
                 true} // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
        };

    // create a Device
    float queuePriority = 0.5f;
    vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
        .queueFamilyIndex = queueIndex,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority};
    vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .enabledExtensionCount =
            static_cast<uint32_t>(requiredDeviceExtension.size()),
        .ppEnabledExtensionNames = requiredDeviceExtension.data()};

    device = vk::raii::Device(physicalDevice, deviceCreateInfo);
    queue = vk::raii::Queue(device, queueIndex, 0);
  }
};
