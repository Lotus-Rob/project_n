#include "gfx/internal/frame/frame.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"

namespace gfx::internal {

std::expected<Frame, core::Error> AcquireFrame(SDL_GPUDevice* device, SDL_Window* window) {
    PN_PROFILE_ZONE();
    Frame frame{ .commands = SDL_AcquireGPUCommandBuffer(device) };
    if (frame.commands == nullptr) {
        return std::unexpected{ core::Error{ std::format("no command buffer: {}", SDL_GetError()) } };
    }
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(frame.commands, window, &frame.swapchain, &frame.width, &frame.height)) {
        core::Error error{ std::format("no swapchain texture: {}", SDL_GetError()) };
        SDL_CancelGPUCommandBuffer(frame.commands); // allowed: no swapchain texture was acquired (§8.3)
        return std::unexpected{ std::move(error) };
    }
    return frame;
}

std::expected<void, core::Error> SubmitFrame(const Frame& frame) {
    PN_PROFILE_ZONE();
    if (!SDL_SubmitGPUCommandBuffer(frame.commands)) {
        return std::unexpected{ core::Error{ std::format("the frame wasn't submitted: {}", SDL_GetError()) } };
    }
    return {};
}

} // namespace gfx::internal
