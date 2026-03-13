#pragma once

#include "../renderer/FullscreenRenderPass.h"
#include "../renderable/ImageBasedLighting.h"
#include "GeometryPass.h"
#include <cstdint>
#include <stdexcept>
#include <vector>

enum class PbrDebugView : uint32_t {
  Final = 0,
  DirectLighting = 1,
  IblDiffuse = 2,
  IblSpecular = 3,
  AmbientTotal = 4,
  Reflections = 5,
  Background = 6,
};

struct PbrPassPushConstant {
  glm::vec4 projParams{1.0f, -1.0f, -1.0f, -0.1f};
  glm::vec4 viewRightAndBackground{1.0f, 0.0f, 0.0f, 1.0f};
  glm::vec4 viewUpAndDiffuse{0.0f, 1.0f, 0.0f, 1.0f};
  glm::vec4 viewForwardAndSpecular{0.0f, 0.0f, -1.0f, 1.0f};
  glm::vec4 lightDirectionAndRotation{0.0f, -1.0f, 0.0f, 0.0f};
  glm::vec4 lightColorAndPrefilterMip{1.0f, 1.0f, 1.0f, 0.0f};
  glm::vec4 specularTuning{2.0f, 0.0f, 0.0f, 0.0f};
  glm::uvec4 settings{0u, 0u, 0u, 0u};
};

class PbrPass : public FullscreenRenderPass {
public:
  enum SettingFlags : uint32_t {
    EnableIbl = 1u << 0,
    ShowBackground = 1u << 1,
  };

  PbrPass(PipelineSpec spec, uint32_t framesInFlight,
          const GeometryPass *sourcePass = nullptr)
      : FullscreenRenderPass(std::move(spec), framesInFlight,
                             RasterPassAttachmentConfig{
                                 .useColorAttachment = true,
                                 .useDepthAttachment = false,
                                 .useMsaaColorAttachment = false,
                                 .resolveToSwapchain = false,
                                 .useSwapchainColorAttachment = false,
                                 .offscreenColorFormat =
                                     vk::Format::eR16G16B16A16Sfloat,
                                 .sampleColorAttachment = true,
                             }),
        sourcePassRef(sourcePass) {}

  void setSourcePass(const GeometryPass &sourcePass) {
    sourcePassRef = &sourcePass;
  }

  void setImageBasedLighting(const ImageBasedLightingResources &ibl) {
    iblResources = &ibl;
    pushData.lightColorAndPrefilterMip.w =
        static_cast<float>(std::max(ibl.prefilteredMap.mipLevelCount(), 1u) - 1u);
  }

  void setCamera(const glm::mat4 &proj, const glm::mat4 &view) {
    pushData.projParams =
        glm::vec4(proj[0][0], proj[1][1], proj[2][2], proj[3][2]);

    const glm::mat4 invView = glm::inverse(view);
    pushData.viewRightAndBackground =
        glm::vec4(glm::normalize(glm::vec3(invView[0])), pushData.viewRightAndBackground.w);
    pushData.viewUpAndDiffuse =
        glm::vec4(glm::normalize(glm::vec3(invView[1])), pushData.viewUpAndDiffuse.w);
    pushData.viewForwardAndSpecular = glm::vec4(
        glm::normalize(-glm::vec3(invView[2])),
        pushData.viewForwardAndSpecular.w);
  }

  void setDirectionalLight(const glm::vec3 &directionViewSpace,
                           const glm::vec3 &color) {
    pushData.lightDirectionAndRotation =
        glm::vec4(glm::normalize(directionViewSpace), 0.0f);
    pushData.lightColorAndPrefilterMip =
        glm::vec4(color, pushData.lightColorAndPrefilterMip.w);
  }

  void setEnvironmentControls(float environmentRotationRadians,
                              float backgroundIntensity,
                              float diffuseIntensity,
                              float specularIntensity, bool enableIbl,
                              bool showBackground) {
    pushData.viewRightAndBackground.w = backgroundIntensity;
    pushData.viewUpAndDiffuse.w = diffuseIntensity;
    pushData.viewForwardAndSpecular.w = specularIntensity;
    pushData.lightDirectionAndRotation.w = environmentRotationRadians;

    uint32_t flags = 0;
    if (enableIbl) {
      flags |= EnableIbl;
    }
    if (showBackground) {
      flags |= ShowBackground;
    }
    pushData.settings.x = flags;
  }

  void setDebugView(PbrDebugView debugView) {
    pushData.settings.y = static_cast<uint32_t>(debugView);
  }

  void setDielectricSpecularScale(float scale) {
    pushData.specularTuning.x = std::max(scale, 0.0f);
  }

protected:
  std::vector<FullscreenImageInputBinding> imageInputBindings() const override {
    return {
        {.binding = 0},
        {.binding = 1},
        {.binding = 2},
        {.binding = 3},
        {.binding = 4},
        {.binding = 5},
        {.binding = 6},
        {.binding = 7},
        {.binding = 8},
    };
  }

  std::vector<vk::PushConstantRange> pushConstantRanges() const override {
    return {
        vk::PushConstantRange{
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
            .offset = 0,
            .size = sizeof(PbrPassPushConstant),
        },
    };
  }

  VertexInputLayoutSpec vertexInputLayout() const override {
    auto attrs = FullscreenVertex::getAttributeDescriptions();
    return VertexInputLayoutSpec{
        .bindings = {FullscreenVertex::getBindingDescription()},
        .attributes = {attrs.begin(), attrs.end()},
    };
  }

  std::vector<PassImageBinding>
  resolveImageBindings(const vk::raii::Sampler &sampler) const override {
    validateResources();

    return {
        {.binding = 0,
         .resource = sourcePassRef->sampledColorOutput(0, sampler)},
        {.binding = 1,
         .resource = sourcePassRef->sampledColorOutput(1, sampler)},
        {.binding = 2,
         .resource = sourcePassRef->sampledColorOutput(2, sampler)},
        {.binding = 3,
         .resource = sourcePassRef->sampledColorOutput(3, sampler)},
        {.binding = 4, .resource = sourcePassRef->sampledDepthOutput(sampler)},
        {.binding = 5,
         .resource =
             iblResources->environmentMap.sampledResource(
                 iblResources->environmentSampler.handle())},
        {.binding = 6,
         .resource = iblResources->irradianceMap.sampledResource(
             iblResources->irradianceSampler.handle())},
        {.binding = 7,
         .resource = iblResources->prefilteredMap.sampledResource(
             iblResources->prefilteredSampler.handle())},
        {.binding = 8,
         .resource = iblResources->brdfLut.sampledResource(
             iblResources->brdfSampler.handle())},
    };
  }

  void bindAdditionalPassResources(const RenderPassContext &context) override {
    context.commandBuffer.pushConstants<PbrPassPushConstant>(
        *pipelineLayoutHandle(), vk::ShaderStageFlagBits::eFragment, 0,
        {pushData});
  }

private:
  const GeometryPass *sourcePassRef = nullptr;
  const ImageBasedLightingResources *iblResources = nullptr;
  PbrPassPushConstant pushData{};

  void validateResources() const {
    if (sourcePassRef == nullptr) {
      throw std::runtime_error("PbrPass requires a GeometryPass source");
    }
    if (iblResources == nullptr) {
      throw std::runtime_error("PbrPass requires image-based lighting resources");
    }
  }
};
