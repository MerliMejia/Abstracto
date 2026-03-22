#pragma once

#include "SkeletonPose.h"
#include <algorithm>
#include <glm/gtc/quaternion.hpp>

struct AnimationPlaybackState {
  int selectedSourceAnimationIndex = -1;
  float currentTimeSeconds = 0.0f;
  float speed = 1.0f;
  bool playing = false;
  bool loop = true;
};

inline glm::vec3 sampleAnimationVec3Track(const NodeAnimationTrack &track,
                                          float timeSeconds) {
  if (track.timesSeconds.empty() || track.vec3Values.empty()) {
    return glm::vec3(0.0f);
  }

  if (track.timesSeconds.size() == 1 || timeSeconds <= track.timesSeconds.front()) {
    return track.vec3Values.front();
  }
  if (timeSeconds >= track.timesSeconds.back()) {
    return track.vec3Values.back();
  }

  const auto upperIt = std::upper_bound(track.timesSeconds.begin(),
                                        track.timesSeconds.end(), timeSeconds);
  const size_t nextIndex =
      static_cast<size_t>(std::distance(track.timesSeconds.begin(), upperIt));
  const size_t prevIndex = nextIndex - 1;
  if (track.interpolation == AnimationInterpolation::Step) {
    return track.vec3Values[prevIndex];
  }

  const float prevTime = track.timesSeconds[prevIndex];
  const float nextTime = track.timesSeconds[nextIndex];
  const float alpha = nextTime > prevTime
                          ? (timeSeconds - prevTime) / (nextTime - prevTime)
                          : 0.0f;
  return glm::mix(track.vec3Values[prevIndex], track.vec3Values[nextIndex],
                  glm::clamp(alpha, 0.0f, 1.0f));
}

inline glm::quat sampleAnimationQuatTrack(const NodeAnimationTrack &track,
                                          float timeSeconds) {
  if (track.timesSeconds.empty() || track.quatValues.empty()) {
    return glm::identity<glm::quat>();
  }

  if (track.timesSeconds.size() == 1 || timeSeconds <= track.timesSeconds.front()) {
    return glm::normalize(track.quatValues.front());
  }
  if (timeSeconds >= track.timesSeconds.back()) {
    return glm::normalize(track.quatValues.back());
  }

  const auto upperIt = std::upper_bound(track.timesSeconds.begin(),
                                        track.timesSeconds.end(), timeSeconds);
  const size_t nextIndex =
      static_cast<size_t>(std::distance(track.timesSeconds.begin(), upperIt));
  const size_t prevIndex = nextIndex - 1;
  if (track.interpolation == AnimationInterpolation::Step) {
    return glm::normalize(track.quatValues[prevIndex]);
  }

  const float prevTime = track.timesSeconds[prevIndex];
  const float nextTime = track.timesSeconds[nextIndex];
  const float alpha = nextTime > prevTime
                          ? (timeSeconds - prevTime) / (nextTime - prevTime)
                          : 0.0f;
  return glm::normalize(glm::slerp(track.quatValues[prevIndex],
                                   track.quatValues[nextIndex],
                                   glm::clamp(alpha, 0.0f, 1.0f)));
}

inline void sampleAnimationClipIntoPose(const SkeletonAssetData &skeleton,
                                        const AnimationClipData &clip,
                                        float timeSeconds, SkeletonPose &pose) {
  pose.resetToBindPose(skeleton);
  const float resolvedTime =
      glm::clamp(timeSeconds, 0.0f, std::max(clip.durationSeconds, 0.0f));

  for (const auto &track : clip.tracks) {
    if (track.targetNodeIndex < 0 ||
        static_cast<size_t>(track.targetNodeIndex) >= skeleton.nodes.size()) {
      continue;
    }

    NodeTransform &nodeTransform =
        pose.localTransform(static_cast<size_t>(track.targetNodeIndex));
    switch (track.targetPath) {
    case AnimationTargetPath::Translation:
      nodeTransform.translation =
          sampleAnimationVec3Track(track, resolvedTime);
      break;
    case AnimationTargetPath::Rotation:
      nodeTransform.rotation =
          sampleAnimationQuatTrack(track, resolvedTime);
      break;
    case AnimationTargetPath::Scale:
      nodeTransform.scale = sampleAnimationVec3Track(track, resolvedTime);
      break;
    }
  }

  pose.recomputeWorldTransforms(skeleton);
}
