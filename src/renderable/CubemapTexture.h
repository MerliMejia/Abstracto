#pragma once

#include "../renderer/SampledImageResource.h"
#include "ImageBasedLightingTypes.h"
#include "RenderUtils.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

class CubemapTexture {
public:
  void create(DeviceContext &deviceContext, CommandContext &commandContext,
              const std::vector<CubemapMipLevelData> &mipLevels,
              vk::Format format = vk::Format::eR32G32B32A32Sfloat) {
    if (mipLevels.empty()) {
      throw std::runtime_error("cubemap mip chain cannot be empty");
    }

    textureFormat = format;
    mipLevelCountValue = static_cast<uint32_t>(mipLevels.size());

    std::vector<float> packedPixels;
    std::vector<vk::BufferImageCopy> copyRegions;
    packedPixels.reserve(totalFloatCount(mipLevels));
    copyRegions.reserve(mipLevels.size() * 6);

    vk::DeviceSize currentOffset = 0;
    for (uint32_t mipIndex = 0; mipIndex < mipLevels.size(); ++mipIndex) {
      const auto &mip = mipLevels[mipIndex];
      const size_t faceFloatCount =
          static_cast<size_t>(mip.width) * mip.height * 4;

      for (uint32_t face = 0; face < 6; ++face) {
        if (mip.faces[face].size() != faceFloatCount) {
          throw std::runtime_error("cubemap face data has unexpected size");
        }

        copyRegions.push_back(vk::BufferImageCopy{
            .bufferOffset = currentOffset,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, mipIndex, face,
                                 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {mip.width, mip.height, 1}});

        packedPixels.insert(packedPixels.end(), mip.faces[face].begin(),
                            mip.faces[face].end());
        currentOffset += static_cast<vk::DeviceSize>(mip.faces[face].size() *
                                                     sizeof(float));
      }
    }

    const vk::DeviceSize bufferSize =
        static_cast<vk::DeviceSize>(packedPixels.size() * sizeof(float));

    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingMemory({});
    RenderUtils::createBuffer(deviceContext, bufferSize,
                              vk::BufferUsageFlagBits::eTransferSrc,
                              vk::MemoryPropertyFlagBits::eHostVisible |
                                  vk::MemoryPropertyFlagBits::eHostCoherent,
                              stagingBuffer, stagingMemory);

    void *mapped = stagingMemory.mapMemory(0, bufferSize);
    std::memcpy(mapped, packedPixels.data(), static_cast<size_t>(bufferSize));
    stagingMemory.unmapMemory();

    RenderUtils::createImage(
        deviceContext, mipLevels.front().width, mipLevels.front().height,
        mipLevelCountValue, 6, vk::SampleCountFlagBits::e1, textureFormat,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal, textureImage,
        textureImageMemory, vk::ImageCreateFlagBits::eCubeCompatible);
    RenderUtils::transitionImageLayout(
        commandContext, deviceContext, textureImage,
        vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
        mipLevelCountValue, 6);
    RenderUtils::copyBufferToImage(stagingBuffer, textureImage, copyRegions,
                                   commandContext, deviceContext);
    RenderUtils::transitionImageLayout(
        commandContext, deviceContext, textureImage,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal, mipLevelCountValue, 6);

    vk::ImageViewCreateInfo viewInfo{
        .image = textureImage,
        .viewType = vk::ImageViewType::eCube,
        .format = textureFormat,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0,
                             mipLevelCountValue, 0, 6}};
    textureImageView =
        vk::raii::ImageView(deviceContext.deviceHandle(), viewInfo);
  }

  uint32_t mipLevelCount() const { return mipLevelCountValue; }

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
  static size_t totalFloatCount(const std::vector<CubemapMipLevelData> &mips) {
    size_t total = 0;
    for (const auto &mip : mips) {
      const size_t faceFloats = static_cast<size_t>(mip.width) * mip.height * 4;
      total += faceFloats * 6;
    }
    return total;
  }

  vk::Format textureFormat = vk::Format::eUndefined;
  uint32_t mipLevelCountValue = 0;
  vk::raii::Image textureImage = nullptr;
  vk::raii::DeviceMemory textureImageMemory = nullptr;
  vk::raii::ImageView textureImageView = nullptr;
};
