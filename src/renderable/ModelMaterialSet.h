#pragma once

#include "DescriptorBindings.h"
#include "FrameUniforms.h"
#include "Mesh.h"
#include "Sampler.h"
#include "Texture.h"
#include <array>
#include <filesystem>
#include <vector>

class ModelMaterialSet {
public:
  void create(DeviceContext &deviceContext, CommandContext &commandContext,
              const vk::raii::DescriptorSetLayout &descriptorSetLayout,
              FrameUniforms &frameUniforms, Sampler &sampler,
              const std::vector<ModelMaterialData> &materials,
              uint32_t framesInFlight) {
    defaultMaterialResource = MaterialResource{};
    materialResources.clear();
    materialResources.reserve(materials.size());

    initializeResource(defaultMaterialResource, nullptr, deviceContext,
                       commandContext, descriptorSetLayout, frameUniforms,
                       sampler, framesInFlight);

    for (const auto &material : materials) {
      materialResources.emplace_back();
      initializeResource(materialResources.back(), &material, deviceContext,
                         commandContext, descriptorSetLayout, frameUniforms,
                         sampler, framesInFlight);
    }
  }

  DescriptorBindings &bindingsForMaterialIndex(int materialIndex) {
    if (materialIndex < 0 ||
        static_cast<size_t>(materialIndex) >= materialResources.size()) {
      return defaultMaterialResource.bindings;
    }

    return materialResources[materialIndex].bindings;
  }

  void updateMaterialParameters(
      const std::vector<ModelMaterialData> &materials) {
    const size_t updateCount =
        std::min(materials.size(), materialResources.size());
    for (size_t i = 0; i < updateCount; ++i) {
      materialResources[i].bindings.updateMaterialUniform(
          buildMaterialUniform(materials[i]));
    }
  }

private:
  struct MaterialResource {
    Texture baseColorTexture;
    Texture metallicRoughnessTexture;
    Texture normalTexture;
    Texture emissiveTexture;
    Texture occlusionTexture;
    DescriptorBindings bindings;
  };

  static std::array<uint8_t, 4> toRgba8(const glm::vec4 &color) {
    auto toByte = [](float channel) {
      return static_cast<uint8_t>(glm::clamp(channel, 0.0f, 1.0f) * 255.0f +
                                  0.5f);
    };

    return {toByte(color.r), toByte(color.g), toByte(color.b), toByte(color.a)};
  }

  static bool hasAvailableTexture(
      const ModelMaterialData::TextureSource &textureSource) {
    return textureSource.hasPath() &&
           std::filesystem::exists(textureSource.resolvedPath);
  }

  static bool hasTextureSource(
      const ModelMaterialData::TextureSource &textureSource) {
    return hasAvailableTexture(textureSource) || textureSource.hasEmbeddedRgba();
  }

  static void createTextureFromSource(
      Texture &texture, const ModelMaterialData::TextureSource &textureSource,
      const std::array<uint8_t, 4> &fallbackColor, TextureEncoding encoding,
      CommandContext &commandContext, DeviceContext &deviceContext) {
    if (hasAvailableTexture(textureSource)) {
      texture.create(textureSource.resolvedPath, commandContext, deviceContext,
                     encoding);
      return;
    }

    if (textureSource.hasEmbeddedRgba()) {
      texture.createRgba(textureSource.rgba.data(), textureSource.width,
                         textureSource.height, commandContext, deviceContext,
                         encoding);
      return;
    }

    texture.createSolidColor(fallbackColor, commandContext, deviceContext,
                             encoding);
  }

  static MaterialUniformData
  buildMaterialUniform(const ModelMaterialData &material) {
    const float normalScale =
        hasTextureSource(material.normalTexture) ? material.normalScale : 0.0f;
    return MaterialUniformData{
        .baseColorFactor = material.baseColorFactor,
        .emissiveFactor = glm::vec4(material.emissiveFactor, 1.0f),
        .surfaceParams = {material.metallicFactor, material.roughnessFactor,
                          normalScale, material.occlusionStrength},
    };
  }

  void initializeResource(
      MaterialResource &resource, const ModelMaterialData *material,
      DeviceContext &deviceContext, CommandContext &commandContext,
      const vk::raii::DescriptorSetLayout &descriptorSetLayout,
      FrameUniforms &frameUniforms, Sampler &sampler, uint32_t framesInFlight) {
    const ModelMaterialData defaultMaterial{};
    const ModelMaterialData &resolvedMaterial =
        material == nullptr ? defaultMaterial : *material;

    createTextureFromSource(resource.baseColorTexture,
                            resolvedMaterial.baseColorTexture,
                            {255, 255, 255, 255}, TextureEncoding::Srgb,
                            commandContext, deviceContext);
    createTextureFromSource(resource.metallicRoughnessTexture,
                            resolvedMaterial.metallicRoughnessTexture,
                            {255, 255, 255, 255}, TextureEncoding::Linear,
                            commandContext, deviceContext);
    createTextureFromSource(resource.normalTexture,
                            resolvedMaterial.normalTexture,
                            {128, 128, 255, 255}, TextureEncoding::Linear,
                            commandContext, deviceContext);
    createTextureFromSource(resource.emissiveTexture,
                            resolvedMaterial.emissiveTexture,
                            {255, 255, 255, 255}, TextureEncoding::Srgb,
                            commandContext, deviceContext);
    createTextureFromSource(resource.occlusionTexture,
                            resolvedMaterial.occlusionTexture,
                            {255, 255, 255, 255}, TextureEncoding::Linear,
                            commandContext, deviceContext);

    MaterialUniformData materialUniform = buildMaterialUniform(resolvedMaterial);

    resource.bindings.create(
        deviceContext, descriptorSetLayout, frameUniforms,
        resource.baseColorTexture, resource.metallicRoughnessTexture,
        resource.normalTexture, resource.emissiveTexture,
        resource.occlusionTexture, sampler, materialUniform, framesInFlight);
  }

  MaterialResource defaultMaterialResource;
  std::vector<MaterialResource> materialResources;
};
