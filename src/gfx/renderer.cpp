#include "gfx/renderer.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"
#include "core/repeated_failure.hpp"
#include "gfx/internal/features/composite.hpp"
#include "gfx/internal/features/meshes.hpp"
#include "gfx/internal/features/sprites.hpp"
#include "gfx/internal/frame/frame.hpp"
#include "gfx/internal/gpu.hpp"
#include "gfx/internal/resources/scene_target.hpp"
#include "gfx/internal/resources/sprite_array.hpp"
#include "gfx/sprite_block.hpp"
#include "gfx/uploads.hpp"
#include "platform/clock.hpp"

#include <SDL3/SDL.h>

namespace gfx {

namespace {

#ifdef NDEBUG
constexpr bool DEBUG_MODE = false;
#else
constexpr bool DEBUG_MODE = true; // SDL_GPU's validation, in debug builds only (§8.9)
#endif

// Ink on dark paper, behind the scene where a window wider or taller than the reference shows past it.
// Linear values, which the composite encodes to about (0.08, 0.08, 0.09).
constexpr SDL_FColor BACKDROP{ .r = 0.0072F, .g = 0.0072F, .b = 0.0085F, .a = 1.0F };

// The outlines (design doc 0002.3 §3), three texels wide: the hovered entity's pale, the selected one's gold.
// Premultiplied RGBA8, red in the low byte.
constexpr u32 HOVER_COLOUR = 0xFFF0'F4F4U;
constexpr u32 SELECTED_COLOUR = 0xFF30'C0FFU;
constexpr f32 OUTLINE_TEXELS = 3.0F;

std::string_view PresentModeName(SDL_GPUPresentMode mode) {
    if (mode == SDL_GPU_PRESENTMODE_MAILBOX) {
        return "mailbox";
    }
    return mode == SDL_GPU_PRESENTMODE_IMMEDIATE ? "immediate" : "vsync";
}

// A scene loading (design doc 0002.3.1 §5): its sprites decode on the job system, then upload a few each frame.
struct SceneLoad {
    SceneLoad(
        core::SceneId loading,
        std::vector<core::atlas::SpriteId> missing,
        const std::filesystem::path& assets,
        const std::shared_ptr<const core::Manifest>& manifest,
        core::Jobs& jobs
    )
      : scene{ loading }
      , sprites{ std::move(missing) }
      , decoded(sprites.size())
      , decoding{ jobs.Start(
            static_cast<u32>(sprites.size()),
            [assets, manifest, names = std::span<const core::atlas::SpriteId>{ sprites }, slots = std::span{ decoded }](
                u32 chunk
            ) { slots[chunk] = internal::DecodeSprite(assets, manifest->Sprite(names[chunk])); }
        ) } {
    }

    core::SceneId scene;
    std::vector<core::atlas::SpriteId> sprites;                         // those not in the atlas when it began
    std::vector<std::expected<internal::Surface, core::Error>> decoded; // a slot per sprite
    std::size_t uploaded = 0;
    core::Jobs::Pending decoding; // last: destroyed first, waiting for the decodes before their slots go
};

} // namespace

struct Renderer::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() = default;

    // Once a swapchain texture is acquired, the frame is submitted even on an error: SDL requires it.
    std::expected<void, core::Error> Frame(const internal::Frame& acquired, const core::Snapshot& snapshot, const View& view);
    // A recorded copy counts as uploaded only once its commands are submitted (`uploaded.Submitted`): a dropped buffer
    // must upload again.
    std::expected<void, core::Error> Upload(SDL_GPUCommandBuffer* commands, const core::Snapshot& snapshot);
    void Report(const std::expected<void, core::Error>& drawn);
    [[nodiscard]] bool Holds(core::SceneId scene) const;
    // The world pass into the scene target, fitted to `width` by `height`, then the final pass into `target`.
    std::expected<void, core::Error> DrawPasses(
        SDL_GPUCommandBuffer* commands,
        SDL_GPUTexture* target,
        u32 width,
        u32 height,
        const core::Snapshot& snapshot,
        const View& view
    );
    void DrawWorld(SDL_GPUCommandBuffer* commands, const core::Snapshot& snapshot, const View& view) const;
    void DrawFinal(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target) const;

    SDL_Window* window = nullptr;
    internal::Device device = nullptr; // first: destroyed after everything it made
    internal::Gpu<SDL_Window> claim = nullptr;
    internal::SpriteArray sprites{};
    std::shared_ptr<const core::Manifest> manifest = nullptr;
    std::filesystem::path assets{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<bool> resident{};   // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::optional<SceneLoad> load = std::nullopt;
    internal::SpriteRenderer instances{};
    internal::MeshRenderer meshes{}; // shortcut: made at start-up, unused until text draws through it (0002.4.2 §10, commit 4)
    internal::Compositor compositor{};
    internal::SceneTarget sceneTarget{};
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
    if (uploaded.Holds(shown) || !Holds(snapshot.scene)) {
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

std::expected<void, core::Error> Renderer::Impl::DrawPasses(
    SDL_GPUCommandBuffer* commands,
    SDL_GPUTexture* target,
    u32 width,
    u32 height,
    const core::Snapshot& snapshot,
    const View& view
) {
    PN_PROFILE_ZONE();
    if (std::expected<void, core::Error> fitted = internal::FitSceneTarget(device.get(), sceneTarget, width, height); !fitted) {
        return fitted;
    }
    DrawWorld(commands, snapshot, view);
    DrawFinal(commands, target);
    return {};
}

void Renderer::Impl::DrawFinal(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target) const {
    PN_PROFILE_ZONE();
    SDL_PushGPUDebugGroup(commands, "final");
    SDL_GPUColorTargetInfo info{};
    info.texture = target;
    info.load_op = SDL_GPU_LOADOP_DONT_CARE; // the composite covers every pixel
    info.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &info, 1, nullptr);
    compositor.Draw(pass, sceneTarget.texture.get());
    SDL_EndGPURenderPass(pass);
    SDL_PopGPUDebugGroup(commands);
}

void Renderer::Impl::DrawWorld(SDL_GPUCommandBuffer* commands, const core::Snapshot& snapshot, const View& view) const {
    PN_PROFILE_ZONE();
    const auto alpha = static_cast<f32>(view.alpha);
    const std::optional<std::size_t> outlined = view.outlined;
    SDL_PushGPUDebugGroup(commands, "world");
    SDL_GPUColorTargetInfo target{};
    target.texture = sceneTarget.texture.get();
    target.clear_color = BACKDROP;
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    target.cycle = true; // nothing loads the old contents (0002 §8.7)
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
    if (!Holds(snapshot.scene)) {
        SDL_EndGPURenderPass(pass);
        SDL_PopGPUDebugGroup(commands);
        return;
    }
    const ClipTransform transform = WorldToClip(view.camera, sceneTarget.width, sceneTarget.height);
    instances.Draw(commands, pass, snapshot.batches, sprites, transform, alpha);
    const f32 ring = OUTLINE_TEXELS / static_cast<f32>(manifest->pageSize);
    if (snapshot.selected) {
        instances.DrawOutline(
            commands,
            pass,
            { .instance = *snapshot.selected, .colour = SELECTED_COLOUR, .ring = ring },
            sprites,
            transform,
            alpha
        );
    }
    // Hovering the selected entity keeps its gold: the selection is what a click changed.
    if (outlined && *outlined < snapshot.instances.size() && *outlined != snapshot.selected) {
        instances.DrawOutline(
            commands,
            pass,
            { .instance = static_cast<u32>(*outlined), .colour = HOVER_COLOUR, .ring = ring },
            sprites,
            transform,
            alpha
        );
    }
    SDL_EndGPURenderPass(pass);
    SDL_PopGPUDebugGroup(commands);
}

std::expected<void, core::Error> Renderer::Impl::Frame(
    const internal::Frame& acquired,
    const core::Snapshot& snapshot,
    const View& view
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
    std::expected<void, core::Error> drawn{};
    if (acquired.swapchain != nullptr) {
        drawn = DrawPasses(acquired.commands, acquired.swapchain, acquired.width, acquired.height, snapshot, view);
    }
    const std::expected<void, core::Error> submitted = internal::SubmitFrame(acquired);
    uploaded.Submitted(submitted.has_value());
    return core::Combine(drawn, submitted);
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

bool Renderer::Impl::Holds(core::SceneId scene) const {
    PN_PROFILE_ZONE();
    return std::ranges::all_of(manifest->Scene(scene).sprites, [this](core::atlas::SpriteId sprite) {
        return resident.at(std::to_underlying(sprite));
    });
}

std::expected<Renderer, core::Error> Renderer::Create(
    platform::Window& window,
    const std::filesystem::path& assets,
    std::shared_ptr<const core::Manifest> manifest
) {
    auto impl = std::make_unique<Impl>();
    impl->manifest = std::move(manifest);
    impl->assets = assets;
    impl->resident.resize(impl->manifest->sprites.size());
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
    // Always plain SDR, which SDL guarantees: the composite encodes sRGB itself (design doc 0002.4.1 §3).
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

    // Pipelines are made at start-up (§8.8), so each backend checks the shaders when the game starts.
    std::expected<internal::SpriteRenderer, core::Error> instances =
        internal::SpriteRenderer::Create(device, internal::SCENE_FORMAT);
    if (!instances) {
        return std::unexpected{ std::move(instances.error()) };
    }
    impl->instances = std::move(*instances);
    std::expected<internal::MeshRenderer, core::Error> meshes = internal::MeshRenderer::Create(device, internal::SCENE_FORMAT);
    if (!meshes) {
        return std::unexpected{ std::move(meshes.error()) };
    }
    impl->meshes = std::move(*meshes);
    std::expected<internal::Compositor, core::Error> compositor =
        internal::Compositor::Create(device, SDL_GetGPUSwapchainTextureFormat(device, impl->window));
    if (!compositor) {
        return std::unexpected{ std::move(compositor.error()) };
    }
    impl->compositor = std::move(*compositor);
    std::expected<internal::SpriteArray, core::Error> sprites = internal::CreateSpriteArray(device, *impl->manifest);
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

void Renderer::BeginScene(core::SceneId scene, core::Jobs& jobs) {
    PN_ASSERT(!m_impl->load, "a scene began loading while another loads");
    std::vector<core::atlas::SpriteId> missing;
    for (const core::atlas::SpriteId sprite : m_impl->manifest->Scene(scene).sprites) {
        if (!m_impl->resident.at(std::to_underlying(sprite))) {
            missing.push_back(sprite);
        }
    }
    if (!missing.empty()) {
        m_impl->load.emplace(scene, std::move(missing), m_impl->assets, m_impl->manifest, jobs);
    }
}

std::expected<bool, core::Error> Renderer::ContinueScene(u64 budgetNs) {
    PN_PROFILE_ZONE();
    Impl& impl = *m_impl;
    if (!impl.load) {
        return true;
    }
    SceneLoad& load = *impl.load;
    if (!load.decoding.Done()) {
        return false;
    }
    const u64 startNs = platform::NowNs();
    const std::size_t first = load.uploaded;
    while (load.uploaded < load.sprites.size() && (load.uploaded == first || platform::NowNs() - startNs < budgetNs)) {
        const std::expected<internal::Surface, core::Error>& image = load.decoded.at(load.uploaded);
        const std::expected<void, core::Error> uploaded = image.and_then([&](const internal::Surface& surface) {
            return internal::UploadSprite(
                impl.device.get(),
                impl.sprites,
                impl.manifest->Sprite(load.sprites.at(load.uploaded)),
                impl.manifest->mipLevels,
                *surface
            );
        });
        if (!uploaded) {
            core::Error error = uploaded.error();
            impl.load.reset();
            return std::unexpected{ std::move(error) };
        }
        load.decoded.at(load.uploaded) = internal::Surface{}; // its pixels are on the GPU now
        ++load.uploaded;
    }
    if (load.uploaded < load.sprites.size()) {
        return false;
    }
    if (const std::expected<void, core::Error> mips = internal::GenerateMips(impl.device.get(), impl.sprites); !mips) {
        impl.load.reset();
        return std::unexpected{ mips.error() };
    }
    for (const core::atlas::SpriteId sprite : load.sprites) {
        impl.resident.at(std::to_underlying(sprite)) = true;
    }
    PN_LOG_DEBUG("{} sprites of scene {} decoded and uploaded", load.sprites.size(), impl.manifest->Scene(load.scene).name);
    impl.load.reset();
    return true;
}

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

void Renderer::Draw(const core::Snapshot& snapshot, const View& view) {
    PN_PROFILE_ZONE();
    if (!m_impl->frame) {
        return; // the acquire failed, and said so
    }
    const internal::Frame acquired = *m_impl->frame;
    m_impl->frame.reset();
    m_impl->Report(m_impl->Frame(acquired, snapshot, view));
}

std::expected<Image, core::Error> Renderer::Capture(const core::Snapshot& snapshot, const View& view, u32 width, u32 height) {
    SDL_GPUDevice* device = m_impl->device.get();
    // The composite's pipeline is made for the swapchain's format, so the picture is drawn in it too.
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
            if (std::expected<void, core::Error> drawn =
                    m_impl->DrawPasses(commands, texture.get(), width, height, snapshot, view);
                !drawn) {
                SDL_CancelGPUCommandBuffer(commands);
                m_impl->uploaded.Submitted(false);
                return std::unexpected{ std::move(drawn.error()) };
            }
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
            if (format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM) {
                for (std::size_t blue = 0; blue + 2 < image.rgba.size(); blue += BYTES_PER_TEXEL) {
                    std::swap(image.rgba[blue], image.rgba[blue + 2]);
                }
            }
            return image;
        }
    );
}

} // namespace gfx
