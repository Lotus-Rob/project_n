#include "gfx/internal/resources/scene_target.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"

namespace gfx::internal {

std::expected<void, core::Error> FitSceneTarget(SDL_GPUDevice* device, SceneTarget& target, u32 width, u32 height) {
    PN_PROFILE_ZONE();
    if (target.texture != nullptr && target.width == width && target.height == height) {
        return {};
    }
    // SDL frees the old texture once the frames in flight that draw with it are done.
    target = SceneTarget{};
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SCENE_FORMAT;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, "scene");
    Gpu<SDL_GPUTexture> texture{ SDL_CreateGPUTexture(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    if (texture == nullptr) {
        return std::unexpected{ core::Error{
            std::format("the {}x{} scene target wasn't created: {}", width, height, SDL_GetError()) } };
    }
    target = SceneTarget{ .texture = std::move(texture), .width = width, .height = height };
    return {};
}

} // namespace gfx::internal
