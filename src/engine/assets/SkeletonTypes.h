#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

struct NodeTransform {
  glm::vec3 translation{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 scale{1.0f};
};

struct SkeletonNode {
  std::string name;
  int parentIndex = -1;
  std::vector<int> childIndices;
  NodeTransform localBindTransform{};
};

struct SkinData {
  std::string name;
  int skeletonRootNodeIndex = -1;
  std::vector<int> jointNodeIndices;
  std::vector<glm::mat4> inverseBindMatrices;
};

enum class AnimationTargetPath {
  Translation,
  Rotation,
  Scale,
};

enum class AnimationInterpolation {
  Linear,
  Step,
  CubicSpline,
};

struct NodeAnimationTrack {
  int targetNodeIndex = -1;
  AnimationTargetPath targetPath = AnimationTargetPath::Translation;
  AnimationInterpolation interpolation = AnimationInterpolation::Linear;
  std::vector<float> timesSeconds;
  std::vector<glm::vec3> vec3InTangents;
  std::vector<glm::vec3> vec3Values;
  std::vector<glm::vec3> vec3OutTangents;
  std::vector<glm::vec4> quatInTangents;
  std::vector<glm::quat> quatValues;
  std::vector<glm::vec4> quatOutTangents;
};

struct AnimationClipData {
  std::string name;
  std::vector<NodeAnimationTrack> tracks;
  float durationSeconds = 0.0f;
};

struct SkeletonAssetData {
  std::vector<SkeletonNode> nodes;
  std::vector<int> sceneRootNodeIndices;
  std::vector<SkinData> skins;
  std::vector<AnimationClipData> animations;
};
