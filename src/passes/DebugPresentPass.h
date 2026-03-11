#pragma once

#include "../renderable/Sampler.h"
#include "../renderer/MeshRenderPass.h"
#include "../renderer/PassImageSet.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

struct DebugPresentVertex {
  glm::vec3 pos;
  glm::vec2 uv;
};

class DebugPresentPass : public MeshRenderPass {
public:
  DebugPresentPass(PipelineSpec spec, uint32_t framesInFlight,
                   const MeshRenderPass *sourcePass = nullptr)
      : MeshRenderPass(
            std::move(spec),
            MeshPassAttachmentConfig{.useColorAttachment = true,
                                     .useDepthAttachment = false,
                                     .useMsaaColorAttachment = false,
                                     .resolveToSwapchain = false,
                                     .useSwapchainColorAttachment = true,
                                     .sampleColorAttachment = false}),
        framesInFlightCount(framesInFlight),
        sourcePassRef(sourcePass) {}

  void setSourcePass(const MeshRenderPass &sourcePass) {
    sourcePassRef = &sourcePass;
  }

  void updateSourcePass(DeviceContext &deviceContext,
                        const MeshRenderPass &sourcePass) {
    sourcePassRef = &sourcePass;
    if (imageSetInitialized) {
      images.update(deviceContext, sourceBindings());
    }
  }

protected:
  std::vector<DescriptorBindingSpec> descriptorBindings() const override {
    return {sampledImageBindingSpec(0)};
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

  void initializePassResources(DeviceContext &deviceContext,
                               SwapchainContext &) override {
    validateSourcePass();
    sampler.create(deviceContext);
    images.initialize(deviceContext, passDescriptorSetLayout(),
                      framesInFlightCount, sourceBindings());
    imageSetInitialized = true;
  }

  void recreatePassResources(DeviceContext &deviceContext,
                             SwapchainContext &) override {
    validateSourcePass();
    images.update(deviceContext, sourceBindings());
  }

  void bindPassResources(const RenderPassContext &context) override {
    images.bind(context.commandBuffer, pipelineLayoutHandle(),
                context.frameIndex);
  }

  void bindRenderItemResources(const RenderPassContext &,
                               const RenderItem &) override {}

private:
  uint32_t framesInFlightCount = 0;
  const MeshRenderPass *sourcePassRef = nullptr;
  Sampler sampler;
  PassImageSet images;
  bool imageSetInitialized = false;

  void validateSourcePass() const {
    if (sourcePassRef == nullptr) {
      throw std::runtime_error("DebugPresentPass requires a source pass");
    }
    if (!sourcePassRef->hasSampledColorOutput()) {
      throw std::runtime_error(
          "DebugPresentPass requires a source pass with a sampled color output");
    }
  }

  std::vector<PassImageBinding> sourceBindings() const {
    return {{
        .binding = 0,
        .resource = sourcePassRef->sampledColorOutput(sampler.handle()),
    }};
  }
};
