#pragma once

#include "engine/animation/ModelAnimationState.h"
#include "renderer/core/RenderPass.h"
#include "renderer/resources/FrameGeometryUniforms.h"
#include "renderer/resources/SkinPaletteBindings.h"
#include "GltfModelAsset.h"
#include "ModelAsset.h"
#include "renderer/resources/ModelMaterialSet.h"
#include "ObjModelAsset.h"
#include "renderer/resources/Sampler.h"
#include <filesystem>
#include <functional>
#include <glm/gtc/matrix_inverse.hpp>
#include <memory>
#include <optional>
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
                   const vk::raii::DescriptorSetLayout *secondaryDescriptorSetLayout,
                   FrameGeometryUniforms &frameGeometryUniforms,
                   Sampler &sampler, uint32_t framesInFlight,
                   MaterialOverrideFn materialOverride = nullptr) {
    loadAsset<ObjModelAsset>(path, commandContext, deviceContext,
                             descriptorSetLayout, secondaryDescriptorSetLayout,
                             frameGeometryUniforms,
                             sampler, framesInFlight, materialOverride);
  }

  void loadFromGltf(const std::string &path, CommandContext &commandContext,
                    DeviceContext &deviceContext,
                    const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                    const vk::raii::DescriptorSetLayout *secondaryDescriptorSetLayout,
                    FrameGeometryUniforms &frameGeometryUniforms,
                    Sampler &sampler, uint32_t framesInFlight,
                    MaterialOverrideFn materialOverride = nullptr) {
    loadAsset<GltfModelAsset>(path, commandContext, deviceContext,
                              descriptorSetLayout, secondaryDescriptorSetLayout,
                              frameGeometryUniforms,
                              sampler, framesInFlight, materialOverride);
  }

  void loadFromFile(const std::string &path, CommandContext &commandContext,
                    DeviceContext &deviceContext,
                    const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                    const vk::raii::DescriptorSetLayout *secondaryDescriptorSetLayout,
                    FrameGeometryUniforms &frameGeometryUniforms,
                    Sampler &sampler, uint32_t framesInFlight,
                    MaterialOverrideFn materialOverride = nullptr) {
    const std::string extension =
        std::filesystem::path(path).extension().string();
    if (extension == ".obj") {
      loadFromObj(path, commandContext, deviceContext, descriptorSetLayout,
                  secondaryDescriptorSetLayout, frameGeometryUniforms, sampler,
                  framesInFlight,
                  materialOverride);
      return;
    }

    if (extension == ".gltf" || extension == ".glb") {
      loadFromGltf(path, commandContext, deviceContext, descriptorSetLayout,
                   secondaryDescriptorSetLayout, frameGeometryUniforms, sampler,
                   framesInFlight,
                   materialOverride);
      return;
    }

    throw std::runtime_error("unsupported model format: " + extension);
  }

  std::vector<RenderItem>
  buildRenderItems(const RenderPass *targetPass,
                   const std::vector<glm::mat4> &itemModelMatrices = {},
                   int selectedBoneNodeIndex = -1) {
    if (asset == nullptr) {
      throw std::runtime_error("RenderableModel has no loaded asset");
    }

    std::vector<RenderItem> items;
    const auto &submeshes = asset->submeshes();
    items.reserve(submeshes.empty() ? 1 : submeshes.size());

    if (submeshes.empty()) {
      const glm::mat4 modelMatrix = itemModelMatrices.empty()
                                        ? glm::mat4(1.0f)
                                        : itemModelMatrices.front();
      items.push_back(RenderItem{
          .mesh = &asset->mesh(),
          .descriptorBindings = &materialSet.bindingsForMaterialIndex(-1),
          .secondaryDescriptorBindings = nullptr,
          .targetPass = targetPass,
          .modelMatrix = modelMatrix,
          .modelNormalMatrix = glm::inverseTranspose(modelMatrix),
          .boneWeightJointIndex = -1,
          .boneWeightDebugEnabled = 0,
          .skinningEnabled = 0,
      });
      return items;
    }

    const SkeletonAssetData *skeleton = asset->skeletonAsset();
    for (size_t index = 0; index < submeshes.size(); ++index) {
      const auto &submesh = submeshes[index];
      glm::mat4 modelMatrix = submesh.transform;
      if (itemModelMatrices.size() == 1) {
        modelMatrix = itemModelMatrices.front() * submesh.transform;
      } else if (itemModelMatrices.size() == submeshes.size()) {
        modelMatrix = itemModelMatrices[index];
      }

      int selectedJointIndex = -1;
      if (selectedBoneNodeIndex >= 0 && skeleton != nullptr &&
          submesh.skinIndex >= 0 &&
          static_cast<size_t>(submesh.skinIndex) < skeleton->skins.size()) {
        const auto &jointNodeIndices =
            skeleton->skins[static_cast<size_t>(submesh.skinIndex)]
                .jointNodeIndices;
        const auto selectedJointIt =
            std::find(jointNodeIndices.begin(), jointNodeIndices.end(),
                      selectedBoneNodeIndex);
        if (selectedJointIt != jointNodeIndices.end()) {
          selectedJointIndex =
              static_cast<int>(std::distance(jointNodeIndices.begin(),
                                             selectedJointIt));
        }
      }

      items.push_back(RenderItem{
          .mesh = &asset->mesh(),
          .descriptorBindings =
              &materialSet.bindingsForMaterialIndex(submesh.materialIndex),
          .secondaryDescriptorBindings =
              index < skinPaletteBindings.size() &&
                      skinPaletteBindings[index].valid
                  ? &skinPaletteBindings[index].bindings
                  : nullptr,
          .targetPass = targetPass,
          .indexOffset = submesh.indexOffset,
          .indexCount = submesh.indexCount,
          .modelMatrix = modelMatrix,
          .modelNormalMatrix = glm::inverseTranspose(modelMatrix),
          .boneWeightJointIndex = selectedJointIndex,
          .boneWeightDebugEnabled = selectedBoneNodeIndex >= 0 ? 1 : 0,
          .skinningEnabled =
              index < skinPaletteBindings.size() &&
                      skinPaletteBindings[index].valid
                  ? 1
                  : 0,
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

  AnimationPlaybackState *mutableAnimationPlayback() {
    return animationState.mutablePlayback(skeletonAsset());
  }

  const AnimationPlaybackState *currentAnimationPlayback() const {
    return animationState.currentPlayback(skeletonAsset());
  }

  SkeletonPose *mutableSkeletonPose() {
    return animationState.mutablePose(skeletonAsset());
  }

  const SkeletonPose *currentSkeletonPose() const {
    return animationState.currentPose(skeletonAsset());
  }

  void resetSkeletonPose() {
    animationState.resetPose(skeletonAsset());
  }

  bool hasSelectedAnimation() const {
    return animationState.hasSelectedAnimation(skeletonAsset());
  }

  const AnimationClipData *selectedAnimationClip() const {
    return animationState.selectedClip(skeletonAsset());
  }

  void selectSourceAnimation(int animationIndex) {
    animationState.selectSourceAnimation(skeletonAsset(), animationIndex);
  }

  void playSelectedAnimation() {
    animationState.playSelectedAnimation(skeletonAsset());
  }

  void pauseAnimationPlayback() { animationState.pauseAnimationPlayback(); }

  void resetSelectedAnimation() {
    animationState.resetSelectedAnimation(skeletonAsset());
  }

  void sampleSelectedAnimation() {
    animationState.sampleSelectedAnimation(skeletonAsset());
  }

  void updateAnimationPlayback(float deltaSeconds) {
    animationState.updatePlayback(skeletonAsset(), deltaSeconds);
  }

  void updateSkinPalettes(uint32_t frameIndex) {
    const SkeletonAssetData *skeleton = skeletonAsset();
    const SkeletonPose *pose = animationState.currentPose(skeleton);
    if (asset == nullptr || skeleton == nullptr || pose == nullptr) {
      return;
    }

    const auto &submeshes = asset->submeshes();
    for (size_t submeshIndex = 0; submeshIndex < submeshes.size();
         ++submeshIndex) {
      if (submeshIndex >= skinPaletteBindings.size() ||
          !skinPaletteBindings[submeshIndex].valid) {
        continue;
      }

      const auto &submesh = submeshes[submeshIndex];
      if (submesh.skinIndex < 0 ||
          static_cast<size_t>(submesh.skinIndex) >= skeleton->skins.size()) {
        continue;
      }

      const SkinData &skin =
          skeleton->skins[static_cast<size_t>(submesh.skinIndex)];
      if (skin.jointNodeIndices.size() > MAX_SKIN_JOINTS) {
        throw std::runtime_error("skin joint count exceeds MAX_SKIN_JOINTS");
      }

      SkinPaletteUniformData palette{};
      for (auto &jointMatrix : palette.joints) {
        jointMatrix = glm::mat4(1.0f);
      }

      glm::mat4 meshNodeWorld = submesh.transform;
      if (submesh.nodeIndex >= 0 &&
          static_cast<size_t>(submesh.nodeIndex) < skeleton->nodes.size()) {
        meshNodeWorld =
            pose->worldTransform(static_cast<size_t>(submesh.nodeIndex));
      }
      const glm::mat4 inverseMeshNodeWorld = glm::inverse(meshNodeWorld);

      for (size_t jointIndex = 0; jointIndex < skin.jointNodeIndices.size();
           ++jointIndex) {
        const int jointNodeIndex = skin.jointNodeIndices[jointIndex];
        if (jointNodeIndex < 0 ||
            static_cast<size_t>(jointNodeIndex) >= skeleton->nodes.size()) {
          continue;
        }

        const glm::mat4 jointWorld =
            pose->worldTransform(static_cast<size_t>(jointNodeIndex));
        palette.joints[jointIndex] =
            inverseMeshNodeWorld * jointWorld *
            skin.inverseBindMatrices[jointIndex];
      }

      skinPaletteBindings[submeshIndex].bindings.write(frameIndex, palette);
    }
  }

private:
  struct SubmeshSkinPaletteResource {
    SkinPaletteBindings bindings;
    bool valid = false;
  };

  const SkeletonAssetData *skeletonAsset() const {
    return asset == nullptr ? nullptr : asset->skeletonAsset();
  }

  template <typename TAsset>
  void loadAsset(const std::string &path, CommandContext &commandContext,
                 DeviceContext &deviceContext,
                 const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                 const vk::raii::DescriptorSetLayout *secondaryDescriptorSetLayout,
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
    if (const SkeletonAssetData *loadedSkeleton = loadedAsset->skeletonAsset();
        loadedSkeleton != nullptr && !loadedSkeleton->nodes.empty()) {
      animationState.reset(loadedSkeleton);
      skinPaletteBindings.clear();
      skinPaletteBindings.resize(loadedAsset->submeshes().size());
      if (secondaryDescriptorSetLayout != nullptr) {
        for (size_t submeshIndex = 0;
             submeshIndex < loadedAsset->submeshes().size(); ++submeshIndex) {
          if (loadedAsset->submeshes()[submeshIndex].skinIndex < 0) {
            continue;
          }
          skinPaletteBindings[submeshIndex].bindings.create(
              deviceContext, *secondaryDescriptorSetLayout, framesInFlight);
          skinPaletteBindings[submeshIndex].valid = true;
        }
      }
    } else {
      animationState.clear();
      skinPaletteBindings.clear();
    }
    asset = std::move(loadedAsset);
  }

  std::unique_ptr<ModelAsset> asset;
  ModelMaterialSet materialSet;
  ModelAnimationState animationState;
  std::vector<SubmeshSkinPaletteResource> skinPaletteBindings;
};
