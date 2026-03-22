#pragma once

#include "engine/animation/AnimationPlayback.h"
#include "engine/animation/SkeletonPose.h"
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
    return asset != nullptr && asset->skeletonAsset() != nullptr &&
                   !asset->skeletonAsset()->animations.empty()
               ? &animationPlayback
               : nullptr;
  }

  const AnimationPlaybackState *currentAnimationPlayback() const {
    return asset != nullptr && asset->skeletonAsset() != nullptr &&
                   !asset->skeletonAsset()->animations.empty()
               ? &animationPlayback
               : nullptr;
  }

  SkeletonPose *mutableSkeletonPose() {
    return skeletonPose && asset != nullptr && asset->skeletonAsset() != nullptr
               ? &*skeletonPose
               : nullptr;
  }

  const SkeletonPose *currentSkeletonPose() const {
    return skeletonPose && asset != nullptr && asset->skeletonAsset() != nullptr
               ? &*skeletonPose
               : nullptr;
  }

  void resetSkeletonPose() {
    if (asset == nullptr || asset->skeletonAsset() == nullptr) {
      skeletonPose.reset();
      return;
    }

    if (!skeletonPose.has_value()) {
      skeletonPose.emplace();
    }
    skeletonPose->resetToBindPose(*asset->skeletonAsset());
  }

  bool hasSelectedAnimation() const {
    return asset != nullptr && asset->skeletonAsset() != nullptr &&
           animationPlayback.selectedSourceAnimationIndex >= 0 &&
           static_cast<size_t>(animationPlayback.selectedSourceAnimationIndex) <
               asset->skeletonAsset()->animations.size();
  }

  const AnimationClipData *selectedAnimationClip() const {
    return hasSelectedAnimation()
               ? &asset->skeletonAsset()->animations[static_cast<size_t>(
                     animationPlayback.selectedSourceAnimationIndex)]
               : nullptr;
  }

  void selectSourceAnimation(int animationIndex) {
    animationPlayback.selectedSourceAnimationIndex = animationIndex;
    animationPlayback.currentTimeSeconds = 0.0f;
    animationPlayback.playing = false;
    sampleSelectedAnimation();
  }

  void playSelectedAnimation() {
    if (selectedAnimationClip() != nullptr) {
      animationPlayback.playing = true;
    }
  }

  void pauseAnimationPlayback() { animationPlayback.playing = false; }

  void resetSelectedAnimation() {
    animationPlayback.currentTimeSeconds = 0.0f;
    animationPlayback.playing = false;
    sampleSelectedAnimation();
  }

  void sampleSelectedAnimation() {
    if (asset == nullptr || asset->skeletonAsset() == nullptr ||
        !skeletonPose.has_value()) {
      return;
    }

    const SkeletonAssetData &skeleton = *asset->skeletonAsset();
    const AnimationClipData *clip = selectedAnimationClip();
    if (clip == nullptr) {
      skeletonPose->resetToBindPose(skeleton);
      return;
    }

    sampleAnimationClipIntoPose(skeleton, *clip,
                                animationPlayback.currentTimeSeconds,
                                *skeletonPose);
  }

  void updateAnimationPlayback(float deltaSeconds) {
    if (!animationPlayback.playing || asset == nullptr ||
        asset->skeletonAsset() == nullptr || !skeletonPose.has_value()) {
      return;
    }

    const AnimationClipData *clip = selectedAnimationClip();
    if (clip == nullptr) {
      animationPlayback.playing = false;
      return;
    }

    const float duration = std::max(clip->durationSeconds, 0.0f);
    if (duration <= 0.0f) {
      animationPlayback.currentTimeSeconds = 0.0f;
      sampleSelectedAnimation();
      animationPlayback.playing = false;
      return;
    }

    animationPlayback.currentTimeSeconds +=
        deltaSeconds * animationPlayback.speed;

    if (animationPlayback.loop) {
      while (animationPlayback.currentTimeSeconds > duration) {
        animationPlayback.currentTimeSeconds -= duration;
      }
      while (animationPlayback.currentTimeSeconds < 0.0f) {
        animationPlayback.currentTimeSeconds += duration;
      }
    } else {
      animationPlayback.currentTimeSeconds = glm::clamp(
          animationPlayback.currentTimeSeconds, 0.0f, duration);
      if (animationPlayback.currentTimeSeconds >= duration) {
        animationPlayback.playing = false;
      }
    }

    sampleSelectedAnimation();
  }

  void updateSkinPalettes(uint32_t frameIndex) {
    if (asset == nullptr || asset->skeletonAsset() == nullptr ||
        !skeletonPose.has_value()) {
      return;
    }

    const SkeletonAssetData &skeleton = *asset->skeletonAsset();
    const auto &submeshes = asset->submeshes();
    for (size_t submeshIndex = 0; submeshIndex < submeshes.size();
         ++submeshIndex) {
      if (submeshIndex >= skinPaletteBindings.size() ||
          !skinPaletteBindings[submeshIndex].valid) {
        continue;
      }

      const auto &submesh = submeshes[submeshIndex];
      if (submesh.skinIndex < 0 ||
          static_cast<size_t>(submesh.skinIndex) >= skeleton.skins.size()) {
        continue;
      }

      const SkinData &skin =
          skeleton.skins[static_cast<size_t>(submesh.skinIndex)];
      if (skin.jointNodeIndices.size() > MAX_SKIN_JOINTS) {
        throw std::runtime_error("skin joint count exceeds MAX_SKIN_JOINTS");
      }

      SkinPaletteUniformData palette{};
      for (auto &jointMatrix : palette.joints) {
        jointMatrix = glm::mat4(1.0f);
      }

      glm::mat4 meshNodeWorld = submesh.transform;
      if (submesh.nodeIndex >= 0 &&
          static_cast<size_t>(submesh.nodeIndex) < skeleton.nodes.size()) {
        meshNodeWorld =
            skeletonPose->worldTransform(static_cast<size_t>(submesh.nodeIndex));
      }
      const glm::mat4 inverseMeshNodeWorld = glm::inverse(meshNodeWorld);

      for (size_t jointIndex = 0; jointIndex < skin.jointNodeIndices.size();
           ++jointIndex) {
        const int jointNodeIndex = skin.jointNodeIndices[jointIndex];
        if (jointNodeIndex < 0 ||
            static_cast<size_t>(jointNodeIndex) >= skeleton.nodes.size()) {
          continue;
        }

        const glm::mat4 jointWorld =
            skeletonPose->worldTransform(static_cast<size_t>(jointNodeIndex));
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
      skeletonPose.emplace();
      skeletonPose->initialize(*loadedSkeleton);
      animationPlayback = AnimationPlaybackState{};
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
      skeletonPose.reset();
      animationPlayback = AnimationPlaybackState{};
      skinPaletteBindings.clear();
    }
    asset = std::move(loadedAsset);
  }

  std::unique_ptr<ModelAsset> asset;
  ModelMaterialSet materialSet;
  std::optional<SkeletonPose> skeletonPose;
  AnimationPlaybackState animationPlayback;
  std::vector<SubmeshSkinPaletteResource> skinPaletteBindings;
};
