#pragma once

#include "renderer/lighting/ImageBasedLighting.h"
#include "renderer/core/FullscreenRenderPass.h"
#include "renderer/core/PassUniformSet.h"
#include "GeometryPass.h"
#include "ShadowPass.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <optional>
#include <stdexcept>
#include <vector>

enum class PbrDebugView : uint32_t {
  Final = 0,
  DirectLighting = 1,
  IblDiffuse = 2,
  IblSpecular = 3,
  AmbientTotal = 4,
  Reflections = 5,
  Background = 6,
};

constexpr uint32_t MAX_PBR_LIGHTS = 8;
constexpr uint32_t MAX_PBR_SHADOW_MAPS = 4;
constexpr uint32_t INVALID_PBR_SHADOW_MAP = ~0u;

enum class PbrLightType : uint32_t {
  Directional = 0,
  Point = 1,
  Spot = 2,
};

struct PbrLightInput {
  uint32_t sourceIndex = 0;
  PbrLightType type = PbrLightType::Directional;
  bool enabled = true;
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  float range = 8.0f;
  glm::vec3 direction{0.0f, -1.0f, 0.0f};
  float radiance = 1.0f;
  glm::vec3 color{1.0f, 1.0f, 1.0f};
  float radius = 0.25f;
  float innerConeAngleRadians = glm::radians(18.0f);
  float outerConeAngleRadians = glm::radians(28.0f);
};

struct PbrLightUniformData {
  glm::vec4 positionAndType{0.0f, 0.0f, 0.0f,
                            static_cast<float>(PbrLightType::Directional)};
  glm::vec4 directionAndRange{0.0f, -1.0f, 0.0f, 1.0f};
  glm::vec4 colorAndRadiance{1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec4 spotAngles{1.0f, 0.0f, 0.0f, 0.0f};
  glm::mat4 shadowMatrix{1.0f};
  glm::vec4 shadowParams{0.0f, 0.0f, 0.0f, 0.0f};
  alignas(16) glm::uvec4 shadowInfo{0u, INVALID_PBR_SHADOW_MAP, 0u, 0u};
};

struct PbrPassUniformData {
  glm::vec4 projParams{1.0f, -1.0f, -1.0f, -0.1f};
  glm::vec4 cameraWorldAndBackground{0.0f, 0.0f, 0.0f, 1.0f};
  glm::vec4 viewRightAndDiffuse{1.0f, 0.0f, 0.0f, 1.0f};
  glm::vec4 viewUpAndSpecular{0.0f, 1.0f, 0.0f, 1.0f};
  glm::vec4 viewForwardAndUnused{0.0f, 0.0f, -1.0f, 0.0f};
  glm::vec4 environmentParams{0.0f, 0.0f, 0.0f, 0.0f};
  glm::vec4 specularTuning{2.0f, 0.0f, 0.0f, 0.0f};
  alignas(16) glm::uvec4 settings{0u, 0u, 0u, 0u};
  std::array<PbrLightUniformData, MAX_PBR_LIGHTS> lights{};
};

class PbrPass : public FullscreenRenderPass {
public:
  enum SettingFlags : uint32_t {
    EnableIbl = 1u << 0,
    ShowBackground = 1u << 1,
  };

  PbrPass(PipelineSpec spec, uint32_t framesInFlight,
          const GeometryPass *sourcePass = nullptr)
      : FullscreenRenderPass(
            std::move(spec), framesInFlight,
            RasterPassAttachmentConfig{
                .useColorAttachment = true,
                .useDepthAttachment = false,
                .useMsaaColorAttachment = false,
                .resolveToSwapchain = false,
                .useSwapchainColorAttachment = false,
                .offscreenColorFormat = vk::Format::eR16G16B16A16Sfloat,
                .sampleColorAttachment = true,
            }),
        sourcePassRef(sourcePass) {}

  void setSourcePass(const GeometryPass &sourcePass) {
    sourcePassRef = &sourcePass;
  }

  void setShadowPass(uint32_t index, const ShadowPass &shadowPass) {
    shadowPassRefs.at(index) = &shadowPass;
  }

  void setImageBasedLighting(const ImageBasedLighting &imageBasedLighting) {
    ibl = &imageBasedLighting;
    uniformData.environmentParams.y = ibl->maxPrefilterMipLevel();
  }

  void setCamera(const glm::mat4 &proj, const glm::mat4 &view) {
    uniformData.projParams =
        glm::vec4(proj[0][0], proj[1][1], proj[2][2], proj[3][2]);

    const glm::mat4 invView = glm::inverse(view);
    uniformData.cameraWorldAndBackground = glm::vec4(
        glm::vec3(invView[3]), uniformData.cameraWorldAndBackground.w);
    uniformData.viewRightAndDiffuse =
        glm::vec4(glm::normalize(glm::vec3(invView[0])),
                  uniformData.viewRightAndDiffuse.w);
    uniformData.viewUpAndSpecular = glm::vec4(
        glm::normalize(glm::vec3(invView[1])), uniformData.viewUpAndSpecular.w);
    uniformData.viewForwardAndUnused =
        glm::vec4(glm::normalize(-glm::vec3(invView[2])),
                  uniformData.viewForwardAndUnused.w);
  }

  void setLights(const std::vector<PbrLightInput> &lights) {
    uint32_t lightCount = 0;
    uint32_t maxSourceIndex = 0;
    for (const auto &light : lights) {
      maxSourceIndex = std::max(maxSourceIndex, light.sourceIndex);
    }
    sourceLightToUniformIndex.assign(
        lights.empty() ? 0 : static_cast<size_t>(maxSourceIndex) + 1, -1);

    for (auto &lightUniform : uniformData.lights) {
      lightUniform = PbrLightUniformData{};
    }

    for (const auto &light : lights) {
      if (!light.enabled || lightCount >= MAX_PBR_LIGHTS) {
        continue;
      }

      auto &lightUniform = uniformData.lights[lightCount];
      lightUniform.directionAndRange = glm::vec4(
          glm::normalize(light.direction), std::max(light.range, 0.01f));
      lightUniform.colorAndRadiance =
          glm::vec4(light.color, std::max(light.radiance, 0.0f));
      lightUniform.positionAndType =
          glm::vec4(light.position, static_cast<float>(light.type));
      lightUniform.spotAngles = glm::vec4(std::cos(light.innerConeAngleRadians),
                                          std::cos(light.outerConeAngleRadians),
                                          std::max(light.radius, 0.0f), 0.0f);
      sourceLightToUniformIndex[light.sourceIndex] =
          static_cast<int32_t>(lightCount);
      ++lightCount;
    }

    uniformData.settings.z = lightCount;
  }

  std::optional<uint32_t>
  uniformLightIndexForSource(size_t sourceIndex) const {
    if (sourceIndex >= sourceLightToUniformIndex.size()) {
      return std::nullopt;
    }

    const int32_t packedIndex = sourceLightToUniformIndex[sourceIndex];
    if (packedIndex < 0) {
      return std::nullopt;
    }
    return static_cast<uint32_t>(packedIndex);
  }

  void clearLightShadows() {
    for (auto &lightUniform : uniformData.lights) {
      lightUniform.shadowMatrix = glm::mat4(1.0f);
      lightUniform.shadowParams = glm::vec4(0.0f);
      lightUniform.shadowInfo = glm::uvec4(0u, INVALID_PBR_SHADOW_MAP, 0u, 0u);
    }
  }

  void setLightShadow(uint32_t lightIndex, uint32_t shadowMapIndex,
                      const glm::mat4 &shadowMatrix, float depthBias,
                      float normalBias, float shadowMapResolution) {
    if (lightIndex >= MAX_PBR_LIGHTS || shadowMapIndex >= MAX_PBR_SHADOW_MAPS) {
      throw std::runtime_error("PbrPass shadow binding index out of range");
    }

    auto &lightUniform = uniformData.lights[lightIndex];
    lightUniform.shadowMatrix = shadowMatrix;
    lightUniform.shadowParams =
        glm::vec4(depthBias, normalBias,
                  1.0f / std::max(shadowMapResolution, 1.0f), 0.0f);
    lightUniform.shadowInfo = glm::uvec4(1u, shadowMapIndex, 0u, 0u);
  }

  void setEnvironmentControls(float environmentRotationRadians,
                              float backgroundIntensity, float diffuseIntensity,
                              float specularIntensity, bool enableIbl,
                              bool showBackground) {
    uniformData.cameraWorldAndBackground.w = backgroundIntensity;
    uniformData.viewRightAndDiffuse.w = diffuseIntensity;
    uniformData.viewUpAndSpecular.w = specularIntensity;
    uniformData.environmentParams.x = environmentRotationRadians;

    uint32_t flags = 0;
    if (enableIbl) {
      flags |= EnableIbl;
    }
    if (showBackground) {
      flags |= ShowBackground;
    }
    uniformData.settings.x = flags;
  }

  void setDebugView(PbrDebugView debugView) {
    uniformData.settings.y = static_cast<uint32_t>(debugView);
  }

  void setDielectricSpecularScale(float scale) {
    uniformData.specularTuning.x = std::max(scale, 0.0f);
  }

protected:
  std::vector<DescriptorBindingSpec>
  secondaryDescriptorBindings() const override {
    return {{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment,
    }};
  }

  std::vector<FullscreenImageInputBinding> imageInputBindings() const override {
    return {
        {.binding = 0},  {.binding = 1}, {.binding = 2},  {.binding = 3},
        {.binding = 4},  {.binding = 5}, {.binding = 6},  {.binding = 7},
        {.binding = 8},  {.binding = 9}, {.binding = 10}, {.binding = 11},
        {.binding = 12},
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
    validateResources();

    return {
        {.binding = 0,
         .resource = sourcePassRef->sampledColorOutput(0, sampler)},
        {.binding = 1,
         .resource = sourcePassRef->sampledColorOutput(1, sampler)},
        {.binding = 2,
         .resource = sourcePassRef->sampledColorOutput(2, sampler)},
        {.binding = 3,
         .resource = sourcePassRef->sampledColorOutput(3, sampler)},
        {.binding = 4, .resource = sourcePassRef->sampledDepthOutput(sampler)},
        {.binding = 5, .resource = ibl->environmentResource()},
        {.binding = 6, .resource = ibl->irradianceResource()},
        {.binding = 7, .resource = ibl->prefilteredResource()},
        {.binding = 8, .resource = ibl->brdfResource()},
        {.binding = 9, .resource = shadowPassRefs[0]->sampledShadowOutput()},
        {.binding = 10, .resource = shadowPassRefs[1]->sampledShadowOutput()},
        {.binding = 11, .resource = shadowPassRefs[2]->sampledShadowOutput()},
        {.binding = 12, .resource = shadowPassRefs[3]->sampledShadowOutput()},
    };
  }

  void initializeAdditionalPassResources(DeviceContext &deviceContext,
                                         SwapchainContext &) override {
    lightUniformSet.initialize(deviceContext, passDescriptorSetLayout(1),
                               framesInFlight());
  }

  void bindAdditionalPassResources(const RenderPassContext &context) override {
    lightUniformSet.write(context.frameIndex, uniformData);
    lightUniformSet.bind(context.commandBuffer, pipelineLayoutHandle(),
                         context.frameIndex, 1);
  }

private:
  const GeometryPass *sourcePassRef = nullptr;
  const ImageBasedLighting *ibl = nullptr;
  std::array<const ShadowPass *, MAX_PBR_SHADOW_MAPS> shadowPassRefs{
      nullptr, nullptr, nullptr, nullptr};
  PbrPassUniformData uniformData{};
  PassUniformSet<PbrPassUniformData> lightUniformSet;
  std::vector<int32_t> sourceLightToUniformIndex;

  void validateResources() const {
    if (sourcePassRef == nullptr) {
      throw std::runtime_error("PbrPass requires a GeometryPass source");
    }
    if (ibl == nullptr) {
      throw std::runtime_error(
          "PbrPass requires image-based lighting resources");
    }
    for (const ShadowPass *shadowPass : shadowPassRefs) {
      if (shadowPass == nullptr) {
        throw std::runtime_error(
            "PbrPass requires all shadow passes to be set");
      }
    }
  }
};
