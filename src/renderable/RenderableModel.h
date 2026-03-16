#pragma once

#include "../renderer/RenderPass.h"
#include "FrameGeometryUniforms.h"
#include "GltfModelAsset.h"
#include "ModelAsset.h"
#include "ModelMaterialSet.h"
#include "ObjModelAsset.h"
#include "Sampler.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

class RenderableModel {
public:
  using MaterialOverrideFn =
      std::function<void(std::vector<ModelMaterialData> &)>;

  void loadFromObj(const std::string &path, CommandContext &commandContext,
                   DeviceContext &deviceContext,
                   const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                   FrameGeometryUniforms &frameGeometryUniforms,
                   Sampler &sampler, uint32_t framesInFlight,
                   MaterialOverrideFn materialOverride = nullptr) {
    loadAsset<ObjModelAsset>(path, commandContext, deviceContext,
                             descriptorSetLayout, frameGeometryUniforms,
                             sampler, framesInFlight, materialOverride);
  }

  void loadFromGltf(const std::string &path, CommandContext &commandContext,
                    DeviceContext &deviceContext,
                    const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                    FrameGeometryUniforms &frameGeometryUniforms,
                    Sampler &sampler, uint32_t framesInFlight,
                    MaterialOverrideFn materialOverride = nullptr) {
    loadAsset<GltfModelAsset>(path, commandContext, deviceContext,
                              descriptorSetLayout, frameGeometryUniforms,
                              sampler, framesInFlight, materialOverride);
  }

  void loadFromFile(const std::string &path, CommandContext &commandContext,
                    DeviceContext &deviceContext,
                    const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                    FrameGeometryUniforms &frameGeometryUniforms,
                    Sampler &sampler, uint32_t framesInFlight,
                    MaterialOverrideFn materialOverride = nullptr) {
    const std::string extension =
        std::filesystem::path(path).extension().string();
    if (extension == ".obj") {
      loadFromObj(path, commandContext, deviceContext, descriptorSetLayout,
                  frameGeometryUniforms, sampler, framesInFlight,
                  materialOverride);
      return;
    }

    if (extension == ".gltf" || extension == ".glb") {
      loadFromGltf(path, commandContext, deviceContext, descriptorSetLayout,
                   frameGeometryUniforms, sampler, framesInFlight,
                   materialOverride);
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
          .descriptorBindings = &materialSet.bindingsForMaterialIndex(-1),
          .targetPass = targetPass,
      });
      return items;
    }

    for (const auto &submesh : submeshes) {
      items.push_back(RenderItem{
          .mesh = &asset->mesh(),
          .descriptorBindings =
              &materialSet.bindingsForMaterialIndex(submesh.materialIndex),
          .targetPass = targetPass,
          .indexOffset = submesh.indexOffset,
          .indexCount = submesh.indexCount,
      });
    }

    return items;
  }

  std::vector<ModelMaterialData> &mutableMaterials() {
    if (asset == nullptr) {
      throw std::runtime_error("RenderableModel has no loaded asset");
    }
    return asset->mutableMaterials();
  }

  const std::vector<ModelMaterialData> &materials() const {
    if (asset == nullptr) {
      throw std::runtime_error("RenderableModel has no loaded asset");
    }
    return asset->materials();
  }

  void syncMaterialParameters() {
    if (asset == nullptr) {
      throw std::runtime_error("RenderableModel has no loaded asset");
    }
    materialSet.updateMaterialParameters(asset->materials());
  }

  const ModelAsset *modelAsset() const { return asset.get(); }

private:
  template <typename TAsset>
  void loadAsset(const std::string &path, CommandContext &commandContext,
                 DeviceContext &deviceContext,
                 const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                 FrameGeometryUniforms &frameGeometryUniforms, Sampler &sampler,
                 uint32_t framesInFlight,
                 const MaterialOverrideFn &materialOverride = nullptr) {
    auto loadedAsset = std::make_unique<TAsset>();
    loadedAsset->load(path);
    loadedAsset->createGpuBuffers(commandContext, deviceContext);
    if (materialOverride) {
      materialOverride(loadedAsset->mutableMaterials());
    }
    materialSet.create(deviceContext, commandContext, descriptorSetLayout,
                       frameGeometryUniforms, sampler, loadedAsset->materials(),
                       framesInFlight);
    asset = std::move(loadedAsset);
  }

  std::unique_ptr<ModelAsset> asset;
  ModelMaterialSet materialSet;
};
