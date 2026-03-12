#pragma once

#include "Mesh.h"

class ModelAsset {
public:
  virtual ~ModelAsset() = default;

  virtual ImportedGeometryMesh &mesh() = 0;
  virtual const ImportedGeometryMesh &mesh() const = 0;
  virtual const std::vector<ModelMaterialData> &materials() const = 0;
  virtual const std::vector<ModelSubmesh> &submeshes() const = 0;
  virtual const std::string &path() const = 0;
};
