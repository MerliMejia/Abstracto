#include "engine/scene/SceneLightSet.h"
#include "engine/scene/SceneTypes.h"

int main() {
  SceneLightSet lights = SceneLightSet::showcaseLights();
  SceneObject object{};
  return lights.empty() || object.visible ? 0 : 1;
}
