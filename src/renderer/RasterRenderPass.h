#pragma once

#include "../renderable/RenderUtils.h"
#include "PipelineSpec.h"
#include "RenderPass.h"
#include "SampledImageResource.h"
#include "ShaderProgram.h"
#include <array>
#include <optional>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

struct DescriptorBindingSpec {
  uint32_t binding = 0;
  vk::DescriptorType descriptorType = vk::DescriptorType::eUniformBuffer;
  uint32_t descriptorCount = 1;
  vk::ShaderStageFlags stageFlags = {};
};

inline DescriptorBindingSpec sampledImageBindingSpec(
    uint32_t binding,
    vk::ShaderStageFlags stageFlags = vk::ShaderStageFlagBits::eFragment) {
  return DescriptorBindingSpec{
      .binding = binding,
      .descriptorType = vk::DescriptorType::eCombinedImageSampler,
      .descriptorCount = 1,
      .stageFlags = stageFlags,
  };
}

struct RasterPassAttachmentConfig {
  bool useColorAttachment = true;
  bool useDepthAttachment = true;
  bool useMsaaColorAttachment = true;
  bool resolveToSwapchain = true;
  bool useSwapchainColorAttachment = true;
  bool sampleColorAttachment = false;
  vk::Format offscreenColorFormat = vk::Format::eUndefined;
  std::array<float, 4> clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
  float clearDepth = 1.0f;
  uint32_t clearStencil = 0;
  vk::AttachmentLoadOp colorLoadOp = vk::AttachmentLoadOp::eClear;
  vk::AttachmentStoreOp colorStoreOp = vk::AttachmentStoreOp::eStore;
  vk::AttachmentLoadOp depthLoadOp = vk::AttachmentLoadOp::eClear;
  vk::AttachmentStoreOp depthStoreOp = vk::AttachmentStoreOp::eDontCare;
};

using MeshPassAttachmentConfig = RasterPassAttachmentConfig;

struct VertexInputLayoutSpec {
  std::vector<vk::VertexInputBindingDescription> bindings;
  std::vector<vk::VertexInputAttributeDescription> attributes;
};

class RasterRenderPass : public RenderPass {
public:
  explicit RasterRenderPass(
      PipelineSpec pipelineSpec,
      RasterPassAttachmentConfig attachmentConfig = RasterPassAttachmentConfig())
      : spec(std::move(pipelineSpec)),
        attachments(std::move(attachmentConfig)) {}

  void initialize(DeviceContext &deviceContext,
                  SwapchainContext &swapchainContext) override {
    validateAttachmentConfig();
    createDescriptorSetLayout(deviceContext);
    createPipelineLayout(deviceContext);
    createGraphicsPipeline(deviceContext, swapchainContext);
    createAttachmentResources(deviceContext, swapchainContext);
    initializePassResources(deviceContext, swapchainContext);
  }

  void recreate(DeviceContext &deviceContext,
                SwapchainContext &swapchainContext) override {
    validateAttachmentConfig();
    createGraphicsPipeline(deviceContext, swapchainContext);
    createAttachmentResources(deviceContext, swapchainContext);
    recreatePassResources(deviceContext, swapchainContext);
  }

  void record(const RenderPassContext &context,
              const std::vector<RenderItem> &renderItems) override {
    transitionToRenderingLayouts(context);

    auto colorAttachment = buildColorAttachment(context);
    auto depthAttachment = buildDepthAttachment();

    vk::RenderingInfo renderingInfo = {
        .renderArea = {.offset = {0, 0},
                       .extent = context.swapchainContext.extent2D()},
        .layerCount = 1,
        .colorAttachmentCount = colorAttachment ? 1u : 0u,
        .pColorAttachments = colorAttachment ? &*colorAttachment : nullptr,
        .pDepthAttachment = depthAttachment ? &*depthAttachment : nullptr};

    context.commandBuffer.beginRendering(renderingInfo);
    context.commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                       *graphicsPipeline);
    context.commandBuffer.setViewport(
        0, vk::Viewport(
               0.0f, 0.0f,
               static_cast<float>(context.swapchainContext.extent2D().width),
               static_cast<float>(context.swapchainContext.extent2D().height),
               0.0f, 1.0f));
    context.commandBuffer.setScissor(
        0, vk::Rect2D(vk::Offset2D(0, 0), context.swapchainContext.extent2D()));

    bindPassResources(context);
    recordDrawCommands(context, renderItems);

    context.commandBuffer.endRendering();
    transitionToFinalLayouts(context);
  }

  vk::raii::DescriptorSetLayout *descriptorSetLayout() override {
    return hasDescriptorSetLayout() ? &descriptorSetLayoutHandle : nullptr;
  }

  bool hasOffscreenColorOutput() const {
    return usesOffscreenColorAttachment();
  }

  bool hasSampledColorOutput() const {
    return usesOffscreenColorAttachment() && attachments.sampleColorAttachment;
  }

  const vk::raii::ImageView &offscreenColorImageView() const {
    if (!hasOffscreenColorOutput()) {
      throw std::runtime_error(
          "Render pass does not expose an offscreen color attachment");
    }
    return colorImageView;
  }

  vk::Format offscreenColorImageFormat() const {
    if (!hasOffscreenColorOutput()) {
      throw std::runtime_error(
          "Render pass does not expose an offscreen color attachment");
    }
    return colorImageFormat;
  }

  vk::ImageLayout offscreenColorImageLayout() const {
    if (!hasOffscreenColorOutput()) {
      throw std::runtime_error(
          "Render pass does not expose an offscreen color attachment");
    }
    return colorImageLayout;
  }

  SampledImageResource
  sampledColorOutput(const vk::raii::Sampler &sampler) const {
    if (!hasSampledColorOutput()) {
      throw std::runtime_error(
          "Render pass does not expose a sampled offscreen color attachment");
    }
    return SampledImageResource{
        .imageView = colorImageView,
        .sampler = sampler,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
  }

protected:
  virtual std::vector<DescriptorBindingSpec> descriptorBindings() const {
    return {};
  }

  virtual std::vector<vk::PushConstantRange> pushConstantRanges() const {
    return {};
  }

  virtual VertexInputLayoutSpec vertexInputLayout() const {
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    return VertexInputLayoutSpec{.bindings = {bindingDescription},
                                 .attributes = {attributeDescriptions.begin(),
                                                attributeDescriptions.end()}};
  }

  virtual void initializePassResources(DeviceContext &deviceContext,
                                       SwapchainContext &swapchainContext) {}

  virtual void recreatePassResources(DeviceContext &deviceContext,
                                     SwapchainContext &swapchainContext) {}

  virtual void bindPassResources(const RenderPassContext &context) {}

  virtual void recordDrawCommands(const RenderPassContext &context,
                                  const std::vector<RenderItem> &renderItems) = 0;

  const PipelineSpec &pipelineSpec() const { return spec; }
  const RasterPassAttachmentConfig &attachmentConfig() const {
    return attachments;
  }

  vk::raii::PipelineLayout &pipelineLayoutHandle() { return pipelineLayout; }
  const vk::raii::PipelineLayout &pipelineLayoutHandle() const {
    return pipelineLayout;
  }

  vk::raii::DescriptorSetLayout &passDescriptorSetLayout() {
    return descriptorSetLayoutHandle;
  }

  const vk::raii::DescriptorSetLayout &passDescriptorSetLayout() const {
    return descriptorSetLayoutHandle;
  }

private:
  PipelineSpec spec;
  RasterPassAttachmentConfig attachments;
  ShaderProgram shaderProgram;

  vk::raii::DescriptorSetLayout descriptorSetLayoutHandle = nullptr;
  vk::raii::PipelineLayout pipelineLayout = nullptr;
  vk::raii::Pipeline graphicsPipeline = nullptr;

  vk::raii::Image colorImage = nullptr;
  vk::raii::DeviceMemory colorImageMemory = nullptr;
  vk::raii::ImageView colorImageView = nullptr;
  vk::Format colorImageFormat = vk::Format::eUndefined;

  vk::raii::Image depthImage = nullptr;
  vk::raii::DeviceMemory depthImageMemory = nullptr;
  vk::raii::ImageView depthImageView = nullptr;
  vk::ImageLayout colorImageLayout = vk::ImageLayout::eUndefined;
  vk::ImageLayout depthImageLayout = vk::ImageLayout::eUndefined;
  std::vector<vk::ImageLayout> swapchainImageLayouts;

  void createDescriptorSetLayout(DeviceContext &deviceContext) {
    auto bindingSpecs = descriptorBindings();
    if (bindingSpecs.empty()) {
      descriptorSetLayoutHandle = nullptr;
      return;
    }

    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    bindings.reserve(bindingSpecs.size());
    for (const auto &bindingSpec : bindingSpecs) {
      bindings.emplace_back(bindingSpec.binding, bindingSpec.descriptorType,
                            bindingSpec.descriptorCount, bindingSpec.stageFlags,
                            nullptr);
    }

    vk::DescriptorSetLayoutCreateInfo layoutInfo{
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data()};
    descriptorSetLayoutHandle =
        vk::raii::DescriptorSetLayout(deviceContext.deviceHandle(), layoutInfo);
  }

  void createPipelineLayout(DeviceContext &deviceContext) {
    auto pushConstants = pushConstantRanges();

    const vk::DescriptorSetLayout *setLayout =
        hasDescriptorSetLayout() ? &*descriptorSetLayoutHandle : nullptr;
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
        .setLayoutCount = setLayout != nullptr ? 1u : 0u,
        .pSetLayouts = setLayout,
        .pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size()),
        .pPushConstantRanges = pushConstants.data()};

    pipelineLayout = vk::raii::PipelineLayout(deviceContext.deviceHandle(),
                                              pipelineLayoutInfo);
  }

  void createGraphicsPipeline(DeviceContext &deviceContext,
                              SwapchainContext &swapchainContext) {
    shaderProgram.load(deviceContext, spec.shaderPath, spec.vertexEntry,
                       spec.fragmentEntry);
    auto shaderStages = shaderProgram.stages();

    auto vertexLayout = vertexInputLayout();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount =
            static_cast<uint32_t>(vertexLayout.bindings.size()),
        .pVertexBindingDescriptions = vertexLayout.bindings.data(),
        .vertexAttributeDescriptionCount =
            static_cast<uint32_t>(vertexLayout.attributes.size()),
        .pVertexAttributeDescriptions = vertexLayout.attributes.data()};
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = spec.topology, .primitiveRestartEnable = vk::False};
    vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1,
                                                      .scissorCount = 1};
    vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = spec.polygonMode,
        .cullMode = spec.cullMode,
        .frontFace = spec.frontFace,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f};
    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = rasterizationSampleCount(deviceContext),
        .sampleShadingEnable = vk::False};
    vk::PipelineDepthStencilStateCreateInfo depthStencil{
        .depthTestEnable =
            spec.enableDepthTest && attachments.useDepthAttachment,
        .depthWriteEnable =
            spec.enableDepthWrite && attachments.useDepthAttachment,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False};
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = spec.enableBlending,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eZero,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};
    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = attachments.useColorAttachment ? 1u : 0u,
        .pAttachments =
            attachments.useColorAttachment ? &colorBlendAttachment : nullptr};
    std::vector dynamicStates = {vk::DynamicState::eViewport,
                                 vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()};

    vk::Format colorFormat = colorAttachmentFormat(swapchainContext);
    vk::Format depthFormat = attachments.useDepthAttachment
                                 ? deviceContext.findDepthFormat()
                                 : vk::Format::eUndefined;
    uint32_t colorAttachmentCount = attachments.useColorAttachment ? 1u : 0u;
    const vk::Format *colorAttachmentFormats =
        attachments.useColorAttachment ? &colorFormat : nullptr;

    vk::StructureChain<vk::GraphicsPipelineCreateInfo,
                       vk::PipelineRenderingCreateInfo>
        pipelineCreateInfoChain = {
            {.stageCount = static_cast<uint32_t>(shaderStages.size()),
             .pStages = shaderStages.data(),
             .pVertexInputState = &vertexInputInfo,
             .pInputAssemblyState = &inputAssembly,
             .pViewportState = &viewportState,
             .pRasterizationState = &rasterizer,
             .pMultisampleState = &multisampling,
             .pDepthStencilState = &depthStencil,
             .pColorBlendState = &colorBlending,
             .pDynamicState = &dynamicState,
             .layout = pipelineLayout,
             .renderPass = nullptr},
            {.colorAttachmentCount = colorAttachmentCount,
             .pColorAttachmentFormats = colorAttachmentFormats,
             .depthAttachmentFormat = depthFormat}};

    graphicsPipeline = vk::raii::Pipeline(
        deviceContext.deviceHandle(), nullptr,
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
  }

  void createAttachmentResources(DeviceContext &deviceContext,
                                 SwapchainContext &swapchainContext) {
    if (usesOwnedColorAttachment()) {
      createColorResources(deviceContext, swapchainContext);
    } else {
      colorImage = nullptr;
      colorImageMemory = nullptr;
      colorImageView = nullptr;
      colorImageFormat = vk::Format::eUndefined;
    }

    if (attachments.useDepthAttachment) {
      createDepthResources(deviceContext, swapchainContext);
    } else {
      depthImage = nullptr;
      depthImageMemory = nullptr;
      depthImageView = nullptr;
    }

    colorImageLayout = vk::ImageLayout::eUndefined;
    depthImageLayout = vk::ImageLayout::eUndefined;
    swapchainImageLayouts.assign(swapchainContext.imageCount(),
                                 vk::ImageLayout::eUndefined);
  }

  void createColorResources(DeviceContext &deviceContext,
                            SwapchainContext &swapchainContext) {
    vk::Format colorFormat = colorAttachmentFormat(swapchainContext);
    vk::ImageUsageFlags imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    if (attachments.useMsaaColorAttachment) {
      imageUsage |= vk::ImageUsageFlagBits::eTransientAttachment;
    }
    if (attachments.sampleColorAttachment) {
      imageUsage |= vk::ImageUsageFlagBits::eSampled;
    }

    RenderUtils::createImage(deviceContext, swapchainContext.extent2D().width,
                             swapchainContext.extent2D().height, 1,
                             rasterizationSampleCount(deviceContext),
                             colorFormat, vk::ImageTiling::eOptimal, imageUsage,
                             vk::MemoryPropertyFlagBits::eDeviceLocal,
                             colorImage, colorImageMemory);
    colorImageView = createImageView(deviceContext, colorImage, colorFormat,
                                     vk::ImageAspectFlagBits::eColor, 1);
    colorImageFormat = colorFormat;
  }

  void createDepthResources(DeviceContext &deviceContext,
                            SwapchainContext &swapchainContext) {
    vk::Format depthFormat = deviceContext.findDepthFormat();

    RenderUtils::createImage(deviceContext, swapchainContext.extent2D().width,
                             swapchainContext.extent2D().height, 1,
                             rasterizationSampleCount(deviceContext),
                             depthFormat, vk::ImageTiling::eOptimal,
                             vk::ImageUsageFlagBits::eDepthStencilAttachment,
                             vk::MemoryPropertyFlagBits::eDeviceLocal,
                             depthImage, depthImageMemory);
    depthImageView = createImageView(deviceContext, depthImage, depthFormat,
                                     vk::ImageAspectFlagBits::eDepth, 1);
  }

  vk::raii::ImageView createImageView(DeviceContext &deviceContext,
                                      const vk::raii::Image &image,
                                      vk::Format format,
                                      vk::ImageAspectFlags aspectFlags,
                                      uint32_t mipLevels) {
    vk::ImageViewCreateInfo viewInfo{
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {aspectFlags, 0, mipLevels, 0, 1}};
    return vk::raii::ImageView(deviceContext.deviceHandle(), viewInfo);
  }

  void transitionToRenderingLayouts(const RenderPassContext &context) {
    if (writesToSwapchain()) {
      auto &swapchainImageLayout = swapchainImageLayouts.at(context.imageIndex);
      transitionImageLayout(
          context.commandBuffer,
          context.swapchainContext.swapchainImages()[context.imageIndex],
          swapchainImageLayout, vk::ImageLayout::eColorAttachmentOptimal, {},
          vk::AccessFlagBits2::eColorAttachmentWrite,
          layoutStageMask(swapchainImageLayout),
          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
          vk::ImageAspectFlagBits::eColor);
      swapchainImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    }

    if (usesOwnedColorAttachment()) {
      transitionImageLayout(context.commandBuffer, *colorImage,
                            colorImageLayout,
                            vk::ImageLayout::eColorAttachmentOptimal,
                            layoutAccessMask(colorImageLayout),
                            vk::AccessFlagBits2::eColorAttachmentWrite,
                            layoutStageMask(colorImageLayout),
                            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            vk::ImageAspectFlagBits::eColor);
      colorImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    }

    if (attachments.useDepthAttachment) {
      transitionImageLayout(context.commandBuffer, *depthImage,
                            depthImageLayout,
                            vk::ImageLayout::eDepthAttachmentOptimal,
                            layoutAccessMask(depthImageLayout),
                            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                            layoutStageMask(depthImageLayout),
                            vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                vk::PipelineStageFlagBits2::eLateFragmentTests,
                            vk::ImageAspectFlagBits::eDepth);
      depthImageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    }
  }

  void transitionToFinalLayouts(const RenderPassContext &context) {
    if (hasSampledColorOutput()) {
      transitionImageLayout(
          context.commandBuffer, *colorImage, colorImageLayout,
          vk::ImageLayout::eShaderReadOnlyOptimal,
          layoutAccessMask(colorImageLayout), vk::AccessFlagBits2::eShaderRead,
          layoutStageMask(colorImageLayout),
          vk::PipelineStageFlagBits2::eFragmentShader,
          vk::ImageAspectFlagBits::eColor);
      colorImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    if (writesToSwapchain()) {
      auto &swapchainImageLayout = swapchainImageLayouts.at(context.imageIndex);
      transitionImageLayout(
          context.commandBuffer,
          context.swapchainContext.swapchainImages()[context.imageIndex],
          swapchainImageLayout, vk::ImageLayout::ePresentSrcKHR,
          layoutAccessMask(swapchainImageLayout), {},
          layoutStageMask(swapchainImageLayout),
          vk::PipelineStageFlagBits2::eBottomOfPipe,
          vk::ImageAspectFlagBits::eColor);
      swapchainImageLayout = vk::ImageLayout::ePresentSrcKHR;
    }
  }

  std::optional<vk::RenderingAttachmentInfo>
  buildColorAttachment(const RenderPassContext &context) const {
    if (!attachments.useColorAttachment) {
      return std::nullopt;
    }

    vk::ClearValue clearColor = {.color = {.float32 = attachments.clearColor}};

    if (attachments.useMsaaColorAttachment) {
      return vk::RenderingAttachmentInfo{
          .imageView = colorImageView,
          .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
          .resolveMode = attachments.resolveToSwapchain
                             ? vk::ResolveModeFlagBits::eAverage
                             : vk::ResolveModeFlagBits::eNone,
          .resolveImageView =
              attachments.resolveToSwapchain
                  ? context.swapchainContext
                        .swapchainImageViews()[context.imageIndex]
                  : vk::ImageView(),
          .resolveImageLayout = attachments.resolveToSwapchain
                                    ? vk::ImageLayout::eColorAttachmentOptimal
                                    : vk::ImageLayout::eUndefined,
          .loadOp = attachments.colorLoadOp,
          .storeOp = attachments.colorStoreOp,
          .clearValue = clearColor};
    }

    if (usesOffscreenColorAttachment()) {
      return vk::RenderingAttachmentInfo{
          .imageView = colorImageView,
          .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
          .loadOp = attachments.colorLoadOp,
          .storeOp = attachments.colorStoreOp,
          .clearValue = clearColor};
    }

    return vk::RenderingAttachmentInfo{
        .imageView =
            context.swapchainContext.swapchainImageViews()[context.imageIndex],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = attachments.colorLoadOp,
        .storeOp = attachments.colorStoreOp,
        .clearValue = clearColor};
  }

  std::optional<vk::RenderingAttachmentInfo> buildDepthAttachment() const {
    if (!attachments.useDepthAttachment) {
      return std::nullopt;
    }

    vk::ClearValue clearDepth = {
        .depthStencil = vk::ClearDepthStencilValue{attachments.clearDepth,
                                                   attachments.clearStencil}};
    return vk::RenderingAttachmentInfo{
        .imageView = depthImageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = attachments.depthLoadOp,
        .storeOp = attachments.depthStoreOp,
        .clearValue = clearDepth};
  }

  vk::SampleCountFlagBits
  rasterizationSampleCount(DeviceContext &deviceContext) const {
    return attachments.useMsaaColorAttachment ? deviceContext.msaaSampleCount()
                                              : vk::SampleCountFlagBits::e1;
  }

  bool writesToSwapchain() const {
    return usesSwapchainColorAttachment() || attachments.resolveToSwapchain;
  }

  bool usesSwapchainColorAttachment() const {
    return attachments.useColorAttachment &&
           attachments.useSwapchainColorAttachment &&
           !attachments.useMsaaColorAttachment;
  }

  bool usesOffscreenColorAttachment() const {
    return attachments.useColorAttachment &&
           !attachments.useSwapchainColorAttachment &&
           !attachments.useMsaaColorAttachment;
  }

  bool usesOwnedColorAttachment() const {
    return attachments.useColorAttachment &&
           (attachments.useMsaaColorAttachment ||
            usesOffscreenColorAttachment());
  }

  bool hasDescriptorSetLayout() const {
    return static_cast<vk::DescriptorSetLayout>(descriptorSetLayoutHandle) !=
           VK_NULL_HANDLE;
  }

  vk::Format
  colorAttachmentFormat(const SwapchainContext &swapchainContext) const {
    if (usesOffscreenColorAttachment() &&
        attachments.offscreenColorFormat != vk::Format::eUndefined) {
      return attachments.offscreenColorFormat;
    }
    return swapchainContext.surfaceFormatInfo().format;
  }

  vk::AccessFlags2 layoutAccessMask(vk::ImageLayout layout) const {
    switch (layout) {
    case vk::ImageLayout::eColorAttachmentOptimal:
      return vk::AccessFlagBits2::eColorAttachmentWrite;
    case vk::ImageLayout::eDepthAttachmentOptimal:
      return vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
      return vk::AccessFlagBits2::eShaderRead;
    default:
      return {};
    }
  }

  vk::PipelineStageFlags2 layoutStageMask(vk::ImageLayout layout) const {
    switch (layout) {
    case vk::ImageLayout::eColorAttachmentOptimal:
      return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    case vk::ImageLayout::eDepthAttachmentOptimal:
      return vk::PipelineStageFlagBits2::eEarlyFragmentTests |
             vk::PipelineStageFlagBits2::eLateFragmentTests;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
      return vk::PipelineStageFlagBits2::eFragmentShader;
    case vk::ImageLayout::ePresentSrcKHR:
      return vk::PipelineStageFlagBits2::eBottomOfPipe;
    default:
      return vk::PipelineStageFlagBits2::eTopOfPipe;
    }
  }

  void validateAttachmentConfig() const {
    if (attachments.sampleColorAttachment && !usesOffscreenColorAttachment()) {
      throw std::runtime_error("sampleColorAttachment requires a "
                               "single-sampled offscreen color attachment");
    }
    if (attachments.useMsaaColorAttachment && !attachments.useColorAttachment) {
      throw std::runtime_error(
          "useMsaaColorAttachment requires useColorAttachment to be enabled");
    }
    if (attachments.resolveToSwapchain && !attachments.useColorAttachment) {
      throw std::runtime_error(
          "resolveToSwapchain requires useColorAttachment to be enabled");
    }
  }

  void transitionImageLayout(vk::raii::CommandBuffer &commandBuffer,
                             vk::Image image, vk::ImageLayout oldLayout,
                             vk::ImageLayout newLayout,
                             vk::AccessFlags2 srcAccessMask,
                             vk::AccessFlags2 dstAccessMask,
                             vk::PipelineStageFlags2 srcStageMask,
                             vk::PipelineStageFlags2 dstStageMask,
                             vk::ImageAspectFlags imageAspectFlags) {
    if (oldLayout == newLayout) {
      return;
    }

    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {.aspectMask = imageAspectFlags,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};
    vk::DependencyInfo dependencyInfo = {.dependencyFlags = {},
                                         .imageMemoryBarrierCount = 1,
                                         .pImageMemoryBarriers = &barrier};
    commandBuffer.pipelineBarrier2(dependencyInfo);
  }
};
