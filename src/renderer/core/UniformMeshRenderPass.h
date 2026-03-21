#pragma once

#include "UniformSceneRenderPass.h"

template <typename TUniform, typename TPush = std::monostate>
using UniformMeshRenderPass = UniformSceneRenderPass<TUniform, TPush>;
