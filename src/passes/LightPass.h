#pragma once

#include "../renderer/FullscreenRenderPass.h"
#include "GeometryPass.h"
#include <stdexcept>
#include <vector>

struct LightPassPushConstant {
  glm::mat4 invProj{1.0f};
  glm::vec4 lightDirection{0.0f, -1.0f, 0.0f, 0.0f};
  glm::vec4 lightColor{1.0f, 1.0f, 1.0f, 1.0f};
};

class LightPass : public FullscreenRenderPass {
public:
  LightPass(PipelineSpec spec, uint32_t framesInFlight,
            const GeometryPass *sourcePass = nullptr)
      : FullscreenRenderPass(std::move(spec), framesInFlight,
                             RasterPassAttachmentConfig{
                                 .useColorAttachment = true,
                                 .useDepthAttachment = false,
                                 .useMsaaColorAttachment = false,
                                 .resolveToSwapchain = false,
                                 .useSwapchainColorAttachment = true,
                             }),
        sourcePassRef(sourcePass) {}

  void setSourcePass(const GeometryPass &sourcePass) {
    sourcePassRef = &sourcePass;
  }

  void setProjection(const glm::mat4 &proj) { pushData.invProj = glm::inverse(proj); }

  void setDirectionalLight(const glm::vec3 &directionViewSpace,
                           const glm::vec3 &color) {
    pushData.lightDirection =
        glm::vec4(glm::normalize(directionViewSpace), 0.0f);
    pushData.lightColor = glm::vec4(color, 1.0f);
  }

protected:
  std::vector<FullscreenImageInputBinding> imageInputBindings() const override {
    return {
        {.binding = 0},
        {.binding = 1},
        {.binding = 2},
        {.binding = 3},
    };
  }

  std::vector<vk::PushConstantRange> pushConstantRanges() const override {
    return {
        vk::PushConstantRange{
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
            .offset = 0,
            .size = sizeof(LightPassPushConstant),
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
        {.binding = 3, .resource = sourcePassRef->sampledDepthOutput(sampler)},
    };
  }

  void bindAdditionalPassResources(const RenderPassContext &context) override {
    context.commandBuffer.pushConstants<LightPassPushConstant>(
        *pipelineLayoutHandle(), vk::ShaderStageFlagBits::eFragment, 0,
        {pushData});
  }

private:
  const GeometryPass *sourcePassRef = nullptr;
  LightPassPushConstant pushData{};

  void validateSourcePass() const {
    if (sourcePassRef == nullptr) {
      throw std::runtime_error("LightPass requires a GeometryPass source");
    }
  }
};
