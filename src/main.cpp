#include "app/DefaultEngineApp.h"
#include <cstdlib>
#include <exception>
#include <iostream>

int main() {
  try {
    DefaultEngineConfig engineConfig{};
    DefaultEngineApp::create(engineConfig).run();
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
