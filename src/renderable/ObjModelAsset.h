#pragma once

#include "Mesh.h"
#include "ModelAsset.h"

class ObjModelAsset : public ModelAsset {
public:
  void load(const std::string &path) {
    sourcePath = path;
    geometryMesh.loadModel(path);
  }

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

  const std::vector<ModelSubmesh> &submeshes() const override {
    return geometryMesh.getSubmeshes();
  }

  const std::string &path() const override { return sourcePath; }

private:
  std::string sourcePath;
  ObjGeometryMesh geometryMesh;
};
