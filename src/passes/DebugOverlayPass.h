#pragma once

#include "../renderable/Mesh.h"
#include "../renderable/SceneLightSet.h"
#include "../renderer/PassUniformSet.h"
#include "../renderer/RasterRenderPass.h"
#include <algorithm>
#include <cmath>

struct DebugOverlayFrameUniformData {
  glm::mat4 view{1.0f};
  glm::mat4 proj{1.0f};
};

struct DebugOverlayPushConstant {
  glm::mat4 model{1.0f};
  glm::vec4 color{1.0f};
};

class DebugOverlayPass : public RasterRenderPass {
public:
  DebugOverlayPass(PipelineSpec spec, uint32_t framesInFlight,
                   const SceneLightSet *sceneLights = nullptr)
      : RasterRenderPass(std::move(spec),
                         RasterPassAttachmentConfig{
                             .useColorAttachment = true,
                             .useDepthAttachment = false,
                             .useMsaaColorAttachment = false,
                             .resolveToSwapchain = false,
                             .useSwapchainColorAttachment = true,
                             .colorLoadOp = vk::AttachmentLoadOp::eLoad,
                         }),
        framesInFlightCount(framesInFlight), sceneLightsRef(sceneLights) {}

  void setSceneLights(const SceneLightSet &sceneLights) {
    sceneLightsRef = &sceneLights;
  }

  void setCamera(const glm::mat4 &view, const glm::mat4 &proj) {
    frameUniform.view = view;
    frameUniform.proj = proj;
  }

  void setPointMarkerMesh(Mesh &mesh) { pointMarkerMesh = &mesh; }
  void setSpotMarkerMesh(Mesh &mesh) { spotMarkerMesh = &mesh; }
  void setDirectionalMarkerMesh(Mesh &mesh) { directionalMarkerMesh = &mesh; }

  void setMarkersVisible(bool visible) { markersVisible = visible; }
  void setMarkerScale(float scale) { markerScale = std::max(scale, 0.01f); }
  void setDirectionalAnchor(const glm::vec3 &anchor) {
    directionalAnchor = anchor;
  }

protected:
  std::vector<DescriptorBindingSpec> descriptorBindings() const override {
    return {{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
    }};
  }

  std::vector<vk::PushConstantRange> pushConstantRanges() const override {
    return {
        vk::PushConstantRange{
            .stageFlags = vk::ShaderStageFlagBits::eVertex |
                          vk::ShaderStageFlagBits::eFragment,
            .offset = 0,
            .size = sizeof(DebugOverlayPushConstant),
        },
    };
  }

  void initializePassResources(DeviceContext &deviceContext,
                               SwapchainContext &) override {
    frameUniformSet.initialize(deviceContext, passDescriptorSetLayout(),
                               framesInFlightCount);
  }

  void bindPassResources(const RenderPassContext &context) override {
    frameUniformSet.write(context.frameIndex, frameUniform);
    frameUniformSet.bind(context.commandBuffer, pipelineLayoutHandle(),
                         context.frameIndex);
  }

  void recordDrawCommands(const RenderPassContext &context,
                          const std::vector<RenderItem> &) override {
    if (!markersVisible || sceneLightsRef == nullptr) {
      return;
    }

    for (const auto &light : sceneLightsRef->lights()) {
      if (!light.enabled) {
        continue;
      }

      switch (light.type) {
      case SceneLightType::Directional:
        if (directionalMarkerMesh != nullptr) {
          const glm::vec3 direction =
              safeDirection(light.direction, glm::vec3(-0.55f, -0.25f, -1.0f));
          const glm::vec3 position =
              directionalAnchor - direction * markerScale * 1.5f;
          drawMarker(
              context, *directionalMarkerMesh,
              buildOrientationTransform(position, direction, markerScale),
              glm::vec4(light.color, 1.0f));
        }
        break;
      case SceneLightType::Point:
        if (pointMarkerMesh != nullptr) {
          drawMarker(context, *pointMarkerMesh,
                     glm::translate(glm::mat4(1.0f), light.position) *
                         glm::scale(glm::mat4(1.0f), glm::vec3(markerScale)),
                     glm::vec4(light.color, 1.0f));
        }
        break;
      case SceneLightType::Spot:
        if (spotMarkerMesh != nullptr) {
          drawMarker(context, *spotMarkerMesh,
                     buildOrientationTransform(light.position, light.direction,
                                               markerScale),
                     glm::vec4(light.color, 1.0f));
        }
        break;
      }
    }
  }

private:
  uint32_t framesInFlightCount = 0;
  PassUniformSet<DebugOverlayFrameUniformData> frameUniformSet;
  DebugOverlayFrameUniformData frameUniform{};
  const SceneLightSet *sceneLightsRef = nullptr;
  Mesh *pointMarkerMesh = nullptr;
  Mesh *spotMarkerMesh = nullptr;
  Mesh *directionalMarkerMesh = nullptr;
  bool markersVisible = true;
  float markerScale = 0.35f;
  glm::vec3 directionalAnchor{0.0f, 0.0f, 0.0f};

  static glm::vec3 safeDirection(const glm::vec3 &direction,
                                 const glm::vec3 &fallback) {
    const float lengthSquared = glm::dot(direction, direction);
    return glm::normalize(lengthSquared > 1e-6f ? direction : fallback);
  }

  static glm::mat4 buildOrientationTransform(const glm::vec3 &position,
                                             const glm::vec3 &direction,
                                             float scale) {
    const glm::vec3 forward =
        safeDirection(direction, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 worldUp =
        std::abs(glm::dot(forward, glm::vec3(0.0f, 0.0f, 1.0f))) > 0.98f
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
    const glm::vec3 up = glm::normalize(glm::cross(forward, right));

    glm::mat4 transform(1.0f);
    transform[0] = glm::vec4(right * scale, 0.0f);
    transform[1] = glm::vec4(up * scale, 0.0f);
    transform[2] = glm::vec4(forward * scale, 0.0f);
    transform[3] = glm::vec4(position, 1.0f);
    return transform;
  }

  void drawMarker(const RenderPassContext &context, Mesh &mesh,
                  const glm::mat4 &model, const glm::vec4 &color) const {
    DebugOverlayPushConstant push{
        .model = model,
        .color = color,
    };
    context.commandBuffer.pushConstants<DebugOverlayPushConstant>(
        *pipelineLayoutHandle(),
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, {push});
    context.commandBuffer.bindVertexBuffers(0, *mesh.getVertexBuffer(), {0});
    context.commandBuffer.bindIndexBuffer(*mesh.getIndexBuffer(), 0,
                                          vk::IndexType::eUint32);
    context.commandBuffer.drawIndexed(
        static_cast<uint32_t>(mesh.getIndices().size()), 1, 0, 0, 0);
  }
};
