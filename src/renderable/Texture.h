#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include <stb_image.h>

#include "../renderer/SampledImageResource.h"
#include "./RenderUtils.h"

class Texture {
public:
  void create(const std::string &path, CommandContext &commandContext,
              DeviceContext &deviceContext) {
    int texWidth, texHeight, texChannels;
    stbi_uc *pixels = stbi_load(path.c_str(), &texWidth, &texHeight,
                                &texChannels, STBI_rgb_alpha);

    if (!pixels) {
      throw std::runtime_error("failed to load texture image!");
    }

    createFromPixels(pixels, texWidth, texHeight, commandContext,
                     deviceContext);
    stbi_image_free(pixels);
  }

  void createSolidColor(const std::array<uint8_t, 4> &rgba,
                        CommandContext &commandContext,
                        DeviceContext &deviceContext) {
    createFromPixels(rgba.data(), 1, 1, commandContext, deviceContext);
  }

  void createRgba(const uint8_t *rgbaPixels, int width, int height,
                  CommandContext &commandContext,
                  DeviceContext &deviceContext) {
    if (rgbaPixels == nullptr || width <= 0 || height <= 0) {
      throw std::runtime_error("invalid RGBA texture data");
    }

    createFromPixels(rgbaPixels, width, height, commandContext, deviceContext);
  }

  vk::raii::ImageView &imageView() { return textureImageView; }
  const vk::raii::ImageView &imageView() const { return textureImageView; }
  uint32_t mipLevelCount() const { return mipLevels; }
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
  void createFromPixels(const stbi_uc *pixels, int texWidth, int texHeight,
                        CommandContext &commandContext,
                        DeviceContext &deviceContext) {
    vk::DeviceSize imageSize =
        static_cast<vk::DeviceSize>(texWidth) * texHeight * 4;
    mipLevels = static_cast<uint32_t>(
                    std::floor(std::log2(std::max(texWidth, texHeight)))) +
                1;

    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingBufferMemory({});
    RenderUtils::createBuffer(deviceContext, imageSize,
                              vk::BufferUsageFlagBits::eTransferSrc,
                              vk::MemoryPropertyFlagBits::eHostVisible |
                                  vk::MemoryPropertyFlagBits::eHostCoherent,
                              stagingBuffer, stagingBufferMemory);

    void *data = stagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, pixels, imageSize);
    stagingBufferMemory.unmapMemory();

    RenderUtils::createImage(deviceContext, texWidth, texHeight, mipLevels,
                             vk::SampleCountFlagBits::e1,
                             vk::Format::eR8G8B8A8Srgb,
                             vk::ImageTiling::eOptimal,
                             vk::ImageUsageFlagBits::eTransferSrc |
                                 vk::ImageUsageFlagBits::eTransferDst |
                                 vk::ImageUsageFlagBits::eSampled,
                             vk::MemoryPropertyFlagBits::eDeviceLocal,
                             textureImage, textureImageMemory);

    RenderUtils::transitionImageLayout(
        commandContext, deviceContext, textureImage,
        vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
        mipLevels);
    RenderUtils::copyBufferToImage(
        stagingBuffer, textureImage, static_cast<uint32_t>(texWidth),
        static_cast<uint32_t>(texHeight), commandContext, deviceContext);

    generateMipmaps(textureImage, vk::Format::eR8G8B8A8Srgb, texWidth,
                    texHeight, mipLevels, commandContext, deviceContext);
    createTextureImageView(deviceContext);
  }
  void generateMipmaps(vk::raii::Image &image, vk::Format imageFormat,
                       int32_t texWidth, int32_t texHeight, uint32_t mipLevels,
                       CommandContext &commandContext,
                       DeviceContext &deviceContext) {
    vk::FormatProperties formatProperties =
        deviceContext.physicalDeviceHandle().getFormatProperties(imageFormat);

    if (!(formatProperties.optimalTilingFeatures &
          vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
      throw std::runtime_error(
          "texture image format does not support linear blitting!");
    }

    std::unique_ptr<vk::raii::CommandBuffer> commandBuffer =
        RenderUtils::beginSingleTimeCommands(commandContext, deviceContext);

    vk::ImageMemoryBarrier barrier = {
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eTransferRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image};
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++) {
      barrier.subresourceRange.baseMipLevel = i - 1;
      barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
      barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
      barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
      barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

      commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                     vk::PipelineStageFlagBits::eTransfer, {},
                                     {}, {}, barrier);

      vk::ArrayWrapper1D<vk::Offset3D, 2> offsets, dstOffsets;
      offsets[0] = vk::Offset3D(0, 0, 0);
      offsets[1] = vk::Offset3D(mipWidth, mipHeight, 1);
      dstOffsets[0] = vk::Offset3D(0, 0, 0);
      dstOffsets[1] = vk::Offset3D(mipWidth > 1 ? mipWidth / 2 : 1,
                                   mipHeight > 1 ? mipHeight / 2 : 1, 1);
      vk::ImageBlit blit = {.srcSubresource = {},
                            .srcOffsets = offsets,
                            .dstSubresource = {},
                            .dstOffsets = dstOffsets};
      blit.srcSubresource = vk::ImageSubresourceLayers(
          vk::ImageAspectFlagBits::eColor, i - 1, 0, 1);
      blit.dstSubresource =
          vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, 1);

      commandBuffer->blitImage(image, vk::ImageLayout::eTransferSrcOptimal,
                               image, vk::ImageLayout::eTransferDstOptimal,
                               {blit}, vk::Filter::eLinear);

      barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
      barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
      barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
      barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

      commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                     vk::PipelineStageFlagBits::eFragmentShader,
                                     {}, {}, {}, barrier);

      if (mipWidth > 1)
        mipWidth /= 2;
      if (mipHeight > 1)
        mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                   vk::PipelineStageFlagBits::eFragmentShader,
                                   {}, {}, {}, barrier);

    RenderUtils::endSingleTimeCommands(commandContext, deviceContext,
                                       *commandBuffer);
  }

  void createTextureImageView(DeviceContext &deviceContext) {
    vk::ImageViewCreateInfo viewInfo{
        .image = textureImage,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR8G8B8A8Srgb,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0,
                             1}};
    textureImageView =
        vk::raii::ImageView(deviceContext.deviceHandle(), viewInfo);
  }

  vk::raii::Image textureImage = nullptr;
  vk::raii::DeviceMemory textureImageMemory = nullptr;
  vk::raii::ImageView textureImageView = nullptr;
  uint32_t mipLevels = 0;
};
