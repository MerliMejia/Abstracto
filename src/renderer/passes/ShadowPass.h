#pragma once

#include "renderer/resources/Sampler.h"
#include "renderer/core/UniformSceneRenderPass.h"
#include <glm/glm.hpp>

struct ShadowPassPushConstant {
  glm::mat4 model{1.0f};
};

struct ShadowPassUniformData {
  glm::mat4 model{1.0f};
  glm::mat4 lightViewProj{1.0f};
};

class ShadowPass : public UniformSceneRenderPass<ShadowPassUniformData> {
public:
  ShadowPass(PipelineSpec spec, uint32_t framesInFlight, uint32_t resolution)
      : UniformSceneRenderPass(
            std::move(spec), framesInFlight,
            RasterPassAttachmentConfig{
                .useColorAttachment = false,
                .useDepthAttachment = true,
                .useMsaaColorAttachment = false,
                .resolveToSwapchain = false,
                .useSwapchainColorAttachment = false,
                .depthLoadOp = vk::AttachmentLoadOp::eClear,
                .depthStoreOp = vk::AttachmentStoreOp::eStore,
                .renderExtent = {.width = resolution, .height = resolution},
                .sampleDepthAttachment = true,
            }) {}

  void setEnabled(bool value) { enabled = value; }

  void setModelMatrix(const glm::mat4 &modelMatrix) {
    uniformData.model = modelMatrix;
  }

  void setLightViewProj(const glm::mat4 &matrix) {
    uniformData.lightViewProj = matrix;
  }

  void setDepthBias(float constantFactor, float slopeFactor,
                    float clamp = 0.0f) {
    bias.constantFactor = constantFactor;
    bias.slopeFactor = slopeFactor;
    bias.clamp = clamp;
  }

  uint32_t resolution() const { return attachmentConfig().renderExtent.width; }

  SampledImageResource sampledShadowOutput() const {
    return sampledDepthOutput(shadowSampler.handle());
  }

protected:
  ShadowPassUniformData buildUniformData(uint32_t) const override {
    return uniformData;
  }

  vk::ShaderStageFlags uniformShaderStages() const override {
    return vk::ShaderStageFlagBits::eVertex;
  }

  VertexInputLayoutSpec vertexInputLayout() const override {
    return VertexInputLayoutSpec{
        .bindings = {GeometryVertex::getBindingDescription()},
        .attributes = {vk::VertexInputAttributeDescription(
            0, 0, vk::Format::eR32G32B32Sfloat, offsetof(GeometryVertex, pos))},
    };
  }

  void initializeAdditionalPassResources(DeviceContext &deviceContext,
                                         SwapchainContext &) override {
    shadowSampler.create(
        deviceContext, Sampler::Config{
                           .magFilter = vk::Filter::eLinear,
                           .minFilter = vk::Filter::eLinear,
                           .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                           .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                           .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                           .anisotropyEnable = false,
                           .maxLod = 0.0f,
                       });
  }

  std::optional<RasterDepthBiasState> depthBiasState() const override {
    return bias;
  }

  bool shouldDrawRenderItem(const RenderItem &renderItem) const override {
    return enabled && SceneRenderPass::shouldDrawRenderItem(renderItem);
  }

  std::vector<vk::PushConstantRange> extraPushConstantRanges() const override {
    return {vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
        .offset = 0,
        .size = sizeof(ShadowPassPushConstant),
    }};
  }

  void bindPerRenderItemResources(const RenderPassContext &context,
                                  const RenderItem &renderItem) override {
    ShadowPassPushConstant push{.model = renderItem.modelMatrix};
    context.commandBuffer.pushConstants<ShadowPassPushConstant>(
        *pipelineLayoutHandle(), vk::ShaderStageFlagBits::eVertex, 0, {push});
  }

private:
  ShadowPassUniformData uniformData{};
  RasterDepthBiasState bias{
      .constantFactor = 1.25f,
      .clamp = 0.0f,
      .slopeFactor = 1.75f,
  };
  Sampler shadowSampler;
  bool enabled = false;
};
