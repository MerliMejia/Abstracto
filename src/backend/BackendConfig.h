#pragma once

#include <cstdint>
#include <string>

struct BackendConfig {
  std::string appName = "RenderCubed";
  uint32_t maxFramesInFlight = 2;
};
