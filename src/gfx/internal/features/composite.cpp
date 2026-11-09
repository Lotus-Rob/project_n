#include "gfx/internal/features/composite.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "gfx/internal/resources/shaders.hpp"

namespace gfx::internal {

std::expected<Compositor, core::Error> Compositor::Create(SDL_GPUDevice* device, SDL_GPUTextureFormat format) {
    Compositor compositor;
    const Gpu<SDL_GPUShader> vertex = CreateShader(device, shaders::COMPOSITE_VERT, SDL_GPU_SHADERSTAGE_VERTEX);
    const Gpu<SDL_GPUShader> fragment = CreateShader(device, shaders::COMPOSITE_FRAG, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (vertex != nullptr && fragment != nullptr) {
        SDL_GPUColorTargetDescription target{};
        target.format = format;
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertex.get();
        info.fragment_shader = fragment.get();
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.target_info.color_target_descriptions = &target;
        info.target_info.num_color_targets = 1;
        info.props = SDL_CreateProperties();
        SDL_SetStringProperty(info.props, SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, "composite");
        compositor.m_pipeline = Gpu<SDL_GPUGraphicsPipeline>{ SDL_CreateGPUGraphicsPipeline(device, &info), { device } };
        SDL_DestroyProperties(info.props);
    }
    if (compositor.m_pipeline == nullptr) {
        return std::unexpected{ core::Error{ std::format("the composite pipeline was rejected: {}", SDL_GetError()) } };
    }
    // Nearest: the scene target is the pass target's size (render scale 1).
    SDL_GPUSamplerCreateInfo sampler{};
    sampler.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    compositor.m_sampler = Gpu<SDL_GPUSampler>{ SDL_CreateGPUSampler(device, &sampler), { device } };
    if (compositor.m_sampler == nullptr) {
        return std::unexpected{ core::Error{ std::format("the composite sampler wasn't created: {}", SDL_GetError()) } };
    }
    return compositor;
}

void Compositor::Draw(SDL_GPURenderPass* pass, SDL_GPUTexture* scene) const {
    PN_PROFILE_ZONE();
    SDL_BindGPUGraphicsPipeline(pass, m_pipeline.get());
    const SDL_GPUTextureSamplerBinding binding{ .texture = scene, .sampler = m_sampler.get() };
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
}

} // namespace gfx::internal
