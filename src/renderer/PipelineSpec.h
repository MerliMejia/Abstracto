#pragma once

#include <string>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

struct PipelineSpec {
  std::string shaderPath;
  std::string vertexEntry = "vertMain";
  std::string fragmentEntry = "fragMain";
  vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
  vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
  vk::CullModeFlags cullMode = vk::CullModeFlagBits::eNone;
  vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;
  bool enableDepthTest = true;
  bool enableDepthWrite = true;
  bool enableDepthBias = false;
  bool enableBlending = false;
};
