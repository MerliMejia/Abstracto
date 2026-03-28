#include "GltfModelAsset.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

namespace {

struct IndexedNode {
  int nodeIndex = -1;
  glm::mat4 worldTransform{1.0f};
};

glm::mat4 matrixFromDoubleArray(const std::vector<double> &values) {
  if (values.size() != 16) {
    throw std::runtime_error("glTF matrix must have 16 components");
  }

  glm::mat4 matrix(1.0f);
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      matrix[column][row] = static_cast<float>(values[column * 4 + row]);
    }
  }
  return matrix;
}

NodeTransform nodeLocalComponents(const tinygltf::Node &node) {
  NodeTransform transform;

  if (node.matrix.size() == 16) {
    const glm::mat4 matrix = matrixFromDoubleArray(node.matrix);
    glm::vec3 skew(0.0f);
    glm::vec4 perspective(0.0f);
    if (!glm::decompose(matrix, transform.scale, transform.rotation,
                        transform.translation, skew, perspective)) {
      throw std::runtime_error("failed to decompose glTF node matrix");
    }
    if (glm::length(transform.rotation) < 1e-6f) {
      transform.rotation = glm::identity<glm::quat>();
    } else {
      transform.rotation = glm::normalize(transform.rotation);
    }
    return transform;
  }

  if (node.translation.size() == 3) {
    transform.translation = {
        static_cast<float>(node.translation[0]),
        static_cast<float>(node.translation[1]),
        static_cast<float>(node.translation[2]),
    };
  }

  if (node.rotation.size() == 4) {
    transform.rotation = glm::quat(static_cast<float>(node.rotation[3]),
                                   static_cast<float>(node.rotation[0]),
                                   static_cast<float>(node.rotation[1]),
                                   static_cast<float>(node.rotation[2]));
    if (glm::length(transform.rotation) < 1e-6f) {
      transform.rotation = glm::identity<glm::quat>();
    } else {
      transform.rotation = glm::normalize(transform.rotation);
    }
  }

  if (node.scale.size() == 3) {
    transform.scale = {
        static_cast<float>(node.scale[0]),
        static_cast<float>(node.scale[1]),
        static_cast<float>(node.scale[2]),
    };
  }

  return transform;
}

glm::mat4 nodeLocalTransform(const tinygltf::Node &node) {
  if (node.matrix.size() == 16) {
    return matrixFromDoubleArray(node.matrix);
  }

  const NodeTransform transform = nodeLocalComponents(node);
  return glm::translate(glm::mat4(1.0f), transform.translation) *
         glm::mat4_cast(transform.rotation) *
         glm::scale(glm::mat4(1.0f), transform.scale);
}

void traverseNode(const tinygltf::Model &model, int nodeIndex,
                  const glm::mat4 &parentTransform,
                  std::vector<IndexedNode> &outNodes) {
  const auto &node = model.nodes.at(static_cast<size_t>(nodeIndex));
  const glm::mat4 worldTransform = parentTransform * nodeLocalTransform(node);
  outNodes.push_back(
      {.nodeIndex = nodeIndex, .worldTransform = worldTransform});

  for (const int childIndex : node.children) {
    traverseNode(model, childIndex, worldTransform, outNodes);
  }
}

std::vector<IndexedNode> collectSceneNodes(const tinygltf::Model &model) {
  std::vector<IndexedNode> nodes;

  if (model.defaultScene >= 0 &&
      static_cast<size_t>(model.defaultScene) < model.scenes.size()) {
    for (const int nodeIndex : model.scenes[model.defaultScene].nodes) {
      traverseNode(model, nodeIndex, glm::mat4(1.0f), nodes);
    }
    return nodes;
  }

  for (const auto &scene : model.scenes) {
    for (const int nodeIndex : scene.nodes) {
      traverseNode(model, nodeIndex, glm::mat4(1.0f), nodes);
    }
  }

  if (nodes.empty()) {
    for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
      traverseNode(model, static_cast<int>(nodeIndex), glm::mat4(1.0f), nodes);
    }
  }

  return nodes;
}

void appendUniqueIndex(std::vector<int> &indices, int index) {
  if (std::find(indices.begin(), indices.end(), index) == indices.end()) {
    indices.push_back(index);
  }
}

std::vector<int> buildNodeParentIndices(const tinygltf::Model &model) {
  std::vector<int> parentIndices(model.nodes.size(), -1);

  for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
    const auto &node = model.nodes[nodeIndex];
    for (const int childIndex : node.children) {
      if (childIndex < 0 ||
          static_cast<size_t>(childIndex) >= model.nodes.size()) {
        throw std::runtime_error("glTF node child index out of range");
      }
      if (parentIndices[static_cast<size_t>(childIndex)] != -1) {
        throw std::runtime_error("glTF node has multiple parents");
      }
      parentIndices[static_cast<size_t>(childIndex)] =
          static_cast<int>(nodeIndex);
    }
  }

  return parentIndices;
}

std::vector<int> collectSceneRootNodeIndices(const tinygltf::Model &model) {
  std::vector<int> roots;

  if (model.defaultScene >= 0 &&
      static_cast<size_t>(model.defaultScene) < model.scenes.size()) {
    for (const int nodeIndex : model.scenes[model.defaultScene].nodes) {
      appendUniqueIndex(roots, nodeIndex);
    }
    return roots;
  }

  for (const auto &scene : model.scenes) {
    for (const int nodeIndex : scene.nodes) {
      appendUniqueIndex(roots, nodeIndex);
    }
  }

  if (!roots.empty()) {
    return roots;
  }

  const auto parentIndices = buildNodeParentIndices(model);
  for (size_t nodeIndex = 0; nodeIndex < parentIndices.size(); ++nodeIndex) {
    if (parentIndices[nodeIndex] == -1) {
      roots.push_back(static_cast<int>(nodeIndex));
    }
  }

  return roots;
}

std::vector<SkeletonNode> buildSkeletonNodes(const tinygltf::Model &model) {
  const auto parentIndices = buildNodeParentIndices(model);
  std::vector<SkeletonNode> nodes;
  nodes.reserve(model.nodes.size());

  for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
    const auto &sourceNode = model.nodes[nodeIndex];
    SkeletonNode node;
    node.name = sourceNode.name.empty() ? "Node " + std::to_string(nodeIndex)
                                        : sourceNode.name;
    node.parentIndex = parentIndices[nodeIndex];
    node.childIndices = sourceNode.children;
    node.localBindTransform = nodeLocalComponents(sourceNode);
    nodes.push_back(std::move(node));
  }

  return nodes;
}

std::string resolveGltfAssetPath(const std::filesystem::path &gltfPath,
                                 const std::string &assetPath) {
  if (assetPath.empty()) {
    return {};
  }

  const std::filesystem::path path(assetPath);
  if (path.is_absolute()) {
    return path.lexically_normal().string();
  }

  return (gltfPath.parent_path() / path).lexically_normal().string();
}

bool isDataUri(std::string_view uri) { return uri.rfind("data:", 0) == 0; }

std::vector<uint8_t> imageToRgba8(const tinygltf::Image &image) {
  if (image.image.empty() || image.width <= 0 || image.height <= 0) {
    return {};
  }

  if (image.bits != 8 && image.bits != 0) {
    throw std::runtime_error("only 8-bit glTF images are currently supported");
  }

  const int components = image.component > 0 ? image.component : 4;
  if (components < 1 || components > 4) {
    throw std::runtime_error("unsupported glTF image component count");
  }

  std::vector<uint8_t> rgba;
  rgba.resize(static_cast<size_t>(image.width) * image.height * 4);

  for (int pixelIndex = 0; pixelIndex < image.width * image.height;
       ++pixelIndex) {
    const size_t src = static_cast<size_t>(pixelIndex) * components;
    const size_t dst = static_cast<size_t>(pixelIndex) * 4;

    rgba[dst + 0] = image.image[src + 0];
    rgba[dst + 1] =
        components > 1 ? image.image[src + 1] : image.image[src + 0];
    rgba[dst + 2] =
        components > 2 ? image.image[src + 2] : image.image[src + 0];
    rgba[dst + 3] = components > 3 ? image.image[src + 3] : 255;
  }

  return rgba;
}

ModelMaterialData::TextureSource
extractTextureSource(const tinygltf::Model &model,
                     const std::filesystem::path &gltfPath, int textureIndex) {
  ModelMaterialData::TextureSource source;

  if (textureIndex < 0 ||
      static_cast<size_t>(textureIndex) >= model.textures.size()) {
    return source;
  }

  const auto &texture = model.textures[static_cast<size_t>(textureIndex)];
  if (texture.source < 0 ||
      static_cast<size_t>(texture.source) >= model.images.size()) {
    return source;
  }

  const auto &image = model.images[static_cast<size_t>(texture.source)];
  if (!image.uri.empty() && !isDataUri(image.uri)) {
    source.resolvedPath = resolveGltfAssetPath(gltfPath, image.uri);
  }

  if (source.resolvedPath.empty()) {
    source.rgba = imageToRgba8(image);
    source.width = image.width;
    source.height = image.height;
  }

  return source;
}

std::vector<ModelMaterialData>
buildGltfMaterials(const tinygltf::Model &model,
                   const std::filesystem::path &gltfPath) {
  std::vector<ModelMaterialData> materials;
  materials.reserve(model.materials.size());

  for (const auto &material : model.materials) {
    ModelMaterialData materialData;
    materialData.name = material.name;

    if (material.pbrMetallicRoughness.baseColorFactor.size() == 4) {
      materialData.baseColorFactor = {
          static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[0]),
          static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[1]),
          static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[2]),
          static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[3]),
      };
    }

    materialData.metallicFactor =
        static_cast<float>(material.pbrMetallicRoughness.metallicFactor);
    materialData.roughnessFactor =
        static_cast<float>(material.pbrMetallicRoughness.roughnessFactor);
    materialData.occlusionStrength =
        static_cast<float>(material.occlusionTexture.strength);

    if (material.emissiveFactor.size() == 3) {
      materialData.emissiveFactor = {
          static_cast<float>(material.emissiveFactor[0]),
          static_cast<float>(material.emissiveFactor[1]),
          static_cast<float>(material.emissiveFactor[2]),
      };
    }

    materialData.baseColorTexture = extractTextureSource(
        model, gltfPath, material.pbrMetallicRoughness.baseColorTexture.index);
    materialData.metallicRoughnessTexture = extractTextureSource(
        model, gltfPath,
        material.pbrMetallicRoughness.metallicRoughnessTexture.index);
    materialData.emissiveTexture =
        extractTextureSource(model, gltfPath, material.emissiveTexture.index);
    materialData.occlusionTexture =
        extractTextureSource(model, gltfPath, material.occlusionTexture.index);

    materials.push_back(std::move(materialData));
  }

  return materials;
}

const tinygltf::Accessor &accessorAt(const tinygltf::Model &model, int index) {
  if (index < 0 || static_cast<size_t>(index) >= model.accessors.size()) {
    throw std::runtime_error("glTF accessor index out of range");
  }

  return model.accessors[static_cast<size_t>(index)];
}

const unsigned char *accessorData(const tinygltf::Model &model,
                                  const tinygltf::Accessor &accessor) {
  if (accessor.sparse.isSparse) {
    throw std::runtime_error(
        "sparse glTF accessors are not currently supported");
  }

  if (accessor.bufferView < 0 ||
      static_cast<size_t>(accessor.bufferView) >= model.bufferViews.size()) {
    throw std::runtime_error("glTF accessor is missing a buffer view");
  }

  const auto &bufferView =
      model.bufferViews[static_cast<size_t>(accessor.bufferView)];
  if (bufferView.buffer < 0 ||
      static_cast<size_t>(bufferView.buffer) >= model.buffers.size()) {
    throw std::runtime_error("glTF buffer view is missing a buffer");
  }

  const auto &buffer = model.buffers[static_cast<size_t>(bufferView.buffer)];
  return buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
}

size_t accessorStride(const tinygltf::Model &model,
                      const tinygltf::Accessor &accessor) {
  const auto &bufferView =
      model.bufferViews[static_cast<size_t>(accessor.bufferView)];
  const int byteStride = accessor.ByteStride(bufferView);
  if (byteStride > 0) {
    return static_cast<size_t>(byteStride);
  }

  return static_cast<size_t>(
      tinygltf::GetComponentSizeInBytes(accessor.componentType) *
      tinygltf::GetNumComponentsInType(accessor.type));
}

const unsigned char *accessorElementData(const tinygltf::Model &model,
                                         const tinygltf::Accessor &accessor,
                                         size_t index) {
  if (index >= accessor.count) {
    throw std::runtime_error("glTF accessor element index out of range");
  }

  return accessorData(model, accessor) +
         accessorStride(model, accessor) * index;
}

glm::vec2 readVec2(const tinygltf::Model &model,
                   const tinygltf::Accessor &accessor, size_t index) {
  if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
      accessor.type != TINYGLTF_TYPE_VEC2) {
    throw std::runtime_error("glTF accessor must be FLOAT VEC2");
  }

  const auto *data = reinterpret_cast<const float *>(
      accessorElementData(model, accessor, index));
  return {data[0], data[1]};
}

glm::vec3 readVec3(const tinygltf::Model &model,
                   const tinygltf::Accessor &accessor, size_t index) {
  if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
      accessor.type != TINYGLTF_TYPE_VEC3) {
    throw std::runtime_error("glTF accessor must be FLOAT VEC3");
  }

  const auto *data = reinterpret_cast<const float *>(
      accessorElementData(model, accessor, index));
  return {data[0], data[1], data[2]};
}

glm::vec4 readVec4(const tinygltf::Model &model,
                   const tinygltf::Accessor &accessor, size_t index) {
  if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
      accessor.type != TINYGLTF_TYPE_VEC4) {
    throw std::runtime_error("glTF accessor must be FLOAT VEC4");
  }

  const auto *data = reinterpret_cast<const float *>(
      accessorElementData(model, accessor, index));
  return {data[0], data[1], data[2], data[3]};
}

glm::mat4 readMat4(const tinygltf::Model &model,
                   const tinygltf::Accessor &accessor, size_t index) {
  if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
      accessor.type != TINYGLTF_TYPE_MAT4) {
    throw std::runtime_error("glTF accessor must be FLOAT MAT4");
  }

  const auto *data = reinterpret_cast<const float *>(
      accessorElementData(model, accessor, index));
  glm::mat4 matrix(1.0f);
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      matrix[column][row] = data[column * 4 + row];
    }
  }
  return matrix;
}

float readScalarFloat(const tinygltf::Model &model,
                      const tinygltf::Accessor &accessor, size_t index) {
  if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
      accessor.type != TINYGLTF_TYPE_SCALAR) {
    throw std::runtime_error("glTF accessor must be FLOAT SCALAR");
  }

  const auto *data = reinterpret_cast<const float *>(
      accessorElementData(model, accessor, index));
  return data[0];
}

uint32_t readIndex(const tinygltf::Model &model,
                   const tinygltf::Accessor &accessor, size_t index) {
  if (accessor.type != TINYGLTF_TYPE_SCALAR) {
    throw std::runtime_error("glTF indices accessor must be SCALAR");
  }

  const unsigned char *data = accessorElementData(model, accessor, index);

  switch (accessor.componentType) {
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
    return *reinterpret_cast<const uint8_t *>(data);
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
    return *reinterpret_cast<const uint16_t *>(data);
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    return *reinterpret_cast<const uint32_t *>(data);
  default:
    throw std::runtime_error("unsupported glTF index component type");
  }
}

uint32_t readUnsignedComponent(const unsigned char *data, int componentType,
                               uint32_t componentIndex) {
  switch (componentType) {
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
    return reinterpret_cast<const uint8_t *>(data)[componentIndex];
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
    return reinterpret_cast<const uint16_t *>(data)[componentIndex];
  default:
    throw std::runtime_error("unsupported unsigned glTF component type");
  }
}

float normalizedComponentScale(int componentType) {
  switch (componentType) {
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
    return 255.0f;
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
    return 65535.0f;
  default:
    throw std::runtime_error("unsupported normalized glTF component type");
  }
}

glm::uvec4 readJointIndices(const tinygltf::Model &model,
                            const tinygltf::Accessor &accessor, size_t index) {
  if (accessor.type != TINYGLTF_TYPE_VEC4) {
    throw std::runtime_error("glTF JOINTS_0 accessor must be VEC4");
  }
  if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
      accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
    throw std::runtime_error(
        "glTF JOINTS_0 accessor must use unsigned byte or unsigned short");
  }

  const unsigned char *data = accessorElementData(model, accessor, index);
  return {
      readUnsignedComponent(data, accessor.componentType, 0),
      readUnsignedComponent(data, accessor.componentType, 1),
      readUnsignedComponent(data, accessor.componentType, 2),
      readUnsignedComponent(data, accessor.componentType, 3),
  };
}

glm::vec4 readJointWeights(const tinygltf::Model &model,
                           const tinygltf::Accessor &accessor, size_t index) {
  if (accessor.type != TINYGLTF_TYPE_VEC4) {
    throw std::runtime_error("glTF WEIGHTS_0 accessor must be VEC4");
  }

  glm::vec4 weights(0.0f);
  const unsigned char *data = accessorElementData(model, accessor, index);

  if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
    const auto *values = reinterpret_cast<const float *>(data);
    weights = {values[0], values[1], values[2], values[3]};
  } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
             accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
    const float normalizedScale =
        accessor.normalized ? normalizedComponentScale(accessor.componentType)
                            : 1.0f;
    for (uint32_t componentIndex = 0; componentIndex < 4; ++componentIndex) {
      weights[componentIndex] =
          static_cast<float>(readUnsignedComponent(data, accessor.componentType,
                                                   componentIndex)) /
          normalizedScale;
    }
  } else {
    throw std::runtime_error(
        "glTF WEIGHTS_0 accessor must use float or normalized unsigned values");
  }

  const float sum = weights.x + weights.y + weights.z + weights.w;
  if (sum <= 1e-6f) {
    return {1.0f, 0.0f, 0.0f, 0.0f};
  }

  return weights / sum;
}

glm::quat normalizedQuatFromVec4(const glm::vec4 &value) {
  glm::quat rotation(value.w, value.x, value.y, value.z);
  if (glm::length(rotation) < 1e-6f) {
    return glm::identity<glm::quat>();
  }
  return glm::normalize(rotation);
}

std::vector<glm::mat4> readMat4Accessor(const tinygltf::Model &model,
                                        const tinygltf::Accessor &accessor) {
  std::vector<glm::mat4> matrices;
  matrices.reserve(accessor.count);

  for (size_t index = 0; index < accessor.count; ++index) {
    matrices.push_back(readMat4(model, accessor, index));
  }

  return matrices;
}

std::vector<float> readFloatAccessor(const tinygltf::Model &model,
                                     const tinygltf::Accessor &accessor) {
  std::vector<float> values;
  values.reserve(accessor.count);

  for (size_t index = 0; index < accessor.count; ++index) {
    values.push_back(readScalarFloat(model, accessor, index));
  }

  return values;
}

std::vector<SkinData> buildSkins(const tinygltf::Model &model) {
  std::vector<SkinData> skins;
  skins.reserve(model.skins.size());

  for (size_t skinIndex = 0; skinIndex < model.skins.size(); ++skinIndex) {
    const auto &sourceSkin = model.skins[skinIndex];
    SkinData skin;
    skin.name = sourceSkin.name.empty() ? "Skin " + std::to_string(skinIndex)
                                        : sourceSkin.name;
    skin.skeletonRootNodeIndex = sourceSkin.skeleton;
    skin.jointNodeIndices.assign(sourceSkin.joints.begin(),
                                 sourceSkin.joints.end());

    if (sourceSkin.inverseBindMatrices >= 0) {
      skin.inverseBindMatrices = readMat4Accessor(
          model, accessorAt(model, sourceSkin.inverseBindMatrices));
    } else {
      skin.inverseBindMatrices.resize(skin.jointNodeIndices.size(),
                                      glm::mat4(1.0f));
    }

    if (skin.jointNodeIndices.size() != skin.inverseBindMatrices.size()) {
      throw std::runtime_error(
          "glTF skin joint count does not match inverse bind matrix count");
    }

    skins.push_back(std::move(skin));
  }

  return skins;
}

AnimationInterpolation mapInterpolation(const std::string &interpolation) {
  if (interpolation.empty() || interpolation == "LINEAR") {
    return AnimationInterpolation::Linear;
  }
  if (interpolation == "STEP") {
    return AnimationInterpolation::Step;
  }
  if (interpolation == "CUBICSPLINE") {
    return AnimationInterpolation::CubicSpline;
  }

  throw std::runtime_error("unsupported glTF animation interpolation: " +
                           interpolation);
}

AnimationTargetPath mapTargetPath(const std::string &targetPath) {
  if (targetPath == "translation") {
    return AnimationTargetPath::Translation;
  }
  if (targetPath == "rotation") {
    return AnimationTargetPath::Rotation;
  }
  if (targetPath == "scale") {
    return AnimationTargetPath::Scale;
  }

  throw std::runtime_error("unsupported glTF animation target path: " +
                           targetPath);
}

std::vector<AnimationClipData> buildAnimations(const tinygltf::Model &model) {
  std::vector<AnimationClipData> animations;
  animations.reserve(model.animations.size());

  for (size_t animationIndex = 0; animationIndex < model.animations.size();
       ++animationIndex) {
    const auto &sourceAnimation = model.animations[animationIndex];
    AnimationClipData clip;
    clip.name = sourceAnimation.name.empty()
                    ? "Animation " + std::to_string(animationIndex)
                    : sourceAnimation.name;

    for (const auto &channel : sourceAnimation.channels) {
      if (channel.target_path == "weights") {
        continue;
      }

      if (channel.target_node < 0 ||
          static_cast<size_t>(channel.target_node) >= model.nodes.size()) {
        throw std::runtime_error(
            "glTF animation channel target node is invalid");
      }
      if (channel.sampler < 0 || static_cast<size_t>(channel.sampler) >=
                                     sourceAnimation.samplers.size()) {
        throw std::runtime_error("glTF animation channel sampler is invalid");
      }

      const auto &sampler =
          sourceAnimation.samplers[static_cast<size_t>(channel.sampler)];
      const auto &inputAccessor = accessorAt(model, sampler.input);
      const auto &outputAccessor = accessorAt(model, sampler.output);

      NodeAnimationTrack track;
      track.targetNodeIndex = channel.target_node;
      track.targetPath = mapTargetPath(channel.target_path);
      track.interpolation = mapInterpolation(sampler.interpolation);
      track.timesSeconds = readFloatAccessor(model, inputAccessor);

      if (track.timesSeconds.empty()) {
        throw std::runtime_error("glTF animation track has no keyframes");
      }

      const size_t keyframeCount = track.timesSeconds.size();
      const size_t expectedOutputCount =
          track.interpolation == AnimationInterpolation::CubicSpline
              ? keyframeCount * 3
              : keyframeCount;
      if (outputAccessor.count != expectedOutputCount) {
        throw std::runtime_error(
            "glTF animation sampler output count does not match keyframes");
      }

      if (track.targetPath == AnimationTargetPath::Rotation) {
        if (track.interpolation == AnimationInterpolation::CubicSpline) {
          track.quatInTangents.reserve(keyframeCount);
          track.quatValues.reserve(keyframeCount);
          track.quatOutTangents.reserve(keyframeCount);
          for (size_t keyframeIndex = 0; keyframeIndex < keyframeCount;
               ++keyframeIndex) {
            const size_t baseIndex = keyframeIndex * 3;
            track.quatInTangents.push_back(
                readVec4(model, outputAccessor, baseIndex));
            track.quatValues.push_back(normalizedQuatFromVec4(
                readVec4(model, outputAccessor, baseIndex + 1)));
            track.quatOutTangents.push_back(
                readVec4(model, outputAccessor, baseIndex + 2));
          }
        } else {
          track.quatValues.reserve(keyframeCount);
          for (size_t keyframeIndex = 0; keyframeIndex < keyframeCount;
               ++keyframeIndex) {
            track.quatValues.push_back(normalizedQuatFromVec4(
                readVec4(model, outputAccessor, keyframeIndex)));
          }
        }
      } else {
        if (track.interpolation == AnimationInterpolation::CubicSpline) {
          track.vec3InTangents.reserve(keyframeCount);
          track.vec3Values.reserve(keyframeCount);
          track.vec3OutTangents.reserve(keyframeCount);
          for (size_t keyframeIndex = 0; keyframeIndex < keyframeCount;
               ++keyframeIndex) {
            const size_t baseIndex = keyframeIndex * 3;
            track.vec3InTangents.push_back(
                readVec3(model, outputAccessor, baseIndex));
            track.vec3Values.push_back(
                readVec3(model, outputAccessor, baseIndex + 1));
            track.vec3OutTangents.push_back(
                readVec3(model, outputAccessor, baseIndex + 2));
          }
        } else {
          track.vec3Values.reserve(keyframeCount);
          for (size_t keyframeIndex = 0; keyframeIndex < keyframeCount;
               ++keyframeIndex) {
            track.vec3Values.push_back(
                readVec3(model, outputAccessor, keyframeIndex));
          }
        }
      }

      clip.durationSeconds =
          std::max(clip.durationSeconds, track.timesSeconds.back());
      clip.tracks.push_back(std::move(track));
    }

    animations.push_back(std::move(clip));
  }

  return animations;
}

void validateImportedSkeletonData(const SkeletonAssetData &data) {
  for (size_t nodeIndex = 0; nodeIndex < data.nodes.size(); ++nodeIndex) {
    const auto &node = data.nodes[nodeIndex];

    if (node.parentIndex >= 0 &&
        static_cast<size_t>(node.parentIndex) >= data.nodes.size()) {
      throw std::runtime_error("skeleton node parent index is out of range");
    }

    for (const int childIndex : node.childIndices) {
      if (childIndex < 0 ||
          static_cast<size_t>(childIndex) >= data.nodes.size()) {
        throw std::runtime_error("skeleton node child index is out of range");
      }
      if (data.nodes[static_cast<size_t>(childIndex)].parentIndex !=
          static_cast<int>(nodeIndex)) {
        throw std::runtime_error("skeleton node hierarchy parent-child "
                                 "relationship is inconsistent");
      }
    }
  }

  for (const int rootIndex : data.sceneRootNodeIndices) {
    if (rootIndex < 0 || static_cast<size_t>(rootIndex) >= data.nodes.size()) {
      throw std::runtime_error("scene root node index is out of range");
    }
    if (data.nodes[static_cast<size_t>(rootIndex)].parentIndex != -1) {
      throw std::runtime_error("scene root node must not have a parent");
    }
  }

  for (const auto &skin : data.skins) {
    if (skin.skeletonRootNodeIndex >= 0 &&
        static_cast<size_t>(skin.skeletonRootNodeIndex) >= data.nodes.size()) {
      throw std::runtime_error("skin skeleton root node index is out of range");
    }
    if (skin.jointNodeIndices.size() != skin.inverseBindMatrices.size()) {
      throw std::runtime_error(
          "skin joint count does not match inverse bind matrix count");
    }
    for (const int jointNodeIndex : skin.jointNodeIndices) {
      if (jointNodeIndex < 0 ||
          static_cast<size_t>(jointNodeIndex) >= data.nodes.size()) {
        throw std::runtime_error("skin joint node index is out of range");
      }
    }
  }

  for (const auto &animation : data.animations) {
    if (!std::isfinite(animation.durationSeconds) ||
        animation.durationSeconds < 0.0f) {
      throw std::runtime_error("animation duration is invalid");
    }

    for (const auto &track : animation.tracks) {
      if (track.targetNodeIndex < 0 ||
          static_cast<size_t>(track.targetNodeIndex) >= data.nodes.size()) {
        throw std::runtime_error("animation track target node is out of range");
      }
      if (track.timesSeconds.empty()) {
        throw std::runtime_error("animation track has no keyframes");
      }
      for (size_t keyframeIndex = 1; keyframeIndex < track.timesSeconds.size();
           ++keyframeIndex) {
        if (track.timesSeconds[keyframeIndex] <
            track.timesSeconds[keyframeIndex - 1]) {
          throw std::runtime_error("animation track keyframe times must be "
                                   "monotonically increasing");
        }
      }

      if (track.targetPath == AnimationTargetPath::Rotation) {
        if (track.quatValues.size() != track.timesSeconds.size()) {
          throw std::runtime_error(
              "rotation track keyframe count does not match keyframe times");
        }
        if (track.interpolation == AnimationInterpolation::CubicSpline &&
            (track.quatInTangents.size() != track.timesSeconds.size() ||
             track.quatOutTangents.size() != track.timesSeconds.size())) {
          throw std::runtime_error(
              "rotation cubic spline tangents do not match keyframe times");
        }
      } else {
        if (track.vec3Values.size() != track.timesSeconds.size()) {
          throw std::runtime_error(
              "vec3 track keyframe count does not match keyframe times");
        }
        if (track.interpolation == AnimationInterpolation::CubicSpline &&
            (track.vec3InTangents.size() != track.timesSeconds.size() ||
             track.vec3OutTangents.size() != track.timesSeconds.size())) {
          throw std::runtime_error(
              "vec3 cubic spline tangents do not match keyframe times");
        }
      }
    }
  }
}

std::string primitiveName(const tinygltf::Node &node,
                          const tinygltf::Mesh &mesh, size_t primitiveIndex) {
  if (!node.name.empty()) {
    return node.name + "_prim_" + std::to_string(primitiveIndex);
  }
  if (!mesh.name.empty()) {
    return mesh.name + "_prim_" + std::to_string(primitiveIndex);
  }
  return "primitive_" + std::to_string(primitiveIndex);
}

} // namespace

void GltfModelAsset::load(const std::string &path) {
  sourcePath = path;
  skeletonData = {};

  tinygltf::TinyGLTF loader;
  tinygltf::Model model;
  std::string warn;
  std::string err;

  const std::filesystem::path gltfPath(path);
  const std::string extension = gltfPath.extension().string();
  const bool isBinary = extension == ".glb";

  bool loaded = false;
  if (isBinary) {
    loaded = loader.LoadBinaryFromFile(&model, &err, &warn, path);
  } else {
    loaded = loader.LoadASCIIFromFile(&model, &err, &warn, path);
  }

  if (!loaded) {
    throw std::runtime_error("failed to load glTF: " + warn + err);
  }

  skeletonData.nodes = buildSkeletonNodes(model);
  skeletonData.sceneRootNodeIndices = collectSceneRootNodeIndices(model);
  skeletonData.skins = buildSkins(model);
  skeletonData.animations = buildAnimations(model);
  validateImportedSkeletonData(skeletonData);

  std::vector<GeometryVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<ModelSubmesh> submeshes;
  std::unordered_map<GeometryVertex, uint32_t> uniqueVertices;
  std::vector<ModelMaterialData> materials =
      buildGltfMaterials(model, gltfPath);

  auto processPrimitive = [&](const tinygltf::Node &node, int nodeIndex,
                              const tinygltf::Mesh &mesh,
                              const tinygltf::Primitive &primitive,
                              size_t primitiveIndex,
                              const glm::mat4 &worldTransform) {
    const int primitiveMode =
        primitive.mode == -1 ? TINYGLTF_MODE_TRIANGLES : primitive.mode;
    if (primitiveMode != TINYGLTF_MODE_TRIANGLES) {
      throw std::runtime_error(
          "only TRIANGLES glTF primitives are currently supported");
    }

    const auto positionIt = primitive.attributes.find("POSITION");
    if (positionIt == primitive.attributes.end()) {
      throw std::runtime_error("glTF primitive is missing POSITION");
    }

    const auto &positionAccessor = accessorAt(model, positionIt->second);
    const tinygltf::Accessor *normalAccessor = nullptr;
    const tinygltf::Accessor *uvAccessor = nullptr;
    const tinygltf::Accessor *jointAccessor = nullptr;
    const tinygltf::Accessor *weightAccessor = nullptr;

    if (const auto normalIt = primitive.attributes.find("NORMAL");
        normalIt != primitive.attributes.end()) {
      normalAccessor = &accessorAt(model, normalIt->second);
    }
    if (const auto uvIt = primitive.attributes.find("TEXCOORD_0");
        uvIt != primitive.attributes.end()) {
      uvAccessor = &accessorAt(model, uvIt->second);
    }
    if (const auto jointIt = primitive.attributes.find("JOINTS_0");
        jointIt != primitive.attributes.end()) {
      jointAccessor = &accessorAt(model, jointIt->second);
    }
    if (const auto weightIt = primitive.attributes.find("WEIGHTS_0");
        weightIt != primitive.attributes.end()) {
      weightAccessor = &accessorAt(model, weightIt->second);
    }

    const int skinIndex = node.skin;
    if (skinIndex >= 0 &&
        static_cast<size_t>(skinIndex) >= skeletonData.skins.size()) {
      throw std::runtime_error("glTF node skin index is out of range");
    }
    if (skinIndex >= 0 &&
        (jointAccessor == nullptr || weightAccessor == nullptr)) {
      throw std::runtime_error(
          "skinned glTF primitive is missing JOINTS_0 or WEIGHTS_0");
    }

    std::vector<uint32_t> localToGlobal;
    localToGlobal.reserve(positionAccessor.count);

    for (size_t vertexIndex = 0; vertexIndex < positionAccessor.count;
         ++vertexIndex) {
      const glm::vec3 position = readVec3(model, positionAccessor, vertexIndex);
      glm::vec3 normal = normalAccessor == nullptr
                             ? glm::vec3(0.0f, 0.0f, 1.0f)
                             : readVec3(model, *normalAccessor, vertexIndex);
      if (glm::length(normal) < 0.0001f) {
        normal = {0.0f, 0.0f, 1.0f};
      } else {
        normal = glm::normalize(normal);
      }

      const glm::vec2 uv = uvAccessor == nullptr
                               ? glm::vec2(0.0f)
                               : readVec2(model, *uvAccessor, vertexIndex);
      const glm::uvec4 jointIndices =
          jointAccessor == nullptr
              ? glm::uvec4(0, 0, 0, 0)
              : readJointIndices(model, *jointAccessor, vertexIndex);
      const glm::vec4 jointWeights =
          weightAccessor == nullptr
              ? glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)
              : readJointWeights(model, *weightAccessor, vertexIndex);
      if (skinIndex >= 0) {
        const auto &skin = skeletonData.skins[static_cast<size_t>(skinIndex)];
        for (uint32_t jointSlot = 0; jointSlot < 4; ++jointSlot) {
          if (jointWeights[jointSlot] <= 0.0f) {
            continue;
          }
          if (jointIndices[jointSlot] >= skin.jointNodeIndices.size()) {
            throw std::runtime_error(
                "glTF vertex joint index is out of range for the bound skin");
          }
        }
      }

      GeometryVertex vertex{
          .pos = position,
          .normal = normal,
          .texCoord = uv,
          .jointIndices = jointIndices,
          .jointWeights = jointWeights,
      };

      const auto [it, inserted] = uniqueVertices.try_emplace(
          vertex, static_cast<uint32_t>(vertices.size()));
      if (inserted) {
        vertices.push_back(vertex);
      }
      localToGlobal.push_back(it->second);
    }

    ModelSubmesh submesh{
        .name = primitiveName(node, mesh, primitiveIndex),
        .indexOffset = static_cast<uint32_t>(indices.size()),
        .indexCount = 0,
        .materialIndex = primitive.material >= 0 ? primitive.material : -1,
        .shapeIndex = nodeIndex >= 0 ? static_cast<uint32_t>(nodeIndex) : 0u,
        .transform = worldTransform,
        .nodeIndex = nodeIndex,
        .skinIndex = skinIndex,
    };

    if (primitive.indices >= 0) {
      const auto &indexAccessor = accessorAt(model, primitive.indices);
      for (size_t index = 0; index < indexAccessor.count; ++index) {
        const uint32_t localIndex = readIndex(model, indexAccessor, index);
        if (localIndex >= localToGlobal.size()) {
          throw std::runtime_error("glTF primitive index is out of range");
        }
        indices.push_back(localToGlobal[localIndex]);
        submesh.indexCount++;
      }
    } else {
      for (const uint32_t globalIndex : localToGlobal) {
        indices.push_back(globalIndex);
        submesh.indexCount++;
      }
    }

    if (submesh.indexCount > 0) {
      submeshes.push_back(std::move(submesh));
    }
  };

  const std::vector<IndexedNode> nodes = collectSceneNodes(model);
  for (const auto &indexedNode : nodes) {
    const auto &node = model.nodes[static_cast<size_t>(indexedNode.nodeIndex)];
    if (node.mesh < 0 ||
        static_cast<size_t>(node.mesh) >= model.meshes.size()) {
      continue;
    }

    const auto &mesh = model.meshes[static_cast<size_t>(node.mesh)];
    for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size();
         ++primitiveIndex) {
      processPrimitive(node, indexedNode.nodeIndex, mesh,
                       mesh.primitives[primitiveIndex], primitiveIndex,
                       indexedNode.worldTransform);
    }
  }

  if (submeshes.empty() && !model.meshes.empty()) {
    for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
      const auto &mesh = model.meshes[meshIndex];
      tinygltf::Node syntheticNode;
      syntheticNode.name = mesh.name;
      for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size();
           ++primitiveIndex) {
        processPrimitive(syntheticNode, -1, mesh,
                         mesh.primitives[primitiveIndex], primitiveIndex,
                         glm::mat4(1.0f));
      }
    }
  }

  geometryMesh.setImportedGeometry(std::move(vertices), std::move(indices),
                                   std::move(submeshes), std::move(materials));
}
