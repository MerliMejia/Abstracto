#pragma once

#include "RasterRenderPass.h"
#include <array>

class SceneRenderPass : public RasterRenderPass {
public:
  explicit SceneRenderPass(PipelineSpec pipelineSpec,
                           RasterPassAttachmentConfig attachmentConfig =
                               RasterPassAttachmentConfig())
      : RasterRenderPass(std::move(pipelineSpec), std::move(attachmentConfig)) {
  }

protected:
  virtual bool shouldDrawRenderItem(const RenderItem &renderItem) const {
    return renderItem.targetPass == nullptr || renderItem.targetPass == this;
  }

  virtual void bindPrimaryRenderItemResources(const RenderPassContext &context,
                                              const RenderItem &renderItem) {
    if (descriptorSetLayout() != nullptr &&
        renderItem.descriptorBindings != nullptr) {
      context.commandBuffer.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics, pipelineLayoutHandle(), 0,
          *renderItem.descriptorBindings->descriptorSet(context.frameIndex),
          nullptr);
    }
  }

  virtual void bindSecondaryRenderItemResources(
      const RenderPassContext &context, const RenderItem &renderItem) {
    if (descriptorSetLayout(1) != nullptr &&
        renderItem.secondaryDescriptorBindings != nullptr) {
      context.commandBuffer.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics, pipelineLayoutHandle(), 1,
          *renderItem.secondaryDescriptorBindings->descriptorSet(
              context.frameIndex),
          nullptr);
    }
  }

  virtual void bindRenderItemResources(const RenderPassContext &context,
                                       const RenderItem &renderItem) {
    bindPrimaryRenderItemResources(context, renderItem);
    bindSecondaryRenderItemResources(context, renderItem);
  }

  virtual void drawRenderItem(const RenderPassContext &context,
                              const RenderItem &renderItem) {
    const uint32_t indexCount =
        renderItem.indexCount == 0
            ? static_cast<uint32_t>(renderItem.mesh->getIndices().size())
            : renderItem.indexCount;
    const uint32_t instanceCount =
        renderItem.instanceCount == 0 ? 1u : renderItem.instanceCount;
    if (renderItem.instanceBuffer != nullptr) {
      std::array<vk::Buffer, 2> vertexBuffers = {
          *renderItem.mesh->getVertexBuffer(),
          *renderItem.instanceBuffer->buffer(context.frameIndex),
      };
      std::array<vk::DeviceSize, 2> offsets = {0, 0};
      context.commandBuffer.bindVertexBuffers(0, vertexBuffers, offsets);
    } else {
      context.commandBuffer.bindVertexBuffers(
          0, *renderItem.mesh->getVertexBuffer(), {0});
    }
    context.commandBuffer.bindIndexBuffer(*renderItem.mesh->getIndexBuffer(), 0,
                                          vk::IndexType::eUint32);
    context.commandBuffer.drawIndexed(indexCount, instanceCount,
                                      renderItem.indexOffset, 0, 0);
  }

private:
  void recordDrawCommands(const RenderPassContext &context,
                          const std::vector<RenderItem> &renderItems) final {
    for (const auto &renderItem : renderItems) {
      if (!shouldDrawRenderItem(renderItem) || renderItem.mesh == nullptr) {
        continue;
      }

      bindRenderItemResources(context, renderItem);
      drawRenderItem(context, renderItem);
    }
  }
};
