#pragma once

#include "../renderer/FullscreenRenderPass.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

struct DebugPresentVertex {
  glm::vec3 pos;
  glm::vec2 uv;
};

class DebugPresentPass : public FullscreenRenderPass {
public:
  DebugPresentPass(PipelineSpec spec, uint32_t framesInFlight,
                   const RasterRenderPass *sourcePass = nullptr)
      : FullscreenRenderPass(
            std::move(spec),
            framesInFlight,
            RasterPassAttachmentConfig{.useColorAttachment = true,
                                       .useDepthAttachment = false,
                                       .useMsaaColorAttachment = false,
                                       .resolveToSwapchain = false,
                                       .useSwapchainColorAttachment = true,
                                       .sampleColorAttachment = false}),
        sourcePassRef(sourcePass) {}

  void setSourcePass(const RasterRenderPass &sourcePass) {
    sourcePassRef = &sourcePass;
  }

  void updateSourcePass(DeviceContext &deviceContext,
                        const RasterRenderPass &sourcePass) {
    sourcePassRef = &sourcePass;
  }

protected:
  std::vector<FullscreenImageInputBinding> imageInputBindings() const override {
    return {{.binding = 0}};
  }

  VertexInputLayoutSpec vertexInputLayout() const override {
    return VertexInputLayoutSpec{
        .bindings = {vk::VertexInputBindingDescription(
            0, sizeof(DebugPresentVertex), vk::VertexInputRate::eVertex)},
        .attributes = {
            vk::VertexInputAttributeDescription(
                0, 0, vk::Format::eR32G32B32Sfloat,
                offsetof(DebugPresentVertex, pos)),
            vk::VertexInputAttributeDescription(
                1, 0, vk::Format::eR32G32Sfloat,
                offsetof(DebugPresentVertex, uv)),
        }};
  }

  std::vector<PassImageBinding>
  resolveImageBindings(const vk::raii::Sampler &sampler) const override {
    validateSourcePass();
    return {{
        .binding = 0,
        .resource = sourcePassRef->sampledColorOutput(sampler),
    }};
  }

private:
  const RasterRenderPass *sourcePassRef = nullptr;

  void validateSourcePass() const {
    if (sourcePassRef == nullptr) {
      throw std::runtime_error("DebugPresentPass requires a source pass");
    }
    if (!sourcePassRef->hasSampledColorOutput()) {
      throw std::runtime_error(
          "DebugPresentPass requires a source pass with a sampled color output");
    }
  }
};
