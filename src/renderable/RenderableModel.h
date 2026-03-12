#pragma once

#include "../renderer/RenderPass.h"
#include "FrameUniforms.h"
#include "GltfModelAsset.h"
#include "ModelAsset.h"
#include "ModelMaterialSet.h"
#include "ObjModelAsset.h"
#include "Sampler.h"
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

class RenderableModel {
public:
  void loadFromObj(const std::string &path, CommandContext &commandContext,
                   DeviceContext &deviceContext,
                   const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                   FrameUniforms &frameUniforms, Sampler &sampler,
                   uint32_t framesInFlight) {
    loadAsset<ObjModelAsset>(path, commandContext, deviceContext,
                             descriptorSetLayout, frameUniforms, sampler,
                             framesInFlight);
  }

  void loadFromGltf(const std::string &path, CommandContext &commandContext,
                    DeviceContext &deviceContext,
                    const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                    FrameUniforms &frameUniforms, Sampler &sampler,
                    uint32_t framesInFlight) {
    loadAsset<GltfModelAsset>(path, commandContext, deviceContext,
                              descriptorSetLayout, frameUniforms, sampler,
                              framesInFlight);
  }

  void loadFromFile(const std::string &path, CommandContext &commandContext,
                    DeviceContext &deviceContext,
                    const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                    FrameUniforms &frameUniforms, Sampler &sampler,
                    uint32_t framesInFlight) {
    const std::string extension =
        std::filesystem::path(path).extension().string();
    if (extension == ".obj") {
      loadFromObj(path, commandContext, deviceContext, descriptorSetLayout,
                  frameUniforms, sampler, framesInFlight);
      return;
    }

    if (extension == ".gltf" || extension == ".glb") {
      loadFromGltf(path, commandContext, deviceContext, descriptorSetLayout,
                   frameUniforms, sampler, framesInFlight);
      return;
    }

    throw std::runtime_error("unsupported model format: " + extension);
  }

  std::vector<RenderItem> buildRenderItems(const RenderPass *targetPass) {
    if (asset == nullptr) {
      throw std::runtime_error("RenderableModel has no loaded asset");
    }

    std::vector<RenderItem> items;
    const auto &submeshes = asset->submeshes();
    items.reserve(submeshes.empty() ? 1 : submeshes.size());

    if (submeshes.empty()) {
      items.push_back(RenderItem{
          .mesh = &asset->mesh(),
          .descriptorBindings = &materials.bindingsForMaterialIndex(-1),
          .targetPass = targetPass,
      });
      return items;
    }

    for (const auto &submesh : submeshes) {
      items.push_back(RenderItem{
          .mesh = &asset->mesh(),
          .descriptorBindings =
              &materials.bindingsForMaterialIndex(submesh.materialIndex),
          .targetPass = targetPass,
          .indexOffset = submesh.indexOffset,
          .indexCount = submesh.indexCount,
      });
    }

    return items;
  }

  const ModelAsset *modelAsset() const { return asset.get(); }

private:
  template <typename TAsset>
  void loadAsset(const std::string &path, CommandContext &commandContext,
                 DeviceContext &deviceContext,
                 const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                 FrameUniforms &frameUniforms, Sampler &sampler,
                 uint32_t framesInFlight) {
    auto loadedAsset = std::make_unique<TAsset>();
    loadedAsset->load(path);
    loadedAsset->createGpuBuffers(commandContext, deviceContext);
    materials.create(deviceContext, commandContext, descriptorSetLayout,
                     frameUniforms, sampler, loadedAsset->materials(),
                     framesInFlight);
    asset = std::move(loadedAsset);
  }

  std::unique_ptr<ModelAsset> asset;
  ModelMaterialSet materials;
};
