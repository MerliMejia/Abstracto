#pragma once

#include "renderer/resources/Mesh.h"
#include <vector>

inline TypedMesh<Vertex> buildPointLightMarkerMesh() {
  TypedMesh<Vertex> mesh;
  const glm::vec3 color(1.0f, 1.0f, 1.0f);
  mesh.setGeometry(
      {
          {{-1.0f, 0.0f, 0.0f}, color, {0.0f, 0.0f}},
          {{1.0f, 0.0f, 0.0f}, color, {0.0f, 0.0f}},
          {{0.0f, -1.0f, 0.0f}, color, {0.0f, 0.0f}},
          {{0.0f, 1.0f, 0.0f}, color, {0.0f, 0.0f}},
          {{0.0f, 0.0f, -1.0f}, color, {0.0f, 0.0f}},
          {{0.0f, 0.0f, 1.0f}, color, {0.0f, 0.0f}},
      },
      {0, 1, 2, 3, 4, 5});
  return mesh;
}

inline TypedMesh<Vertex> buildSpotLightMarkerMesh() {
  TypedMesh<Vertex> mesh;
  const glm::vec3 color(1.0f, 1.0f, 1.0f);
  const float coneZ = 1.4f;
  const float coneRadius = 0.45f;
  mesh.setGeometry(
      {
          {{0.0f, 0.0f, 0.0f}, color, {0.0f, 0.0f}},
          {{0.0f, 0.0f, coneZ}, color, {0.0f, 0.0f}},
          {{coneRadius, 0.0f, coneZ}, color, {0.0f, 0.0f}},
          {{-coneRadius, 0.0f, coneZ}, color, {0.0f, 0.0f}},
          {{0.0f, coneRadius, coneZ}, color, {0.0f, 0.0f}},
          {{0.0f, -coneRadius, coneZ}, color, {0.0f, 0.0f}},
      },
      {
          0,
          1,
          0,
          2,
          0,
          3,
          0,
          4,
          0,
          5,
          2,
          4,
          4,
          3,
          3,
          5,
          5,
          2,
      });
  return mesh;
}

inline TypedMesh<Vertex> buildDirectionalLightMarkerMesh() {
  TypedMesh<Vertex> mesh;
  const glm::vec3 color(1.0f, 1.0f, 1.0f);
  mesh.setGeometry(
      {
          {{0.0f, 0.0f, 0.0f}, color, {0.0f, 0.0f}},
          {{0.0f, 0.0f, 1.6f}, color, {0.0f, 0.0f}},
          {{0.20f, 0.0f, 1.2f}, color, {0.0f, 0.0f}},
          {{-0.20f, 0.0f, 1.2f}, color, {0.0f, 0.0f}},
          {{0.0f, 0.20f, 1.2f}, color, {0.0f, 0.0f}},
          {{0.0f, -0.20f, 1.2f}, color, {0.0f, 0.0f}},
      },
      {
          0,
          1,
          1,
          2,
          1,
          3,
          1,
          4,
          1,
          5,
      });
  return mesh;
}

inline TypedMesh<Vertex> buildBoneSegmentMesh() {
  TypedMesh<Vertex> mesh;
  const glm::vec3 color(1.0f, 1.0f, 1.0f);
  mesh.setGeometry(
      {
          {{0.0f, 0.0f, 0.0f}, color, {0.0f, 0.0f}},
          {{0.18f, 0.0f, 0.26f}, color, {0.0f, 0.0f}},
          {{-0.18f, 0.0f, 0.26f}, color, {0.0f, 0.0f}},
          {{0.0f, 0.18f, 0.26f}, color, {0.0f, 0.0f}},
          {{0.0f, -0.18f, 0.26f}, color, {0.0f, 0.0f}},
          {{0.0f, 0.0f, 1.0f}, color, {0.0f, 0.0f}},
      },
      {
          0, 1, 0, 2, 0, 3, 0, 4,
          1, 3, 3, 2, 2, 4, 4, 1,
          1, 5, 2, 5, 3, 5, 4, 5,
          0, 5,
      });
  return mesh;
}

inline TypedMesh<Vertex> buildBoneJointMarkerMesh() {
  TypedMesh<Vertex> mesh;
  const glm::vec3 color(1.0f, 1.0f, 1.0f);
  mesh.setGeometry(
      {
          {{-1.0f, 0.0f, 0.0f}, color, {0.0f, 0.0f}},
          {{1.0f, 0.0f, 0.0f}, color, {0.0f, 0.0f}},
          {{0.0f, -1.0f, 0.0f}, color, {0.0f, 0.0f}},
          {{0.0f, 1.0f, 0.0f}, color, {0.0f, 0.0f}},
          {{0.0f, 0.0f, -1.0f}, color, {0.0f, 0.0f}},
          {{0.0f, 0.0f, 1.0f}, color, {0.0f, 0.0f}},
          {{-0.55f, -0.55f, -0.55f}, color, {0.0f, 0.0f}},
          {{0.55f, 0.55f, 0.55f}, color, {0.0f, 0.0f}},
          {{-0.55f, 0.55f, -0.55f}, color, {0.0f, 0.0f}},
          {{0.55f, -0.55f, 0.55f}, color, {0.0f, 0.0f}},
          {{0.55f, -0.55f, -0.55f}, color, {0.0f, 0.0f}},
          {{-0.55f, 0.55f, 0.55f}, color, {0.0f, 0.0f}},
      },
      {
          0, 1, 2, 3, 4, 5,
          6, 7, 8, 9, 10, 11,
      });
  return mesh;
}
