#pragma once

#include "FrameGeometryUniforms.h"
#include "RenderUtils.h"
#include "Sampler.h"
#include "Texture.h"
#include <array>
#include <cstring>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

struct MaterialUniformData {
  alignas(16) glm::vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
  alignas(16) glm::vec4 emissiveFactor{0.0f, 0.0f, 0.0f, 1.0f};
  alignas(16) glm::vec4 surfaceParams{0.0f, 1.0f, 1.0f, 1.0f};
};

class DescriptorBindings {
public:
  void create(DeviceContext &deviceContext,
              const vk::raii::DescriptorSetLayout &descriptorSetLayout,
              FrameGeometryUniforms &frameGeometryUniforms,
              Texture &baseColorTexture,
              Texture &metallicRoughnessTexture, Texture &normalTexture,
              Texture &emissiveTexture, Texture &occlusionTexture,
              Sampler &sampler, const MaterialUniformData &materialUniform,
              uint32_t framesInFlight) {
    createMaterialBuffer(deviceContext, materialUniform);

    std::array poolSize{
        vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer,
                               framesInFlight * 2),
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler,
                               framesInFlight * 5)};
    vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = framesInFlight,
        .poolSizeCount = static_cast<uint32_t>(poolSize.size()),
        .pPoolSizes = poolSize.data()};
    descriptorPool =
        vk::raii::DescriptorPool(deviceContext.deviceHandle(), poolInfo);

    std::vector<vk::DescriptorSetLayout> layouts(framesInFlight,
                                                 descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data()};

    descriptorSets.clear();
    descriptorSets =
        deviceContext.deviceHandle().allocateDescriptorSets(allocInfo);

    for (uint32_t i = 0; i < framesInFlight; i++) {
      vk::DescriptorBufferInfo bufferInfo{
          .buffer = frameGeometryUniforms.buffer(i),
          .offset = 0,
          .range = sizeof(GeometryUniformData)};
      vk::DescriptorBufferInfo materialBufferInfo{
          .buffer = materialBuffer,
          .offset = 0,
          .range = sizeof(MaterialUniformData)};
      vk::DescriptorImageInfo baseColorImageInfo{
          .sampler = sampler.handle(),
          .imageView = baseColorTexture.imageView(),
          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
      vk::DescriptorImageInfo metallicRoughnessImageInfo{
          .sampler = sampler.handle(),
          .imageView = metallicRoughnessTexture.imageView(),
          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
      vk::DescriptorImageInfo normalImageInfo{
          .sampler = sampler.handle(),
          .imageView = normalTexture.imageView(),
          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
      vk::DescriptorImageInfo emissiveImageInfo{
          .sampler = sampler.handle(),
          .imageView = emissiveTexture.imageView(),
          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
      vk::DescriptorImageInfo occlusionImageInfo{
          .sampler = sampler.handle(),
          .imageView = occlusionTexture.imageView(),
          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
      std::array descriptorWrites{
          vk::WriteDescriptorSet{.dstSet = descriptorSets[i],
                                 .dstBinding = 0,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType =
                                     vk::DescriptorType::eUniformBuffer,
                                 .pBufferInfo = &bufferInfo},
          vk::WriteDescriptorSet{.dstSet = descriptorSets[i],
                                 .dstBinding = 1,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType =
                                     vk::DescriptorType::eCombinedImageSampler,
                                 .pImageInfo = &baseColorImageInfo},
          vk::WriteDescriptorSet{.dstSet = descriptorSets[i],
                                 .dstBinding = 2,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType =
                                     vk::DescriptorType::eCombinedImageSampler,
                                 .pImageInfo = &metallicRoughnessImageInfo},
          vk::WriteDescriptorSet{.dstSet = descriptorSets[i],
                                 .dstBinding = 3,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType =
                                     vk::DescriptorType::eCombinedImageSampler,
                                 .pImageInfo = &normalImageInfo},
          vk::WriteDescriptorSet{.dstSet = descriptorSets[i],
                                 .dstBinding = 4,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType =
                                     vk::DescriptorType::eCombinedImageSampler,
                                 .pImageInfo = &emissiveImageInfo},
          vk::WriteDescriptorSet{.dstSet = descriptorSets[i],
                                 .dstBinding = 5,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType =
                                     vk::DescriptorType::eCombinedImageSampler,
                                 .pImageInfo = &occlusionImageInfo},
          vk::WriteDescriptorSet{.dstSet = descriptorSets[i],
                                 .dstBinding = 6,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType =
                                     vk::DescriptorType::eUniformBuffer,
                                 .pBufferInfo = &materialBufferInfo}};
      deviceContext.deviceHandle().updateDescriptorSets(descriptorWrites, {});
    }
  }

  void updateMaterialUniform(const MaterialUniformData &materialUniform) {
    if (materialBufferMapped == nullptr) {
      return;
    }
    std::memcpy(materialBufferMapped, &materialUniform,
                sizeof(MaterialUniformData));
  }

  vk::raii::DescriptorSet &descriptorSet(uint32_t frameIndex) {
    return descriptorSets[frameIndex];
  }

private:
  void createMaterialBuffer(DeviceContext &deviceContext,
                            const MaterialUniformData &materialUniform) {
    RenderUtils::createBuffer(deviceContext, sizeof(MaterialUniformData),
                              vk::BufferUsageFlagBits::eUniformBuffer,
                              vk::MemoryPropertyFlagBits::eHostVisible |
                                  vk::MemoryPropertyFlagBits::eHostCoherent,
                              materialBuffer, materialBufferMemory);

    materialBufferMapped =
        materialBufferMemory.mapMemory(0, sizeof(MaterialUniformData));
    std::memcpy(materialBufferMapped, &materialUniform,
                sizeof(MaterialUniformData));
  }

  vk::raii::Buffer materialBuffer = nullptr;
  vk::raii::DeviceMemory materialBufferMemory = nullptr;
  void *materialBufferMapped = nullptr;
  vk::raii::DescriptorPool descriptorPool = nullptr;
  std::vector<vk::raii::DescriptorSet> descriptorSets;
};
