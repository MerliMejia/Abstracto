#pragma once

#include "core/SceneRenderPass.h"
#include "vulkan/vulkan.hpp"
#include <vector>

struct GeometryPassPushConstant {
  glm::mat4 model{1.0f};
  glm::mat4 modelNormal{1.0f};
  int boneWeightJointIndex = -1;
  int boneWeightDebugEnabled = 0;
  int skinningEnabled = 0;
  int debugPadding = 0;
};

class GeometryPass : public SceneRenderPass {
public:
  explicit GeometryPass(PipelineSpec spec)
      : SceneRenderPass(
            std::move(spec),
            RasterPassAttachmentConfig{
                .useColorAttachment = true,
                .useDepthAttachment = true,
                .useMsaaColorAttachment = false,
                .resolveToSwapchain = false,
                .useSwapchainColorAttachment = false,
                .colorAttachments = {{.name = "gbuffer0_albedo",
                                      .format = RasterAttachmentFormat::RGBA8,
                                      .sampled = true},
                                     {.name = "gbuffer1_normal",
                                      .format = RasterAttachmentFormat::RGBA16F,
                                      .sampled = true},
                                     {.name = "gbuffer2_material",
                                      .format = RasterAttachmentFormat::RGBA8,
                                      .sampled = true},
                                     {.name = "gbuffer3_emissive",
                                      .format = RasterAttachmentFormat::RGBA16F,
                                      .sampled = true}},
                .sampleDepthAttachment = true}) {}

protected:
  std::vector<DescriptorBindingSpec> descriptorBindings() const override {
    return {{.binding = 0,
             .descriptorType = vk::DescriptorType::eUniformBuffer,
             .descriptorCount = 1,
             .stageFlags = vk::ShaderStageFlagBits::eVertex},
            sampledImageBindingSpec(1, vk::ShaderStageFlagBits::eFragment),
            sampledImageBindingSpec(2, vk::ShaderStageFlagBits::eFragment),
            sampledImageBindingSpec(3, vk::ShaderStageFlagBits::eFragment),
            sampledImageBindingSpec(4, vk::ShaderStageFlagBits::eFragment),
            sampledImageBindingSpec(5, vk::ShaderStageFlagBits::eFragment),
            {.binding = 6,
             .descriptorType = vk::DescriptorType::eUniformBuffer,
             .descriptorCount = 1,
             .stageFlags = vk::ShaderStageFlagBits::eFragment}};
  }

  std::vector<DescriptorBindingSpec> secondaryDescriptorBindings() const override {
    return {{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
    }};
  }
  VertexInputLayoutSpec vertexInputLayout() const override {
    auto attrs = GeometryVertex::getAttributeDescriptions();
    return VertexInputLayoutSpec{
        .bindings = {GeometryVertex::getBindingDescription()},
        .attributes = {attrs.begin(), attrs.end()},
    };
  }

  std::vector<vk::PushConstantRange> pushConstantRanges() const override {
    return {vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
        .offset = 0,
        .size = sizeof(GeometryPassPushConstant),
    }};
  }

  void bindRenderItemResources(const RenderPassContext &context,
                               const RenderItem &renderItem) override {
    SceneRenderPass::bindRenderItemResources(context, renderItem);
    GeometryPassPushConstant push{
        .model = renderItem.modelMatrix,
        .modelNormal = renderItem.modelNormalMatrix,
        .boneWeightJointIndex = renderItem.boneWeightJointIndex,
        .boneWeightDebugEnabled = renderItem.boneWeightDebugEnabled,
        .skinningEnabled = renderItem.skinningEnabled,
    };
    context.commandBuffer.pushConstants<GeometryPassPushConstant>(
        *pipelineLayoutHandle(), vk::ShaderStageFlagBits::eVertex, 0, {push});
  }
};
