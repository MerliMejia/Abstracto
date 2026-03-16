#pragma once
#include "RenderUtils.h"
#include "vulkan/vulkan.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include <tiny_obj_loader.h>

struct Vertex {
  glm::vec3 pos;
  glm::vec3 color;
  glm::vec2 texCoord;

  static vk::VertexInputBindingDescription getBindingDescription() {
    return {0, sizeof(Vertex), vk::VertexInputRate::eVertex};
  }

  static std::array<vk::VertexInputAttributeDescription, 3>
  getAttributeDescriptions() {
    return {vk::VertexInputAttributeDescription(
                0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),
            vk::VertexInputAttributeDescription(
                1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)),
            vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat,
                                                offsetof(Vertex, texCoord))};
  }

  bool operator==(const Vertex &other) const {
    return pos == other.pos && color == other.color &&
           texCoord == other.texCoord;
  }
};

template <> struct std::hash<Vertex> {
  size_t operator()(Vertex const &vertex) const noexcept {
    return ((hash<glm::vec3>()(vertex.pos) ^
             (hash<glm::vec3>()(vertex.color) << 1)) >>
            1) ^
           (hash<glm::vec2>()(vertex.texCoord) << 1);
  }
};

struct ModelMaterialData {
  struct TextureSource {
    std::string resolvedPath;
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;

    bool hasPath() const { return !resolvedPath.empty(); }
    bool hasEmbeddedRgba() const {
      return !rgba.empty() && width > 0 && height > 0;
    }
  };

  std::string name;
  glm::vec4 baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
  TextureSource baseColorTexture;
  TextureSource metallicRoughnessTexture;
  TextureSource emissiveTexture;
  TextureSource occlusionTexture;
  float metallicFactor = 0.0f;
  float roughnessFactor = 1.0f;
  glm::vec3 emissiveFactor = {0.0f, 0.0f, 0.0f};
  float occlusionStrength = 1.0f;
  tinyobj::material_t raw{};
  bool hasObjMaterial = false;

  glm::vec4 baseColorRgba() const { return baseColorFactor; }

  bool hasBaseColorTexture() const {
    return baseColorTexture.hasPath() || hasEmbeddedBaseColorTexture();
  }

  bool hasEmbeddedBaseColorTexture() const {
    return baseColorTexture.hasEmbeddedRgba();
  }

  bool hasBaseColorTexturePath() const {
    return baseColorTexture.hasPath();
  }

  glm::vec4 diffuseRgba() const { return baseColorRgba(); }
  bool hasDiffuseTexture() const { return hasBaseColorTexture(); }
};

using ObjMaterialData = ModelMaterialData;

struct ModelSubmesh {
  std::string name;
  uint32_t indexOffset = 0;
  uint32_t indexCount = 0;
  int materialIndex = -1;
  uint32_t shapeIndex = 0;
};

using ObjSubmesh = ModelSubmesh;

class Mesh {
public:
  virtual ~Mesh() = default;
  Mesh() = default;
  Mesh(const Mesh &) = delete;
  Mesh &operator=(const Mesh &) = delete;
  Mesh(Mesh &&) noexcept = default;
  Mesh &operator=(Mesh &&) noexcept = default;

  void createVertexBuffer(CommandContext &commandContext,
                          DeviceContext &deviceContext) {
    if (vertexBytes.empty()) {
      throw std::runtime_error(
          "cannot create a vertex buffer with no vertices");
    }

    vk::DeviceSize bufferSize = vertexBytes.size();
    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingBufferMemory({});
    RenderUtils::createBuffer(deviceContext, bufferSize,
                              vk::BufferUsageFlagBits::eTransferSrc,
                              vk::MemoryPropertyFlagBits::eHostVisible |
                                  vk::MemoryPropertyFlagBits::eHostCoherent,
                              stagingBuffer, stagingBufferMemory);

    void *dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    std::memcpy(dataStaging, vertexBytes.data(), bufferSize);
    stagingBufferMemory.unmapMemory();

    RenderUtils::createBuffer(deviceContext, bufferSize,
                              vk::BufferUsageFlagBits::eTransferDst |
                                  vk::BufferUsageFlagBits::eVertexBuffer,
                              vk::MemoryPropertyFlagBits::eDeviceLocal,
                              vertexBuffer, vertexBufferMemory);

    RenderUtils::copyBuffer(commandContext, deviceContext, stagingBuffer,
                            vertexBuffer, bufferSize);
  }

  void createIndexBuffer(CommandContext &commandContext,
                         DeviceContext &deviceContext) {
    if (indices.empty()) {
      throw std::runtime_error("cannot create an index buffer with no indices");
    }

    vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingBufferMemory({});
    RenderUtils::createBuffer(deviceContext, bufferSize,
                              vk::BufferUsageFlagBits::eTransferSrc,
                              vk::MemoryPropertyFlagBits::eHostVisible |
                                  vk::MemoryPropertyFlagBits::eHostCoherent,
                              stagingBuffer, stagingBufferMemory);

    void *data = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(data, indices.data(), bufferSize);
    stagingBufferMemory.unmapMemory();

    RenderUtils::createBuffer(deviceContext, bufferSize,
                              vk::BufferUsageFlagBits::eTransferDst |
                                  vk::BufferUsageFlagBits::eIndexBuffer,
                              vk::MemoryPropertyFlagBits::eDeviceLocal,
                              indexBuffer, indexBufferMemory);

    RenderUtils::copyBuffer(commandContext, deviceContext, stagingBuffer,
                            indexBuffer, bufferSize);
  }

  vk::raii::Buffer &getVertexBuffer() { return vertexBuffer; }
  vk::raii::Buffer &getIndexBuffer() { return indexBuffer; }
  std::vector<uint32_t> &getIndices() { return indices; }
  const std::vector<uint32_t> &getIndices() const { return indices; }
  const std::vector<ModelMaterialData> &getMaterials() const {
    return materials;
  }
  std::vector<ModelMaterialData> &mutableMaterials() { return materials; }
  const std::vector<ModelSubmesh> &getSubmeshes() const { return submeshes; }
  size_t vertexCount() const { return vertexCountValue; }
  size_t vertexStride() const { return vertexStrideValue; }

protected:
  template <typename TVertex>
  void setTypedGeometry(std::vector<TVertex> meshVertices,
                        std::vector<uint32_t> meshIndices) {
    static_assert(std::is_trivially_copyable_v<TVertex>,
                  "Mesh vertex types must be trivially copyable");

    vertexStrideValue = sizeof(TVertex);
    vertexCountValue = meshVertices.size();
    vertexBytes.resize(vertexStrideValue * vertexCountValue);
    if (!meshVertices.empty()) {
      std::memcpy(vertexBytes.data(), meshVertices.data(), vertexBytes.size());
    }
    indices = std::move(meshIndices);
  }

  void clearGeometry() {
    vertexBytes.clear();
    indices.clear();
    materials.clear();
    submeshes.clear();
    vertexStrideValue = 0;
    vertexCountValue = 0;
  }

  void setModelMetadata(std::vector<ModelSubmesh> meshSubmeshes,
                        std::vector<ModelMaterialData> meshMaterials) {
    submeshes = std::move(meshSubmeshes);
    materials = std::move(meshMaterials);
  }

private:
  std::vector<std::byte> vertexBytes;
  std::vector<uint32_t> indices;
  std::vector<ModelMaterialData> materials;
  std::vector<ModelSubmesh> submeshes;
  size_t vertexStrideValue = 0;
  size_t vertexCountValue = 0;
  vk::raii::Buffer vertexBuffer = nullptr;
  vk::raii::DeviceMemory vertexBufferMemory = nullptr;
  vk::raii::Buffer indexBuffer = nullptr;
  vk::raii::DeviceMemory indexBufferMemory = nullptr;
};

template <typename TVertex> class TypedMesh : public Mesh {
public:
  using VertexType = TVertex;
  TypedMesh() = default;
  TypedMesh(const TypedMesh &) = delete;
  TypedMesh &operator=(const TypedMesh &) = delete;
  TypedMesh(TypedMesh &&) noexcept = default;
  TypedMesh &operator=(TypedMesh &&) noexcept = default;

  void setGeometry(std::vector<TVertex> meshVertices,
                   std::vector<uint32_t> meshIndices) {
    vertices = std::move(meshVertices);
    Mesh::setTypedGeometry(vertices, std::move(meshIndices));
  }

  const std::vector<TVertex> &vertexData() const { return vertices; }

protected:
  std::vector<TVertex> &mutableVertexData() { return vertices; }

private:
  std::vector<TVertex> vertices;
};

struct ObjData {
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
};

inline std::string resolveObjAssetPath(const std::filesystem::path &objPath,
                                       const std::string &assetPath) {
  if (assetPath.empty()) {
    return {};
  }

  const std::filesystem::path path(assetPath);
  if (path.is_absolute()) {
    return path.lexically_normal().string();
  }

  return (objPath.parent_path() / path).lexically_normal().string();
}

inline std::vector<ModelMaterialData>
buildObjMaterials(const ObjData &objData,
                  const std::filesystem::path &objPath) {
  std::vector<ModelMaterialData> materials;
  materials.reserve(objData.materials.size());

  for (const auto &material : objData.materials) {
    materials.push_back(ModelMaterialData{
        .name = material.name,
        .baseColorFactor = {material.diffuse[0], material.diffuse[1],
                            material.diffuse[2], material.dissolve},
        .baseColorTexture =
            {.resolvedPath =
                 resolveObjAssetPath(objPath, material.diffuse_texname)},
        .metallicFactor = 0.0f,
        .roughnessFactor = 1.0f,
        .emissiveFactor = {material.emission[0], material.emission[1],
                           material.emission[2]},
        .raw = material,
        .hasObjMaterial = true,
    });
  }

  return materials;
}

inline std::string buildSubmeshName(const tinyobj::shape_t &shape,
                                    size_t shapeIndex, size_t partIndex) {
  const std::string baseName =
      shape.name.empty() ? "shape_" + std::to_string(shapeIndex) : shape.name;
  if (partIndex == 0) {
    return baseName;
  }

  return baseName + "_part_" + std::to_string(partIndex);
}

class ObjVertexRef {
public:
  ObjVertexRef(const tinyobj::attrib_t &attribData, tinyobj::index_t objIndex)
      : attrib(&attribData), index(objIndex) {}

  glm::vec3 position() const {
    if (index.vertex_index < 0) {
      throw std::runtime_error("OBJ vertex is missing a position index");
    }

    return {attrib->vertices[3 * index.vertex_index + 0],
            attrib->vertices[3 * index.vertex_index + 1],
            attrib->vertices[3 * index.vertex_index + 2]};
  }

  glm::vec2 texCoord() const {
    if (!hasTexCoord()) {
      return {0.0f, 0.0f};
    }

    return {attrib->texcoords[2 * index.texcoord_index + 0],
            1.0f - attrib->texcoords[2 * index.texcoord_index + 1]};
  }

  glm::vec3 normal() const {
    if (!hasNormal()) {
      return {0.0f, 0.0f, 0.0f};
    }

    return {attrib->normals[3 * index.normal_index + 0],
            attrib->normals[3 * index.normal_index + 1],
            attrib->normals[3 * index.normal_index + 2]};
  }

  bool hasTexCoord() const {
    return index.texcoord_index >= 0 &&
           (2 * index.texcoord_index + 1) <
               static_cast<int>(attrib->texcoords.size());
  }

  bool hasNormal() const {
    return index.normal_index >= 0 &&
           (3 * index.normal_index + 2) <
               static_cast<int>(attrib->normals.size());
  }

private:
  const tinyobj::attrib_t *attrib = nullptr;
  tinyobj::index_t index{};
};

inline ObjData loadObjData(const std::string &path) {
  ObjData data;
  std::string warn, err;
  std::filesystem::path objPath(path);
  std::string basePath = objPath.parent_path().string();
  if (!basePath.empty() && basePath.back() != '/' && basePath.back() != '\\') {
    basePath.push_back(std::filesystem::path::preferred_separator);
  }

  if (!LoadObj(&data.attrib, &data.shapes, &data.materials, &warn, &err,
               path.c_str(), basePath.empty() ? nullptr : basePath.c_str())) {
    throw std::runtime_error(warn + err);
  }

  return data;
}

template <typename TVertex> struct BuiltObjMeshData {
  std::vector<TVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<ModelSubmesh> submeshes;
  std::vector<ModelMaterialData> materials;
};

template <typename TVertex, typename TVertexFactory>
BuiltObjMeshData<TVertex> buildMeshFromObj(const ObjData &objData,
                                           const std::filesystem::path &objPath,
                                           TVertexFactory &&vertexFactory) {
  BuiltObjMeshData<TVertex> meshData;
  meshData.materials = buildObjMaterials(objData, objPath);
  std::unordered_map<TVertex, uint32_t> uniqueVertices;

  for (size_t shapeIndex = 0; shapeIndex < objData.shapes.size();
       ++shapeIndex) {
    const auto &shape = objData.shapes[shapeIndex];

    if (shape.mesh.num_face_vertices.empty()) {
      ObjSubmesh submesh{.name = buildSubmeshName(shape, shapeIndex, 0),
                         .indexOffset =
                             static_cast<uint32_t>(meshData.indices.size()),
                         .indexCount = 0,
                         .materialIndex = shape.mesh.material_ids.empty()
                                              ? -1
                                              : shape.mesh.material_ids.front(),
                         .shapeIndex = static_cast<uint32_t>(shapeIndex)};

      for (const auto &index : shape.mesh.indices) {
        ObjVertexRef objVertex(objData.attrib, index);
        TVertex vertex = vertexFactory(objVertex);
        auto [it, inserted] = uniqueVertices.try_emplace(
            vertex, static_cast<uint32_t>(meshData.vertices.size()));
        if (inserted) {
          meshData.vertices.push_back(vertex);
        }
        meshData.indices.push_back(it->second);
        submesh.indexCount++;
      }

      if (submesh.indexCount > 0) {
        meshData.submeshes.push_back(std::move(submesh));
      }
      continue;
    }

    size_t runningIndex = 0;
    size_t partIndex = 0;
    ObjSubmesh *currentSubmesh = nullptr;

    for (size_t faceIndex = 0; faceIndex < shape.mesh.num_face_vertices.size();
         ++faceIndex) {
      const uint32_t faceVertexCount = shape.mesh.num_face_vertices[faceIndex];
      const int materialIndex = faceIndex < shape.mesh.material_ids.size()
                                    ? shape.mesh.material_ids[faceIndex]
                                    : -1;

      if (currentSubmesh == nullptr ||
          currentSubmesh->materialIndex != materialIndex) {
        meshData.submeshes.push_back(ObjSubmesh{
            .name = buildSubmeshName(shape, shapeIndex, partIndex++),
            .indexOffset = static_cast<uint32_t>(meshData.indices.size()),
            .indexCount = 0,
            .materialIndex = materialIndex,
            .shapeIndex = static_cast<uint32_t>(shapeIndex),
        });
        currentSubmesh = &meshData.submeshes.back();
      }

      for (uint32_t vertexIndex = 0; vertexIndex < faceVertexCount;
           ++vertexIndex) {
        const auto &index = shape.mesh.indices[runningIndex++];
        ObjVertexRef objVertex(objData.attrib, index);
        TVertex vertex = vertexFactory(objVertex);
        auto [it, inserted] = uniqueVertices.try_emplace(
            vertex, static_cast<uint32_t>(meshData.vertices.size()));
        if (inserted) {
          meshData.vertices.push_back(vertex);
        }
        meshData.indices.push_back(it->second);
        currentSubmesh->indexCount++;
      }
    }
  }

  if (meshData.submeshes.empty() && !meshData.indices.empty()) {
    meshData.submeshes.push_back(
        ObjSubmesh{.name = "mesh",
                   .indexOffset = 0,
                   .indexCount = static_cast<uint32_t>(meshData.indices.size()),
                   .materialIndex = -1,
                   .shapeIndex = 0});
  }

  return meshData;
}

class ObjMesh : public TypedMesh<Vertex> {
public:
  void loadModel(const std::string &path) {
    auto objData = loadObjData(path);
    auto mesh = buildMeshFromObj<Vertex>(objData, std::filesystem::path(path),
                                         [](const ObjVertexRef &vertex) {
                                           return Vertex{
                                               .pos = vertex.position(),
                                               .color = {1.0f, 1.0f, 1.0f},
                                               .texCoord = vertex.texCoord(),
                                           };
                                         });
    setGeometry(std::move(mesh.vertices), std::move(mesh.indices));
    setModelMetadata(std::move(mesh.submeshes), std::move(mesh.materials));
  }
};

using VertexMesh = TypedMesh<Vertex>;

struct GeometryVertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 texCoord;
  glm::vec4 tangent = {1.0f, 0.0f, 0.0f, 1.0f};

  static vk::VertexInputBindingDescription getBindingDescription() {
    return {0, sizeof(GeometryVertex), vk::VertexInputRate::eVertex};
  }

  static std::array<vk::VertexInputAttributeDescription, 4>
  getAttributeDescriptions() {
    return {
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat,
                                            offsetof(GeometryVertex, pos)),
        vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat,
                                            offsetof(GeometryVertex, normal)),
        vk::VertexInputAttributeDescription(
            2, 0, vk::Format::eR32G32Sfloat,
            offsetof(GeometryVertex, texCoord)),
        vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32B32A32Sfloat,
                                            offsetof(GeometryVertex, tangent))};
  }

  bool operator==(const GeometryVertex &other) const {
    return pos == other.pos && normal == other.normal &&
           texCoord == other.texCoord;
  }
};

template <> struct std::hash<GeometryVertex> {
  size_t operator()(GeometryVertex const &vertex) const noexcept {
    return ((hash<glm::vec3>()(vertex.pos) ^
             (hash<glm::vec3>()(vertex.normal) << 1)) >>
            1) ^
           (hash<glm::vec2>()(vertex.texCoord) << 1);
  }
};

class ImportedGeometryMesh : public TypedMesh<GeometryVertex> {
public:
  void setImportedGeometry(std::vector<GeometryVertex> meshVertices,
                           std::vector<uint32_t> meshIndices,
                           std::vector<ModelSubmesh> meshSubmeshes,
                           std::vector<ModelMaterialData> meshMaterials) {
    computeTangents(meshVertices, meshIndices);
    setGeometry(std::move(meshVertices), std::move(meshIndices));
    setModelMetadata(std::move(meshSubmeshes), std::move(meshMaterials));
  }

private:
  static void computeTangents(std::vector<GeometryVertex> &vertices,
                              const std::vector<uint32_t> &indices) {
    if (vertices.empty() || indices.size() < 3) {
      return;
    }

    std::vector<glm::vec3> tangentAccum(vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitangentAccum(vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
      const uint32_t i0 = indices[i + 0];
      const uint32_t i1 = indices[i + 1];
      const uint32_t i2 = indices[i + 2];
      if (i0 >= vertices.size() || i1 >= vertices.size() ||
          i2 >= vertices.size()) {
        continue;
      }

      const auto &v0 = vertices[i0];
      const auto &v1 = vertices[i1];
      const auto &v2 = vertices[i2];

      const glm::vec3 edge1 = v1.pos - v0.pos;
      const glm::vec3 edge2 = v2.pos - v0.pos;
      const glm::vec2 deltaUv1 = v1.texCoord - v0.texCoord;
      const glm::vec2 deltaUv2 = v2.texCoord - v0.texCoord;

      const float det =
          deltaUv1.x * deltaUv2.y - deltaUv1.y * deltaUv2.x;
      if (std::abs(det) < 1e-6f) {
        continue;
      }

      const float invDet = 1.0f / det;
      const glm::vec3 tangent =
          (edge1 * deltaUv2.y - edge2 * deltaUv1.y) * invDet;
      const glm::vec3 bitangent =
          (edge2 * deltaUv1.x - edge1 * deltaUv2.x) * invDet;

      tangentAccum[i0] += tangent;
      tangentAccum[i1] += tangent;
      tangentAccum[i2] += tangent;
      bitangentAccum[i0] += bitangent;
      bitangentAccum[i1] += bitangent;
      bitangentAccum[i2] += bitangent;
    }

    for (size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex) {
      const glm::vec3 normal = glm::normalize(vertices[vertexIndex].normal);
      glm::vec3 tangent = tangentAccum[vertexIndex];

      if (glm::dot(tangent, tangent) < 1e-6f) {
        tangent = std::abs(normal.z) < 0.999f
                      ? glm::normalize(glm::cross(normal, glm::vec3(0.0f, 0.0f, 1.0f)))
                      : glm::normalize(glm::cross(normal, glm::vec3(0.0f, 1.0f, 0.0f)));
        vertices[vertexIndex].tangent = glm::vec4(tangent, 1.0f);
        continue;
      }

      tangent = glm::normalize(tangent - normal * glm::dot(normal, tangent));
      const glm::vec3 bitangent = bitangentAccum[vertexIndex];
      const float handedness =
          glm::dot(glm::cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
      vertices[vertexIndex].tangent = glm::vec4(tangent, handedness);
    }
  }
};

struct FullscreenVertex {
  glm::vec3 pos;
  glm::vec2 uv;

  static vk::VertexInputBindingDescription getBindingDescription() {
    return {0, sizeof(FullscreenVertex), vk::VertexInputRate::eVertex};
  }

  static std::array<vk::VertexInputAttributeDescription, 2>
  getAttributeDescriptions() {
    return {
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat,
                                            offsetof(FullscreenVertex, pos)),
        vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32Sfloat,
                                            offsetof(FullscreenVertex, uv)),
    };
  }
};

class ObjGeometryMesh : public ImportedGeometryMesh {
public:
  void loadModel(const std::string &path) {
    auto objData = loadObjData(path);
    auto mesh = buildMeshFromObj<GeometryVertex>(
        objData, std::filesystem::path(path), [](const ObjVertexRef &v) {
          glm::vec3 n = v.normal();
          if (glm::length(n) < 0.0001f) {
            n = {0.0f, 0.0f, 1.0f};
          } else {
            n = glm::normalize(n);
          }

          return GeometryVertex{
              .pos = v.position(), .normal = n, .texCoord = v.texCoord()};
        });

    setImportedGeometry(std::move(mesh.vertices), std::move(mesh.indices),
                        std::move(mesh.submeshes), std::move(mesh.materials));
  }
};

using FullscreenMesh = TypedMesh<FullscreenVertex>;

inline FullscreenMesh buildFullscreenQuadMesh() {
  FullscreenMesh mesh;
  mesh.setGeometry(
      {
          {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
          {{1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
          {{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
          {{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
      },
      {0, 1, 2, 2, 3, 0});
  return mesh;
}
