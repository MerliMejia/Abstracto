#pragma once

#include "../renderer/SceneRenderPass.h"
#include "vulkan/vulkan.hpp"
#include <algorithm>
#include <vector>

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
            {.binding = 5,
             .descriptorType = vk::DescriptorType::eUniformBuffer,
             .descriptorCount = 1,
             .stageFlags = vk::ShaderStageFlagBits::eFragment}};
  }
  VertexInputLayoutSpec vertexInputLayout() const override {
    auto attrs = GeometryVertex::getAttributeDescriptions();
    return VertexInputLayoutSpec{
        .bindings = {GeometryVertex::getBindingDescription()},
        .attributes = {attrs.begin(), attrs.end()},
    };
  }
};
