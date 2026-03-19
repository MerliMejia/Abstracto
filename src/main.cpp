#include "apps/TriangleApp.h"
#include <cmath>
#include <glm/gtx/quaternion.hpp>

int main() {
  TriangleApp triangle;

  triangle.onUpdate([](TriangleFrameContext &context) {
    const float t = context.timeSeconds;
    const auto wave = [t](float speed, float phase) {
      return 0.5f + 0.5f * std::sin(t * speed + phase);
    };

    context.transform.position = {0.0f, 0.0f, 0.0f};
    context.transform.rotation = glm::angleAxis(t, glm::vec3(0.0f, 0.0f, 1.0f));
    context.transform.scale = {0.85f, 0.85f, 1.0f};

    context.positions[0] = {0.0f, 0.5f, 0.0f};
    context.positions[1] = {-0.5f, -0.5f, 0.0f};
    context.positions[2] = {0.5f, -0.5f, 0.0f};

    context.colors[0] = {wave(1.8f, 0.0f), wave(1.1f, 2.1f), wave(1.4f, 4.2f),
                         1.0f};
    context.colors[1] = {wave(1.2f, 1.3f), wave(1.7f, 3.2f), wave(0.9f, 5.0f),
                         1.0f};
    context.colors[2] = {wave(1.5f, 2.4f), wave(1.0f, 4.0f), wave(1.9f, 0.8f),
                         1.0f};
  });

  triangle.run();

  return 0;
}
