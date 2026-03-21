#pragma once

#include "PassUniformSet.h"
#include "SceneRenderPass.h"
#include <type_traits>
#include <utility>
#include <variant>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

template <typename TUniform, typename TPush = std::monostate>
class UniformSceneRenderPass : public SceneRenderPass {
public:
  explicit UniformSceneRenderPass(
      PipelineSpec pipelineSpec, uint32_t framesInFlight,
      RasterPassAttachmentConfig attachmentConfig = RasterPassAttachmentConfig())
      : SceneRenderPass(std::move(pipelineSpec), std::move(attachmentConfig)),
        framesInFlightCount(framesInFlight) {}

protected:
  virtual TUniform buildUniformData(uint32_t frameIndex) const = 0;

  virtual TPush buildPushConstantData(uint32_t frameIndex) const {
    return TPush{};
  }

  virtual vk::ShaderStageFlags uniformShaderStages() const {
    return vk::ShaderStageFlagBits::eVertex |
           vk::ShaderStageFlagBits::eFragment;
  }

  virtual vk::ShaderStageFlags pushConstantShaderStages() const {
    return vk::ShaderStageFlagBits::eFragment;
  }

  virtual uint32_t uniformDescriptorBinding() const { return 0; }

  virtual uint32_t uniformDescriptorSetIndex() const { return 0; }

  virtual std::vector<DescriptorBindingSpec> extraDescriptorBindings() const {
    return {};
  }

  virtual std::vector<vk::PushConstantRange> extraPushConstantRanges() const {
    return {};
  }

  virtual void initializeAdditionalPassResources(DeviceContext &deviceContext,
                                                 SwapchainContext &) {}

  virtual void recreateAdditionalPassResources(DeviceContext &deviceContext,
                                               SwapchainContext &) {}

  virtual void bindAdditionalPassResources(const RenderPassContext &context) {}

  virtual void bindPerRenderItemResources(const RenderPassContext &context,
                                          const RenderItem &renderItem) {}

  PassUniformSet<TUniform> &uniformSet() { return passUniforms; }
  const PassUniformSet<TUniform> &uniformSet() const { return passUniforms; }
  uint32_t framesInFlight() const { return framesInFlightCount; }

private:
  uint32_t framesInFlightCount = 0;
  PassUniformSet<TUniform> passUniforms;

  std::vector<DescriptorBindingSpec> descriptorBindings() const final {
    auto bindings = extraDescriptorBindings();
    bindings.insert(bindings.begin(),
                    DescriptorBindingSpec{
                        .binding = uniformDescriptorBinding(),
                        .descriptorType = vk::DescriptorType::eUniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags = uniformShaderStages()});
    return bindings;
  }

  std::vector<vk::PushConstantRange> pushConstantRanges() const final {
    auto ranges = extraPushConstantRanges();
    if constexpr (!std::is_same_v<TPush, std::monostate>) {
      ranges.insert(ranges.begin(), vk::PushConstantRange{
                                        .stageFlags = pushConstantShaderStages(),
                                        .offset = 0,
                                        .size = sizeof(TPush)});
    }
    return ranges;
  }

  void initializePassResources(DeviceContext &deviceContext,
                               SwapchainContext &swapchainContext) final {
    passUniforms.initialize(deviceContext, passDescriptorSetLayout(),
                            framesInFlightCount, uniformDescriptorBinding());
    initializeAdditionalPassResources(deviceContext, swapchainContext);
  }

  void recreatePassResources(DeviceContext &deviceContext,
                             SwapchainContext &swapchainContext) final {
    recreateAdditionalPassResources(deviceContext, swapchainContext);
  }

  void bindPassResources(const RenderPassContext &context) final {
    passUniforms.write(context.frameIndex, buildUniformData(context.frameIndex));
    passUniforms.bind(context.commandBuffer, pipelineLayoutHandle(),
                      context.frameIndex, uniformDescriptorSetIndex());

    if constexpr (!std::is_same_v<TPush, std::monostate>) {
      auto pushData = buildPushConstantData(context.frameIndex);
      context.commandBuffer.pushConstants<TPush>(
          *pipelineLayoutHandle(), pushConstantShaderStages(), 0, {pushData});
    }

    bindAdditionalPassResources(context);
  }

  void bindRenderItemResources(const RenderPassContext &context,
                               const RenderItem &renderItem) final {
    bindPerRenderItemResources(context, renderItem);
  }
};
