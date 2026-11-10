#pragma once

/// @file
/// @ingroup gfx
/// Owners of SDL objects (design doc 0002 §8.2): GPU objects are released once, through the device
/// that made it, and the device outlives them; other SDL objects through their destroy function.

#include "core/log.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <memory>
#include <span>

namespace gfx::internal {

/// Destroys a GPU device.
struct DeviceDeleter {
    /// Destroys `device`; everything it made must be released already.
    void operator()(SDL_GPUDevice* device) const {
        SDL_DestroyGPUDevice(device);
        PN_LOG_INFO("GPU device destroyed");
    }
};

/// Owns a GPU device. Declare it before the objects it makes, so it is destroyed after them.
using Device = std::unique_ptr<SDL_GPUDevice, DeviceDeleter>;

/// Releases an object through the device that made it.
struct GpuDeleter {
    /// The device that made the object.
    SDL_GPUDevice* device = nullptr;

    /// Releases `shader`.
    void operator()(SDL_GPUShader* shader) const {
        SDL_ReleaseGPUShader(device, shader);
    }

    /// Releases `pipeline`.
    void operator()(SDL_GPUGraphicsPipeline* pipeline) const {
        SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    }

    /// Releases `texture`.
    void operator()(SDL_GPUTexture* texture) const {
        SDL_ReleaseGPUTexture(device, texture);
    }

    /// Releases `sampler`.
    void operator()(SDL_GPUSampler* sampler) const {
        SDL_ReleaseGPUSampler(device, sampler);
    }

    /// Releases `buffer`.
    void operator()(SDL_GPUBuffer* buffer) const {
        SDL_ReleaseGPUBuffer(device, buffer);
    }

    /// Releases `buffer`.
    void operator()(SDL_GPUTransferBuffer* buffer) const {
        SDL_ReleaseGPUTransferBuffer(device, buffer);
    }

    /// Releases the device's claim on `window`; the window itself stays open.
    void operator()(SDL_Window* window) const {
        SDL_ReleaseWindowFromGPUDevice(device, window);
    }
};

/// Owns an object made by a device: a shader, a pipeline, a texture, a sampler, a buffer, or a
/// claimed window.
template<typename Object>
using Gpu = std::unique_ptr<Object, GpuDeleter>;

/// Destroys an SDL object that needs no device, with its destroy function `Destroy`.
template<auto Destroy>
struct SdlDeleter {
    /// Destroys `object`.
    void operator()(auto* object) const {
        Destroy(object);
    }
};

/// The `count` objects at `data`: memory SDL hands out as a pointer and a count (a mapped transfer buffer, a surface's
/// pixels, a text's draw operations), viewed as a bounds-checked span from then on.
template<typename Object>
[[nodiscard]] std::span<Object> SpanAt(Object* data, std::size_t count) {
#ifdef __clang__
#pragma clang diagnostic push
// The one place a pointer from a C API becomes a span: no constructor checks what SDL allocated.
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    return { data, count };
#ifdef __clang__
#pragma clang diagnostic pop
#endif
}

/// The `size` bytes at `data`, as SpanAt() views them.
[[nodiscard]] inline std::span<std::byte> BytesAt(void* data, std::size_t size) {
    return SpanAt(static_cast<std::byte*>(data), size);
}

/// Owns an SDL surface (a decoded image).
using Surface = std::unique_ptr<SDL_Surface, SdlDeleter<SDL_DestroySurface>>;

} // namespace gfx::internal
