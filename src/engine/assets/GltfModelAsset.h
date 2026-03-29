#pragma once

#include "ModelAsset.h"
#include "engine/animation/SkeletonTypes.h"

class GltfModelAsset : public ModelAsset {
public:
  void load(const std::string &path);

  void createGpuBuffers(CommandContext &commandContext,
                        DeviceContext &deviceContext) {
    geometryMesh.createVertexBuffer(commandContext, deviceContext);
    geometryMesh.createIndexBuffer(commandContext, deviceContext);
  }

  ImportedGeometryMesh &mesh() override { return geometryMesh; }
  const ImportedGeometryMesh &mesh() const override { return geometryMesh; }

  const std::vector<ModelMaterialData> &materials() const override {
    return geometryMesh.getMaterials();
  }

  std::vector<ModelMaterialData> &mutableMaterials() override {
    return geometryMesh.mutableMaterials();
  }

  const std::vector<ModelSubmesh> &submeshes() const override {
    return geometryMesh.getSubmeshes();
  }

  const std::string &path() const override { return sourcePath; }

  const SkeletonAssetData *skeletonAsset() const override {
    return skeletonData.nodes.empty() && skeletonData.skins.empty() &&
                   skeletonData.animations.empty()
               ? nullptr
               : &skeletonData;
  }

private:
  std::string sourcePath;
  ImportedGeometryMesh geometryMesh;
  SkeletonAssetData skeletonData;
};
