#include "engine/runtime/DefaultEngineConfig.h"
#include "engine/runtime/RendererSceneAdapters.h"

int main() {
  DefaultEngineConfig config{};
  SceneLightSet lights = SceneLightSet::showcaseLights();
  const auto pbrLights = RendererSceneAdapters::buildPbrLightInputs(lights);
  return config.width > 0 && !pbrLights.empty() ? 0 : 1;
}
