#pragma once

#include "../backend/DeviceContext.h"
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

class ShaderProgram {
public:
  void load(DeviceContext &deviceContext, const std::string &shaderPath,
            std::string vertexEntry, std::string fragmentEntry) {
    vertexEntryPoint = std::move(vertexEntry);
    fragmentEntryPoint = std::move(fragmentEntry);

    vk::ShaderModuleCreateInfo createInfo{
        .codeSize = 0,
        .pCode = nullptr,
    };
    auto code = readFile(shaderPath);
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());
    shaderModule = vk::raii::ShaderModule(deviceContext.deviceHandle(), createInfo);
  }

  std::array<vk::PipelineShaderStageCreateInfo, 2> stages() const {
    return {
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eVertex,
                                          .module = shaderModule,
                                          .pName = vertexEntryPoint.c_str()},
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eFragment,
                                          .module = shaderModule,
                                          .pName = fragmentEntryPoint.c_str()}};
  }

private:
  vk::raii::ShaderModule shaderModule = nullptr;
  std::string vertexEntryPoint = "vertMain";
  std::string fragmentEntryPoint = "fragMain";

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
