#pragma once

#include "../backend/AppWindow.h"
#include "../backend/CommandContext.h"
#include "../backend/InstanceContext.h"
#include "../backend/SwapchainContext.h"
#include "../renderable/RenderUtils.h"
#include "../renderer/RenderPass.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <array>
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
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                 ImGuiDockNodeFlags_PassthruCentralNode);
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
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *context.commandBuffer);
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

private:
  AppWindow &windowRef;
  InstanceContext &instanceContextRef;
  CommandContext &commandContextRef;
  vk::raii::DescriptorPool descriptorPool = nullptr;
  std::vector<vk::ImageLayout> swapchainImageLayouts;
  bool initialized = false;
  VkFormat colorAttachmentFormat = VK_FORMAT_UNDEFINED;

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
    ImGui::StyleColorsDark();

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
    initInfo.MinImageCount = static_cast<uint32_t>(swapchainContext.imageCount());
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
