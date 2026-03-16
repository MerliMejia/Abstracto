#pragma once

#include "../renderer/FullscreenRenderPass.h"
#include "GeometryPass.h"
#include "ShadowPass.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

constexpr uint32_t MAX_DEBUG_SPOT_SHADOW_MAPS = 3;

struct DebugPresentPassPushConstant {
  uint32_t selectedOutput = 0;
  float nearPlane = 0.1f;
  float farPlane = 100.0f;
  float padding = 0.0f;
};

class DebugPresentPass : public FullscreenRenderPass {
public:
  DebugPresentPass(PipelineSpec spec, uint32_t framesInFlight,
                   const GeometryPass *sourcePass = nullptr,
                   const RasterRenderPass *lightPass = nullptr,
                   const RasterRenderPass *tonemapPass = nullptr,
                   const ShadowPass *directionalShadowPass = nullptr,
                   std::array<ShadowPass *, MAX_DEBUG_SPOT_SHADOW_MAPS>
                       spotShadowPasses = {nullptr, nullptr, nullptr})
      : FullscreenRenderPass(std::move(spec), framesInFlight,
                             RasterPassAttachmentConfig{
                                 .useColorAttachment = true,
                                 .useDepthAttachment = false,
                                 .useMsaaColorAttachment = false,
                                 .resolveToSwapchain = false,
                                 .useSwapchainColorAttachment = true,
                             }),
        sourcePassRef(sourcePass), lightPassRef(lightPass),
        tonemapPassRef(tonemapPass),
        directionalShadowPassRef(directionalShadowPass) {
    for (uint32_t index = 0; index < MAX_DEBUG_SPOT_SHADOW_MAPS; ++index) {
      spotShadowPassRefs[index] = spotShadowPasses[index];
    }
  }

  void setSourcePass(const GeometryPass &sourcePass) {
    sourcePassRef = &sourcePass;
  }

  void setLightPass(const RasterRenderPass &lightPass) {
    lightPassRef = &lightPass;
  }

  void setTonemapPass(const RasterRenderPass &tonemapPass) {
    tonemapPassRef = &tonemapPass;
  }

  void setDirectionalShadowPass(const ShadowPass &shadowPass) {
    directionalShadowPassRef = &shadowPass;
  }

  void setSpotShadowPass(uint32_t index, const ShadowPass &shadowPass) {
    spotShadowPassRefs.at(index) = &shadowPass;
  }

  void setSelectedOutput(uint32_t index) { selectedOutput = index; }

  void setClipPlanes(float nearPlaneValue, float farPlaneValue) {
    nearPlane = nearPlaneValue;
    farPlane = farPlaneValue;
  }

protected:
  std::vector<FullscreenImageInputBinding> imageInputBindings() const override {
    return {
        {.binding = 0}, {.binding = 1}, {.binding = 2},  {.binding = 3},
        {.binding = 4}, {.binding = 5}, {.binding = 6},  {.binding = 7},
        {.binding = 8}, {.binding = 9}, {.binding = 10},
    };
  }

  std::vector<vk::PushConstantRange> pushConstantRanges() const override {
    return {
        vk::PushConstantRange{
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
            .offset = 0,
            .size = sizeof(DebugPresentPassPushConstant),
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
    validateSourcePass();

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
        {.binding = 5, .resource = lightPassRef->sampledColorOutput(sampler)},
        {.binding = 6, .resource = tonemapPassRef->sampledColorOutput(sampler)},
        {.binding = 7,
         .resource = directionalShadowPassRef->sampledShadowOutput()},
        {.binding = 8,
         .resource = spotShadowPassRefs[0]->sampledShadowOutput()},
        {.binding = 9,
         .resource = spotShadowPassRefs[1]->sampledShadowOutput()},
        {.binding = 10,
         .resource = spotShadowPassRefs[2]->sampledShadowOutput()},
    };
  }

  void bindAdditionalPassResources(const RenderPassContext &context) override {
    DebugPresentPassPushConstant push{};
    push.selectedOutput = selectedOutput;
    push.nearPlane = nearPlane;
    push.farPlane = farPlane;

    context.commandBuffer.pushConstants<DebugPresentPassPushConstant>(
        *pipelineLayoutHandle(), vk::ShaderStageFlagBits::eFragment, 0, {push});
  }

private:
  const GeometryPass *sourcePassRef = nullptr;
  const RasterRenderPass *lightPassRef = nullptr;
  const RasterRenderPass *tonemapPassRef = nullptr;
  const ShadowPass *directionalShadowPassRef = nullptr;
  std::array<const ShadowPass *, MAX_DEBUG_SPOT_SHADOW_MAPS> spotShadowPassRefs{
      nullptr, nullptr, nullptr};
  uint32_t selectedOutput = 0;
  float nearPlane = 0.1f;
  float farPlane = 100.0f;

  void validateSourcePass() const {
    if (sourcePassRef == nullptr) {
      throw std::runtime_error(
          "DebugPresentPass requires a GeometryPass source");
    }
    if (lightPassRef == nullptr) {
      throw std::runtime_error("DebugPresentPass requires a light source pass");
    }
    if (tonemapPassRef == nullptr) {
      throw std::runtime_error(
          "DebugPresentPass requires a tonemap source pass");
    }
    if (directionalShadowPassRef == nullptr) {
      throw std::runtime_error(
          "DebugPresentPass requires a directional shadow pass");
    }
    for (const ShadowPass *shadowPass : spotShadowPassRefs) {
      if (shadowPass == nullptr) {
        throw std::runtime_error(
            "DebugPresentPass requires all spot shadow passes");
      }
    }
  }
};
