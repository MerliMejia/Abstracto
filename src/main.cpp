#include "backend/AppWindow.h"
#include "backend/BackendConfig.h"
#include "backend/VulkanBackend.h"
#include "passes/DebugPass.h"
#include "passes/GeometryPass.h"
#include "passes/ImGuiPass.h"
#include "passes/PbrPass.h"
#include "passes/TonemapPass.h"
#include "renderable/FrameUniforms.h"
#include "renderable/RenderableModel.h"
#include "renderable/Sampler.h"
#include "renderer/PassRenderer.h"
#include "renderer/PipelineSpec.h"
#include "renderer/RenderPass.h"
#include "vulkan/vulkan.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <imgui.h>
#include <iostream>
#include <memory>

constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 720;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;
constexpr bool DEBUG_SHOW_SOLID_TRANSFORM_PASS = false;
const std::string ASSET_PATH = "assets";

enum class PresentedOutput : uint32_t {
  GBufferAlbedo = 0,
  GBufferNormal = 1,
  GBufferMaterial = 2,
  GBufferEmissive = 3,
  GBufferDepth = 4,
  GeometryPass = 5,
  LightPass = 6,
  TonemapPass = 7,
};

std::string normalizedMaterialName(std::string_view name) {
  std::string normalized;
  normalized.reserve(name.size());

  for (char c : name) {
    normalized.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }

  return normalized;
}

class DoublePassApp {
public:
  void run() {
    initWindow();
    initVulkan();
    mainLopp();
    cleanup();
  }

private:
  AppWindow window;
  VulkanBackend backend;
  BackendConfig config{.appName = "Double Pass",
                       .maxFramesInFlight = MAX_FRAMES_IN_FLIGHT};

  DeviceContext &deviceContext() { return backend.device(); }
  SwapchainContext &swapchainContext() { return backend.swapchain(); }
  CommandContext &commandContext() { return backend.commands(); }

  PassRenderer renderer;
  std::vector<RenderItem> renderItems;

  RenderableModel sceneModel;
  FullscreenMesh lightQuad;
  FrameUniforms frameUniforms;
  Sampler sampler;
  PbrPass *pbrPass = nullptr;
  TonemapPass *tonemapPass = nullptr;
  DebugPass *debugPass = nullptr;
  ImGuiPass *imguiPass = nullptr;
  std::chrono::steady_clock::time_point lastFrameTime =
      std::chrono::steady_clock::now();
  float lightAzimuthRadians = glm::radians(225.0f);
  float lightElevationRadians = glm::radians(-35.264f);
  float lightIntensity = 1.0f;
  glm::vec3 lightColor = {1.0f, 0.95f, 0.9f};
  float exposure = 1.0f;
  float autoExposureKey = 1.0f;
  float whitePoint = 4.0f;
  float gamma = 2.2f;
  bool autoExposureEnabled = false;
  TonemapOperator tonemapOperator = TonemapOperator::ACES;
  PresentedOutput presentedOutput = PresentedOutput::TonemapPass;
  int selectedMaterialIndex = 0;

  void initWindow() { window.create(WIDTH, HEIGHT, "Double Pass"); }

  void initVulkan() {
    backend.initialize(window, config);

    sampler.create(deviceContext());

    lightQuad = buildFullscreenQuadMesh();
    lightQuad.createVertexBuffer(commandContext(), deviceContext());
    lightQuad.createIndexBuffer(commandContext(), deviceContext());

    auto geometryPass = std::make_unique<GeometryPass>(
        PipelineSpec{.shaderPath = ASSET_PATH + "/shaders/geometry_pass.spv",
                     .cullMode = vk::CullModeFlagBits::eBack,
                     .frontFace = vk::FrontFace::eCounterClockwise});
    auto *geometryPassPtr = geometryPass.get();
    renderer.addPass(std::move(geometryPass));

    auto pbrPassLocal = std::make_unique<PbrPass>(
        PipelineSpec{.shaderPath = ASSET_PATH + "/shaders/pbr_pass.spv",
                     .enableDepthTest = false,
                     .enableDepthWrite = false},
        MAX_FRAMES_IN_FLIGHT, geometryPassPtr);
    pbrPass = pbrPassLocal.get();
    renderer.addPass(std::move(pbrPassLocal));

    auto tonemapPassLocal = std::make_unique<TonemapPass>(
        PipelineSpec{.shaderPath = ASSET_PATH + "/shaders/tonemap_pass.spv",
                     .enableDepthTest = false,
                     .enableDepthWrite = false},
        MAX_FRAMES_IN_FLIGHT, pbrPass);
    tonemapPass = tonemapPassLocal.get();
    renderer.addPass(std::move(tonemapPassLocal));

    auto debugPassLocal = std::make_unique<DebugPass>(
        PipelineSpec{.shaderPath =
                         ASSET_PATH + "/shaders/debug_gbuffer_pass.spv",
                     .enableDepthTest = false,
                     .enableDepthWrite = false},
        MAX_FRAMES_IN_FLIGHT, geometryPassPtr, pbrPass, tonemapPass);
    debugPass = debugPassLocal.get();
    debugPass->setSelectedOutput(static_cast<uint32_t>(presentedOutput));
    renderer.addPass(std::move(debugPassLocal));

    auto imguiPassLocal = std::make_unique<ImGuiPass>(
        window, backend.instance(), commandContext());
    imguiPass = imguiPassLocal.get();
    renderer.addPass(std::move(imguiPassLocal));

    renderer.initialize(deviceContext(), swapchainContext());

    frameUniforms.create(deviceContext(), MAX_FRAMES_IN_FLIGHT);
    sceneModel.loadFromFile(ASSET_PATH + "/models/chronograph_watch.glb",
                            commandContext(), deviceContext(),
                            renderer.descriptorSetLayout(), frameUniforms,
                            sampler, MAX_FRAMES_IN_FLIGHT);

    renderItems = sceneModel.buildRenderItems(geometryPassPtr);
    renderItems.push_back(RenderItem{.mesh = &lightQuad,
                                     .descriptorBindings = nullptr,
                                     .targetPass = pbrPass});
    renderItems.push_back(RenderItem{.mesh = &lightQuad,
                                     .descriptorBindings = nullptr,
                                     .targetPass = tonemapPass});
    renderItems.push_back(RenderItem{.mesh = &lightQuad,
                                     .descriptorBindings = nullptr,
                                     .targetPass = debugPass});
  }

  bool buildMaterialEditorUi() {
    bool materialChanged = false;
    auto &materials = sceneModel.mutableMaterials();
    if (materials.empty()) {
      return false;
    }

    selectedMaterialIndex = std::clamp(selectedMaterialIndex, 0,
                                       static_cast<int>(materials.size()) - 1);

    ImGui::Begin("Materials");
    for (int index = 0; index < static_cast<int>(materials.size()); ++index) {
      const bool selected = selectedMaterialIndex == index;
      const char *label = materials[index].name.empty()
                              ? "<unnamed>"
                              : materials[index].name.c_str();
      if (ImGui::Selectable(label, selected)) {
        selectedMaterialIndex = index;
      }
    }
    ImGui::End();

    auto &material = materials[static_cast<size_t>(selectedMaterialIndex)];
    ImGui::Begin("Material Properties");
    ImGui::Text("Selected: %s",
                material.name.empty() ? "<unnamed>" : material.name.c_str());
    materialChanged |=
        ImGui::ColorEdit4("Base Color", &material.baseColorFactor.x);
    materialChanged |=
        ImGui::ColorEdit3("Emissive", &material.emissiveFactor.x);
    materialChanged |=
        ImGui::SliderFloat("Metallic", &material.metallicFactor, 0.0f, 1.0f);
    materialChanged |=
        ImGui::SliderFloat("Roughness", &material.roughnessFactor, 0.0f, 1.0f);
    materialChanged |=
        ImGui::SliderFloat("Normal Scale", &material.normalScale, 0.0f, 2.0f);
    materialChanged |= ImGui::SliderFloat(
        "Occlusion Strength", &material.occlusionStrength, 0.0f, 1.0f);

    ImGui::Separator();
    ImGui::TextUnformatted("Textures");
    ImGui::BulletText("Base Color: %s",
                      material.baseColorTexture.hasPath() ||
                              material.baseColorTexture.hasEmbeddedRgba()
                          ? "yes"
                          : "no");
    ImGui::BulletText(
        "Metallic/Roughness: %s",
        material.metallicRoughnessTexture.hasPath() ||
                material.metallicRoughnessTexture.hasEmbeddedRgba()
            ? "yes"
            : "no");
    ImGui::BulletText("Normal: %s",
                      material.normalTexture.hasPath() ||
                              material.normalTexture.hasEmbeddedRgba()
                          ? "yes"
                          : "no");
    ImGui::BulletText("Emissive: %s",
                      material.emissiveTexture.hasPath() ||
                              material.emissiveTexture.hasEmbeddedRgba()
                          ? "yes"
                          : "no");
    ImGui::BulletText("Occlusion: %s",
                      material.occlusionTexture.hasPath() ||
                              material.occlusionTexture.hasEmbeddedRgba()
                          ? "yes"
                          : "no");
    ImGui::End();

    return materialChanged;
  }

  void buildLightUi() {
    ImGui::Begin("Light");
    float azimuthDegrees = glm::degrees(lightAzimuthRadians);
    float elevationDegrees = glm::degrees(lightElevationRadians);
    if (ImGui::SliderFloat("Azimuth", &azimuthDegrees, -180.0f, 180.0f)) {
      lightAzimuthRadians = glm::radians(azimuthDegrees);
    }
    if (ImGui::SliderFloat("Elevation", &elevationDegrees, -89.0f, 89.0f)) {
      lightElevationRadians = glm::radians(elevationDegrees);
    }
    ImGui::SliderFloat("Intensity", &lightIntensity, 0.0f, 20.0f);
    ImGui::ColorEdit3("Color", &lightColor.x);
    ImGui::SeparatorText("Tonemap");

    int tonemapOperatorIndex = static_cast<int>(tonemapOperator);
    ImGui::Combo("Operator", &tonemapOperatorIndex, "None\0Reinhard\0ACES\0");
    tonemapOperator = static_cast<TonemapOperator>(tonemapOperatorIndex);

    ImGui::Checkbox("Auto Exposure", &autoExposureEnabled);
    if (autoExposureEnabled) {
      ImGui::SliderFloat("Auto Key", &autoExposureKey, 0.1f, 2.5f);
    } else {
      ImGui::SliderFloat("Exposure", &exposure, 0.1f, 4.0f);
    }
    ImGui::SliderFloat("White Point", &whitePoint, 0.5f, 16.0f);
    ImGui::SliderFloat("Gamma", &gamma, 1.0f, 3.0f);
    ImGui::Text("Direction: %.2f %.2f %.2f", currentLightDirectionWorld().x,
                currentLightDirectionWorld().y, currentLightDirectionWorld().z);
    ImGui::End();
  }

  void buildDebugUi() {
    ImGui::Begin("View");
    int output = static_cast<int>(presentedOutput);
    ImGui::SeparatorText("GBuffers (Geometry Pass)");
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

    ImGui::SeparatorText("Pass Outputs");
    ImGui::RadioButton("Geometry Pass", &output,
                       static_cast<int>(PresentedOutput::GeometryPass));
    ImGui::RadioButton("Light Pass", &output,
                       static_cast<int>(PresentedOutput::LightPass));
    ImGui::RadioButton("Tone Mapping Pass", &output,
                       static_cast<int>(PresentedOutput::TonemapPass));

    presentedOutput = static_cast<PresentedOutput>(output);
    if (debugPass != nullptr) {
      debugPass->setSelectedOutput(static_cast<uint32_t>(presentedOutput));
    }
    ImGui::End();
  }

  glm::vec3 currentLightDirectionWorld() const {
    const float cosElevation = std::cos(lightElevationRadians);
    return glm::normalize(
        glm::vec3(cosElevation * std::cos(lightAzimuthRadians),
                  cosElevation * std::sin(lightAzimuthRadians),
                  std::sin(lightElevationRadians)));
  }

  void drawFrame() {
    auto frameState = backend.beginFrame(window);

    if (!frameState.has_value()) {
      backend.recreateSwapchain(window);
      renderer.recreate(deviceContext(), swapchainContext());
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    const float deltaSeconds = std::min(
        std::chrono::duration<float>(now - lastFrameTime).count(), 0.1f);
    lastFrameTime = now;

    if (imguiPass != nullptr) {
      imguiPass->beginFrame();
      const bool materialChanged = buildMaterialEditorUi();
      buildLightUi();
      buildDebugUi();
      if (materialChanged) {
        sceneModel.syncMaterialParameters();
      }
      imguiPass->endFrame();
    }

    UniformBufferObject ubo{};

    ubo.model = glm::mat4(1.0f);
    ubo.model = glm::scale(ubo.model, glm::vec3(0.3f));

    ubo.view =
        glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 0.0f, 1.0f));

    ubo.proj = glm::perspective(
        glm::radians(45.0f),
        static_cast<float>(swapchainContext().extent2D().width) /
            static_cast<float>(swapchainContext().extent2D().height),
        0.1f, 10.0f);

    // Vulkan inverts Y.
    ubo.proj[1][1] *= -1.0f;

    frameUniforms.write(frameState->frameIndex, ubo);

    if (pbrPass != nullptr) {
      glm::vec3 lightDirectionWorld = currentLightDirectionWorld();
      glm::vec3 lightDirectionView =
          glm::normalize(glm::mat3(ubo.view) * lightDirectionWorld);

      pbrPass->setProjection(ubo.proj);
      pbrPass->setDirectionalLight(lightDirectionView,
                                   lightColor * lightIntensity);
    }
    if (tonemapPass != nullptr) {
      const glm::vec3 lightRadiance = lightColor * lightIntensity;
      const float lightLuminance =
          glm::dot(lightRadiance, glm::vec3(0.2126f, 0.7152f, 0.0722f));
      const float resolvedExposure =
          autoExposureEnabled
              ? glm::clamp(autoExposureKey / std::max(lightLuminance, 0.001f),
                           0.05f, 8.0f)
              : exposure;
      tonemapPass->setExposure(resolvedExposure);
      tonemapPass->setWhitePoint(whitePoint);
      tonemapPass->setGamma(gamma);
      tonemapPass->setOperator(tonemapOperator);
    }

    renderer.record(backend.commands().commandBuffer(frameState->frameIndex),
                    swapchainContext(), renderItems, frameState->frameIndex,
                    frameState->imageIndex);

    bool shouldRecreate = backend.endFrame(*frameState, window);
    if (shouldRecreate) {
      backend.recreateSwapchain(window);
      renderer.recreate(deviceContext(), swapchainContext());
    }
  }
  void mainLopp() {
    while (!window.shouldClose()) {
      window.pollEvents();
      drawFrame();
    }
    backend.waitIdle();
  }
  void cleanup() { window.destroy(); }
};

int main() {
  try {
    DoublePassApp app;
    app.run();
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
