#include "gfx/renderer.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"
#include "core/repeated_failure.hpp"
#include "gfx/internal/gpu.hpp"
#include "shaders.hpp" // generated in shaders/bin/ by `project_n shaders`

#include <SDL3/SDL.h>

namespace gfx {

namespace {

#ifdef NDEBUG
constexpr bool DEBUG_MODE = false;
#else
constexpr bool DEBUG_MODE = true; // SDL_GPU's validation, in debug builds only (§8.9)
#endif

std::string_view PresentModeName(SDL_GPUPresentMode mode) {
    if (mode == SDL_GPU_PRESENTMODE_MAILBOX) {
        return "mailbox";
    }
    return mode == SDL_GPU_PRESENTMODE_IMMEDIATE ? "immediate" : "vsync";
}

// The device's shader in the first format it takes; it was created for exactly these three.
internal::Gpu<SDL_GPUShader> CreateShader(SDL_GPUDevice* device, const shaders::Stage& stage, SDL_GPUShaderStage kind) {
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
    internal::Gpu<SDL_GPUShader> shader{ SDL_CreateGPUShader(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return shader;
}

std::expected<void, core::Error> SubmitFrame(SDL_GPUDevice* device, SDL_Window* window, Colour colour) {
    PN_PROFILE_ZONE();
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        return std::unexpected{ core::Error{ std::format("no command buffer: {}", SDL_GetError()) } };
    }
    SDL_GPUTexture* swapchain = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &swapchain, nullptr, nullptr)) {
        core::Error error{ std::format("no swapchain texture: {}", SDL_GetError()) };
        SDL_CancelGPUCommandBuffer(commands); // allowed: no swapchain texture was acquired (§8.3)
        return std::unexpected{ std::move(error) };
    }
    // Without a swapchain texture (the window can't be shown), the command buffer goes in empty (§8.3).
    if (swapchain != nullptr) {
        SDL_PushGPUDebugGroup(commands, "clear");
        SDL_GPUColorTargetInfo target{};
        target.texture = swapchain;
        target.clear_color = { .r = colour.red, .g = colour.green, .b = colour.blue, .a = 1.0F };
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_EndGPURenderPass(SDL_BeginGPURenderPass(commands, &target, 1, nullptr));
        SDL_PopGPUDebugGroup(commands);
    }
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return std::unexpected{ core::Error{ std::format("the frame wasn't submitted: {}", SDL_GetError()) } };
    }
    return {};
}

} // namespace

struct Renderer::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() = default;

    SDL_Window* window = nullptr;
    internal::Device device = nullptr; // first: destroyed after everything it made
    internal::Gpu<SDL_Window> claim = nullptr;
    internal::Gpu<SDL_GPUGraphicsPipeline> sprites = nullptr;
    core::RepeatedFailure failures{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
};

std::expected<Renderer, core::Error> Renderer::Create(platform::Window& window) {
    auto impl = std::make_unique<Impl>();
    impl->window = static_cast<SDL_Window*>(window.Handle());
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
    SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
    SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, DEBUG_MODE);
    impl->device.reset(SDL_CreateGPUDeviceWithProperties(properties));
    SDL_DestroyProperties(properties);
    if (impl->device == nullptr) {
        return std::unexpected{ core::Error{ std::format("SDL_CreateGPUDeviceWithProperties failed: {}", SDL_GetError()) } };
    }
    SDL_GPUDevice* device = impl->device.get();
    if (!SDL_ClaimWindowForGPUDevice(device, impl->window)) {
        return std::unexpected{ core::Error{ std::format("SDL_ClaimWindowForGPUDevice failed: {}", SDL_GetError()) } };
    }
    impl->claim = internal::Gpu<SDL_Window>{ impl->window, { device } };

    // Rendering is uncapped (design doc 0002 §6): mailbox, then immediate; vsync is always there.
    SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
    for (const SDL_GPUPresentMode uncapped : { SDL_GPU_PRESENTMODE_MAILBOX, SDL_GPU_PRESENTMODE_IMMEDIATE }) {
        if (SDL_WindowSupportsGPUPresentMode(device, impl->window, uncapped)) {
            mode = uncapped;
            break;
        }
    }
    if (!SDL_SetGPUSwapchainParameters(device, impl->window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode)) {
        return std::unexpected{ core::Error{ std::format("SDL_SetGPUSwapchainParameters failed: {}", SDL_GetError()) } };
    }
    PN_LOG_INFO(
        "GPU driver: {}, presenting with {}{}",
        SDL_GetGPUDeviceDriver(device),
        PresentModeName(mode),
        DEBUG_MODE ? ", debug mode" : ""
    );
    if (mode == SDL_GPU_PRESENTMODE_VSYNC) {
        PN_LOG_WARNING("neither mailbox nor immediate presentation is available, so frames follow the display");
    }

    // Made at start-up like every pipeline (§8.8), so each backend checks the shaders when the game
    // starts (spike S8); the instanced sprites of phase 3 draw with it. The shaders are released
    // here: the pipeline keeps what it needs.
    const internal::Gpu<SDL_GPUShader> vertex = CreateShader(device, shaders::SPRITE_VERT, SDL_GPU_SHADERSTAGE_VERTEX);
    const internal::Gpu<SDL_GPUShader> fragment = CreateShader(device, shaders::SPRITE_FRAG, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (vertex == nullptr || fragment == nullptr) {
        return std::unexpected{ core::Error{ std::format("a sprite shader was rejected: {}", SDL_GetError()) } };
    }
    SDL_GPUColorTargetDescription target{};
    target.format = SDL_GetGPUSwapchainTextureFormat(device, impl->window);
    // Premultiplied alpha's normal blend (§8.8).
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex.get();
    info.fragment_shader = fragment.get();
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, "sprites");
    impl->sprites = internal::Gpu<SDL_GPUGraphicsPipeline>{ SDL_CreateGPUGraphicsPipeline(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    if (impl->sprites == nullptr) {
        return std::unexpected{ core::Error{ std::format("the sprite pipeline was rejected: {}", SDL_GetError()) } };
    }
    return Renderer{ std::move(impl) };
}

Renderer::Renderer(std::unique_ptr<Impl> impl)
  : m_impl{ std::move(impl) } {
}
Renderer::Renderer(Renderer&& other) noexcept = default;
Renderer::~Renderer() = default;

void Renderer::Draw(Colour colour) {
    PN_PROFILE_ZONE();
    const std::expected<void, core::Error> submitted = SubmitFrame(m_impl->device.get(), m_impl->window, colour);
    if (!submitted) {
        if (m_impl->failures.Fail()) {
            PN_LOG_ERROR("frames are failing: {}", submitted.error().message);
        }
    }
    else if (const u64 failed = m_impl->failures.Succeed(); failed > 0) {
        PN_LOG_WARNING("frames are drawn again after {} failed", failed);
    }
}

} // namespace gfx
