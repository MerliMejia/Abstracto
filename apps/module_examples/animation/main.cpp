#include "engine/animation/ModelAnimationState.h"

int main() {
  SkeletonAssetData skeleton;
  skeleton.nodes.push_back(SkeletonNode{});

  ModelAnimationState animationState;
  animationState.reset(&skeleton);
  return animationState.currentPose(&skeleton) == nullptr ? 1 : 0;
}
