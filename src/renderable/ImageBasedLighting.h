#pragma once

#include "../renderer/SampledImageResource.h"
#include "RenderUtils.h"
#include "Sampler.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <stb_image.h>

struct ProceduralSkySettings {
  glm::vec3 zenithColor{0.16f, 0.32f, 0.72f};
  glm::vec3 horizonColor{0.85f, 0.72f, 0.58f};
  glm::vec3 groundColor{0.10f, 0.09f, 0.08f};
  glm::vec3 sunColor{1.0f, 0.95f, 0.82f};
  float sunIntensity = 24.0f;
  float sunAngularRadius = 0.045f;
  float sunGlow = 2.5f;
  float horizonGlow = 0.28f;
  float skyExponent = 0.45f;
  float groundFalloff = 1.6f;
  float sunAzimuthRadians = glm::radians(35.0f);
  float sunElevationRadians = glm::radians(32.0f);
};

struct ImageBasedLightingBakeSettings {
  uint32_t environmentResolution = 1024;
  uint32_t irradianceResolution = 32;
  uint32_t prefilteredResolution = 256;
  uint32_t brdfResolution = 256;
  uint32_t irradianceSamples = 1024;
  uint32_t prefilteredSamples = 64;
  uint32_t brdfSamples = 128;
  std::string environmentHdrPath;
  ProceduralSkySettings sky;
};

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
    RenderUtils::transitionImageLayout(
        commandContext, deviceContext, textureImage,
        vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1,
        1);
    RenderUtils::copyBufferToImage(stagingBuffer, textureImage, width, height,
                                   commandContext, deviceContext);
    RenderUtils::transitionImageLayout(
        commandContext, deviceContext, textureImage,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal, 1, 1);

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

struct CubemapMipLevelData {
  uint32_t width = 0;
  uint32_t height = 0;
  std::array<std::vector<float>, 6> faces;
};

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
      const size_t faceFloatCount = static_cast<size_t>(mip.width) * mip.height * 4;

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

struct ImageBasedLightingResources {
  CubemapTexture environmentMap;
  CubemapTexture irradianceMap;
  CubemapTexture prefilteredMap;
  FloatTexture2D brdfLut;

  Sampler environmentSampler;
  Sampler irradianceSampler;
  Sampler prefilteredSampler;
  Sampler brdfSampler;

  ImageBasedLightingBakeSettings bakeSettings;

  void create(DeviceContext &deviceContext, CommandContext &commandContext,
              const ImageBasedLightingBakeSettings &settings) {
    bakeSettings = settings;

    std::vector<CubemapMipLevelData> environment;
    std::vector<CubemapMipLevelData> irradiance;
    std::vector<CubemapMipLevelData> prefiltered;
    std::vector<float> brdf;

    if (!tryLoadBakeCache(settings, environment, irradiance, prefiltered,
                          brdf)) {
      const EnvironmentSource environmentSource = loadEnvironmentSource(settings);
      environment = bakeEnvironmentMap(settings, environmentSource);
      irradiance = bakeIrradianceMap(settings, environmentSource);
      prefiltered = bakePrefilteredMap(settings, environmentSource);
      brdf = bakeBrdfLut(settings);

      fixupCubemapSeams(environment);
      fixupCubemapSeams(irradiance);
      fixupCubemapSeams(prefiltered);
      saveBakeCache(settings, environment, irradiance, prefiltered, brdf);
    }

    environmentMap.create(deviceContext, commandContext, environment);
    irradianceMap.create(deviceContext, commandContext, irradiance);
    prefilteredMap.create(deviceContext, commandContext, prefiltered);
    brdfLut.create(deviceContext, commandContext, settings.brdfResolution,
                   settings.brdfResolution, brdf);

    const Sampler::Config cubeSamplerConfig{
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxLod = 0.0f,
    };
    environmentSampler.create(deviceContext, cubeSamplerConfig);
    irradianceSampler.create(deviceContext, cubeSamplerConfig);
    prefilteredSampler.create(
        deviceContext,
        Sampler::Config{
            .addressModeU = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW = vk::SamplerAddressMode::eClampToEdge,
            .maxLod = static_cast<float>(
                std::max(prefilteredMap.mipLevelCount(), 1u) - 1u),
        });
    brdfSampler.create(
        deviceContext,
        Sampler::Config{
            .addressModeU = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW = vk::SamplerAddressMode::eClampToEdge,
            .anisotropyEnable = false,
            .maxLod = 0.0f,
        });
  }

  void rebuild(DeviceContext &deviceContext, CommandContext &commandContext,
               const ImageBasedLightingBakeSettings &settings) {
    create(deviceContext, commandContext, settings);
  }

private:
  struct EnvironmentSource {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<float> rgbaPixels;

    bool isHdr() const {
      return width > 0 && height > 0 &&
             rgbaPixels.size() == static_cast<size_t>(width) * height * 4;
    }
  };

  static constexpr uint64_t kBakeCacheMagic = 0x49424C4341434831ull;

  template <typename T>
  static void writeBinary(std::ofstream &stream, const T &value) {
    stream.write(reinterpret_cast<const char *>(&value), sizeof(T));
  }

  template <typename T>
  static bool readBinary(std::ifstream &stream, T &value) {
    stream.read(reinterpret_cast<char *>(&value), sizeof(T));
    return stream.good();
  }

  static uint64_t fnv1a64(std::string_view text) {
    uint64_t hash = 14695981039346656037ull;
    for (const unsigned char c : text) {
      hash ^= static_cast<uint64_t>(c);
      hash *= 1099511628211ull;
    }
    return hash;
  }

  static std::string buildBakeCacheKey(
      const ImageBasedLightingBakeSettings &settings) {
    std::ostringstream key;
    key << "envRes=" << settings.environmentResolution
        << ";irrRes=" << settings.irradianceResolution
        << ";prefRes=" << settings.prefilteredResolution
        << ";brdfRes=" << settings.brdfResolution
        << ";irrSamples=" << settings.irradianceSamples
        << ";prefSamples=" << settings.prefilteredSamples
        << ";brdfSamples=" << settings.brdfSamples;

    if (!settings.environmentHdrPath.empty()) {
      const std::filesystem::path hdrPath =
          std::filesystem::absolute(settings.environmentHdrPath)
              .lexically_normal();
      key << ";hdr=" << hdrPath.string();
      if (std::filesystem::exists(hdrPath)) {
        const auto hdrTimestamp =
            std::filesystem::last_write_time(hdrPath).time_since_epoch().count();
        key << ";hdrSize=" << std::filesystem::file_size(hdrPath);
        key << ";hdrMtime="
            << static_cast<long long>(hdrTimestamp);
      }
    } else {
      const auto &sky = settings.sky;
      key << ";zenith=" << sky.zenithColor.x << "," << sky.zenithColor.y << ","
          << sky.zenithColor.z << ";horizon=" << sky.horizonColor.x << ","
          << sky.horizonColor.y << "," << sky.horizonColor.z << ";ground="
          << sky.groundColor.x << "," << sky.groundColor.y << ","
          << sky.groundColor.z << ";sunColor=" << sky.sunColor.x << ","
          << sky.sunColor.y << "," << sky.sunColor.z << ";sunIntensity="
          << sky.sunIntensity << ";sunRadius=" << sky.sunAngularRadius
          << ";sunGlow=" << sky.sunGlow << ";horizonGlow="
          << sky.horizonGlow << ";skyExp=" << sky.skyExponent
          << ";groundFalloff=" << sky.groundFalloff << ";sunAz="
          << sky.sunAzimuthRadians << ";sunEl=" << sky.sunElevationRadians;
    }

    std::ostringstream hashed;
    hashed << std::hex << fnv1a64(key.str());
    return hashed.str();
  }

  static std::filesystem::path
  bakeCachePath(const ImageBasedLightingBakeSettings &settings) {
    return std::filesystem::path(".cache") / "ibl" /
           (buildBakeCacheKey(settings) + ".bin");
  }

  static void writeFloatVector(std::ofstream &stream,
                               const std::vector<float> &values) {
    const uint64_t valueCount = static_cast<uint64_t>(values.size());
    writeBinary(stream, valueCount);
    if (!values.empty()) {
      stream.write(reinterpret_cast<const char *>(values.data()),
                   static_cast<std::streamsize>(values.size() * sizeof(float)));
    }
  }

  static bool readFloatVector(std::ifstream &stream,
                              std::vector<float> &values) {
    uint64_t valueCount = 0;
    if (!readBinary(stream, valueCount)) {
      return false;
    }

    values.resize(static_cast<size_t>(valueCount));
    if (!values.empty()) {
      stream.read(reinterpret_cast<char *>(values.data()),
                  static_cast<std::streamsize>(values.size() * sizeof(float)));
    }
    return stream.good();
  }

  static void writeCubemapMipChain(
      std::ofstream &stream, const std::vector<CubemapMipLevelData> &mipChain) {
    const uint32_t mipCount = static_cast<uint32_t>(mipChain.size());
    writeBinary(stream, mipCount);
    for (const auto &mip : mipChain) {
      writeBinary(stream, mip.width);
      writeBinary(stream, mip.height);
      for (const auto &face : mip.faces) {
        writeFloatVector(stream, face);
      }
    }
  }

  static bool readCubemapMipChain(std::ifstream &stream,
                                  std::vector<CubemapMipLevelData> &mipChain) {
    uint32_t mipCount = 0;
    if (!readBinary(stream, mipCount)) {
      return false;
    }

    mipChain.clear();
    mipChain.resize(mipCount);
    for (auto &mip : mipChain) {
      if (!readBinary(stream, mip.width) || !readBinary(stream, mip.height)) {
        return false;
      }
      for (auto &face : mip.faces) {
        if (!readFloatVector(stream, face)) {
          return false;
        }
      }
    }
    return true;
  }

  static bool tryLoadBakeCache(const ImageBasedLightingBakeSettings &settings,
                               std::vector<CubemapMipLevelData> &environment,
                               std::vector<CubemapMipLevelData> &irradiance,
                               std::vector<CubemapMipLevelData> &prefiltered,
                               std::vector<float> &brdf) {
    const std::filesystem::path cachePath = bakeCachePath(settings);
    if (!std::filesystem::exists(cachePath)) {
      return false;
    }

    std::ifstream stream(cachePath, std::ios::binary);
    if (!stream.is_open()) {
      return false;
    }

    uint64_t magic = 0;
    if (!readBinary(stream, magic) || magic != kBakeCacheMagic) {
      return false;
    }

    uint32_t brdfWidth = 0;
    uint32_t brdfHeight = 0;
    if (!readCubemapMipChain(stream, environment) ||
        !readCubemapMipChain(stream, irradiance) ||
        !readCubemapMipChain(stream, prefiltered) ||
        !readBinary(stream, brdfWidth) || !readBinary(stream, brdfHeight) ||
        !readFloatVector(stream, brdf)) {
      return false;
    }

    return brdfWidth == settings.brdfResolution &&
           brdfHeight == settings.brdfResolution && stream.good();
  }

  static void saveBakeCache(const ImageBasedLightingBakeSettings &settings,
                            const std::vector<CubemapMipLevelData> &environment,
                            const std::vector<CubemapMipLevelData> &irradiance,
                            const std::vector<CubemapMipLevelData> &prefiltered,
                            const std::vector<float> &brdf) {
    const std::filesystem::path cachePath = bakeCachePath(settings);
    std::filesystem::create_directories(cachePath.parent_path());

    std::ofstream stream(cachePath, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
      return;
    }

    writeBinary(stream, kBakeCacheMagic);
    writeCubemapMipChain(stream, environment);
    writeCubemapMipChain(stream, irradiance);
    writeCubemapMipChain(stream, prefiltered);
    writeBinary(stream, settings.brdfResolution);
    writeBinary(stream, settings.brdfResolution);
    writeFloatVector(stream, brdf);
  }

  static EnvironmentSource
  loadEnvironmentSource(const ImageBasedLightingBakeSettings &settings) {
    if (settings.environmentHdrPath.empty() ||
        !std::filesystem::exists(settings.environmentHdrPath)) {
      return {};
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    float *pixels = stbi_loadf(settings.environmentHdrPath.c_str(), &width,
                               &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr || width <= 0 || height <= 0) {
      throw std::runtime_error("failed to load environment HDR image: " +
                               settings.environmentHdrPath);
    }

    EnvironmentSource source;
    source.width = static_cast<uint32_t>(width);
    source.height = static_cast<uint32_t>(height);
    source.rgbaPixels.assign(
        pixels, pixels + static_cast<size_t>(source.width) * source.height * 4);
    stbi_image_free(pixels);
    return source;
  }

  static glm::vec4 sampleEquirectBilinear(const EnvironmentSource &source,
                                          float u, float v) {
    const float wrappedU = u - std::floor(u);
    const float clampedV = glm::clamp(v, 0.0f, 1.0f);
    const float x = wrappedU * static_cast<float>(source.width);
    const float y = clampedV * static_cast<float>(source.height - 1);

    const uint32_t x0 =
        static_cast<uint32_t>(std::floor(x)) % std::max(source.width, 1u);
    const uint32_t x1 = (x0 + 1u) % std::max(source.width, 1u);
    const uint32_t y0 = std::min(static_cast<uint32_t>(std::floor(y)),
                                 source.height - 1u);
    const uint32_t y1 = std::min(y0 + 1u, source.height - 1u);
    const float tx = x - std::floor(x);
    const float ty = y - std::floor(y);

    auto readPixel = [&](uint32_t px, uint32_t py) {
      const size_t index = (static_cast<size_t>(py) * source.width + px) * 4;
      return glm::vec4(source.rgbaPixels[index + 0], source.rgbaPixels[index + 1],
                       source.rgbaPixels[index + 2],
                       source.rgbaPixels[index + 3]);
    };

    const glm::vec4 c00 = readPixel(x0, y0);
    const glm::vec4 c10 = readPixel(x1, y0);
    const glm::vec4 c01 = readPixel(x0, y1);
    const glm::vec4 c11 = readPixel(x1, y1);
    const glm::vec4 cx0 = glm::mix(c00, c10, tx);
    const glm::vec4 cx1 = glm::mix(c01, c11, tx);
    return glm::mix(cx0, cx1, ty);
  }

  static glm::vec3 sampleEnvironmentSource(
      const glm::vec3 &direction, const EnvironmentSource &source,
      const ProceduralSkySettings &settings) {
    if (!source.isHdr()) {
      return sampleProceduralSky(direction, settings);
    }

    const glm::vec3 dir = glm::normalize(direction);
    const float phi = std::atan2(dir.y, dir.x);
    const float theta = std::acos(glm::clamp(dir.z, -1.0f, 1.0f));
    const float u = phi / (2.0f * std::numbers::pi_v<float>) + 0.5f;
    const float v = theta / std::numbers::pi_v<float>;
    return glm::vec3(sampleEquirectBilinear(source, u, v));
  }

  static glm::vec3 faceTexelDirection(uint32_t face, float u, float v) {
    switch (face) {
    case 0:
      return glm::normalize(glm::vec3(1.0f, -v, -u));
    case 1:
      return glm::normalize(glm::vec3(-1.0f, -v, u));
    case 2:
      return glm::normalize(glm::vec3(u, 1.0f, v));
    case 3:
      return glm::normalize(glm::vec3(u, -1.0f, -v));
    case 4:
      return glm::normalize(glm::vec3(u, -v, 1.0f));
    default:
      return glm::normalize(glm::vec3(-u, -v, -1.0f));
    }
  }

  struct FaceUv {
    uint32_t face = 0;
    float u = 0.0f;
    float v = 0.0f;
  };

  enum class FaceEdge : uint32_t {
    Left = 0,
    Right = 1,
    Top = 2,
    Bottom = 3,
  };

  static FaceUv directionToFaceUv(const glm::vec3 &direction) {
    const glm::vec3 dir = glm::normalize(direction);
    const glm::vec3 absDir = glm::abs(dir);

    if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
      if (dir.x >= 0.0f) {
        return {.face = 0, .u = -dir.z / absDir.x, .v = -dir.y / absDir.x};
      }
      return {.face = 1, .u = dir.z / absDir.x, .v = -dir.y / absDir.x};
    }

    if (absDir.y >= absDir.x && absDir.y >= absDir.z) {
      if (dir.y >= 0.0f) {
        return {.face = 2, .u = dir.x / absDir.y, .v = dir.z / absDir.y};
      }
      return {.face = 3, .u = dir.x / absDir.y, .v = -dir.z / absDir.y};
    }

    if (dir.z >= 0.0f) {
      return {.face = 4, .u = dir.x / absDir.z, .v = -dir.y / absDir.z};
    }
    return {.face = 5, .u = -dir.x / absDir.z, .v = -dir.y / absDir.z};
  }

  static FaceEdge edgeFromUv(float u, float v) {
    const float duMin = std::abs(u + 1.0f);
    const float duMax = std::abs(u - 1.0f);
    const float dvMin = std::abs(v + 1.0f);
    const float dvMax = std::abs(v - 1.0f);

    float best = duMin;
    FaceEdge edge = FaceEdge::Left;
    if (duMax < best) {
      best = duMax;
      edge = FaceEdge::Right;
    }
    if (dvMin < best) {
      best = dvMin;
      edge = FaceEdge::Bottom;
    }
    if (dvMax < best) {
      edge = FaceEdge::Top;
    }
    return edge;
  }

  static std::pair<uint32_t, uint32_t> edgeTexelCoordinates(uint32_t size,
                                                            FaceEdge edge,
                                                            uint32_t index) {
    switch (edge) {
    case FaceEdge::Left:
      return {0u, index};
    case FaceEdge::Right:
      return {size - 1u, index};
    case FaceEdge::Top:
      return {index, size - 1u};
    case FaceEdge::Bottom:
    default:
      return {index, 0u};
    }
  }

  static std::pair<uint32_t, uint32_t> uvToTexelCoordinates(uint32_t size,
                                                            float u, float v) {
    const float xNorm = glm::clamp(u * 0.5f + 0.5f, 0.0f, 1.0f);
    const float yNorm = glm::clamp(v * 0.5f + 0.5f, 0.0f, 1.0f);
    const uint32_t x =
        std::min(static_cast<uint32_t>(std::round(xNorm * (size - 1u))),
                 size - 1u);
    const uint32_t y =
        std::min(static_cast<uint32_t>(std::round(yNorm * (size - 1u))),
                 size - 1u);
    return {x, y};
  }

  static glm::vec4 readTexel(const std::vector<float> &pixels, uint32_t size,
                             uint32_t x, uint32_t y) {
    const size_t pixelIndex = (static_cast<size_t>(y) * size + x) * 4;
    return {pixels[pixelIndex + 0], pixels[pixelIndex + 1], pixels[pixelIndex + 2],
            pixels[pixelIndex + 3]};
  }

  static void writeTexel(std::vector<float> &pixels, uint32_t size, uint32_t x,
                         uint32_t y, const glm::vec4 &value) {
    const size_t pixelIndex = (static_cast<size_t>(y) * size + x) * 4;
    pixels[pixelIndex + 0] = value.x;
    pixels[pixelIndex + 1] = value.y;
    pixels[pixelIndex + 2] = value.z;
    pixels[pixelIndex + 3] = value.w;
  }

  static uint64_t edgePairKey(uint32_t faceA, FaceEdge edgeA, uint32_t faceB,
                              FaceEdge edgeB) {
    const uint32_t a = (faceA << 2u) | static_cast<uint32_t>(edgeA);
    const uint32_t b = (faceB << 2u) | static_cast<uint32_t>(edgeB);
    const uint32_t lo = std::min(a, b);
    const uint32_t hi = std::max(a, b);
    return (static_cast<uint64_t>(lo) << 32u) | hi;
  }

  static void fixupCubemapSeams(std::vector<CubemapMipLevelData> &mipChain) {
    constexpr float kEdgeEpsilon = 0.02f;

    for (auto &mip : mipChain) {
      if (mip.width < 2 || mip.height < 2) {
        continue;
      }

      std::array<std::vector<float>, 6> original = mip.faces;
      std::set<uint64_t> processedPairs;

      for (uint32_t face = 0; face < 6; ++face) {
        for (FaceEdge edge : {FaceEdge::Left, FaceEdge::Right, FaceEdge::Top,
                              FaceEdge::Bottom}) {
          const uint32_t edgeLength =
              (edge == FaceEdge::Left || edge == FaceEdge::Right) ? mip.height
                                                                  : mip.width;

          const float edgeCoord =
              (edge == FaceEdge::Left || edge == FaceEdge::Bottom) ? -1.0f : 1.0f;
          const float neighborCoord =
              (edge == FaceEdge::Left || edge == FaceEdge::Bottom)
                  ? (-1.0f - kEdgeEpsilon)
                  : (1.0f + kEdgeEpsilon);

          const float firstCoord =
              ((0.5f) / static_cast<float>(edgeLength)) * 2.0f - 1.0f;
          const glm::vec3 probeDir =
              (edge == FaceEdge::Left || edge == FaceEdge::Right)
                  ? faceTexelDirection(face, neighborCoord, firstCoord)
                  : faceTexelDirection(face, firstCoord, neighborCoord);
          const FaceUv neighborSample = directionToFaceUv(probeDir);
          const FaceEdge neighborEdge =
              edgeFromUv(neighborSample.u, neighborSample.v);

          const uint64_t pairKey =
              edgePairKey(face, edge, neighborSample.face, neighborEdge);
          if (!processedPairs.insert(pairKey).second) {
            continue;
          }

          for (uint32_t index = 0; index < edgeLength; ++index) {
            const float varyingCoord =
                ((static_cast<float>(index) + 0.5f) /
                     static_cast<float>(edgeLength)) *
                    2.0f -
                1.0f;

            const glm::vec3 dir =
                (edge == FaceEdge::Left || edge == FaceEdge::Right)
                    ? faceTexelDirection(face, edgeCoord, varyingCoord)
                    : faceTexelDirection(face, varyingCoord, edgeCoord);
            const glm::vec3 neighborDir =
                (edge == FaceEdge::Left || edge == FaceEdge::Right)
                    ? faceTexelDirection(face, neighborCoord, varyingCoord)
                    : faceTexelDirection(face, varyingCoord, neighborCoord);

            const FaceUv currentUv = directionToFaceUv(dir);
            const FaceUv neighborUv = directionToFaceUv(neighborDir);
            if (currentUv.face != face) {
              continue;
            }

            const auto [xA, yA] = edgeTexelCoordinates(mip.width, edge, index);
            const auto [xB, yB] =
                uvToTexelCoordinates(mip.width, neighborUv.u, neighborUv.v);

            const glm::vec4 a = readTexel(original[face], mip.width, xA, yA);
            const glm::vec4 b =
                readTexel(original[neighborUv.face], mip.width, xB, yB);
            const glm::vec4 average = (a + b) * 0.5f;

            writeTexel(mip.faces[face], mip.width, xA, yA, average);
            writeTexel(mip.faces[neighborUv.face], mip.width, xB, yB, average);
          }
        }
      }
    }
  }

  static float radicalInverseVdC(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
  }

  static glm::vec2 hammersley(uint32_t index, uint32_t count) {
    return {static_cast<float>(index) / static_cast<float>(count),
            radicalInverseVdC(index)};
  }

  static glm::mat3 tangentBasis(const glm::vec3 &n) {
    const glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 tangent = glm::normalize(glm::cross(up, n));
    const glm::vec3 bitangent = glm::normalize(glm::cross(n, tangent));
    return glm::mat3(tangent, bitangent, n);
  }

  static glm::vec3 sampleCosineHemisphere(const glm::vec2 &xi) {
    const float r = std::sqrt(xi.x);
    const float phi = 2.0f * std::numbers::pi_v<float> * xi.y;
    return {r * std::cos(phi), r * std::sin(phi),
            std::sqrt(std::max(0.0f, 1.0f - xi.x))};
  }

  static glm::vec3 importanceSampleGgx(const glm::vec2 &xi,
                                       const glm::vec3 &normal,
                                       float roughness) {
    const float a = roughness * roughness;
    const float phi = 2.0f * std::numbers::pi_v<float> * xi.x;
    const float cosTheta = std::sqrt((1.0f - xi.y) /
                                     (1.0f + (a * a - 1.0f) * xi.y));
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const glm::vec3 halfway = {sinTheta * std::cos(phi), sinTheta * std::sin(phi),
                               cosTheta};
    return glm::normalize(tangentBasis(normal) * halfway);
  }

  static glm::vec3 sunDirection(const ProceduralSkySettings &settings) {
    const float cosElevation = std::cos(settings.sunElevationRadians);
    return glm::normalize(glm::vec3(
        cosElevation * std::cos(settings.sunAzimuthRadians),
        cosElevation * std::sin(settings.sunAzimuthRadians),
        std::sin(settings.sunElevationRadians)));
  }

  static glm::vec3 sampleProceduralSky(const glm::vec3 &direction,
                                       const ProceduralSkySettings &settings) {
    const glm::vec3 dir = glm::normalize(direction);
    const glm::vec3 sunDir = sunDirection(settings);

    const float skyT =
        std::pow(glm::clamp(dir.z * 0.5f + 0.5f, 0.0f, 1.0f), settings.skyExponent);
    glm::vec3 sky =
        glm::mix(settings.horizonColor, settings.zenithColor, skyT);

    const float groundT =
        std::pow(glm::clamp(-dir.z, 0.0f, 1.0f), settings.groundFalloff);
    glm::vec3 base = dir.z >= 0.0f ? sky : settings.groundColor * groundT;

    const float horizonMask =
        std::exp(-std::abs(dir.z) * 9.0f) * settings.horizonGlow;
    base += settings.horizonColor * horizonMask * 0.18f;

    const float sunDot = glm::clamp(glm::dot(dir, sunDir), 0.0f, 1.0f);
    const float sunDisk =
        std::pow(sunDot,
                 std::max(24.0f,
                          2.0f / std::max(settings.sunAngularRadius, 0.0025f)));
    const float sunHalo = std::pow(sunDot, 48.0f) * settings.sunGlow;
    const glm::vec3 sunRadiance =
        settings.sunColor * (settings.sunIntensity * sunDisk + sunHalo);

    return glm::max(base + sunRadiance, glm::vec3(0.0f));
  }

  static std::vector<CubemapMipLevelData>
  bakeEnvironmentMap(const ImageBasedLightingBakeSettings &settings,
                     const EnvironmentSource &environmentSource) {
    CubemapMipLevelData mip{
        .width = settings.environmentResolution,
        .height = settings.environmentResolution,
    };

    for (uint32_t face = 0; face < 6; ++face) {
      auto &pixels = mip.faces[face];
      pixels.resize(static_cast<size_t>(mip.width) * mip.height * 4);

      for (uint32_t y = 0; y < mip.height; ++y) {
        for (uint32_t x = 0; x < mip.width; ++x) {
          const float u = ((static_cast<float>(x) + 0.5f) /
                               static_cast<float>(mip.width) *
                               2.0f) -
                          1.0f;
          const float v = ((static_cast<float>(y) + 0.5f) /
                               static_cast<float>(mip.height) *
                               2.0f) -
                          1.0f;
          const glm::vec3 color = sampleEnvironmentSource(
              faceTexelDirection(face, u, v), environmentSource, settings.sky);
          const size_t pixelIndex =
              (static_cast<size_t>(y) * mip.width + x) * 4;
          pixels[pixelIndex + 0] = color.r;
          pixels[pixelIndex + 1] = color.g;
          pixels[pixelIndex + 2] = color.b;
          pixels[pixelIndex + 3] = 1.0f;
        }
      }
    }

    return {std::move(mip)};
  }

  static std::vector<CubemapMipLevelData>
  bakeIrradianceMap(const ImageBasedLightingBakeSettings &settings,
                    const EnvironmentSource &environmentSource) {
    CubemapMipLevelData mip{
        .width = settings.irradianceResolution,
        .height = settings.irradianceResolution,
    };

    for (uint32_t face = 0; face < 6; ++face) {
      auto &pixels = mip.faces[face];
      pixels.resize(static_cast<size_t>(mip.width) * mip.height * 4);

      for (uint32_t y = 0; y < mip.height; ++y) {
        for (uint32_t x = 0; x < mip.width; ++x) {
          const float u = ((static_cast<float>(x) + 0.5f) /
                               static_cast<float>(mip.width) *
                               2.0f) -
                          1.0f;
          const float v = ((static_cast<float>(y) + 0.5f) /
                               static_cast<float>(mip.height) *
                               2.0f) -
                          1.0f;
          const glm::vec3 normal = faceTexelDirection(face, u, v);
          const glm::mat3 basis = tangentBasis(normal);

          glm::vec3 accum(0.0f);
          for (uint32_t sampleIndex = 0; sampleIndex < settings.irradianceSamples;
               ++sampleIndex) {
            const glm::vec3 localSample =
                sampleCosineHemisphere(hammersley(sampleIndex,
                                                  settings.irradianceSamples));
            const glm::vec3 worldSample = glm::normalize(basis * localSample);
            accum +=
                sampleEnvironmentSource(worldSample, environmentSource, settings.sky);
          }

          const glm::vec3 irradiance =
              accum * (std::numbers::pi_v<float> /
                       static_cast<float>(settings.irradianceSamples));
          const size_t pixelIndex =
              (static_cast<size_t>(y) * mip.width + x) * 4;
          pixels[pixelIndex + 0] = irradiance.r;
          pixels[pixelIndex + 1] = irradiance.g;
          pixels[pixelIndex + 2] = irradiance.b;
          pixels[pixelIndex + 3] = 1.0f;
        }
      }
    }

    return {std::move(mip)};
  }

  static std::vector<CubemapMipLevelData>
  bakePrefilteredMap(const ImageBasedLightingBakeSettings &settings,
                     const EnvironmentSource &environmentSource) {
    const uint32_t mipCount = std::max(
        1u, static_cast<uint32_t>(
                std::floor(std::log2(settings.prefilteredResolution))) +
                1u);
    std::vector<CubemapMipLevelData> mipChain;
    mipChain.reserve(mipCount);

    for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex) {
      const uint32_t size =
          std::max(settings.prefilteredResolution >> mipIndex, 1u);
      const float roughness =
          mipCount == 1
              ? 0.0f
              : static_cast<float>(mipIndex) /
                    static_cast<float>(mipCount - 1);

      CubemapMipLevelData mip{.width = size, .height = size};
      for (uint32_t face = 0; face < 6; ++face) {
        auto &pixels = mip.faces[face];
        pixels.resize(static_cast<size_t>(size) * size * 4);

        for (uint32_t y = 0; y < size; ++y) {
          for (uint32_t x = 0; x < size; ++x) {
            const float u =
                ((static_cast<float>(x) + 0.5f) / static_cast<float>(size) *
                     2.0f) -
                1.0f;
            const float v =
                ((static_cast<float>(y) + 0.5f) / static_cast<float>(size) *
                     2.0f) -
                1.0f;
            const glm::vec3 normal = faceTexelDirection(face, u, v);
            const glm::vec3 view = normal;

            glm::vec3 accum(0.0f);
            float weight = 0.0f;
            for (uint32_t sampleIndex = 0;
                 sampleIndex < settings.prefilteredSamples; ++sampleIndex) {
              const glm::vec3 halfway = importanceSampleGgx(
                  hammersley(sampleIndex, settings.prefilteredSamples), normal,
                  std::max(roughness, 0.02f));
              const glm::vec3 light = glm::normalize(2.0f * glm::dot(view, halfway) *
                                                         halfway -
                                                     view);
              const float ndotl = glm::clamp(glm::dot(normal, light), 0.0f, 1.0f);
              if (ndotl <= 0.0f) {
                continue;
              }

              accum +=
                  sampleEnvironmentSource(light, environmentSource, settings.sky) *
                  ndotl;
              weight += ndotl;
            }

            const glm::vec3 color =
                weight > 0.0f ? accum / weight : glm::vec3(0.0f);
            const size_t pixelIndex =
                (static_cast<size_t>(y) * size + x) * 4;
            pixels[pixelIndex + 0] = color.r;
            pixels[pixelIndex + 1] = color.g;
            pixels[pixelIndex + 2] = color.b;
            pixels[pixelIndex + 3] = 1.0f;
          }
        }
      }
      mipChain.push_back(std::move(mip));
    }

    return mipChain;
  }

  static float distributionGgx(float nDotH, float roughness) {
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float denom = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
    return a2 / std::max(std::numbers::pi_v<float> * denom * denom, 1e-5f);
  }

  static float geometrySchlickGgx(float nDotV, float roughness) {
    const float r = roughness + 1.0f;
    const float k = (r * r) / 8.0f;
    return nDotV / std::max(nDotV * (1.0f - k) + k, 1e-5f);
  }

  static float geometrySmith(float nDotV, float nDotL, float roughness) {
    return geometrySchlickGgx(nDotV, roughness) *
           geometrySchlickGgx(nDotL, roughness);
  }

  static glm::vec2 integrateBrdf(float nDotV, float roughness,
                                 uint32_t sampleCount) {
    glm::vec3 view(std::sqrt(std::max(0.0f, 1.0f - nDotV * nDotV)), 0.0f, nDotV);
    const glm::vec3 normal(0.0f, 0.0f, 1.0f);

    float accumA = 0.0f;
    float accumB = 0.0f;
    for (uint32_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
      const glm::vec3 halfway = importanceSampleGgx(
          hammersley(sampleIndex, sampleCount), normal, std::max(roughness, 0.02f));
      const glm::vec3 light = glm::normalize(2.0f * glm::dot(view, halfway) *
                                                 halfway -
                                             view);

      const float nDotL = glm::clamp(light.z, 0.0f, 1.0f);
      const float nDotH = glm::clamp(halfway.z, 0.0f, 1.0f);
      const float vDotH = glm::clamp(glm::dot(view, halfway), 0.0f, 1.0f);
      if (nDotL <= 0.0f) {
        continue;
      }

      const float g = geometrySmith(nDotV, nDotL, roughness);
      const float gVis = (g * vDotH) / std::max(nDotH * nDotV, 1e-5f);
      const float fc = std::pow(1.0f - vDotH, 5.0f);
      accumA += (1.0f - fc) * gVis;
      accumB += fc * gVis;
    }

    return {accumA / static_cast<float>(sampleCount),
            accumB / static_cast<float>(sampleCount)};
  }

  static std::vector<float>
  bakeBrdfLut(const ImageBasedLightingBakeSettings &settings) {
    std::vector<float> pixels(static_cast<size_t>(settings.brdfResolution) *
                              settings.brdfResolution * 4);

    for (uint32_t y = 0; y < settings.brdfResolution; ++y) {
      for (uint32_t x = 0; x < settings.brdfResolution; ++x) {
        const float nDotV =
            (static_cast<float>(x) + 0.5f) /
            static_cast<float>(settings.brdfResolution);
        const float roughness =
            (static_cast<float>(y) + 0.5f) /
            static_cast<float>(settings.brdfResolution);
        const glm::vec2 integrated =
            integrateBrdf(nDotV, roughness, settings.brdfSamples);
        const size_t pixelIndex =
            (static_cast<size_t>(y) * settings.brdfResolution + x) * 4;
        pixels[pixelIndex + 0] = integrated.x;
        pixels[pixelIndex + 1] = integrated.y;
        pixels[pixelIndex + 2] = 0.0f;
        pixels[pixelIndex + 3] = 1.0f;
      }
    }

    return pixels;
  }
};
