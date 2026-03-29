#pragma once

#include "engine/animation/SkeletonTypes.h"
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stdexcept>
#include <vector>

inline glm::mat4 composeNodeTransform(const NodeTransform &transform) {
  return glm::translate(glm::mat4(1.0f), transform.translation) *
         glm::mat4_cast(transform.rotation) *
         glm::scale(glm::mat4(1.0f), transform.scale);
}

class SkeletonPose {
public:
  void initialize(const SkeletonAssetData &skeleton) {
    localTransforms.clear();
    localTransforms.reserve(skeleton.nodes.size());
    for (const auto &node : skeleton.nodes) {
      localTransforms.push_back(node.localBindTransform);
    }
    worldTransforms.assign(skeleton.nodes.size(), glm::mat4(1.0f));
    recomputeWorldTransforms(skeleton);
  }

  void resetToBindPose(const SkeletonAssetData &skeleton) {
    initialize(skeleton);
  }

  void recomputeWorldTransforms(const SkeletonAssetData &skeleton) {
    if (localTransforms.size() != skeleton.nodes.size()) {
      throw std::runtime_error("SkeletonPose size does not match skeleton node "
                               "count");
    }

    worldTransforms.assign(skeleton.nodes.size(), glm::mat4(1.0f));
    std::vector<bool> computed(skeleton.nodes.size(), false);

    std::function<void(int)> computeWorldTransform = [&](int nodeIndex) {
      if (computed[static_cast<size_t>(nodeIndex)]) {
        return;
      }

      const SkeletonNode &node = skeleton.nodes[static_cast<size_t>(nodeIndex)];
      const glm::mat4 localMatrix =
          composeNodeTransform(localTransforms[static_cast<size_t>(nodeIndex)]);

      if (node.parentIndex >= 0) {
        computeWorldTransform(node.parentIndex);
        worldTransforms[static_cast<size_t>(nodeIndex)] =
            worldTransforms[static_cast<size_t>(node.parentIndex)] *
            localMatrix;
      } else {
        worldTransforms[static_cast<size_t>(nodeIndex)] = localMatrix;
      }

      computed[static_cast<size_t>(nodeIndex)] = true;
    };

    for (size_t nodeIndex = 0; nodeIndex < skeleton.nodes.size(); ++nodeIndex) {
      computeWorldTransform(static_cast<int>(nodeIndex));
    }
  }

  bool empty() const { return localTransforms.empty(); }
  size_t size() const { return localTransforms.size(); }

  NodeTransform &localTransform(size_t nodeIndex) {
    return localTransforms.at(nodeIndex);
  }

  const NodeTransform &localTransform(size_t nodeIndex) const {
    return localTransforms.at(nodeIndex);
  }

  const glm::mat4 &worldTransform(size_t nodeIndex) const {
    return worldTransforms.at(nodeIndex);
  }

private:
  std::vector<NodeTransform> localTransforms;
  std::vector<glm::mat4> worldTransforms;
};
