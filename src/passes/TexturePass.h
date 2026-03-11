#pragma once

#include "../renderer/MeshRenderPass.h"
#include "../renderer/PassImageSet.h"
#include "SolidTransformPass.h"
#include "vulkan/vulkan.hpp"
#include <cstdint>

class TexturePass : public MeshRenderPass {
public:
  TexturePass(PipelineSpec spec, uint32_t framesInFlight,
              const SolidTransformPass &solidPass, const Texture &albedoTexture)
      : MeshRenderPass(
            std::move(spec),
            MeshPassAttachmentConfig{.useColorAttachment = true,
                                     .useDepthAttachment = false,
                                     .useMsaaColorAttachment = false,
                                     .resolveToSwapchain = false,
                                     .useSwapchainColorAttachment = true,
                                     .sampleColorAttachment = false}),
        framesInFlightCount(framesInFlight), solidTransformRef(solidPass),
        albedoTexture(albedoTexture) {}

protected:
  std::vector<DescriptorBindingSpec> descriptorBindings() const override {
    return {
        sampledImageBindingSpec(0),
        sampledImageBindingSpec(1),
    };
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

  void initializePassResources(DeviceContext &deviceContext,
                               SwapchainContext &swapchainContext) override {
    sampler.create(deviceContext);

    images.initialize(
        deviceContext, passDescriptorSetLayout(), framesInFlightCount,
        {{
            {0, solidTransformRef.sampledColorOutput(sampler.handle())},
            {1, albedoTexture.sampledResource(sampler.handle())},
        }});
  }

  void recreatePassResources(DeviceContext &deviceContext,
                             SwapchainContext &swapchainContext) override {
    images.update(
        deviceContext,
        {
            {0, solidTransformRef.sampledColorOutput(sampler.handle())},
            {1, albedoTexture.sampledResource(sampler.handle())},
        });
  }

  void bindPassResources(const RenderPassContext &context) override {
    images.bind(context.commandBuffer, pipelineLayoutHandle(),
                context.frameIndex);
  }

  void bindRenderItemResources(const RenderPassContext &context,
                               const RenderItem &renderItem) override {}

private:
  uint32_t framesInFlightCount = 0;
  const SolidTransformPass &solidTransformRef;
  const Texture &albedoTexture;
  Sampler sampler;
  PassImageSet images;
};
