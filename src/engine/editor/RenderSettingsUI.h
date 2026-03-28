#pragma once

#include "DebugUIState.h"
#include <algorithm>
#include <utility>

class RenderSettingsUI {
public:
  explicit RenderSettingsUI(DefaultDebugUIBindings bindings)
      : bindings(std::move(bindings)) {}

  bool buildWorldPanel() {
    bool rebuildRequested = false;
    ImGui::Begin("World");
    if (ImGui::BeginTabBar("WorldTabs")) {
      if (ImGui::BeginTabItem("Rendering")) {
        buildLightingDebugUi();
        buildTonemapUi();
        buildPbrDebugUi();
        buildViewUi();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Environment")) {
        rebuildRequested = buildEnvironmentUi();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    ImGui::End();
    return rebuildRequested;
  }

private:
  DefaultDebugUIBindings bindings;

  void buildLightingDebugUi() {
    auto &settings = bindings.settings;
    ImGui::SeparatorText("Lighting & Shadows");
    ImGui::Checkbox("Enable Shadows", &settings.shadowsEnabled);
    ImGui::SliderFloat("Directional Shadow Extent",
                       &settings.directionalShadowExtent, 1.0f, 40.0f, "%.2f");
    ImGui::SliderFloat("Directional Shadow Near",
                       &settings.directionalShadowNearPlane, 0.01f, 5.0f,
                       "%.2f");
    ImGui::SliderFloat("Directional Shadow Far",
                       &settings.directionalShadowFarPlane, 1.0f, 80.0f,
                       "%.2f");
    settings.directionalShadowFarPlane =
        std::max(settings.directionalShadowFarPlane,
                 settings.directionalShadowNearPlane + 0.5f);

    ImGui::SeparatorText("Debug Overlay");
    ImGui::Checkbox("Show Light Markers", &settings.lightMarkersVisible);
    ImGui::SliderFloat("Marker Scale", &settings.lightMarkerScale, 0.05f, 2.5f);
    ImGui::Checkbox("Show Bones", &settings.bonesVisible);
    ImGui::SliderFloat("Bone Marker Scale", &settings.boneMarkerScale, 0.01f,
                       0.5f, "%.3f");
    ImGui::Checkbox("Show Bone Weights", &settings.showBoneWeights);
  }

  void buildTonemapUi() {
    auto &settings = bindings.settings;
    ImGui::SeparatorText("Tonemap");
    int tonemapOperatorIndex = static_cast<int>(settings.tonemapOperator);
    ImGui::Combo("Operator", &tonemapOperatorIndex,
                 "None\0Reinhard\0ACES\0Filmic\0");
    settings.tonemapOperator =
        static_cast<TonemapOperator>(tonemapOperatorIndex);

    ImGui::Checkbox("Auto Exposure", &settings.autoExposureEnabled);
    if (settings.autoExposureEnabled) {
      ImGui::SliderFloat("Auto Key", &settings.autoExposureKey, 0.1f, 2.5f);
    } else {
      ImGui::SliderFloat("Exposure", &settings.exposure, 0.1f, 4.0f);
    }
    ImGui::SliderFloat("White Point", &settings.whitePoint, 0.5f, 16.0f);
    ImGui::SliderFloat("Gamma", &settings.gamma, 1.0f, 3.0f);
  }

  void buildViewUi() {
    auto &settings = bindings.settings;
    ImGui::SeparatorText("Render View");
    int output = static_cast<int>(settings.presentedOutput);
    ImGui::TextUnformatted("GBuffers");
    ImGui::RadioButton("Albedo", &output,
                       static_cast<int>(PresentedOutput::GBufferAlbedo));
    ImGui::RadioButton("Normal", &output,
                       static_cast<int>(PresentedOutput::GBufferNormal));
    ImGui::RadioButton("Material", &output,
                       static_cast<int>(PresentedOutput::GBufferMaterial));
    ImGui::RadioButton("Emissive", &output,
                       static_cast<int>(PresentedOutput::GBufferEmissive));
    ImGui::RadioButton("Depth", &output,
                       static_cast<int>(PresentedOutput::GBufferDepth));

    ImGui::Separator();
    ImGui::TextUnformatted("Pass Outputs");
    ImGui::RadioButton("Geometry Pass", &output,
                       static_cast<int>(PresentedOutput::GeometryPass));
    ImGui::RadioButton("PBR Pass", &output,
                       static_cast<int>(PresentedOutput::PbrPass));
    ImGui::RadioButton("Tone Mapping Pass", &output,
                       static_cast<int>(PresentedOutput::TonemapPass));

    ImGui::Separator();
    ImGui::TextUnformatted("Shadow Maps");
    ImGui::RadioButton("Directional Shadow", &output,
                       static_cast<int>(PresentedOutput::DirectionalShadow));
    ImGui::RadioButton("Spot Shadow 1", &output,
                       static_cast<int>(PresentedOutput::SpotShadow0));
    ImGui::RadioButton("Spot Shadow 2", &output,
                       static_cast<int>(PresentedOutput::SpotShadow1));
    ImGui::RadioButton("Spot Shadow 3", &output,
                       static_cast<int>(PresentedOutput::SpotShadow2));

    settings.presentedOutput = static_cast<PresentedOutput>(output);
  }

  void buildPbrDebugUi() {
    auto &settings = bindings.settings;
    ImGui::SeparatorText("PBR Debug");
    int pbrDebugMode = static_cast<int>(settings.pbrDebugView);
    ImGui::RadioButton("Final", &pbrDebugMode,
                       static_cast<int>(PbrDebugView::Final));
    ImGui::RadioButton("Direct Lighting", &pbrDebugMode,
                       static_cast<int>(PbrDebugView::DirectLighting));
    ImGui::RadioButton("IBL Diffuse", &pbrDebugMode,
                       static_cast<int>(PbrDebugView::IblDiffuse));
    ImGui::RadioButton("IBL Specular", &pbrDebugMode,
                       static_cast<int>(PbrDebugView::IblSpecular));
    ImGui::RadioButton("Ambient Total", &pbrDebugMode,
                       static_cast<int>(PbrDebugView::AmbientTotal));
    ImGui::RadioButton("Reflections", &pbrDebugMode,
                       static_cast<int>(PbrDebugView::Reflections));
    ImGui::RadioButton("Background", &pbrDebugMode,
                       static_cast<int>(PbrDebugView::Background));
    settings.pbrDebugView = static_cast<PbrDebugView>(pbrDebugMode);
  }

  bool buildEnvironmentUi() {
    auto &settings = bindings.settings;
    ImGui::SeparatorText("Environment");
    ImGui::Checkbox("Enable IBL", &settings.iblEnabled);
    ImGui::Checkbox("Show Skybox", &settings.skyboxVisible);
    ImGui::SliderFloat("Env Intensity", &settings.environmentIntensity, 0.0f,
                       4.0f);
    ImGui::SliderFloat("Skybox Weight", &settings.environmentBackgroundWeight,
                       0.0f, 4.0f);
    ImGui::SliderFloat("Diffuse IBL", &settings.environmentDiffuseWeight, 0.0f,
                       4.0f);
    ImGui::SliderFloat("Specular IBL", &settings.environmentSpecularWeight,
                       0.0f, 4.0f);
    ImGui::SliderFloat("Dielectric Specular", &settings.dielectricSpecularScale,
                       0.5f, 3.0f);
    ImGui::SliderAngle("Env Rotation", &settings.environmentRotationRadians,
                       -180.0f, 180.0f);

    ImGui::SeparatorText("Procedural Sky");
    ImGui::TextUnformatted("Changes here do not rebuild automatically.");
    ImGui::TextUnformatted("Use the button below to regenerate the IBL.");
    ImGui::Separator();
    if (!settings.iblBakeSettings.environmentHdrPath.empty()) {
      ImGui::TextWrapped("Using HDRI environment: %s",
                         settings.iblBakeSettings.environmentHdrPath.c_str());
      ImGui::TextUnformatted(
          "Procedural sky controls are ignored while an HDRI is active.");
    } else {
      ImGui::Checkbox("Sync Sun To Light", &settings.syncSkySunToLight);

      if (settings.syncSkySunToLight) {
        bindings.callbacks.syncProceduralSkySunWithLight();
        ImGui::Text(
            "Sun Azimuth: %.1f deg",
            glm::degrees(settings.iblBakeSettings.sky.sunAzimuthRadians));
        ImGui::Text(
            "Sun Elevation: %.1f deg",
            glm::degrees(settings.iblBakeSettings.sky.sunElevationRadians));
      } else {
        float sunAzimuthDegrees =
            glm::degrees(settings.iblBakeSettings.sky.sunAzimuthRadians);
        float sunElevationDegrees =
            glm::degrees(settings.iblBakeSettings.sky.sunElevationRadians);
        if (ImGui::SliderFloat("Sun Azimuth", &sunAzimuthDegrees, -180.0f,
                               180.0f)) {
          settings.iblBakeSettings.sky.sunAzimuthRadians =
              glm::radians(sunAzimuthDegrees);
        }
        if (ImGui::SliderFloat("Sun Elevation", &sunElevationDegrees, -89.0f,
                               89.0f)) {
          settings.iblBakeSettings.sky.sunElevationRadians =
              glm::radians(sunElevationDegrees);
        }
      }

      ImGui::ColorEdit3("Zenith", &settings.iblBakeSettings.sky.zenithColor.x);
      ImGui::ColorEdit3("Horizon",
                        &settings.iblBakeSettings.sky.horizonColor.x);
      ImGui::ColorEdit3("Ground", &settings.iblBakeSettings.sky.groundColor.x);
      ImGui::ColorEdit3("Sun Color", &settings.iblBakeSettings.sky.sunColor.x);
      ImGui::SliderFloat("Sun Intensity",
                         &settings.iblBakeSettings.sky.sunIntensity, 0.0f,
                         80.0f);
      ImGui::SliderFloat("Sun Radius",
                         &settings.iblBakeSettings.sky.sunAngularRadius, 0.005f,
                         0.15f);
      ImGui::SliderFloat("Sun Glow", &settings.iblBakeSettings.sky.sunGlow,
                         0.0f, 8.0f);
      ImGui::SliderFloat("Horizon Glow",
                         &settings.iblBakeSettings.sky.horizonGlow, 0.0f, 1.0f);
    }

    return ImGui::Button("Rebuild IBL");
  }
};
