#pragma once

#include "renderer/backend/AppWindow.h"
#include "renderer/backend/CommandContext.h"
#include "renderer/backend/InstanceContext.h"
#include "renderer/passes/DebugOverlayPass.h"
#include "renderer/passes/DebugPresentPass.h"
#include "renderer/passes/GeometryPass.h"
#include "renderer/passes/ImGuiPass.h"
#include "renderer/passes/PbrPass.h"
#include "renderer/passes/ShadowPass.h"
#include "renderer/passes/TonemapPass.h"
#include "renderer/lighting/ImageBasedLighting.h"
#include "renderer/core/PassRenderer.h"
#include "renderer/core/PipelineSpec.h"
#include "engine/editor/DebugUIState.h"
#include <array>
#include <memory>
#include <string>

class AppRendererSetup {
public:
  template <size_t SpotShadowPassCount>
  static void registerShadowPasses(
      PassRenderer &renderer, ShadowPass *&directionalShadowPass,
      std::array<ShadowPass *, SpotShadowPassCount> &spotShadowPasses,
      uint32_t maxFramesInFlight, uint32_t shadowMapResolution,
      const std::string &rendererAssetPath) {
    auto directionalShadowPassLocal = std::make_unique<ShadowPass>(
        PipelineSpec{.shaderPath =
                         rendererAssetPath + "/shaders/shadow_pass.spv",
                     .cullMode = vk::CullModeFlagBits::eBack,
                     .frontFace = vk::FrontFace::eCounterClockwise,
                     .enableDepthBias = true},
        maxFramesInFlight, shadowMapResolution);
    directionalShadowPass = directionalShadowPassLocal.get();
    renderer.addPass(std::move(directionalShadowPassLocal));

    for (uint32_t index = 0; index < SpotShadowPassCount; ++index) {
      auto spotShadowPassLocal = std::make_unique<ShadowPass>(
          PipelineSpec{.shaderPath =
                           rendererAssetPath + "/shaders/shadow_pass.spv",
                       .cullMode = vk::CullModeFlagBits::eBack,
                       .frontFace = vk::FrontFace::eCounterClockwise,
                       .enableDepthBias = true},
          maxFramesInFlight, shadowMapResolution);
      spotShadowPasses[index] = spotShadowPassLocal.get();
      renderer.addPass(std::move(spotShadowPassLocal));
    }
  }

  template <size_t SpotShadowPassCount>
  static void registerMainPasses(
      PassRenderer &renderer, GeometryPass *&geometryPass, PbrPass *&pbrPass,
      TonemapPass *&tonemapPass, DebugPresentPass *&debugPresentPass,
      DebugOverlayPass *&debugOverlayPass, ImGuiPass *&imguiPass,
      AppWindow &window, InstanceContext &instanceContext,
      CommandContext &commandContext, const DefaultDebugUISettings &settings,
      ImageBasedLighting &imageBasedLighting, ShadowPass *directionalShadowPass,
      const std::array<ShadowPass *, SpotShadowPassCount> &spotShadowPasses,
      Mesh &pointLightMarkerMesh, Mesh &spotLightMarkerMesh,
      Mesh &directionalLightMarkerMesh, Mesh &boneSegmentMesh,
      Mesh &boneJointMesh, uint32_t maxFramesInFlight, float cameraNearPlane,
      const std::string &rendererAssetPath) {
    static_assert(SpotShadowPassCount == MAX_DEBUG_SPOT_SHADOW_MAPS,
                  "DebugPresentPass expects all spot shadow slots");

    auto geometryPassLocal = std::make_unique<GeometryPass>(
        PipelineSpec{.shaderPath =
                         rendererAssetPath + "/shaders/geometry_pass.spv",
                     .cullMode = vk::CullModeFlagBits::eNone,
                     .frontFace = vk::FrontFace::eCounterClockwise});
    geometryPass = geometryPassLocal.get();
    renderer.addPass(std::move(geometryPassLocal));

    auto pbrPassLocal = std::make_unique<PbrPass>(
        PipelineSpec{.shaderPath =
                         rendererAssetPath + "/shaders/pbr_pass.spv",
                     .enableDepthTest = false,
                     .enableDepthWrite = false},
        maxFramesInFlight, geometryPass);
    pbrPass = pbrPassLocal.get();
    pbrPass->setImageBasedLighting(imageBasedLighting);
    pbrPass->setShadowPass(0, *directionalShadowPass);
    for (uint32_t index = 0; index < SpotShadowPassCount; ++index) {
      pbrPass->setShadowPass(index + 1, *spotShadowPasses[index]);
    }
    renderer.addPass(std::move(pbrPassLocal));

    auto tonemapPassLocal = std::make_unique<TonemapPass>(
        PipelineSpec{.shaderPath =
                         rendererAssetPath + "/shaders/tonemap_pass.spv",
                     .enableDepthTest = false,
                     .enableDepthWrite = false},
        maxFramesInFlight, pbrPass);
    tonemapPass = tonemapPassLocal.get();
    renderer.addPass(std::move(tonemapPassLocal));

    auto debugPresentPassLocal = std::make_unique<DebugPresentPass>(
        PipelineSpec{.shaderPath = rendererAssetPath +
                                    "/shaders/debug_gbuffer_pass.spv",
                     .enableDepthTest = false,
                     .enableDepthWrite = false},
        maxFramesInFlight, geometryPass, pbrPass, tonemapPass,
        directionalShadowPass, spotShadowPasses);
    debugPresentPass = debugPresentPassLocal.get();
    debugPresentPass->setSelectedOutput(
        static_cast<uint32_t>(settings.presentedOutput));
    debugPresentPass->setClipPlanes(cameraNearPlane, settings.cameraFarPlane);
    renderer.addPass(std::move(debugPresentPassLocal));

    auto debugOverlayPassLocal = std::make_unique<DebugOverlayPass>(
        PipelineSpec{.shaderPath = rendererAssetPath +
                                    "/shaders/debug_overlay_pass.spv",
                     .topology = vk::PrimitiveTopology::eLineList,
                     .cullMode = vk::CullModeFlagBits::eNone,
                     .enableDepthTest = false,
                     .enableDepthWrite = false,
                     .enableBlending = true},
        maxFramesInFlight);
    debugOverlayPass = debugOverlayPassLocal.get();
    debugOverlayPass->setPointMarkerMesh(pointLightMarkerMesh);
    debugOverlayPass->setSpotMarkerMesh(spotLightMarkerMesh);
    debugOverlayPass->setDirectionalMarkerMesh(directionalLightMarkerMesh);
    debugOverlayPass->setBoneSegmentMesh(boneSegmentMesh);
    debugOverlayPass->setBoneJointMesh(boneJointMesh);
    debugOverlayPass->setMarkersVisible(settings.lightMarkersVisible);
    debugOverlayPass->setMarkerScale(settings.lightMarkerScale);
    debugOverlayPass->setBonesVisible(settings.bonesVisible);
    renderer.addPass(std::move(debugOverlayPassLocal));

    auto imguiPassLocal =
        std::make_unique<ImGuiPass>(window, instanceContext, commandContext);
    imguiPass = imguiPassLocal.get();
    renderer.addPass(std::move(imguiPassLocal));
  }
};
