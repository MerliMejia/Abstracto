#pragma once

#include "../renderable/Sampler.h"
#include "PassImageSet.h"
#include "RasterRenderPass.h"

struct FullscreenImageInputBinding {
  uint32_t binding = 0;
  vk::ShaderStageFlags stageFlags = vk::ShaderStageFlagBits::eFragment;
};

class FullscreenRenderPass : public RasterRenderPass {
public:
  explicit FullscreenRenderPass(
      PipelineSpec pipelineSpec, uint32_t framesInFlight,
      RasterPassAttachmentConfig attachmentConfig = RasterPassAttachmentConfig())
      : RasterRenderPass(std::move(pipelineSpec), std::move(attachmentConfig)),
        framesInFlightCount(framesInFlight) {}

protected:
  virtual std::vector<FullscreenImageInputBinding>
  imageInputBindings() const = 0;

  virtual std::vector<PassImageBinding>
  resolveImageBindings(const vk::raii::Sampler &sampler) const = 0;

  virtual bool shouldDrawFullscreenItem(const RenderItem &renderItem) const {
    return renderItem.targetPass == nullptr || renderItem.targetPass == this;
  }

  virtual void drawFullscreenItem(const RenderPassContext &context,
                                  const RenderItem &renderItem) {
    context.commandBuffer.bindVertexBuffers(
        0, *renderItem.mesh->getVertexBuffer(), {0});
    context.commandBuffer.bindIndexBuffer(*renderItem.mesh->getIndexBuffer(), 0,
                                          vk::IndexType::eUint32);
    context.commandBuffer.drawIndexed(renderItem.mesh->getIndices().size(), 1,
                                      0, 0, 0);
  }

  virtual void initializeAdditionalPassResources(DeviceContext &deviceContext,
                                                 SwapchainContext &) {}

  virtual void recreateAdditionalPassResources(DeviceContext &deviceContext,
                                               SwapchainContext &) {}

  virtual void bindAdditionalPassResources(const RenderPassContext &context) {}

  uint32_t framesInFlight() const { return framesInFlightCount; }

private:
  uint32_t framesInFlightCount = 0;
  Sampler sampler;
  PassImageSet images;
  bool imageSetInitialized = false;

  std::vector<DescriptorBindingSpec> descriptorBindings() const final {
    auto bindings = imageInputBindings();
    std::vector<DescriptorBindingSpec> specs;
    specs.reserve(bindings.size());
    for (const auto &binding : bindings) {
      specs.push_back(
          sampledImageBindingSpec(binding.binding, binding.stageFlags));
    }
    return specs;
  }

  void initializePassResources(DeviceContext &deviceContext,
                               SwapchainContext &swapchainContext) final {
    initializeAdditionalPassResources(deviceContext, swapchainContext);

    auto bindings = imageInputBindings();
    if (!bindings.empty()) {
      sampler.create(deviceContext);
      images.initialize(deviceContext, passDescriptorSetLayout(),
                        framesInFlightCount,
                        resolveImageBindings(sampler.handle()));
      imageSetInitialized = true;
    }
  }

  void recreatePassResources(DeviceContext &deviceContext,
                             SwapchainContext &swapchainContext) final {
    recreateAdditionalPassResources(deviceContext, swapchainContext);

    if (imageSetInitialized) {
      images.update(deviceContext, resolveImageBindings(sampler.handle()));
    }
  }

  void bindPassResources(const RenderPassContext &context) final {
    if (imageSetInitialized) {
      images.bind(context.commandBuffer, pipelineLayoutHandle(),
                  context.frameIndex);
    }

    bindAdditionalPassResources(context);
  }

  void recordDrawCommands(const RenderPassContext &context,
                          const std::vector<RenderItem> &renderItems) final {
    for (const auto &renderItem : renderItems) {
      if (!shouldDrawFullscreenItem(renderItem) || renderItem.mesh == nullptr) {
        continue;
      }

      drawFullscreenItem(context, renderItem);
    }
  }
};
