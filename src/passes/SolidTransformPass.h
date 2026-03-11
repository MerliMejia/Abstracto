#pragma once

#include "../renderer/PipelineSpec.h"
#include "../renderer/UniformSceneRenderPass.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

struct SolidTransformData {
  glm::mat4 model;
  glm::mat4 view;
  glm::mat4 proj;
};

struct PositionUvVertex {
  glm::vec3 pos;
  glm::vec2 uv;
};

class SolidTransformPass : public UniformSceneRenderPass<SolidTransformData> {
public:
  SolidTransformPass(PipelineSpec spec, uint32_t framesInFlight)
      : UniformSceneRenderPass<SolidTransformData>(
            std::move(spec), framesInFlight,
            RasterPassAttachmentConfig{.useColorAttachment = true,
                                       .useDepthAttachment = true,
                                       .useMsaaColorAttachment = false,
                                       .resolveToSwapchain = false,
                                       .useSwapchainColorAttachment = false,
                                       .sampleColorAttachment = true}) {}

private:
  SolidTransformData buildUniformData(uint32_t frameIndex) const override {
    SolidTransformData data{};

    data.model = glm::mat4(1.0f);

    data.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f),  // camera
                            glm::vec3(0.0f, 0.0f, 0.0f),  // target
                            glm::vec3(0.0f, 0.0f, 1.0f)); // up

    data.proj =
        glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 10.0f);

    data.proj[1][1] *= -1.0f;

    return data;
  }

  VertexInputLayoutSpec vertexInputLayout() const override {
    return VertexInputLayoutSpec{
        .bindings = {vk::VertexInputBindingDescription(
            0, sizeof(PositionUvVertex), vk::VertexInputRate::eVertex)},
        .attributes = {
            vk::VertexInputAttributeDescription(
                0, 0, vk::Format::eR32G32B32Sfloat,
                offsetof(PositionUvVertex, pos)),
            vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32Sfloat,
                                                offsetof(PositionUvVertex, uv)),
        }};
  }
};
