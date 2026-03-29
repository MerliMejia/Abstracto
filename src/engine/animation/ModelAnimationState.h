#pragma once

#include "AnimationPlayback.h"
#include "SkeletonPose.h"
#include <algorithm>
#include <optional>

class ModelAnimationState {
public:
  void reset(const SkeletonAssetData *skeleton) {
    if (skeleton == nullptr || skeleton->nodes.empty()) {
      clear();
      return;
    }

    skeletonPose.emplace();
    skeletonPose->initialize(*skeleton);
    animationPlayback = AnimationPlaybackState{};
  }

  void clear() {
    skeletonPose.reset();
    animationPlayback = AnimationPlaybackState{};
  }

  AnimationPlaybackState *mutablePlayback(const SkeletonAssetData *skeleton) {
    return supportsPlayback(skeleton) ? &animationPlayback : nullptr;
  }

  const AnimationPlaybackState *
  currentPlayback(const SkeletonAssetData *skeleton) const {
    return supportsPlayback(skeleton) ? &animationPlayback : nullptr;
  }

  SkeletonPose *mutablePose(const SkeletonAssetData *skeleton) {
    return hasPose(skeleton) ? &*skeletonPose : nullptr;
  }

  const SkeletonPose *currentPose(const SkeletonAssetData *skeleton) const {
    return hasPose(skeleton) ? &*skeletonPose : nullptr;
  }

  void resetPose(const SkeletonAssetData *skeleton) {
    if (skeleton == nullptr || skeleton->nodes.empty()) {
      clear();
      return;
    }

    if (!skeletonPose.has_value()) {
      skeletonPose.emplace();
    }
    skeletonPose->resetToBindPose(*skeleton);
  }

  bool hasSelectedAnimation(const SkeletonAssetData *skeleton) const {
    return selectedClip(skeleton) != nullptr;
  }

  const AnimationClipData *
  selectedClip(const SkeletonAssetData *skeleton) const {
    if (!supportsPlayback(skeleton)) {
      return nullptr;
    }

    if (animationPlayback.selectedSourceAnimationIndex < 0) {
      return nullptr;
    }

    const size_t selectedIndex =
        static_cast<size_t>(animationPlayback.selectedSourceAnimationIndex);
    if (selectedIndex >= skeleton->animations.size()) {
      return nullptr;
    }
    return &skeleton->animations[selectedIndex];
  }

  void selectSourceAnimation(const SkeletonAssetData *skeleton,
                             int animationIndex) {
    animationPlayback.selectedSourceAnimationIndex = animationIndex;
    animationPlayback.currentTimeSeconds = 0.0f;
    animationPlayback.playing = false;
    sampleSelectedAnimation(skeleton);
  }

  void playSelectedAnimation(const SkeletonAssetData *skeleton) {
    if (selectedClip(skeleton) != nullptr) {
      animationPlayback.playing = true;
    }
  }

  void pauseAnimationPlayback() { animationPlayback.playing = false; }

  void resetSelectedAnimation(const SkeletonAssetData *skeleton) {
    animationPlayback.currentTimeSeconds = 0.0f;
    animationPlayback.playing = false;
    sampleSelectedAnimation(skeleton);
  }

  void sampleSelectedAnimation(const SkeletonAssetData *skeleton) {
    if (!hasPose(skeleton)) {
      return;
    }

    const AnimationClipData *clip = selectedClip(skeleton);
    if (clip == nullptr) {
      skeletonPose->resetToBindPose(*skeleton);
      return;
    }

    sampleAnimationClipIntoPose(*skeleton, *clip,
                                animationPlayback.currentTimeSeconds,
                                *skeletonPose);
  }

  void updatePlayback(const SkeletonAssetData *skeleton, float deltaSeconds) {
    if (!animationPlayback.playing || !hasPose(skeleton)) {
      return;
    }

    const AnimationClipData *clip = selectedClip(skeleton);
    if (clip == nullptr) {
      animationPlayback.playing = false;
      return;
    }

    const float duration = std::max(clip->durationSeconds, 0.0f);
    if (duration <= 0.0f) {
      animationPlayback.currentTimeSeconds = 0.0f;
      sampleSelectedAnimation(skeleton);
      animationPlayback.playing = false;
      return;
    }

    animationPlayback.currentTimeSeconds +=
        deltaSeconds * animationPlayback.speed;

    if (animationPlayback.loop) {
      while (animationPlayback.currentTimeSeconds > duration) {
        animationPlayback.currentTimeSeconds -= duration;
      }
      while (animationPlayback.currentTimeSeconds < 0.0f) {
        animationPlayback.currentTimeSeconds += duration;
      }
    } else {
      animationPlayback.currentTimeSeconds =
          glm::clamp(animationPlayback.currentTimeSeconds, 0.0f, duration);
      if (animationPlayback.currentTimeSeconds >= duration) {
        animationPlayback.playing = false;
      }
    }

    sampleSelectedAnimation(skeleton);
  }

private:
  bool hasPose(const SkeletonAssetData *skeleton) const {
    return skeletonPose.has_value() && skeleton != nullptr &&
           !skeleton->nodes.empty();
  }

  bool supportsPlayback(const SkeletonAssetData *skeleton) const {
    return hasPose(skeleton) && !skeleton->animations.empty();
  }

  std::optional<SkeletonPose> skeletonPose;
  AnimationPlaybackState animationPlayback;
};
