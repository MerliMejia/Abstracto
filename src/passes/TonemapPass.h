#pragma once

#include "../renderer/FullscreenRenderPass.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

struct TonemapPassPushConstant {
  glm::vec4 params0{1.0f, 4.0f, 2.2f, 2.0f};
};

enum class TonemapOperator : uint32_t {
  None = 0,
  Reinhard = 1,
  ACES = 2,
  Filmic = 3,
};

class TonemapPass : public FullscreenRenderPass {
public:
  TonemapPass(PipelineSpec spec, uint32_t framesInFlight,
              const RasterRenderPass *sourcePass = nullptr)
      : FullscreenRenderPass(
            std::move(spec), framesInFlight,
            RasterPassAttachmentConfig{
                .useColorAttachment = true,
                .useDepthAttachment = false,
                .useMsaaColorAttachment = false,
                .resolveToSwapchain = false,
                .useSwapchainColorAttachment = false,
                .colorAttachments = {{.name = "tonemapped",
                                      .format = RasterAttachmentFormat::RGBA8,
                                      .sampled = true}},
            }),
        sourcePassRef(sourcePass) {}

  void setSourcePass(const RasterRenderPass &sourcePass) {
    sourcePassRef = &sourcePass;
  }

  void setExposure(float exposure) {
    pushData.params0.x = std::max(exposure, 0.0f);
  }

  void setWhitePoint(float whitePoint) {
    pushData.params0.y = std::max(whitePoint, 0.001f);
  }

  void setGamma(float gamma) {
    pushData.params0.z = std::max(gamma, 0.001f);
  }

  void setOperator(TonemapOperator tonemapOperator) {
    pushData.params0.w = static_cast<float>(tonemapOperator);
  }

protected:
  std::vector<FullscreenImageInputBinding> imageInputBindings() const override {
    return {{.binding = 0}};
  }

  std::vector<vk::PushConstantRange> pushConstantRanges() const override {
    return {
        vk::PushConstantRange{
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
            .offset = 0,
            .size = sizeof(TonemapPassPushConstant),
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
        {.binding = 0, .resource = sourcePassRef->sampledColorOutput(sampler)}};
  }

  void bindAdditionalPassResources(const RenderPassContext &context) override {
    context.commandBuffer.pushConstants<TonemapPassPushConstant>(
        *pipelineLayoutHandle(), vk::ShaderStageFlagBits::eFragment, 0,
        {pushData});
  }

private:
  const RasterRenderPass *sourcePassRef = nullptr;
  TonemapPassPushConstant pushData{};

  void validateSourcePass() const {
    if (sourcePassRef == nullptr) {
      throw std::runtime_error("TonemapPass requires a source pass");
    }
  }
};
