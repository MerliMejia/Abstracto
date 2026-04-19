#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include <stb_image.h>

#include "core/SampledImageResource.h"
#include "./RenderUtils.h"

enum class TextureEncoding {
  Srgb,
  Linear,
};

class Texture {
public:
  void create(const std::string &path, CommandContext &commandContext,
              DeviceContext &deviceContext,
              TextureEncoding encoding = TextureEncoding::Srgb,
              bool generateTextureMipmaps = true) {
    int texWidth, texHeight, texChannels;
    stbi_uc *pixels = stbi_load(path.c_str(), &texWidth, &texHeight,
                                &texChannels, STBI_rgb_alpha);

    if (!pixels) {
      throw std::runtime_error("failed to load texture image!");
    }

    createFromPixels(pixels, texWidth, texHeight, encoding,
                     generateTextureMipmaps, commandContext, deviceContext);
    stbi_image_free(pixels);
  }

  void createSolidColor(const std::array<uint8_t, 4> &rgba,
                        CommandContext &commandContext,
                        DeviceContext &deviceContext,
                        TextureEncoding encoding = TextureEncoding::Srgb,
                        bool generateTextureMipmaps = true) {
    createFromPixels(rgba.data(), 1, 1, encoding, generateTextureMipmaps,
                     commandContext, deviceContext);
  }

  void createRgba(const uint8_t *rgbaPixels, int width, int height,
                  CommandContext &commandContext,
                  DeviceContext &deviceContext,
                  TextureEncoding encoding = TextureEncoding::Srgb,
                  bool generateTextureMipmaps = true) {
    if (rgbaPixels == nullptr || width <= 0 || height <= 0) {
      throw std::runtime_error("invalid RGBA texture data");
    }

    createFromPixels(rgbaPixels, width, height, encoding,
                     generateTextureMipmaps, commandContext, deviceContext);
  }

  vk::raii::ImageView &imageView() { return textureImageView; }
  const vk::raii::ImageView &imageView() const { return textureImageView; }
  uint32_t mipLevelCount() const { return mipLevels; }
  bool isCompatibleRgba(int width, int height, TextureEncoding encoding) const {
    return textureImage != nullptr && textureWidth == width &&
           textureHeight == height && textureEncoding == encoding;
  }

  bool recordRgbaUpdate(DeviceContext &deviceContext,
                        vk::raii::CommandBuffer &commandBuffer,
                        uint32_t frameIndex, const uint8_t *rgbaPixels,
                        int sourceWidth, int sourceHeight, int x, int y,
                        int width, int height,
                        TextureEncoding encoding = TextureEncoding::Srgb) {
    if (rgbaPixels == nullptr || sourceWidth <= 0 || sourceHeight <= 0 ||
        width <= 0 || height <= 0 || x < 0 || y < 0 ||
        x + width > sourceWidth || y + height > sourceHeight ||
        !isCompatibleRgba(sourceWidth, sourceHeight, encoding)) {
      return false;
    }

    if (frameUploads.size() <= frameIndex) {
      frameUploads.resize(static_cast<size_t>(frameIndex) + 1u);
    }
    frameUploads[frameIndex].clear();

    const vk::DeviceSize uploadSize =
        static_cast<vk::DeviceSize>(width) * static_cast<vk::DeviceSize>(height) *
        4u;
    StagedTextureUpload upload;
    RenderUtils::createBuffer(deviceContext, uploadSize,
                              vk::BufferUsageFlagBits::eTransferSrc,
                              vk::MemoryPropertyFlagBits::eHostVisible |
                                  vk::MemoryPropertyFlagBits::eHostCoherent,
                              upload.buffer, upload.memory);

    auto *data = static_cast<uint8_t *>(upload.memory.mapMemory(0, uploadSize));
    const size_t sourceStride = static_cast<size_t>(sourceWidth) * 4u;
    const size_t uploadStride = static_cast<size_t>(width) * 4u;
    for (int row = 0; row < height; ++row) {
      const uint8_t *sourceRow =
          rgbaPixels + (static_cast<size_t>(y + row) * sourceStride) +
          static_cast<size_t>(x) * 4u;
      std::memcpy(data + static_cast<size_t>(row) * uploadStride, sourceRow,
                  uploadStride);
    }
    upload.memory.unmapMemory();

    frameUploads[frameIndex].push_back(std::move(upload));
    const StagedTextureUpload &stagedUpload = frameUploads[frameIndex].back();

    recordSingleMipTransition(commandBuffer,
                              vk::ImageLayout::eShaderReadOnlyOptimal,
                              vk::ImageLayout::eTransferDstOptimal);
    const vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .imageOffset = {x, y, 0},
        .imageExtent = {static_cast<uint32_t>(width),
                        static_cast<uint32_t>(height), 1}};
    commandBuffer.copyBufferToImage(stagedUpload.buffer, textureImage,
                                    vk::ImageLayout::eTransferDstOptimal,
                                    region);
    recordSingleMipTransition(commandBuffer, vk::ImageLayout::eTransferDstOptimal,
                              vk::ImageLayout::eShaderReadOnlyOptimal);
    return true;
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
  struct StagedTextureUpload {
    vk::raii::Buffer buffer = nullptr;
    vk::raii::DeviceMemory memory = nullptr;
  };

  void createFromPixels(const stbi_uc *pixels, int texWidth, int texHeight,
                        TextureEncoding encoding, bool generateTextureMipmaps,
                        CommandContext &commandContext,
                        DeviceContext &deviceContext) {
    vk::DeviceSize imageSize =
        static_cast<vk::DeviceSize>(texWidth) * texHeight * 4;
    mipLevels =
        generateTextureMipmaps
            ? static_cast<uint32_t>(
                  std::floor(std::log2(std::max(texWidth, texHeight)))) +
                  1
            : 1;
    textureWidth = texWidth;
    textureHeight = texHeight;
    textureEncoding = encoding;

    textureFormat = encoding == TextureEncoding::Srgb
                        ? vk::Format::eR8G8B8A8Srgb
                        : vk::Format::eR8G8B8A8Unorm;

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
                             1, vk::SampleCountFlagBits::e1,
                             textureFormat,
                             vk::ImageTiling::eOptimal,
                             (generateTextureMipmaps
                                  ? vk::ImageUsageFlagBits::eTransferSrc
                                  : vk::ImageUsageFlags{}) |
                                 vk::ImageUsageFlagBits::eTransferDst |
                                 vk::ImageUsageFlagBits::eSampled,
                             vk::MemoryPropertyFlagBits::eDeviceLocal,
                             textureImage, textureImageMemory);

    RenderUtils::transitionImageLayout(
        commandContext, deviceContext, textureImage,
        vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
        mipLevels, 1);
    RenderUtils::copyBufferToImage(
        stagingBuffer, textureImage, static_cast<uint32_t>(texWidth),
        static_cast<uint32_t>(texHeight), commandContext, deviceContext);

    if (generateTextureMipmaps) {
      generateMipmaps(textureImage, textureFormat, texWidth, texHeight, mipLevels,
                      commandContext, deviceContext);
    } else {
      RenderUtils::transitionImageLayout(
          commandContext, deviceContext, textureImage,
          vk::ImageLayout::eTransferDstOptimal,
          vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels, 1);
    }
    createTextureImageView(deviceContext);
    frameUploads.clear();
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
        .format = textureFormat,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0,
                             1}};
    textureImageView =
        vk::raii::ImageView(deviceContext.deviceHandle(), viewInfo);
  }

  void recordSingleMipTransition(vk::raii::CommandBuffer &commandBuffer,
                                 vk::ImageLayout oldLayout,
                                 vk::ImageLayout newLayout) {
    vk::ImageMemoryBarrier barrier{
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = textureImage,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};

    vk::PipelineStageFlags sourceStage;
    vk::PipelineStageFlags destinationStage;
    if (oldLayout == vk::ImageLayout::eShaderReadOnlyOptimal &&
        newLayout == vk::ImageLayout::eTransferDstOptimal) {
      barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
      barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
      sourceStage = vk::PipelineStageFlagBits::eFragmentShader;
      destinationStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
               newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
      barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
      barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
      sourceStage = vk::PipelineStageFlagBits::eTransfer;
      destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else {
      throw std::invalid_argument("unsupported texture update transition!");
    }

    commandBuffer.pipelineBarrier(sourceStage, destinationStage, {}, {}, {},
                                  barrier);
  }

  vk::raii::Image textureImage = nullptr;
  vk::raii::DeviceMemory textureImageMemory = nullptr;
  vk::raii::ImageView textureImageView = nullptr;
  int textureWidth = 0;
  int textureHeight = 0;
  uint32_t mipLevels = 0;
  TextureEncoding textureEncoding = TextureEncoding::Srgb;
  vk::Format textureFormat = vk::Format::eR8G8B8A8Srgb;
  std::vector<std::vector<StagedTextureUpload>> frameUploads;
};
