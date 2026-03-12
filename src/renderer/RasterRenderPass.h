#pragma once

#include "../renderable/RenderUtils.h"
#include "PipelineSpec.h"
#include "RenderPass.h"
#include "SampledImageResource.h"
#include "ShaderProgram.h"
#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

enum class RasterAttachmentFormat {
  Auto,
  RGBA8,
  RGBA16F,
  R32F,
};

struct RasterColorAttachmentConfig {
  std::string name;
  RasterAttachmentFormat format = RasterAttachmentFormat::Auto;
  bool writeToSwapchain = false;
  bool sampled = false;
  std::array<float, 4> clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
  vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear;
  vk::AttachmentStoreOp storeOp = vk::AttachmentStoreOp::eStore;
};

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
  vk::AttachmentStoreOp depthStoreOp = vk::AttachmentStoreOp::eStore;

  // If non-empty, these replace the legacy single-color attachment fields
  // above and enable explicit multi-output pass declarations.
  std::vector<RasterColorAttachmentConfig> colorAttachments;
  bool sampleDepthAttachment = false;
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

    auto colorAttachments = buildColorAttachments(context);
    auto depthAttachment = buildDepthAttachment();

    vk::RenderingInfo renderingInfo = {
        .renderArea = {.offset = {0, 0},
                       .extent = context.swapchainContext.extent2D()},
        .layerCount = 1,
        .colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
        .pColorAttachments =
            colorAttachments.empty() ? nullptr : colorAttachments.data(),
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

  uint32_t colorOutputCount() const {
    if (usesExplicitColorAttachments()) {
      return static_cast<uint32_t>(attachments.colorAttachments.size());
    }
    return attachments.useColorAttachment ? 1u : 0u;
  }

  bool hasOffscreenColorOutput() const {
    return hasOffscreenColorOutput(0);
  }

  bool hasOffscreenColorOutput(uint32_t index) const {
    validateColorOutputIndex(index);
    return colorAttachmentIsOffscreen(index);
  }

  bool hasSampledColorOutput() const { return hasSampledColorOutput(0); }

  bool hasSampledColorOutput(uint32_t index) const {
    validateColorOutputIndex(index);
    return colorAttachmentIsOffscreen(index) && colorAttachmentConfig(index).sampled;
  }

  std::optional<uint32_t> findColorOutput(std::string_view name) const {
    if (name.empty()) {
      return std::nullopt;
    }

    for (uint32_t i = 0; i < colorOutputCount(); ++i) {
      if (colorAttachmentConfig(i).name == name) {
        return i;
      }
    }
    return std::nullopt;
  }

  const vk::raii::ImageView &offscreenColorImageView() const {
    return offscreenColorImageView(0);
  }

  const vk::raii::ImageView &offscreenColorImageView(uint32_t index) const {
    if (!hasOffscreenColorOutput(index)) {
      throw std::runtime_error(
          "Render pass does not expose the requested offscreen color attachment");
    }
    return colorAttachmentResources.at(index).imageView;
  }

  vk::Format offscreenColorImageFormat() const {
    return offscreenColorImageFormat(0);
  }

  vk::Format offscreenColorImageFormat(uint32_t index) const {
    if (!hasOffscreenColorOutput(index)) {
      throw std::runtime_error(
          "Render pass does not expose the requested offscreen color attachment");
    }
    return colorAttachmentResources.at(index).format;
  }

  vk::ImageLayout offscreenColorImageLayout() const {
    return offscreenColorImageLayout(0);
  }

  vk::ImageLayout offscreenColorImageLayout(uint32_t index) const {
    if (!hasOffscreenColorOutput(index)) {
      throw std::runtime_error(
          "Render pass does not expose the requested offscreen color attachment");
    }
    return colorAttachmentResources.at(index).layout;
  }

  SampledImageResource sampledColorOutput(const vk::raii::Sampler &sampler) const {
    return sampledColorOutput(0, sampler);
  }

  SampledImageResource sampledColorOutput(uint32_t index,
                                          const vk::raii::Sampler &sampler) const {
    if (!hasSampledColorOutput(index)) {
      throw std::runtime_error(
          "Render pass does not expose the requested sampled color attachment");
    }
    return SampledImageResource{
        .imageView = colorAttachmentResources.at(index).imageView,
        .sampler = sampler,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
  }

  bool hasDepthOutput() const { return attachments.useDepthAttachment; }

  bool hasSampledDepthOutput() const {
    return attachments.useDepthAttachment && attachments.sampleDepthAttachment;
  }

  const vk::raii::ImageView &depthImageView() const {
    if (!hasDepthOutput()) {
      throw std::runtime_error("Render pass does not expose a depth attachment");
    }
    return depthImageViewHandle;
  }

  SampledImageResource sampledDepthOutput(const vk::raii::Sampler &sampler) const {
    if (!hasSampledDepthOutput()) {
      throw std::runtime_error(
          "Render pass does not expose a sampled depth attachment");
    }
    return SampledImageResource{
        .imageView = depthImageViewHandle,
        .sampler = sampler,
        .imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
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
  struct ColorAttachmentResource {
    vk::raii::Image image = nullptr;
    vk::raii::DeviceMemory memory = nullptr;
    vk::raii::ImageView imageView = nullptr;
    vk::Format format = vk::Format::eUndefined;
    vk::ImageLayout layout = vk::ImageLayout::eUndefined;
  };

  PipelineSpec spec;
  RasterPassAttachmentConfig attachments;
  ShaderProgram shaderProgram;

  vk::raii::DescriptorSetLayout descriptorSetLayoutHandle = nullptr;
  vk::raii::PipelineLayout pipelineLayout = nullptr;
  vk::raii::Pipeline graphicsPipeline = nullptr;

  std::vector<ColorAttachmentResource> colorAttachmentResources;

  vk::raii::Image depthImage = nullptr;
  vk::raii::DeviceMemory depthImageMemory = nullptr;
  vk::raii::ImageView depthImageViewHandle = nullptr;
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
    std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments(
        colorOutputCount(), colorBlendAttachment);
    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size()),
        .pAttachments =
            colorBlendAttachments.empty() ? nullptr : colorBlendAttachments.data()};
    std::vector dynamicStates = {vk::DynamicState::eViewport,
                                 vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()};

    std::vector<vk::Format> colorFormats;
    colorFormats.reserve(colorOutputCount());
    for (uint32_t i = 0; i < colorOutputCount(); ++i) {
      colorFormats.push_back(colorAttachmentFormat(swapchainContext, i));
    }

    vk::Format depthFormat = attachments.useDepthAttachment
                                 ? deviceContext.findDepthFormat()
                                 : vk::Format::eUndefined;

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
            {.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size()),
             .pColorAttachmentFormats =
                 colorFormats.empty() ? nullptr : colorFormats.data(),
             .depthAttachmentFormat = depthFormat}};

    graphicsPipeline = vk::raii::Pipeline(
        deviceContext.deviceHandle(), nullptr,
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
  }

  void createAttachmentResources(DeviceContext &deviceContext,
                                 SwapchainContext &swapchainContext) {
    colorAttachmentResources.clear();
    colorAttachmentResources.reserve(colorOutputCount());
    for (uint32_t i = 0; i < colorOutputCount(); ++i) {
      ColorAttachmentResource resource;
      if (colorAttachmentIsOffscreen(i)) {
        createColorResource(deviceContext, swapchainContext, i, resource);
      }
      colorAttachmentResources.push_back(std::move(resource));
    }

    if (attachments.useDepthAttachment) {
      createDepthResources(deviceContext, swapchainContext);
    } else {
      depthImage = nullptr;
      depthImageMemory = nullptr;
      depthImageViewHandle = nullptr;
      depthImageLayout = vk::ImageLayout::eUndefined;
    }

    swapchainImageLayouts.assign(swapchainContext.imageCount(),
                                 vk::ImageLayout::eUndefined);
  }

  void createColorResource(DeviceContext &deviceContext,
                           SwapchainContext &swapchainContext, uint32_t index,
                           ColorAttachmentResource &resource) {
    vk::Format colorFormat = colorAttachmentFormat(swapchainContext, index);
    vk::ImageUsageFlags imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    if (colorAttachmentConfig(index).sampled) {
      imageUsage |= vk::ImageUsageFlagBits::eSampled;
    }

    RenderUtils::createImage(deviceContext, swapchainContext.extent2D().width,
                             swapchainContext.extent2D().height, 1,
                             rasterizationSampleCount(deviceContext),
                             colorFormat, vk::ImageTiling::eOptimal, imageUsage,
                             vk::MemoryPropertyFlagBits::eDeviceLocal,
                             resource.image, resource.memory);
    resource.imageView = createImageView(deviceContext, resource.image,
                                         colorFormat,
                                         vk::ImageAspectFlagBits::eColor, 1);
    resource.format = colorFormat;
    resource.layout = vk::ImageLayout::eUndefined;
  }

  void createDepthResources(DeviceContext &deviceContext,
                            SwapchainContext &swapchainContext) {
    vk::Format depthFormat = deviceContext.findDepthFormat();
    vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
    if (attachments.sampleDepthAttachment) {
      usage |= vk::ImageUsageFlagBits::eSampled;
    }

    RenderUtils::createImage(deviceContext, swapchainContext.extent2D().width,
                             swapchainContext.extent2D().height, 1,
                             rasterizationSampleCount(deviceContext),
                             depthFormat, vk::ImageTiling::eOptimal, usage,
                             vk::MemoryPropertyFlagBits::eDeviceLocal,
                             depthImage, depthImageMemory);
    depthImageViewHandle = createImageView(deviceContext, depthImage, depthFormat,
                                           vk::ImageAspectFlagBits::eDepth, 1);
    depthImageLayout = vk::ImageLayout::eUndefined;
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

    for (uint32_t i = 0; i < colorOutputCount(); ++i) {
      if (!colorAttachmentIsOffscreen(i)) {
        continue;
      }

      auto &resource = colorAttachmentResources.at(i);
      transitionImageLayout(
          context.commandBuffer, *resource.image, resource.layout,
          vk::ImageLayout::eColorAttachmentOptimal, layoutAccessMask(resource.layout),
          vk::AccessFlagBits2::eColorAttachmentWrite, layoutStageMask(resource.layout),
          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
          vk::ImageAspectFlagBits::eColor);
      resource.layout = vk::ImageLayout::eColorAttachmentOptimal;
    }

    if (attachments.useDepthAttachment) {
      transitionImageLayout(context.commandBuffer, *depthImage, depthImageLayout,
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
    for (uint32_t i = 0; i < colorOutputCount(); ++i) {
      if (!hasSampledColorOutput(i)) {
        continue;
      }

      auto &resource = colorAttachmentResources.at(i);
      transitionImageLayout(
          context.commandBuffer, *resource.image, resource.layout,
          vk::ImageLayout::eShaderReadOnlyOptimal, layoutAccessMask(resource.layout),
          vk::AccessFlagBits2::eShaderRead, layoutStageMask(resource.layout),
          vk::PipelineStageFlagBits2::eFragmentShader,
          vk::ImageAspectFlagBits::eColor);
      resource.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    if (attachments.sampleDepthAttachment && attachments.useDepthAttachment) {
      transitionImageLayout(
          context.commandBuffer, *depthImage, depthImageLayout,
          vk::ImageLayout::eDepthStencilReadOnlyOptimal,
          layoutAccessMask(depthImageLayout), vk::AccessFlagBits2::eShaderRead,
          layoutStageMask(depthImageLayout),
          vk::PipelineStageFlagBits2::eFragmentShader,
          vk::ImageAspectFlagBits::eDepth);
      depthImageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
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

  std::vector<vk::RenderingAttachmentInfo>
  buildColorAttachments(const RenderPassContext &context) const {
    std::vector<vk::RenderingAttachmentInfo> result;
    result.reserve(colorOutputCount());
    for (uint32_t i = 0; i < colorOutputCount(); ++i) {
      result.push_back(buildColorAttachment(context, i));
    }
    return result;
  }

  vk::RenderingAttachmentInfo
  buildColorAttachment(const RenderPassContext &context, uint32_t index) const {
    const auto attachment = colorAttachmentConfig(index);
    vk::ClearValue clearColor = {.color = {.float32 = attachment.clearColor}};

    if (!usesExplicitColorAttachments() && attachments.useMsaaColorAttachment) {
      return vk::RenderingAttachmentInfo{
          .imageView = colorAttachmentResources.at(index).imageView,
          .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
          .resolveMode = attachments.resolveToSwapchain
                             ? vk::ResolveModeFlagBits::eAverage
                             : vk::ResolveModeFlagBits::eNone,
          .resolveImageView =
              attachments.resolveToSwapchain
                  ? context.swapchainContext.swapchainImageViews()[context.imageIndex]
                  : vk::ImageView(),
          .resolveImageLayout = attachments.resolveToSwapchain
                                    ? vk::ImageLayout::eColorAttachmentOptimal
                                    : vk::ImageLayout::eUndefined,
          .loadOp = attachment.loadOp,
          .storeOp = attachment.storeOp,
          .clearValue = clearColor};
    }

    if (attachment.writeToSwapchain) {
      return vk::RenderingAttachmentInfo{
          .imageView =
              context.swapchainContext.swapchainImageViews()[context.imageIndex],
          .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
          .loadOp = attachment.loadOp,
          .storeOp = attachment.storeOp,
          .clearValue = clearColor};
    }

    return vk::RenderingAttachmentInfo{
        .imageView = colorAttachmentResources.at(index).imageView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = attachment.loadOp,
        .storeOp = attachment.storeOp,
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
        .imageView = depthImageViewHandle,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = attachments.depthLoadOp,
        .storeOp = attachments.depthStoreOp,
        .clearValue = clearDepth};
  }

  vk::SampleCountFlagBits
  rasterizationSampleCount(DeviceContext &deviceContext) const {
    if (usesExplicitColorAttachments()) {
      return vk::SampleCountFlagBits::e1;
    }
    return attachments.useMsaaColorAttachment ? deviceContext.msaaSampleCount()
                                              : vk::SampleCountFlagBits::e1;
  }

  bool writesToSwapchain() const {
    if (usesExplicitColorAttachments()) {
      return std::any_of(
          attachments.colorAttachments.begin(), attachments.colorAttachments.end(),
          [](const RasterColorAttachmentConfig &attachment) {
            return attachment.writeToSwapchain;
          });
    }
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

  bool hasDescriptorSetLayout() const {
    return static_cast<vk::DescriptorSetLayout>(descriptorSetLayoutHandle) !=
           VK_NULL_HANDLE;
  }

  bool usesExplicitColorAttachments() const {
    return !attachments.colorAttachments.empty();
  }

  RasterColorAttachmentConfig colorAttachmentConfig(uint32_t index) const {
    validateColorOutputIndex(index);
    if (usesExplicitColorAttachments()) {
      return attachments.colorAttachments.at(index);
    }

    return RasterColorAttachmentConfig{
        .name = "color0",
        .format = attachments.offscreenColorFormat != vk::Format::eUndefined
                      ? fromVkFormat(attachments.offscreenColorFormat)
                      : RasterAttachmentFormat::Auto,
        .writeToSwapchain =
            attachments.useSwapchainColorAttachment && !attachments.useMsaaColorAttachment,
        .sampled = attachments.sampleColorAttachment,
        .clearColor = attachments.clearColor,
        .loadOp = attachments.colorLoadOp,
        .storeOp = attachments.colorStoreOp,
    };
  }

  bool colorAttachmentIsOffscreen(uint32_t index) const {
    return !colorAttachmentConfig(index).writeToSwapchain;
  }

  vk::Format colorAttachmentFormat(const SwapchainContext &swapchainContext,
                                   uint32_t index) const {
    auto attachment = colorAttachmentConfig(index);
    if (attachment.writeToSwapchain) {
      return swapchainContext.surfaceFormatInfo().format;
    }
    if (attachment.format != RasterAttachmentFormat::Auto) {
      return toVkFormat(attachment.format);
    }
    return swapchainContext.surfaceFormatInfo().format;
  }

  void validateColorOutputIndex(uint32_t index) const {
    if (index >= colorOutputCount()) {
      throw std::runtime_error("Color attachment index out of range");
    }
  }

  vk::AccessFlags2 layoutAccessMask(vk::ImageLayout layout) const {
    switch (layout) {
    case vk::ImageLayout::eColorAttachmentOptimal:
      return vk::AccessFlagBits2::eColorAttachmentWrite;
    case vk::ImageLayout::eDepthAttachmentOptimal:
      return vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
    case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
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
    case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
      return vk::PipelineStageFlagBits2::eFragmentShader;
    case vk::ImageLayout::ePresentSrcKHR:
      return vk::PipelineStageFlagBits2::eBottomOfPipe;
    default:
      return vk::PipelineStageFlagBits2::eTopOfPipe;
    }
  }

  void validateAttachmentConfig() const {
    if (usesExplicitColorAttachments()) {
      const auto swapchainOutputCount =
          std::count_if(attachments.colorAttachments.begin(),
                        attachments.colorAttachments.end(),
                        [](const RasterColorAttachmentConfig &attachment) {
                          return attachment.writeToSwapchain;
                        });
      if (swapchainOutputCount > 1) {
        throw std::runtime_error(
            "Only one explicit color attachment may target the swapchain");
      }
      return;
    }

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

  static vk::Format toVkFormat(RasterAttachmentFormat format) {
    switch (format) {
    case RasterAttachmentFormat::RGBA8:
      return vk::Format::eR8G8B8A8Unorm;
    case RasterAttachmentFormat::RGBA16F:
      return vk::Format::eR16G16B16A16Sfloat;
    case RasterAttachmentFormat::R32F:
      return vk::Format::eR32Sfloat;
    case RasterAttachmentFormat::Auto:
    default:
      return vk::Format::eUndefined;
    }
  }

  static RasterAttachmentFormat fromVkFormat(vk::Format format) {
    switch (format) {
    case vk::Format::eR8G8B8A8Unorm:
      return RasterAttachmentFormat::RGBA8;
    case vk::Format::eR16G16B16A16Sfloat:
      return RasterAttachmentFormat::RGBA16F;
    case vk::Format::eR32Sfloat:
      return RasterAttachmentFormat::R32F;
    default:
      return RasterAttachmentFormat::Auto;
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
