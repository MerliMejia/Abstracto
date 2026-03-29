#include "renderer/passes/DebugOverlayPass.h"
#include "renderer/passes/PbrPass.h"

int main() {
  PbrLightInput light{};
  DebugOverlayMarker marker{};
  return light.enabled && marker.type == DebugOverlayMarkerType::Point ? 0 : 1;
}
