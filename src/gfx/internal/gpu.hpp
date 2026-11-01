#pragma once

/// @file
/// @ingroup gfx
/// Owners of SDL_GPU objects (design doc 0002 §8.2): each is released once, through the device that
/// made it, and the device outlives them.

#include "core/log.hpp"

#include <SDL3/SDL.h>

#include <memory>

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

    /// Releases the device's claim on `window`; the window itself stays open.
    void operator()(SDL_Window* window) const {
        SDL_ReleaseWindowFromGPUDevice(device, window);
    }
};

/// Owns an object made by a device: a shader, a pipeline, or a claimed window.
template<typename Object>
using Gpu = std::unique_ptr<Object, GpuDeleter>;

} // namespace gfx::internal
