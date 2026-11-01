#pragma once

/// @file
/// @ingroup gfx
/// Shaders from the embedded binaries (src/gfx/generated/shaders.hpp) in the format the device takes.

#include "gfx/generated/shaders.hpp" // by `project_n shaders`
#include "gfx/internal/gpu.hpp"

#include <SDL3/SDL.h>

namespace gfx::internal {

/// `stage` as a shader of `kind` on `device`, named for GPU captures, in the first format the
/// device takes (it is created for SPIR-V, DXIL and MSL); null, with SDL's error set, if rejected.
[[nodiscard]] Gpu<SDL_GPUShader> CreateShader(SDL_GPUDevice* device, const shaders::Stage& stage, SDL_GPUShaderStage kind);

} // namespace gfx::internal
