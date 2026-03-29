#include "engine/assets/GltfModelAsset.h"
#include "engine/assets/ObjModelAsset.h"

int main() {
  GltfModelAsset gltfAsset;
  ObjModelAsset objAsset;
  return gltfAsset.path().empty() && objAsset.path().empty() ? 0 : 1;
}
