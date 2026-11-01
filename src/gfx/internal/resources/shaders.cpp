#include "gfx/internal/resources/shaders.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace gfx::internal {

Gpu<SDL_GPUShader> CreateShader(SDL_GPUDevice* device, const shaders::Stage& stage, SDL_GPUShaderStage kind) {
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderCreateInfo info{};
    std::span<const u8> code = stage.msl;
    info.format = SDL_GPU_SHADERFORMAT_MSL;
    if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0) {
        code = stage.spirv;
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    }
    else if ((formats & SDL_GPU_SHADERFORMAT_DXIL) != 0) {
        code = stage.dxil;
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
    }
    info.code_size = code.size();
    info.code = code.data();
    info.entrypoint = stage.entryPoint;
    info.stage = kind;
    info.num_samplers = stage.samplers;
    info.num_storage_textures = stage.storageTextures;
    info.num_storage_buffers = stage.storageBuffers;
    info.num_uniform_buffers = stage.uniformBuffers;
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_SHADER_CREATE_NAME_STRING, stage.name);
    Gpu<SDL_GPUShader> shader{ SDL_CreateGPUShader(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return shader;
}

} // namespace gfx::internal
