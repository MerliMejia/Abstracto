#include "GltfModelAsset.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/hash.hpp>
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

glm::mat4 nodeLocalTransform(const tinygltf::Node &node) {
  if (node.matrix.size() == 16) {
    glm::mat4 matrix(1.0f);
    for (int column = 0; column < 4; ++column) {
      for (int row = 0; row < 4; ++row) {
        matrix[column][row] = static_cast<float>(node.matrix[column * 4 + row]);
      }
    }
    return matrix;
  }

  glm::vec3 translation(0.0f);
  if (node.translation.size() == 3) {
    translation = {
        static_cast<float>(node.translation[0]),
        static_cast<float>(node.translation[1]),
        static_cast<float>(node.translation[2]),
    };
  }

  glm::quat rotation = glm::identity<glm::quat>();
  if (node.rotation.size() == 4) {
    rotation = glm::quat(static_cast<float>(node.rotation[3]),
                         static_cast<float>(node.rotation[0]),
                         static_cast<float>(node.rotation[1]),
                         static_cast<float>(node.rotation[2]));
  }

  glm::vec3 scale(1.0f);
  if (node.scale.size() == 3) {
    scale = {
        static_cast<float>(node.scale[0]),
        static_cast<float>(node.scale[1]),
        static_cast<float>(node.scale[2]),
    };
  }

  return glm::translate(glm::mat4(1.0f), translation) *
         glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
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

  for (size_t sceneIndex = 0; sceneIndex < model.scenes.size(); ++sceneIndex) {
    for (const int nodeIndex : model.scenes[sceneIndex].nodes) {
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

    if (material.emissiveFactor.size() == 3) {
      materialData.emissiveFactor = {
          static_cast<float>(material.emissiveFactor[0]),
          static_cast<float>(material.emissiveFactor[1]),
          static_cast<float>(material.emissiveFactor[2]),
      };
    }

    const int textureIndex =
        material.pbrMetallicRoughness.baseColorTexture.index;
    if (textureIndex >= 0 &&
        static_cast<size_t>(textureIndex) < model.textures.size()) {
      const auto &texture = model.textures[textureIndex];
      if (texture.source >= 0 &&
          static_cast<size_t>(texture.source) < model.images.size()) {
        const auto &image = model.images[texture.source];

        if (!image.uri.empty() && !isDataUri(image.uri)) {
          materialData.resolvedBaseColorTexturePath =
              resolveGltfAssetPath(gltfPath, image.uri);
        }

        if (materialData.resolvedBaseColorTexturePath.empty()) {
          materialData.baseColorTextureRgba = imageToRgba8(image);
          materialData.baseColorTextureWidth = image.width;
          materialData.baseColorTextureHeight = image.height;
        }
      }
    }

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

glm::vec3 readVec3(const tinygltf::Model &model,
                   const tinygltf::Accessor &accessor, size_t index) {
  if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
      accessor.type != TINYGLTF_TYPE_VEC3) {
    throw std::runtime_error("glTF accessor must be FLOAT VEC3");
  }

  const auto *data = reinterpret_cast<const float *>(
      accessorData(model, accessor) + accessorStride(model, accessor) * index);
  return {data[0], data[1], data[2]};
}

glm::vec2 readVec2(const tinygltf::Model &model,
                   const tinygltf::Accessor &accessor, size_t index) {
  if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
      accessor.type != TINYGLTF_TYPE_VEC2) {
    throw std::runtime_error("glTF accessor must be FLOAT VEC2");
  }

  const auto *data = reinterpret_cast<const float *>(
      accessorData(model, accessor) + accessorStride(model, accessor) * index);
  return {data[0], data[1]};
}

uint32_t readIndex(const tinygltf::Model &model,
                   const tinygltf::Accessor &accessor, size_t index) {
  if (accessor.type != TINYGLTF_TYPE_SCALAR) {
    throw std::runtime_error("glTF indices accessor must be SCALAR");
  }

  const unsigned char *data =
      accessorData(model, accessor) + accessorStride(model, accessor) * index;

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

    if (const auto normalIt = primitive.attributes.find("NORMAL");
        normalIt != primitive.attributes.end()) {
      normalAccessor = &accessorAt(model, normalIt->second);
    }
    if (const auto uvIt = primitive.attributes.find("TEXCOORD_0");
        uvIt != primitive.attributes.end()) {
      uvAccessor = &accessorAt(model, uvIt->second);
    }

    std::vector<uint32_t> localToGlobal;
    localToGlobal.reserve(positionAccessor.count);

    const glm::mat3 normalMatrix =
        glm::transpose(glm::inverse(glm::mat3(worldTransform)));

    for (size_t vertexIndex = 0; vertexIndex < positionAccessor.count;
         ++vertexIndex) {
      const glm::vec3 position = glm::vec3(
          worldTransform *
          glm::vec4(readVec3(model, positionAccessor, vertexIndex), 1.0f));
      glm::vec3 normal =
          normalAccessor == nullptr
              ? glm::vec3(0.0f, 0.0f, 1.0f)
              : normalMatrix * readVec3(model, *normalAccessor, vertexIndex);
      if (glm::length(normal) < 0.0001f) {
        normal = {0.0f, 0.0f, 1.0f};
      } else {
        normal = glm::normalize(normal);
      }

      const glm::vec2 uv = uvAccessor == nullptr
                               ? glm::vec2(0.0f)
                               : readVec2(model, *uvAccessor, vertexIndex);

      GeometryVertex vertex{
          .pos = position,
          .normal = normal,
          .texCoord = uv,
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
        .shapeIndex = static_cast<uint32_t>(nodeIndex),
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
        processPrimitive(syntheticNode, static_cast<int>(meshIndex), mesh,
                         mesh.primitives[primitiveIndex], primitiveIndex,
                         glm::mat4(1.0f));
      }
    }
  }

  geometryMesh.setImportedGeometry(std::move(vertices), std::move(indices),
                                   std::move(submeshes), std::move(materials));
}
