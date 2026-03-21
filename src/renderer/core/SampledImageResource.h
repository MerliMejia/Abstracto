#pragma once

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

struct SampledImageResource {
  vk::ImageView imageView = nullptr;
  vk::Sampler sampler = nullptr;
  vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
};
