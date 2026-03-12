#pragma once

#include "../renderer/FullscreenRenderPass.h"
#include "GeometryPass.h"
#include <cstdint>
#include <stdexcept>
#include <vector>

struct DebugPassPushConstant {
  uint32_t selectedOutput = 0;
};

class DebugPass : public FullscreenRenderPass {
public:
  DebugPass(PipelineSpec spec, uint32_t framesInFlight,
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

  void setSelectedOutput(uint32_t index) { selectedOutput = index; }

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
            .size = sizeof(DebugPassPushConstant),
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
    DebugPassPushConstant push{};
    push.selectedOutput = selectedOutput;

    context.commandBuffer.pushConstants<DebugPassPushConstant>(
        *pipelineLayoutHandle(), vk::ShaderStageFlagBits::eFragment, 0, {push});
  }

private:
  const GeometryPass *sourcePassRef = nullptr;
  uint32_t selectedOutput = 0;

  void validateSourcePass() const {
    if (sourcePassRef == nullptr) {
      throw std::runtime_error("DebugPass requires a GeometryPass source");
    }
  }
};
