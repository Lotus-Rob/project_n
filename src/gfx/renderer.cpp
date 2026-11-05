#include "gfx/renderer.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"
#include "core/repeated_failure.hpp"
#include "gfx/internal/features/sprites.hpp"
#include "gfx/internal/frame/frame.hpp"
#include "gfx/internal/gpu.hpp"
#include "gfx/internal/resources/sprite_array.hpp"
#include "gfx/sprite_block.hpp"
#include "gfx/uploads.hpp"

#include <SDL3/SDL.h>

namespace gfx {

namespace {

#ifdef NDEBUG
constexpr bool DEBUG_MODE = false;
#else
constexpr bool DEBUG_MODE = true; // SDL_GPU's validation, in debug builds only (§8.9)
#endif

// Ink on dark paper, behind the scene where a window wider or taller than the reference shows past it.
// Linear values, which the sRGB swapchain encodes to about (0.08, 0.08, 0.09).
constexpr SDL_FColor BACKDROP{ .r = 0.0072F, .g = 0.0072F, .b = 0.0085F, .a = 1.0F };

std::string_view PresentModeName(SDL_GPUPresentMode mode) {
    if (mode == SDL_GPU_PRESENTMODE_MAILBOX) {
        return "mailbox";
    }
    return mode == SDL_GPU_PRESENTMODE_IMMEDIATE ? "immediate" : "vsync";
}

} // namespace

struct Renderer::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() = default;

    // Once a swapchain texture is acquired, the frame is submitted even on an error: SDL requires it.
    std::expected<void, core::Error> Frame(
        const internal::Frame& acquired,
        const core::Snapshot& snapshot,
        f32 alpha,
        const Camera& camera
    );
    // A recorded copy counts as uploaded only once its commands are submitted (`uploaded.Submitted`): a dropped buffer
    // must upload again.
    std::expected<void, core::Error> Upload(SDL_GPUCommandBuffer* commands, const core::Snapshot& snapshot);
    void Report(const std::expected<void, core::Error>& drawn);
    void DrawWorld(
        SDL_GPUCommandBuffer* commands,
        SDL_GPUTexture* texture,
        u32 width,
        u32 height,
        const core::Snapshot& snapshot,
        f32 alpha,
        const Camera& camera
    ) const;

    SDL_Window* window = nullptr;
    internal::Device device = nullptr; // first: destroyed after everything it made
    internal::Gpu<SDL_Window> claim = nullptr;
    internal::SpriteArray sprites{};
    internal::SpriteRenderer instances{};
    std::optional<internal::Frame> frame = std::nullopt; // acquired by Acquire(), submitted by Draw()
    UploadedSnapshot uploaded{};
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    core::RepeatedFailure failures{};
    core::RepeatedFailure overflows{};
    core::RepeatedFailure uploadFailures{};
    // NOLINTEND(readability-redundant-member-init)
};

std::expected<void, core::Error> Renderer::Impl::Upload(SDL_GPUCommandBuffer* commands, const core::Snapshot& snapshot) {
    PN_PROFILE_ZONE();
    const SnapshotId shown{ .epoch = snapshot.epoch, .wallTick = snapshot.wallTick };
    if (uploaded.Holds(shown)) {
        return {};
    }
    SDL_PushGPUDebugGroup(commands, "upload");
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    const std::expected<std::size_t, core::Error> dropped = instances.Upload(copy, snapshot.instances);
    SDL_EndGPUCopyPass(copy);
    SDL_PopGPUDebugGroup(commands);
    if (!dropped) {
        return std::unexpected{ dropped.error() }; // not marked uploaded: the next frame tries again
    }
    uploaded.Recorded(shown);
    if (*dropped > 0) {
        if (overflows.Fail()) {
            PN_LOG_WARNING("{} sprite instances past the buffer's {} aren't drawn", *dropped, internal::MAX_INSTANCES);
        }
    }
    else if (const u64 overflowed = overflows.Succeed(); overflowed > 0) {
        PN_LOG_INFO("every sprite instance fits again, after {} snapshots that didn't", overflowed);
    }
    return {};
}

void Renderer::Impl::DrawWorld(
    SDL_GPUCommandBuffer* commands,
    SDL_GPUTexture* texture,
    u32 width,
    u32 height,
    const core::Snapshot& snapshot,
    f32 alpha,
    const Camera& camera
) const {
    PN_PROFILE_ZONE();
    SDL_PushGPUDebugGroup(commands, "world");
    SDL_GPUColorTargetInfo target{};
    target.texture = texture;
    target.clear_color = BACKDROP;
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
    instances.Draw(commands, pass, snapshot.batches, sprites, WorldToClip(camera, width, height), alpha);
    SDL_EndGPURenderPass(pass);
    SDL_PopGPUDebugGroup(commands);
}

std::expected<void, core::Error> Renderer::Impl::Frame(
    const internal::Frame& acquired,
    const core::Snapshot& snapshot,
    f32 alpha,
    const Camera& camera
) {
    PN_PROFILE_ZONE();
    // A failed upload draws no sprites, but the frame is still submitted, as SDL requires.
    if (const std::expected<void, core::Error> copied = Upload(acquired.commands, snapshot); !copied) {
        if (uploadFailures.Fail()) {
            PN_LOG_ERROR("{}; no sprites are drawn until an upload succeeds", copied.error().message);
        }
    }
    else if (const u64 failed = uploadFailures.Succeed(); failed > 0) {
        PN_LOG_INFO("sprite instances uploaded again, after {} failed uploads", failed);
    }
    // Without a swapchain texture (the window can't be shown), the copy still runs and nothing is drawn (§8.3).
    if (acquired.swapchain != nullptr) {
        DrawWorld(acquired.commands, acquired.swapchain, acquired.width, acquired.height, snapshot, alpha, camera);
    }
    std::expected<void, core::Error> submitted = internal::SubmitFrame(acquired);
    uploaded.Submitted(submitted.has_value());
    return submitted;
}

void Renderer::Impl::Report(const std::expected<void, core::Error>& drawn) {
    PN_PROFILE_ZONE();
    if (!drawn) {
        if (failures.Fail()) {
            PN_LOG_ERROR("frames are failing: {}", drawn.error().message);
        }
    }
    else if (const u64 failed = failures.Succeed(); failed > 0) {
        PN_LOG_WARNING("frames are drawn again after {} failed", failed);
    }
}

std::expected<Renderer, core::Error> Renderer::Create(platform::Window& window, const std::filesystem::path& assets) {
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
    // shortcut: SDL guarantees only SDR, so a window without an sRGB swapchain can't run; the scene target (§8.4) lifts this.
    if (!SDL_WindowSupportsGPUSwapchainComposition(device, impl->window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR)) {
        return std::unexpected{ core::Error{ "the window has no sRGB swapchain, which linear blending needs" } };
    }
    if (!SDL_SetGPUSwapchainParameters(device, impl->window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR, mode)) {
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

    // Pipelines are made at start-up (§8.8), so each backend checks the shaders when the game starts.
    std::expected<internal::SpriteRenderer, core::Error> instances =
        internal::SpriteRenderer::Create(device, SDL_GetGPUSwapchainTextureFormat(device, impl->window));
    if (!instances) {
        return std::unexpected{ std::move(instances.error()) };
    }
    impl->instances = std::move(*instances);
    std::expected<internal::SpriteArray, core::Error> sprites = internal::LoadSpriteArray(device, assets);
    if (!sprites) {
        return std::unexpected{ std::move(sprites.error()) };
    }
    impl->sprites = std::move(*sprites);
    return Renderer{ std::move(impl) };
}

Renderer::Renderer(std::unique_ptr<Impl> impl)
  : m_impl{ std::move(impl) } {
}
Renderer::Renderer(Renderer&& other) noexcept = default;
Renderer::~Renderer() = default;

void Renderer::Acquire() {
    PN_PROFILE_ZONE();
    PN_ASSERT(!m_impl->frame, "Acquire() twice without a Draw() between");
    std::expected<internal::Frame, core::Error> acquired = internal::AcquireFrame(m_impl->device.get(), m_impl->window);
    if (!acquired) {
        m_impl->Report(std::unexpected{ std::move(acquired.error()) });
        return;
    }
    m_impl->frame = *acquired;
}

void Renderer::Draw(const core::Snapshot& snapshot, f64 alpha, const Camera& camera) {
    PN_PROFILE_ZONE();
    if (!m_impl->frame) {
        return; // the acquire failed, and said so
    }
    const internal::Frame acquired = *m_impl->frame;
    m_impl->frame.reset();
    m_impl->Report(m_impl->Frame(acquired, snapshot, static_cast<f32>(alpha), camera));
}

std::expected<Image, core::Error> Renderer::Capture(
    const core::Snapshot& snapshot,
    f64 alpha,
    const Camera& camera,
    u32 width,
    u32 height
) {
    SDL_GPUDevice* device = m_impl->device.get();
    // The pipelines are made for the swapchain's format, so the picture is drawn in it too.
    const SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(device, m_impl->window);
    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = format;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    textureInfo.width = width;
    textureInfo.height = height;
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = 1;
    const internal::Gpu<SDL_GPUTexture> texture{ SDL_CreateGPUTexture(device, &textureInfo), { device } };
    const u32 bytes = width * height * static_cast<u32>(BYTES_PER_TEXEL);
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transferInfo.size = bytes;
    const internal::Gpu<SDL_GPUTransferBuffer> transfer{ SDL_CreateGPUTransferBuffer(device, &transferInfo), { device } };
    SDL_GPUCommandBuffer* commands = texture == nullptr || transfer == nullptr ? nullptr : SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        return std::unexpected{ core::Error{ std::format("nothing to capture into: {}", SDL_GetError()) } };
    }
    return AfterUpload(
        m_impl->Upload(commands, snapshot),
        [commands] { SDL_CancelGPUCommandBuffer(commands); }, // allowed: no swapchain texture is acquired
        [&]() -> std::expected<Image, core::Error> {
            m_impl->DrawWorld(commands, texture.get(), width, height, snapshot, static_cast<f32>(alpha), camera);
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
            SDL_GPUTextureRegion region{};
            region.texture = texture.get();
            region.w = width;
            region.h = height;
            region.d = 1;
            SDL_GPUTextureTransferInfo destination{};
            destination.transfer_buffer = transfer.get();
            SDL_DownloadFromGPUTexture(copy, &region, &destination);
            SDL_EndGPUCopyPass(copy);
            const bool submitted = SDL_SubmitGPUCommandBuffer(commands);
            m_impl->uploaded.Submitted(submitted);
            if (!submitted) {
                return std::unexpected{ core::Error{ std::format("the capture wasn't submitted: {}", SDL_GetError()) } };
            }
            if (!SDL_WaitForGPUIdle(device)) {
                return std::unexpected{ core::Error{ std::format("the capture didn't finish: {}", SDL_GetError()) } };
            }
            void* mapped = SDL_MapGPUTransferBuffer(device, transfer.get(), false);
            if (mapped == nullptr) {
                return std::unexpected{ core::Error{ std::format("the capture wasn't read back: {}", SDL_GetError()) } };
            }
            const std::span<const std::byte> pixels = internal::BytesAt(mapped, bytes);
            Image image{ .width = width, .height = height, .rgba = { pixels.begin(), pixels.end() } };
            SDL_UnmapGPUTransferBuffer(device, transfer.get());
            if (format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB) {
                for (std::size_t blue = 0; blue + 2 < image.rgba.size(); blue += BYTES_PER_TEXEL) {
                    std::swap(image.rgba[blue], image.rgba[blue + 2]);
                }
            }
            return image;
        }
    );
}

} // namespace gfx
