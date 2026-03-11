#pragma once

#include "../renderer/FullscreenRenderPass.h"
#include "SolidTransformPass.h"
#include "vulkan/vulkan.hpp"
#include <cstdint>

class TexturePass : public FullscreenRenderPass {
public:
  TexturePass(PipelineSpec spec, uint32_t framesInFlight,
              const SolidTransformPass &solidPass, const Texture &albedoTexture)
      : FullscreenRenderPass(
            std::move(spec),
            framesInFlight,
            RasterPassAttachmentConfig{.useColorAttachment = true,
                                       .useDepthAttachment = false,
                                       .useMsaaColorAttachment = false,
                                       .resolveToSwapchain = false,
                                       .useSwapchainColorAttachment = true,
                                       .sampleColorAttachment = false}),
        solidTransformRef(solidPass),
        albedoTexture(albedoTexture) {}

protected:
  std::vector<FullscreenImageInputBinding> imageInputBindings() const override {
    return {{.binding = 0}, {.binding = 1}};
  }

  VertexInputLayoutSpec vertexInputLayout() const override {
    return VertexInputLayoutSpec{
        .bindings = {vk::VertexInputBindingDescription(
            0, sizeof(PositionUvVertex), vk::VertexInputRate::eVertex)},
        .attributes =
            {
                vk::VertexInputAttributeDescription(
                    0, 0, vk::Format::eR32G32B32Sfloat,
                    offsetof(PositionUvVertex, pos)),
                vk::VertexInputAttributeDescription(
                    1, 0, vk::Format::eR32G32Sfloat,
                    offsetof(PositionUvVertex, uv)),
            },
    };
  }

  std::vector<PassImageBinding>
  resolveImageBindings(const vk::raii::Sampler &sampler) const override {
    return {
        {0, solidTransformRef.sampledColorOutput(sampler)},
        {1, albedoTexture.sampledResource(sampler)},
    };
  }

private:
  const SolidTransformPass &solidTransformRef;
  const Texture &albedoTexture;
};
