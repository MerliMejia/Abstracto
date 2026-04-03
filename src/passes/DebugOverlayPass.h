#pragma once

#include "resources/Mesh.h"
#include "core/PassUniformSet.h"
#include "core/RasterRenderPass.h"
#include <algorithm>
#include <cmath>
#include <vector>

struct DebugOverlayFrameUniformData {
  glm::mat4 view{1.0f};
  glm::mat4 proj{1.0f};
};

struct DebugOverlayPushConstant {
  glm::mat4 model{1.0f};
  glm::vec4 color{1.0f};
};

struct DebugOverlayInstance {
  glm::mat4 model{1.0f};
  glm::vec4 color{1.0f};
};

enum class DebugOverlayMarkerType : uint32_t {
  Directional = 0,
  Point = 1,
  Spot = 2,
};

struct DebugOverlayMarker {
  DebugOverlayMarkerType type = DebugOverlayMarkerType::Point;
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  glm::vec3 direction{0.0f, 0.0f, 1.0f};
  glm::vec4 color{1.0f};
};

class DebugOverlayPass : public RasterRenderPass {
public:
  DebugOverlayPass(PipelineSpec spec, uint32_t framesInFlight)
      : RasterRenderPass(std::move(spec),
                         RasterPassAttachmentConfig{
                             .useColorAttachment = true,
                             .useDepthAttachment = false,
                             .useMsaaColorAttachment = false,
                             .resolveToSwapchain = false,
                             .useSwapchainColorAttachment = true,
                             .colorLoadOp = vk::AttachmentLoadOp::eLoad,
                         }),
        framesInFlightCount(framesInFlight) {}

  void setCamera(const glm::mat4 &view, const glm::mat4 &proj) {
    frameUniform.view = view;
    frameUniform.proj = proj;
  }

  void setPointMarkerMesh(Mesh &mesh) { pointMarkerMesh = &mesh; }
  void setSpotMarkerMesh(Mesh &mesh) { spotMarkerMesh = &mesh; }
  void setDirectionalMarkerMesh(Mesh &mesh) { directionalMarkerMesh = &mesh; }
  void setBoneSegmentMesh(Mesh &mesh) { boneSegmentMesh = &mesh; }
  void setBoneJointMesh(Mesh &mesh) { boneJointMesh = &mesh; }

  void setMarkersVisible(bool visible) { markersVisible = visible; }
  void setMarkerScale(float scale) { markerScale = std::max(scale, 0.01f); }
  void setBonesVisible(bool visible) { bonesVisible = visible; }
  void setBoneSegments(std::vector<DebugOverlayInstance> instances) {
    boneSegments = std::move(instances);
  }
  void setBoneMarkers(std::vector<DebugOverlayInstance> instances) {
    boneMarkers = std::move(instances);
  }
  void setLightMarkers(std::vector<DebugOverlayMarker> markers) {
    lightMarkers = std::move(markers);
  }
  void setCustomMarkerMesh(Mesh &mesh) { customMarkerMesh = &mesh; }
  void setCustomVisible(bool visible) { customVisible = visible; }
  void setCustomSegments(std::vector<DebugOverlayInstance> instances) {
    customSegments = std::move(instances);
  }
  void setCustomMarkers(std::vector<DebugOverlayInstance> instances) {
    customMarkers = std::move(instances);
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
    if (markersVisible) {
      for (const auto &marker : lightMarkers) {
        switch (marker.type) {
        case DebugOverlayMarkerType::Directional:
          if (directionalMarkerMesh != nullptr) {
            drawMarker(
                context, *directionalMarkerMesh,
                buildOrientationTransform(marker.position, marker.direction,
                                          markerScale),
                marker.color);
          }
          break;
        case DebugOverlayMarkerType::Point:
          if (pointMarkerMesh != nullptr) {
            drawMarker(context, *pointMarkerMesh,
                       glm::translate(glm::mat4(1.0f), marker.position) *
                           glm::scale(glm::mat4(1.0f), glm::vec3(markerScale)),
                       marker.color);
          }
          break;
        case DebugOverlayMarkerType::Spot:
          if (spotMarkerMesh != nullptr) {
            drawMarker(context, *spotMarkerMesh,
                       buildOrientationTransform(marker.position,
                                                 marker.direction, markerScale),
                       marker.color);
          }
          break;
        }
      }
    }

    if (customVisible) {
      if (boneSegmentMesh != nullptr) {
        for (const auto &segment : customSegments) {
          drawMarker(context, *boneSegmentMesh, segment.model, segment.color);
        }
      }
      if (boneJointMesh != nullptr) {
        for (const auto &marker : customMarkers) {
          drawMarker(context,
                     customMarkerMesh != nullptr ? *customMarkerMesh
                                                 : *boneJointMesh,
                     marker.model, marker.color);
        }
      }
    }

    if (!bonesVisible) {
      return;
    }

    if (boneSegmentMesh != nullptr) {
      for (const auto &segment : boneSegments) {
        drawMarker(context, *boneSegmentMesh, segment.model, segment.color);
      }
    }

    if (boneJointMesh != nullptr) {
      for (const auto &marker : boneMarkers) {
        drawMarker(context, *boneJointMesh, marker.model, marker.color);
      }
    }
  }

private:
  uint32_t framesInFlightCount = 0;
  PassUniformSet<DebugOverlayFrameUniformData> frameUniformSet;
  DebugOverlayFrameUniformData frameUniform{};
  Mesh *pointMarkerMesh = nullptr;
  Mesh *spotMarkerMesh = nullptr;
  Mesh *directionalMarkerMesh = nullptr;
  Mesh *boneSegmentMesh = nullptr;
  Mesh *boneJointMesh = nullptr;
  Mesh *customMarkerMesh = nullptr;
  bool markersVisible = true;
  float markerScale = 0.35f;
  bool bonesVisible = true;
  bool customVisible = false;
  std::vector<DebugOverlayMarker> lightMarkers;
  std::vector<DebugOverlayInstance> boneSegments;
  std::vector<DebugOverlayInstance> boneMarkers;
  std::vector<DebugOverlayInstance> customSegments;
  std::vector<DebugOverlayInstance> customMarkers;

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
