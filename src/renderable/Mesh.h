#pragma once
#include "RenderUtils.h"
#include <array>
#include <cstring>
#include <stdexcept>
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

#define TINYOBJLOADER_IMPLEMENTATION
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
      throw std::runtime_error("cannot create a vertex buffer with no vertices");
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
    vertexStrideValue = 0;
    vertexCountValue = 0;
  }

private:
  std::vector<std::byte> vertexBytes;
  std::vector<uint32_t> indices;
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

class ObjVertexRef {
public:
  ObjVertexRef(const tinyobj::attrib_t &attribData, tinyobj::index_t objIndex)
      : attrib(&attribData),
        index(objIndex) {}

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

  if (!LoadObj(&data.attrib, &data.shapes, &data.materials, &warn, &err,
               path.c_str())) {
    throw std::runtime_error(warn + err);
  }

  return data;
}

template <typename TVertex, typename TVertexFactory>
TypedMesh<TVertex> buildMeshFromObj(const ObjData &objData,
                                    TVertexFactory &&vertexFactory) {
  std::vector<TVertex> meshVertices;
  std::vector<uint32_t> meshIndices;

  for (const auto &shape : objData.shapes) {
    for (const auto &index : shape.mesh.indices) {
      ObjVertexRef objVertex(objData.attrib, index);
      meshVertices.push_back(vertexFactory(objVertex));
      meshIndices.push_back(static_cast<uint32_t>(meshVertices.size() - 1));
    }
  }

  TypedMesh<TVertex> mesh;
  mesh.setGeometry(std::move(meshVertices), std::move(meshIndices));
  return mesh;
}

class ObjMesh : public TypedMesh<Vertex> {
public:
  void loadModel(const std::string &path) {
    auto objData = loadObjData(path);
    auto mesh = buildMeshFromObj<Vertex>(objData, [](const ObjVertexRef &vertex) {
      return Vertex{
          .pos = vertex.position(),
          .color = {1.0f, 1.0f, 1.0f},
          .texCoord = vertex.texCoord(),
      };
    });
    setGeometry(std::vector<Vertex>(mesh.vertexData().begin(), mesh.vertexData().end()),
                std::vector<uint32_t>(mesh.getIndices().begin(),
                                      mesh.getIndices().end()));
  }
};

using VertexMesh = TypedMesh<Vertex>;
