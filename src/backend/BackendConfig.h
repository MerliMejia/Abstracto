#pragma once

#include <cstdint>
#include <string>

struct BackendConfig {
  std::string appName = "Abstracto";
  uint32_t maxFramesInFlight = 2;
};
