#include "backend/AppWindow.h"
#include "backend/VulkanBackend.h"
#include <algorithm>
#include <array>
#include <assert.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
const std::string ASSET_PATH = "assets";
const std::string MODEL_PATH = ASSET_PATH + "/models/viking_room.obj";
const std::string TEXTURE_PATH = ASSET_PATH + "/textures/viking_room.png";
const std::string SHADER_PATH = ASSET_PATH + "/shaders/slang.spv";
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

struct Vertex {
  glm::vec3 pos;
  glm::vec3 color;
  glm::vec2 texCoord;

  static vk::VertexInputBindingDescription getBindingDescription() {
    return {0, sizeof(Vertex), vk::VertexInputRate::eVertex};
  }

  static std::array<vk::VertexInputAttributeDescription, 3>
  getAttributeDescriptions() {
    return {vk::VertexInputAttributeDescription(
                0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),
            vk::VertexInputAttributeDescription(
                1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)),
            vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat,
                                                offsetof(Vertex, texCoord))};
  }

  bool operator==(const Vertex &other) const {
    return pos == other.pos && color == other.color &&
           texCoord == other.texCoord;
  }
};

template <> struct std::hash<Vertex> {
  size_t operator()(Vertex const &vertex) const noexcept {
    return ((hash<glm::vec3>()(vertex.pos) ^
             (hash<glm::vec3>()(vertex.color) << 1)) >>
            1) ^
           (hash<glm::vec2>()(vertex.texCoord) << 1);
  }
};

struct UniformBufferObject {
  alignas(16) glm::mat4 model;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 proj;
};

class HelloTriangleApplication {
public:
  void run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

private:
  AppWindow appWindow;
  VulkanBackend backend;
  BackendConfig backendConfig{.appName = "App Window",
                              .maxFramesInFlight = MAX_FRAMES_IN_FLIGHT};

  vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
  vk::raii::PipelineLayout pipelineLayout = nullptr;
  vk::raii::Pipeline graphicsPipeline = nullptr;

  vk::raii::Image colorImage = nullptr;
  vk::raii::DeviceMemory colorImageMemory = nullptr;
  vk::raii::ImageView colorImageView = nullptr;

  vk::raii::Image depthImage = nullptr;
  vk::raii::DeviceMemory depthImageMemory = nullptr;
  vk::raii::ImageView depthImageView = nullptr;

  uint32_t mipLevels = 0;
  vk::raii::Image textureImage = nullptr;
  vk::raii::DeviceMemory textureImageMemory = nullptr;
  vk::raii::ImageView textureImageView = nullptr;
  vk::raii::Sampler textureSampler = nullptr;

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  vk::raii::Buffer vertexBuffer = nullptr;
  vk::raii::DeviceMemory vertexBufferMemory = nullptr;
  vk::raii::Buffer indexBuffer = nullptr;
  vk::raii::DeviceMemory indexBufferMemory = nullptr;

  std::vector<vk::raii::Buffer> uniformBuffers;
  std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
  std::vector<void *> uniformBuffersMapped;

  vk::raii::DescriptorPool descriptorPool = nullptr;
  std::vector<vk::raii::DescriptorSet> descriptorSets;

  DeviceContext &deviceContext() { return backend.device(); }
  SwapchainContext &swapchainContext() { return backend.swapchain(); }
  CommandContext &commandContext() { return backend.commands(); }
  FrameSync &frameSync() { return backend.sync(); }

  void initWindow() { appWindow.create(WIDTH, HEIGHT, "App Window"); }

  void initVulkan() {
    backend.initialize(appWindow, backendConfig);
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createColorResources();
    createDepthResources();
    createTextureImage();
    createTextureImageView();
    createTextureSampler();
    loadModel();
    createVertexBuffer();
    createIndexBuffer();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
  }

  void mainLoop() {
    while (!appWindow.shouldClose()) {
      appWindow.pollEvents();
      drawFrame();
    }

    backend.waitIdle();
  }

  void cleanup() const { appWindow.destroy(); }

  void recreateSwapChain() {
    backend.recreateSwapchain(appWindow);
    createColorResources();
    createDepthResources();
  }

  void createDescriptorSetLayout() {
    std::array bindings = {vk::DescriptorSetLayoutBinding(
                               0, vk::DescriptorType::eUniformBuffer, 1,
                               vk::ShaderStageFlagBits::eVertex, nullptr),
                           vk::DescriptorSetLayoutBinding(
                               1, vk::DescriptorType::eCombinedImageSampler, 1,
                               vk::ShaderStageFlagBits::eFragment, nullptr)};

    vk::DescriptorSetLayoutCreateInfo layoutInfo{
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data()};
    descriptorSetLayout = vk::raii::DescriptorSetLayout(
        deviceContext().deviceHandle(), layoutInfo);
  }

  void createGraphicsPipeline() {
    vk::raii::ShaderModule shaderModule =
        createShaderModule(readFile(SHADER_PATH));

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = shaderModule,
        .pName = "vertMain"};
    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = shaderModule,
        .pName = "fragMain"};
    vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo,
                                                        fragShaderStageInfo};

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount =
            static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions = attributeDescriptions.data()};
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = vk::False};
    vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1,
                                                      .scissorCount = 1};
    vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f};
    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = deviceContext().msaaSampleCount(),
        .sampleShadingEnable = vk::False};
    vk::PipelineDepthStencilStateCreateInfo depthStencil{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False};
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::False,
        .colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};
    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment};
    std::vector dynamicStates = {vk::DynamicState::eViewport,
                                 vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()};

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &*descriptorSetLayout,
        .pushConstantRangeCount = 0};

    pipelineLayout = vk::raii::PipelineLayout(deviceContext().deviceHandle(),
                                              pipelineLayoutInfo);

    vk::Format depthFormat = deviceContext().findDepthFormat();

    vk::StructureChain<vk::GraphicsPipelineCreateInfo,
                       vk::PipelineRenderingCreateInfo>
        pipelineCreateInfoChain = {
            {.stageCount = 2,
             .pStages = shaderStages,
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
            {.colorAttachmentCount = 1,
             .pColorAttachmentFormats =
                 &swapchainContext().surfaceFormatInfo().format,
             .depthAttachmentFormat = depthFormat}};

    graphicsPipeline = vk::raii::Pipeline(
        deviceContext().deviceHandle(), nullptr,
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
  }

  void createColorResources() {
    vk::Format colorFormat = swapchainContext().surfaceFormatInfo().format;

    createImage(swapchainContext().extent2D().width,
                swapchainContext().extent2D().height, 1,
                deviceContext().msaaSampleCount(), colorFormat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransientAttachment |
                    vk::ImageUsageFlagBits::eColorAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal, colorImage,
                colorImageMemory);
    colorImageView = createImageView(colorImage, colorFormat,
                                     vk::ImageAspectFlagBits::eColor, 1);
  }

  void createDepthResources() {
    vk::Format depthFormat = deviceContext().findDepthFormat();

    createImage(swapchainContext().extent2D().width,
                swapchainContext().extent2D().height, 1,
                deviceContext().msaaSampleCount(), depthFormat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eDepthStencilAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal, depthImage,
                depthImageMemory);
    depthImageView = createImageView(depthImage, depthFormat,
                                     vk::ImageAspectFlagBits::eDepth, 1);
  }

  static bool hasStencilComponent(vk::Format format) {
    return format == vk::Format::eD32SfloatS8Uint ||
           format == vk::Format::eD24UnormS8Uint;
  }

  void createTextureImage() {
    int texWidth, texHeight, texChannels;
    stbi_uc *pixels = stbi_load(TEXTURE_PATH.c_str(), &texWidth, &texHeight,
                                &texChannels, STBI_rgb_alpha);
    vk::DeviceSize imageSize = texWidth * texHeight * 4;
    mipLevels = static_cast<uint32_t>(
                    std::floor(std::log2(std::max(texWidth, texHeight)))) +
                1;

    if (!pixels) {
      throw std::runtime_error("failed to load texture image!");
    }

    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingBufferMemory({});
    createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible |
                     vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void *data = stagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, pixels, imageSize);
    stagingBufferMemory.unmapMemory();

    stbi_image_free(pixels);

    createImage(texWidth, texHeight, mipLevels, vk::SampleCountFlagBits::e1,
                vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferSrc |
                    vk::ImageUsageFlagBits::eTransferDst |
                    vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal, textureImage,
                textureImageMemory);

    transitionImageLayout(textureImage, vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal, mipLevels);
    copyBufferToImage(stagingBuffer, textureImage,
                      static_cast<uint32_t>(texWidth),
                      static_cast<uint32_t>(texHeight));

    generateMipmaps(textureImage, vk::Format::eR8G8B8A8Srgb, texWidth,
                    texHeight, mipLevels);
  }

  void generateMipmaps(vk::raii::Image &image, vk::Format imageFormat,
                       int32_t texWidth, int32_t texHeight,
                       uint32_t mipLevels) {
    // Check if image format supports linear blit-ing
    vk::FormatProperties formatProperties =
        deviceContext().physicalDeviceHandle().getFormatProperties(imageFormat);

    if (!(formatProperties.optimalTilingFeatures &
          vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
      throw std::runtime_error(
          "texture image format does not support linear blitting!");
    }

    std::unique_ptr<vk::raii::CommandBuffer> commandBuffer =
        beginSingleTimeCommands();

    vk::ImageMemoryBarrier barrier = {
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eTransferRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image};
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++) {
      barrier.subresourceRange.baseMipLevel = i - 1;
      barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
      barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
      barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
      barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

      commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                     vk::PipelineStageFlagBits::eTransfer, {},
                                     {}, {}, barrier);

      vk::ArrayWrapper1D<vk::Offset3D, 2> offsets, dstOffsets;
      offsets[0] = vk::Offset3D(0, 0, 0);
      offsets[1] = vk::Offset3D(mipWidth, mipHeight, 1);
      dstOffsets[0] = vk::Offset3D(0, 0, 0);
      dstOffsets[1] = vk::Offset3D(mipWidth > 1 ? mipWidth / 2 : 1,
                                   mipHeight > 1 ? mipHeight / 2 : 1, 1);
      vk::ImageBlit blit = {.srcSubresource = {},
                            .srcOffsets = offsets,
                            .dstSubresource = {},
                            .dstOffsets = dstOffsets};
      blit.srcSubresource = vk::ImageSubresourceLayers(
          vk::ImageAspectFlagBits::eColor, i - 1, 0, 1);
      blit.dstSubresource =
          vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, 1);

      commandBuffer->blitImage(image, vk::ImageLayout::eTransferSrcOptimal,
                               image, vk::ImageLayout::eTransferDstOptimal,
                               {blit}, vk::Filter::eLinear);

      barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
      barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
      barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
      barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

      commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                     vk::PipelineStageFlagBits::eFragmentShader,
                                     {}, {}, {}, barrier);

      if (mipWidth > 1)
        mipWidth /= 2;
      if (mipHeight > 1)
        mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                   vk::PipelineStageFlagBits::eFragmentShader,
                                   {}, {}, {}, barrier);

    endSingleTimeCommands(*commandBuffer);
  }

  void createTextureImageView() {
    textureImageView =
        createImageView(textureImage, vk::Format::eR8G8B8A8Srgb,
                        vk::ImageAspectFlagBits::eColor, mipLevels);
  }

  void createTextureSampler() {
    vk::PhysicalDeviceProperties properties =
        deviceContext().physicalDeviceHandle().getProperties();
    vk::SamplerCreateInfo samplerInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .mipLodBias = 0.0f,
        .anisotropyEnable = vk::True,
        .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
        .compareEnable = vk::False,
        .compareOp = vk::CompareOp::eAlways};
    textureSampler =
        vk::raii::Sampler(deviceContext().deviceHandle(), samplerInfo);
  }

  [[nodiscard]] vk::raii::ImageView
  createImageView(const vk::raii::Image &image, vk::Format format,
                  vk::ImageAspectFlags aspectFlags, uint32_t mipLevels) {
    vk::ImageViewCreateInfo viewInfo{
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {aspectFlags, 0, mipLevels, 0, 1}};
    return vk::raii::ImageView(deviceContext().deviceHandle(), viewInfo);
  }

  void createImage(uint32_t width, uint32_t height, uint32_t mipLevels,
                   vk::SampleCountFlagBits numSamples, vk::Format format,
                   vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                   vk::MemoryPropertyFlags properties, vk::raii::Image &image,
                   vk::raii::DeviceMemory &imageMemory) {
    vk::ImageCreateInfo imageInfo{.imageType = vk::ImageType::e2D,
                                  .format = format,
                                  .extent = {width, height, 1},
                                  .mipLevels = mipLevels,
                                  .arrayLayers = 1,
                                  .samples = numSamples,
                                  .tiling = tiling,
                                  .usage = usage,
                                  .sharingMode = vk::SharingMode::eExclusive,
                                  .initialLayout = vk::ImageLayout::eUndefined};
    image = vk::raii::Image(deviceContext().deviceHandle(), imageInfo);

    vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = deviceContext().findMemoryType(
            memRequirements.memoryTypeBits, properties)};
    imageMemory =
        vk::raii::DeviceMemory(deviceContext().deviceHandle(), allocInfo);
    image.bindMemory(imageMemory, 0);
  }

  void transitionImageLayout(const vk::raii::Image &image,
                             const vk::ImageLayout oldLayout,
                             const vk::ImageLayout newLayout,
                             uint32_t mipLevels) {
    const auto commandBuffer = beginSingleTimeCommands();

    vk::ImageMemoryBarrier barrier{
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .image = image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0,
                             1}};

    vk::PipelineStageFlags sourceStage;
    vk::PipelineStageFlags destinationStage;

    if (oldLayout == vk::ImageLayout::eUndefined &&
        newLayout == vk::ImageLayout::eTransferDstOptimal) {
      barrier.srcAccessMask = {};
      barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

      sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
      destinationStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
               newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
      barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
      barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

      sourceStage = vk::PipelineStageFlagBits::eTransfer;
      destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else {
      throw std::invalid_argument("unsupported layout transition!");
    }
    commandBuffer->pipelineBarrier(sourceStage, destinationStage, {}, {},
                                   nullptr, barrier);
    endSingleTimeCommands(*commandBuffer);
  }

  void copyBufferToImage(const vk::raii::Buffer &buffer,
                         const vk::raii::Image &image, uint32_t width,
                         uint32_t height) {
    std::unique_ptr<vk::raii::CommandBuffer> commandBuffer =
        beginSingleTimeCommands();
    vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1}};
    commandBuffer->copyBufferToImage(
        buffer, image, vk::ImageLayout::eTransferDstOptimal, {region});
    endSingleTimeCommands(*commandBuffer);
  }

  void loadModel() {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!LoadObj(&attrib, &shapes, &materials, &warn, &err,
                 MODEL_PATH.c_str())) {
      throw std::runtime_error(warn + err);
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};

    for (const auto &shape : shapes) {
      for (const auto &index : shape.mesh.indices) {
        Vertex vertex{};

        vertex.pos = {attrib.vertices[3 * index.vertex_index + 0],
                      attrib.vertices[3 * index.vertex_index + 1],
                      attrib.vertices[3 * index.vertex_index + 2]};

        vertex.texCoord = {attrib.texcoords[2 * index.texcoord_index + 0],
                           1.0f -
                               attrib.texcoords[2 * index.texcoord_index + 1]};

        vertex.color = {1.0f, 1.0f, 1.0f};

        if (!uniqueVertices.contains(vertex)) {
          uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
          vertices.push_back(vertex);
        }

        indices.push_back(uniqueVertices[vertex]);
      }
    }
  }

  void createVertexBuffer() {
    vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingBufferMemory({});
    createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible |
                     vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void *dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, vertices.data(), bufferSize);
    stagingBufferMemory.unmapMemory();

    createBuffer(bufferSize,
                 vk::BufferUsageFlagBits::eTransferDst |
                     vk::BufferUsageFlagBits::eVertexBuffer,
                 vk::MemoryPropertyFlagBits::eDeviceLocal, vertexBuffer,
                 vertexBufferMemory);

    copyBuffer(stagingBuffer, vertexBuffer, bufferSize);
  }

  void createIndexBuffer() {
    vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingBufferMemory({});
    createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible |
                     vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void *data = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(data, indices.data(), bufferSize);
    stagingBufferMemory.unmapMemory();

    createBuffer(bufferSize,
                 vk::BufferUsageFlagBits::eTransferDst |
                     vk::BufferUsageFlagBits::eIndexBuffer,
                 vk::MemoryPropertyFlagBits::eDeviceLocal, indexBuffer,
                 indexBufferMemory);

    copyBuffer(stagingBuffer, indexBuffer, bufferSize);
  }

  void createUniformBuffers() {
    uniformBuffers.clear();
    uniformBuffersMemory.clear();
    uniformBuffersMapped.clear();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
      vk::raii::Buffer buffer({});
      vk::raii::DeviceMemory bufferMem({});
      createBuffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer,
                   vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent,
                   buffer, bufferMem);
      uniformBuffers.emplace_back(std::move(buffer));
      uniformBuffersMemory.emplace_back(std::move(bufferMem));
      uniformBuffersMapped.emplace_back(
          uniformBuffersMemory[i].mapMemory(0, bufferSize));
    }
  }

  void createDescriptorPool() {
    std::array poolSize{
        vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer,
                               MAX_FRAMES_IN_FLIGHT),
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler,
                               MAX_FRAMES_IN_FLIGHT)};
    vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = static_cast<uint32_t>(poolSize.size()),
        .pPoolSizes = poolSize.data()};
    descriptorPool =
        vk::raii::DescriptorPool(deviceContext().deviceHandle(), poolInfo);
  }

  void createDescriptorSets() {
    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
                                                 descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data()};

    descriptorSets.clear();
    descriptorSets =
        deviceContext().deviceHandle().allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vk::DescriptorBufferInfo bufferInfo{.buffer = uniformBuffers[i],
                                          .offset = 0,
                                          .range = sizeof(UniformBufferObject)};
      vk::DescriptorImageInfo imageInfo{
          .sampler = textureSampler,
          .imageView = textureImageView,
          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
      std::array descriptorWrites{
          vk::WriteDescriptorSet{.dstSet = descriptorSets[i],
                                 .dstBinding = 0,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType =
                                     vk::DescriptorType::eUniformBuffer,
                                 .pBufferInfo = &bufferInfo},
          vk::WriteDescriptorSet{.dstSet = descriptorSets[i],
                                 .dstBinding = 1,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType =
                                     vk::DescriptorType::eCombinedImageSampler,
                                 .pImageInfo = &imageInfo}};
      deviceContext().deviceHandle().updateDescriptorSets(descriptorWrites, {});
    }
  }

  void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                    vk::MemoryPropertyFlags properties,
                    vk::raii::Buffer &buffer,
                    vk::raii::DeviceMemory &bufferMemory) {
    vk::BufferCreateInfo bufferInfo{.size = size,
                                    .usage = usage,
                                    .sharingMode = vk::SharingMode::eExclusive};
    buffer = vk::raii::Buffer(deviceContext().deviceHandle(), bufferInfo);
    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = deviceContext().findMemoryType(
            memRequirements.memoryTypeBits, properties)};
    bufferMemory =
        vk::raii::DeviceMemory(deviceContext().deviceHandle(), allocInfo);
    buffer.bindMemory(bufferMemory, 0);
  }

  std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands() {
    return commandContext().beginSingleTimeCommands(deviceContext());
  }

  void endSingleTimeCommands(const vk::raii::CommandBuffer &commandBuffer) {
    commandContext().endSingleTimeCommands(deviceContext(), commandBuffer);
  }

  void copyBuffer(vk::raii::Buffer &srcBuffer, vk::raii::Buffer &dstBuffer,
                  vk::DeviceSize size) {
    auto commandCopyBuffer = beginSingleTimeCommands();
    commandCopyBuffer->copyBuffer(*srcBuffer, *dstBuffer,
                                  vk::BufferCopy{.size = size});
    endSingleTimeCommands(*commandCopyBuffer);
  }

  void recordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex) {
    auto &commandBuffer = commandContext().commandBuffer(frameIndex);
    commandBuffer.begin({});
    // Before starting rendering, transition the swapchain image to
    // COLOR_ATTACHMENT_OPTIMAL
    transition_image_layout(
        commandBuffer, swapchainContext().swapchainImages()[imageIndex],
        vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
        {}, // srcAccessMask (no need to wait for previous operations)
        vk::AccessFlagBits2::eColorAttachmentWrite,         // dstAccessMask
        vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
        vk::PipelineStageFlagBits2::eColorAttachmentOutput, // dstStage
        vk::ImageAspectFlagBits::eColor);
    // Transition the multisampled color image to COLOR_ATTACHMENT_OPTIMAL
    transition_image_layout(commandBuffer, *colorImage,
                            vk::ImageLayout::eUndefined,
                            vk::ImageLayout::eColorAttachmentOptimal,
                            vk::AccessFlagBits2::eColorAttachmentWrite,
                            vk::AccessFlagBits2::eColorAttachmentWrite,
                            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            vk::ImageAspectFlagBits::eColor);
    // Transition the depth image to DEPTH_ATTACHMENT_OPTIMAL
    transition_image_layout(commandBuffer, *depthImage,
                            vk::ImageLayout::eUndefined,
                            vk::ImageLayout::eDepthAttachmentOptimal,
                            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                            vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                vk::PipelineStageFlagBits2::eLateFragmentTests,
                            vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                vk::PipelineStageFlagBits2::eLateFragmentTests,
                            vk::ImageAspectFlagBits::eDepth);

    vk::ClearValue clearColor = {
        .color = {.float32 = std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}}};
    vk::ClearValue clearDepth = {.depthStencil =
                                     vk::ClearDepthStencilValue{1.0f, 0}};

    // Color attachment (multisampled) with resolve attachment
    vk::RenderingAttachmentInfo colorAttachment = {
        .imageView = colorImageView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .resolveMode = vk::ResolveModeFlagBits::eAverage,
        .resolveImageView =
            swapchainContext().swapchainImageViews()[imageIndex],
        .resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearColor};

    // Depth attachment
    vk::RenderingAttachmentInfo depthAttachment = {
        .imageView = depthImageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearDepth};

    vk::RenderingInfo renderingInfo = {
        .renderArea = {.offset = {0, 0},
                       .extent = swapchainContext().extent2D()},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment};
    commandBuffer.beginRendering(renderingInfo);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                               *graphicsPipeline);
    commandBuffer.setViewport(
        0,
        vk::Viewport(0.0f, 0.0f,
                     static_cast<float>(swapchainContext().extent2D().width),
                     static_cast<float>(swapchainContext().extent2D().height),
                     0.0f, 1.0f));
    commandBuffer.setScissor(
        0, vk::Rect2D(vk::Offset2D(0, 0), swapchainContext().extent2D()));
    commandBuffer.bindVertexBuffers(0, *vertexBuffer, {0});
    commandBuffer.bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint32);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                     pipelineLayout, 0,
                                     *descriptorSets[frameIndex], nullptr);
    commandBuffer.drawIndexed(indices.size(), 1, 0, 0, 0);
    commandBuffer.endRendering();
    // After rendering, transition the swapchain image to PRESENT_SRC
    transition_image_layout(
        commandBuffer, swapchainContext().swapchainImages()[imageIndex],
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eColorAttachmentWrite,         // srcAccessMask
        {},                                                 // dstAccessMask
        vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
        vk::PipelineStageFlagBits2::eBottomOfPipe,          // dstStage
        vk::ImageAspectFlagBits::eColor);
    commandBuffer.end();
  }

  void transition_image_layout(vk::raii::CommandBuffer &commandBuffer,
                               vk::Image image, vk::ImageLayout old_layout,
                               vk::ImageLayout new_layout,
                               vk::AccessFlags2 src_access_mask,
                               vk::AccessFlags2 dst_access_mask,
                               vk::PipelineStageFlags2 src_stage_mask,
                               vk::PipelineStageFlags2 dst_stage_mask,
                               vk::ImageAspectFlags image_aspect_flags) {
    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask = src_stage_mask,
        .srcAccessMask = src_access_mask,
        .dstStageMask = dst_stage_mask,
        .dstAccessMask = dst_access_mask,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {.aspectMask = image_aspect_flags,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};
    vk::DependencyInfo dependency_info = {.dependencyFlags = {},
                                          .imageMemoryBarrierCount = 1,
                                          .pImageMemoryBarriers = &barrier};
    commandBuffer.pipelineBarrier2(dependency_info);
  }

  void updateUniformBuffer(uint32_t currentImage) {
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(currentTime - startTime).count();

    UniformBufferObject ubo{};
    ubo.model = rotate(glm::mat4(1.0f), time * glm::radians(90.0f),
                       glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                      glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.proj = glm::perspective(
        glm::radians(45.0f),
        static_cast<float>(swapchainContext().extent2D().width) /
            static_cast<float>(swapchainContext().extent2D().height),
        0.1f, 10.0f);
    ubo.proj[1][1] *= -1;

    memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
  }

  void drawFrame() {
    auto frameState = backend.beginFrame(appWindow);
    if (!frameState) {
      recreateSwapChain();
      return;
    }
    updateUniformBuffer(frameState->frameIndex);
    recordCommandBuffer(frameState->frameIndex, frameState->imageIndex);
    if (backend.endFrame(*frameState, appWindow)) {
      recreateSwapChain();
    }
  }

  [[nodiscard]] vk::raii::ShaderModule
  createShaderModule(const std::vector<char> &code) {
    vk::ShaderModuleCreateInfo createInfo{
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t *>(code.data())};
    vk::raii::ShaderModule shaderModule{deviceContext().deviceHandle(),
                                        createInfo};

    return shaderModule;
  }

  static std::vector<char> readFile(const std::string &filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
      throw std::runtime_error("failed to open file!");
    }
    std::vector<char> buffer(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    file.close();

    return buffer;
  }
};

int main() {
  try {
    HelloTriangleApplication app;
    app.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
