#pragma once

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "../backend/CommandContext.h"
#include "../backend/DeviceContext.h"

class RenderUtils {
public:
  static void createBuffer(DeviceContext &deviceContext, vk::DeviceSize size,
                           vk::BufferUsageFlags usage,
                           vk::MemoryPropertyFlags properties,
                           vk::raii::Buffer &buffer,
                           vk::raii::DeviceMemory &bufferMemory) {
    vk::BufferCreateInfo bufferInfo{.size = size,
                                    .usage = usage,
                                    .sharingMode = vk::SharingMode::eExclusive};
    buffer = vk::raii::Buffer(deviceContext.deviceHandle(), bufferInfo);
    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = deviceContext.findMemoryType(
            memRequirements.memoryTypeBits, properties)};
    bufferMemory =
        vk::raii::DeviceMemory(deviceContext.deviceHandle(), allocInfo);
    buffer.bindMemory(bufferMemory, 0);
  }

  static std::unique_ptr<vk::raii::CommandBuffer>
  beginSingleTimeCommands(CommandContext &commandContext,
                          DeviceContext &deviceContext) {
    return commandContext.beginSingleTimeCommands(deviceContext);
  }

  static void
  endSingleTimeCommands(CommandContext &commandContext,
                        DeviceContext &deviceContext,
                        const vk::raii::CommandBuffer &commandBuffer) {
    commandContext.endSingleTimeCommands(deviceContext, commandBuffer);
  }

  static void copyBuffer(CommandContext &commandContext,
                         DeviceContext &deviceContext,
                         vk::raii::Buffer &srcBuffer,
                         vk::raii::Buffer &dstBuffer, vk::DeviceSize size) {
    auto commandCopyBuffer =
        beginSingleTimeCommands(commandContext, deviceContext);
    commandCopyBuffer->copyBuffer(*srcBuffer, *dstBuffer,
                                  vk::BufferCopy{.size = size});
    endSingleTimeCommands(commandContext, deviceContext, *commandCopyBuffer);
  }

  static void createImage(DeviceContext &deviceContext, uint32_t width,
                          uint32_t height, uint32_t mipLevels,
                          uint32_t arrayLayers,
                          vk::SampleCountFlagBits numSamples, vk::Format format,
                          vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                          vk::MemoryPropertyFlags properties,
                          vk::raii::Image &image,
                          vk::raii::DeviceMemory &imageMemory,
                          vk::ImageCreateFlags flags = {}) {
    vk::ImageCreateInfo imageInfo{.imageType = vk::ImageType::e2D,
                                  .flags = flags,
                                  .format = format,
                                  .extent = {width, height, 1},
                                  .mipLevels = mipLevels,
                                  .arrayLayers = arrayLayers,
                                  .samples = numSamples,
                                  .tiling = tiling,
                                  .usage = usage,
                                  .sharingMode = vk::SharingMode::eExclusive,
                                  .initialLayout = vk::ImageLayout::eUndefined};
    image = vk::raii::Image(deviceContext.deviceHandle(), imageInfo);

    vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = deviceContext.findMemoryType(
            memRequirements.memoryTypeBits, properties)};
    imageMemory =
        vk::raii::DeviceMemory(deviceContext.deviceHandle(), allocInfo);
    image.bindMemory(imageMemory, 0);
  }

  static void transitionImageLayout(CommandContext &commandContext,
                                    DeviceContext &deviceContext,
                                    const vk::raii::Image &image,
                                    const vk::ImageLayout oldLayout,
                                    const vk::ImageLayout newLayout,
                                    uint32_t mipLevels,
                                    uint32_t arrayLayers = 1) {
    const auto commandBuffer =
        RenderUtils::beginSingleTimeCommands(commandContext, deviceContext);

    vk::ImageMemoryBarrier barrier{
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .image = image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0,
                             arrayLayers}};

    vk::PipelineStageFlags sourceStage;
    vk::PipelineStageFlags destinationStage;

    if (oldLayout == vk::ImageLayout::eUndefined &&
        newLayout == vk::ImageLayout::eTransferDstOptimal) {
      barrier.srcAccessMask = {};
      barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

      sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
      destinationStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
               newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
      barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
      barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

      sourceStage = vk::PipelineStageFlagBits::eTransfer;
      destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else {
      throw std::invalid_argument("unsupported layout transition!");
    }
    commandBuffer->pipelineBarrier(sourceStage, destinationStage, {}, {},
                                   nullptr, barrier);
    RenderUtils::endSingleTimeCommands(commandContext, deviceContext,
                                       *commandBuffer);
  }

  static void copyBufferToImage(const vk::raii::Buffer &buffer,
                                const vk::raii::Image &image, uint32_t width,
                                uint32_t height, CommandContext &commandContext,
                                DeviceContext &deviceContext) {
    copyBufferToImage(
        buffer, image,
        std::vector<vk::BufferImageCopy>{
            vk::BufferImageCopy{.bufferOffset = 0,
                                .bufferRowLength = 0,
                                .bufferImageHeight = 0,
                                .imageSubresource = {vk::ImageAspectFlagBits::eColor,
                                                     0, 0, 1},
                                .imageOffset = {0, 0, 0},
                                .imageExtent = {width, height, 1}}},
        commandContext, deviceContext);
  }

  static void copyBufferToImage(const vk::raii::Buffer &buffer,
                                const vk::raii::Image &image,
                                const std::vector<vk::BufferImageCopy> &regions,
                                CommandContext &commandContext,
                                DeviceContext &deviceContext) {
    std::unique_ptr<vk::raii::CommandBuffer> commandBuffer =
        RenderUtils::beginSingleTimeCommands(commandContext, deviceContext);
    commandBuffer->copyBufferToImage(
        buffer, image, vk::ImageLayout::eTransferDstOptimal, regions);
    RenderUtils::endSingleTimeCommands(commandContext, deviceContext,
                                       *commandBuffer);
  }
};
