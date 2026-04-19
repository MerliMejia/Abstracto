#pragma once

#include "core/SampledImageResource.h"
#include "RenderUtils.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

class FloatTexture2D {
public:
  void create(DeviceContext &deviceContext, CommandContext &commandContext,
              uint32_t width, uint32_t height,
              const std::vector<float> &rgbaPixels,
              vk::Format format = vk::Format::eR32G32B32A32Sfloat) {
    if (width == 0 || height == 0 || rgbaPixels.size() != width * height * 4) {
      throw std::runtime_error("invalid 2D float texture data");
    }

    textureFormat = format;
    extent = {width, height};

    const vk::DeviceSize imageSize =
        static_cast<vk::DeviceSize>(rgbaPixels.size() * sizeof(float));

    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingMemory({});
    RenderUtils::createBuffer(deviceContext, imageSize,
                              vk::BufferUsageFlagBits::eTransferSrc,
                              vk::MemoryPropertyFlagBits::eHostVisible |
                                  vk::MemoryPropertyFlagBits::eHostCoherent,
                              stagingBuffer, stagingMemory);

    void *mapped = stagingMemory.mapMemory(0, imageSize);
    std::memcpy(mapped, rgbaPixels.data(), static_cast<size_t>(imageSize));
    stagingMemory.unmapMemory();

    RenderUtils::createImage(
        deviceContext, width, height, 1, 1, vk::SampleCountFlagBits::e1, format,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal, textureImage,
        textureImageMemory);

    std::unique_ptr<vk::raii::CommandBuffer> commandBuffer =
        RenderUtils::beginSingleTimeCommands(commandContext, deviceContext);
    RenderUtils::recordImageLayoutTransition(
        *commandBuffer, textureImage,
        vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1, 1);
    RenderUtils::recordCopyBufferToImage(
        *commandBuffer, stagingBuffer, textureImage,
        std::vector<vk::BufferImageCopy>{
            vk::BufferImageCopy{.bufferOffset = 0,
                                .bufferRowLength = 0,
                                .bufferImageHeight = 0,
                                .imageSubresource = {vk::ImageAspectFlagBits::eColor,
                                                     0, 0, 1},
                                .imageOffset = {0, 0, 0},
                                .imageExtent = {width, height, 1}}});
    RenderUtils::recordImageLayoutTransition(
        *commandBuffer, textureImage,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal, 1, 1);
    RenderUtils::endSingleTimeCommands(commandContext, deviceContext,
                                       *commandBuffer);

    vk::ImageViewCreateInfo viewInfo{
        .image = textureImage,
        .viewType = vk::ImageViewType::e2D,
        .format = textureFormat,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    textureImageView =
        vk::raii::ImageView(deviceContext.deviceHandle(), viewInfo);
  }

  SampledImageResource
  sampledResource(const vk::raii::Sampler &sampler,
                  vk::ImageLayout imageLayout =
                      vk::ImageLayout::eShaderReadOnlyOptimal) const {
    return SampledImageResource{
        .imageView = textureImageView,
        .sampler = sampler,
        .imageLayout = imageLayout,
    };
  }

private:
  vk::Extent2D extent{};
  vk::Format textureFormat = vk::Format::eUndefined;
  vk::raii::Image textureImage = nullptr;
  vk::raii::DeviceMemory textureImageMemory = nullptr;
  vk::raii::ImageView textureImageView = nullptr;
};
