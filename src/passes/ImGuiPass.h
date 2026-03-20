#pragma once

#include "../backend/AppWindow.h"
#include "../backend/CommandContext.h"
#include "../backend/InstanceContext.h"
#include "../backend/SwapchainContext.h"
#include "../renderer/RenderPass.h"
#include <array>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <stdexcept>

class ImGuiPass : public RenderPass {
public:
  ImGuiPass(AppWindow &window, InstanceContext &instanceContext,
            CommandContext &commandContext)
      : windowRef(window), instanceContextRef(instanceContext),
        commandContextRef(commandContext) {}

  ~ImGuiPass() override { shutdown(); }

  void beginFrame() {
    if (!initialized) {
      return;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    mainDockspaceId = ImGui::DockSpaceOverViewport(
        0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
  }

  void endFrame() {
    if (!initialized) {
      return;
    }

    ImGui::Render();
  }

  void initialize(DeviceContext &deviceContext,
                  SwapchainContext &swapchainContext) override {
    shutdown();
    createDescriptorPool(deviceContext);
    initializeContext(deviceContext, swapchainContext);
    swapchainImageLayouts.assign(swapchainContext.imageCount(),
                                 vk::ImageLayout::eUndefined);
    initialized = true;
  }

  void recreate(DeviceContext &deviceContext,
                SwapchainContext &swapchainContext) override {
    if (!initialized) {
      initialize(deviceContext, swapchainContext);
      return;
    }

    colorAttachmentFormat =
        static_cast<VkFormat>(swapchainContext.surfaceFormatInfo().format);
    swapchainImageLayouts.assign(swapchainContext.imageCount(),
                                 vk::ImageLayout::eUndefined);
    ImGui_ImplVulkan_SetMinImageCount(
        static_cast<uint32_t>(swapchainContext.imageCount()));
  }

  void record(const RenderPassContext &context,
              const std::vector<RenderItem> &) override {
    if (!initialized || ImGui::GetDrawData() == nullptr) {
      return;
    }

    auto &swapchainImageLayout = swapchainImageLayouts.at(context.imageIndex);
    transitionImageLayout(
        context.commandBuffer,
        context.swapchainContext.swapchainImages()[context.imageIndex],
        swapchainImageLayout, vk::ImageLayout::eColorAttachmentOptimal,
        layoutAccessMask(swapchainImageLayout),
        vk::AccessFlagBits2::eColorAttachmentWrite,
        layoutStageMask(swapchainImageLayout),
        vk::PipelineStageFlagBits2::eColorAttachmentOutput);
    swapchainImageLayout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::RenderingAttachmentInfo colorAttachment{
        .imageView =
            context.swapchainContext.swapchainImageViews()[context.imageIndex],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    vk::RenderingInfo renderingInfo{
        .renderArea = {.offset = {0, 0},
                       .extent = context.swapchainContext.extent2D()},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };

    context.commandBuffer.beginRendering(renderingInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
                                    *context.commandBuffer);
    context.commandBuffer.endRendering();

    transitionImageLayout(
        context.commandBuffer,
        context.swapchainContext.swapchainImages()[context.imageIndex],
        swapchainImageLayout, vk::ImageLayout::ePresentSrcKHR,
        layoutAccessMask(swapchainImageLayout), {},
        layoutStageMask(swapchainImageLayout),
        vk::PipelineStageFlagBits2::eBottomOfPipe);
    swapchainImageLayout = vk::ImageLayout::ePresentSrcKHR;
  }

  ImGuiID dockspaceId() const { return mainDockspaceId; }

private:
  AppWindow &windowRef;
  InstanceContext &instanceContextRef;
  CommandContext &commandContextRef;
  vk::raii::DescriptorPool descriptorPool = nullptr;
  std::vector<vk::ImageLayout> swapchainImageLayouts;
  bool initialized = false;
  VkFormat colorAttachmentFormat = VK_FORMAT_UNDEFINED;
  ImGuiID mainDockspaceId = 0;

  static void checkVkResult(VkResult result) {
    if (result != VK_SUCCESS) {
      throw std::runtime_error("ImGui Vulkan backend call failed");
    }
  }

  static vk::AccessFlags2 layoutAccessMask(vk::ImageLayout layout) {
    switch (layout) {
    case vk::ImageLayout::eColorAttachmentOptimal:
      return vk::AccessFlagBits2::eColorAttachmentWrite;
    case vk::ImageLayout::ePresentSrcKHR:
      return {};
    default:
      return {};
    }
  }

  static vk::PipelineStageFlags2 layoutStageMask(vk::ImageLayout layout) {
    switch (layout) {
    case vk::ImageLayout::eColorAttachmentOptimal:
      return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    case vk::ImageLayout::ePresentSrcKHR:
      return vk::PipelineStageFlagBits2::eBottomOfPipe;
    default:
      return vk::PipelineStageFlagBits2::eTopOfPipe;
    }
  }

  static void transitionImageLayout(vk::raii::CommandBuffer &commandBuffer,
                                    vk::Image image, vk::ImageLayout oldLayout,
                                    vk::ImageLayout newLayout,
                                    vk::AccessFlags2 srcAccessMask,
                                    vk::AccessFlags2 dstAccessMask,
                                    vk::PipelineStageFlags2 srcStageMask,
                                    vk::PipelineStageFlags2 dstStageMask) {
    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    vk::DependencyInfo dependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    commandBuffer.pipelineBarrier2(dependencyInfo);
  }

  static void applyEditorTheme() {
    ImGui::StyleColorsDark();

    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    style.WindowTitleAlign = ImVec2(0.03f, 0.5f);
    style.ColorButtonPosition = ImGuiDir_Right;

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.93f, 0.95f, 0.98f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.57f, 0.64f, 1.0f);

    colors[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.04f, 0.06f, 0.74f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.05f, 0.07f, 0.10f, 0.52f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.07f, 0.10f, 0.88f);
    colors[ImGuiCol_Border] = ImVec4(0.23f, 0.31f, 0.41f, 0.34f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.11f, 0.15f, 0.70f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.13f, 0.18f, 0.24f, 0.80f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.25f, 0.33f, 0.90f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.02f, 0.03f, 0.05f, 0.82f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.05f, 0.07f, 0.11f, 0.90f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.02f, 0.03f, 0.05f, 0.58f);

    colors[ImGuiCol_MenuBarBg] = ImVec4(0.04f, 0.05f, 0.08f, 0.72f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.03f, 0.05f, 0.28f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.18f, 0.24f, 0.31f, 0.62f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.24f, 0.34f, 0.44f, 0.74f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.30f, 0.43f, 0.55f, 0.82f);

    colors[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.82f, 1.0f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.44f, 0.67f, 0.92f, 0.88f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.62f, 0.82f, 1.0f, 1.0f);

    colors[ImGuiCol_Button] = ImVec4(0.10f, 0.14f, 0.20f, 0.74f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.17f, 0.24f, 0.33f, 0.82f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.31f, 0.42f, 0.90f);

    colors[ImGuiCol_Header] = ImVec4(0.12f, 0.17f, 0.24f, 0.76f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.19f, 0.27f, 0.37f, 0.84f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.35f, 0.47f, 0.90f);

    colors[ImGuiCol_Separator] = ImVec4(0.30f, 0.42f, 0.54f, 0.36f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.43f, 0.62f, 0.79f, 0.68f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.53f, 0.74f, 0.93f, 0.84f);

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.34f, 0.48f, 0.61f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.47f, 0.67f, 0.85f, 0.62f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.57f, 0.81f, 1.0f, 0.90f);

    colors[ImGuiCol_Tab] = ImVec4(0.06f, 0.08f, 0.12f, 0.78f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.17f, 0.25f, 0.35f, 0.88f);
    colors[ImGuiCol_TabActive] = ImVec4(0.12f, 0.18f, 0.26f, 0.90f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.04f, 0.06f, 0.09f, 0.66f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.09f, 0.13f, 0.19f, 0.78f);

    colors[ImGuiCol_DockingPreview] = ImVec4(0.41f, 0.73f, 1.0f, 0.18f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.02f, 0.03f, 0.05f, 0.96f);

    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.08f, 0.11f, 0.16f, 0.84f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.18f, 0.25f, 0.33f, 0.42f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.12f, 0.16f, 0.22f, 0.28f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.07f, 0.10f, 0.14f, 0.22f);

    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.34f, 0.58f, 0.84f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.63f, 0.86f, 1.0f, 0.95f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.48f, 0.76f, 1.0f, 0.78f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.90f, 0.95f, 1.0f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.02f, 0.03f, 0.05f, 0.32f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.03f, 0.05f, 0.48f);
  }

  void createDescriptorPool(DeviceContext &deviceContext) {
    std::array poolSizes{
        vk::DescriptorPoolSize(vk::DescriptorType::eSampler, 1000),
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1000),
        vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, 1000),
        vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 1000),
        vk::DescriptorPoolSize(vk::DescriptorType::eUniformTexelBuffer, 1000),
        vk::DescriptorPoolSize(vk::DescriptorType::eStorageTexelBuffer, 1000),
        vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1000),
        vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 1000),
        vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, 1000),
        vk::DescriptorPoolSize(vk::DescriptorType::eStorageBufferDynamic, 1000),
        vk::DescriptorPoolSize(vk::DescriptorType::eInputAttachment, 1000),
    };

    vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = 1000 * static_cast<uint32_t>(poolSizes.size()),
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    descriptorPool =
        vk::raii::DescriptorPool(deviceContext.deviceHandle(), poolInfo);
  }

  void initializeContext(DeviceContext &deviceContext,
                         SwapchainContext &swapchainContext) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    applyEditorTheme();

    ImGui_ImplGlfw_InitForVulkan(windowRef.handle(), true);

    colorAttachmentFormat =
        static_cast<VkFormat>(swapchainContext.surfaceFormatInfo().format);
    VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
    pipelineRenderingInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipelineRenderingInfo.colorAttachmentCount = 1;
    pipelineRenderingInfo.pColorAttachmentFormats = &colorAttachmentFormat;

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = *instanceContextRef.instanceHandle();
    initInfo.PhysicalDevice = *deviceContext.physicalDeviceHandle();
    initInfo.Device = *deviceContext.deviceHandle();
    initInfo.QueueFamily = deviceContext.queueFamilyIndex();
    initInfo.Queue = *deviceContext.queueHandle();
    initInfo.DescriptorPool = *descriptorPool;
    initInfo.MinImageCount =
        static_cast<uint32_t>(swapchainContext.imageCount());
    initInfo.ImageCount = static_cast<uint32_t>(swapchainContext.imageCount());
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo =
        pipelineRenderingInfo;
    initInfo.CheckVkResultFn = checkVkResult;

    ImGui_ImplVulkan_Init(&initInfo);
  }

  void shutdown() {
    if (!initialized) {
      return;
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    initialized = false;
    descriptorPool = nullptr;
    swapchainImageLayouts.clear();
  }
};
