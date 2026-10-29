#pragma once
#include <SDL3/SDL.h>

// Presentation-only proof: CPU pattern -> GPU texture -> letterboxed target -> swapchain.
// No guest shaders, game assets, depth or stencil are loaded here.
class VulkanPresenter {
    SDL_GPUDevice* device_ = nullptr;
    SDL_Window* window_ = nullptr;
    SDL_GPUTexture* source_ = nullptr;
    SDL_GPUTexture* output_ = nullptr;
    SDL_GPUTransferBuffer* upload_ = nullptr;
    SDL_GPUTransferBuffer* download_ = nullptr;
    Uint32 width_ = 0, height_ = 0;
public:
    SDL_FRect viewport{};
    unsigned frames = 0, readbacks = 0;
    VulkanPresenter() = default;
    VulkanPresenter(const VulkanPresenter&) = delete;
    VulkanPresenter& operator=(const VulkanPresenter&) = delete;
    ~VulkanPresenter();
    void Init(SDL_Window* window);
    // Caller owns the optional readback surface. Null when minimized or no capture requested.
    SDL_Surface* Present(SDL_Surface* pattern, bool capture);
};
