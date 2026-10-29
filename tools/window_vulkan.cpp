#include "window_vulkan.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {
void Require(bool ok, const char* op) {
    if (!ok) throw std::runtime_error(std::string(op) + ": " + SDL_GetError());
}
SDL_GPUTexture* Texture(SDL_GPUDevice* device, Uint32 w, Uint32 h, bool target) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (target ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
    info.width = w; info.height = h; info.layer_count_or_depth = 1; info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    auto* result = SDL_CreateGPUTexture(device, &info);
    Require(result != nullptr, "create Vulkan texture");
    return result;
}
struct Command {
    SDL_GPUCommandBuffer* buffer;
    bool acquiredSwapchain = false;
    ~Command() {
        if (buffer) {
            // SDL forbids cancel after acquiring a swapchain texture.
            if (acquiredSwapchain) SDL_SubmitGPUCommandBuffer(buffer);
            else SDL_CancelGPUCommandBuffer(buffer);
        }
    }
};
}

void VulkanPresenter::Init(SDL_Window* window) {
    SDL_Log("creating Vulkan GPU device");
    device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, "vulkan");
    Require(device_ != nullptr, "create explicit Vulkan device (no software-renderer fallback)");
    Require(std::strcmp(SDL_GetGPUDeviceDriver(device_), "vulkan") == 0, "verify Vulkan backend");
    SDL_Log("claiming SDL window for Vulkan GPU device");
    Require(SDL_ClaimWindowForGPUDevice(device_, window), "claim Vulkan window");
    window_ = window;
    SDL_Log("Vulkan device: backend=%s name=%s", SDL_GetGPUDeviceDriver(device_),
            SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device_), SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown"));
    source_ = Texture(device_, 960, 720, false);
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; info.size = 960*720*4;
    upload_ = SDL_CreateGPUTransferBuffer(device_, &info);
    Require(upload_ != nullptr, "create Vulkan upload buffer");
}

VulkanPresenter::~VulkanPresenter() {
    if (!device_) return;
    SDL_WaitForGPUIdle(device_);
    if (download_) SDL_ReleaseGPUTransferBuffer(device_, download_);
    if (upload_) SDL_ReleaseGPUTransferBuffer(device_, upload_);
    if (output_) SDL_ReleaseGPUTexture(device_, output_);
    if (source_) SDL_ReleaseGPUTexture(device_, source_);
    if (window_) SDL_ReleaseWindowFromGPUDevice(device_, window_);
    SDL_DestroyGPUDevice(device_);
}

SDL_Surface* VulkanPresenter::Present(SDL_Surface* pattern, bool capture) {
    Require(pattern->w == 960 && pattern->h == 720 && pattern->format == SDL_PIXELFORMAT_RGBA32, "RGBA source pattern");
    auto* mapped = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device_, upload_, true));
    Require(mapped != nullptr, "map Vulkan upload buffer");
    for (int y = 0; y < 720; ++y)
        std::memcpy(mapped + y*960*4, static_cast<const Uint8*>(pattern->pixels) + y*pattern->pitch, 960*4);
    SDL_UnmapGPUTransferBuffer(device_, upload_);
    Command command{SDL_AcquireGPUCommandBuffer(device_)};
    Require(command.buffer != nullptr, "acquire Vulkan commands");
    SDL_GPUTexture* swapchain = nullptr;
    Uint32 w=0,h=0;
    Require(SDL_WaitAndAcquireGPUSwapchainTexture(command.buffer, window_, &swapchain, &w, &h), "acquire Vulkan swapchain");
    if (!swapchain || !w || !h) return nullptr; // valid while minimized; RAII releases commands
    command.acquiredSwapchain = true;
    Require(w <= 16384 && h <= 16384, "diagnostic output dimensions");
    if (!output_ || w != width_ || h != height_) {
        auto* replacement = Texture(device_, w, h, true);
        if (output_) SDL_ReleaseGPUTexture(device_, output_);
        output_ = replacement;
        if (download_) SDL_ReleaseGPUTransferBuffer(device_, download_);
        download_ = nullptr;
        width_ = w; height_ = h;
        SDL_Log("Vulkan swapchain: %ux%u format=%d", w, h, int(SDL_GetGPUSwapchainTextureFormat(device_, window_)));
    }
    const float scale = std::min(float(w)/960, float(h)/720);
    const Uint32 vw = Uint32(std::round(960*scale)), vh = Uint32(std::round(720*scale));
    const Uint32 vx = (w-vw)/2, vy = (h-vh)/2;
    viewport = {float(vx), float(vy), float(vw), float(vh)};
    auto* copy = SDL_BeginGPUCopyPass(command.buffer);
    Require(copy != nullptr, "begin Vulkan upload");
    SDL_GPUTextureTransferInfo from{upload_, 0, 960, 720};
    SDL_GPUTextureRegion to{source_, 0, 0, 0, 0, 0, 960, 720, 1};
    SDL_UploadToGPUTexture(copy, &from, &to, true);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUBlitInfo blit{};
    blit.source = {source_, 0, 0, 0, 0, 960, 720};
    blit.destination = {output_, 0, 0, vx, vy, vw, vh};
    blit.load_op = SDL_GPU_LOADOP_CLEAR; blit.clear_color = {0,0,0,1};
    blit.filter = SDL_GPU_FILTER_NEAREST; blit.cycle = true;
    SDL_BlitGPUTexture(command.buffer, &blit);
    blit.source = {output_, 0, 0, 0, 0, w, h};
    blit.destination = {swapchain, 0, 0, 0, 0, w, h};
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE; blit.cycle = false;
    SDL_BlitGPUTexture(command.buffer, &blit);
    if (!capture) {
        auto* submitted = command.buffer; command.buffer = nullptr;
        Require(SDL_SubmitGPUCommandBuffer(submitted), "submit Vulkan presentation");
        ++frames;
        return nullptr;
    }
    if (!download_) {
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; info.size = w*h*4;
        download_ = SDL_CreateGPUTransferBuffer(device_, &info);
        Require(download_ != nullptr, "create Vulkan readback buffer");
    }
    copy = SDL_BeginGPUCopyPass(command.buffer);
    Require(copy != nullptr, "begin Vulkan readback");
    SDL_GPUTextureRegion fromTexture{output_, 0, 0, 0, 0, 0, w, h, 1};
    SDL_GPUTextureTransferInfo toBuffer{download_, 0, w, h};
    SDL_DownloadFromGPUTexture(copy, &fromTexture, &toBuffer);
    SDL_EndGPUCopyPass(copy);
    auto* submitted = command.buffer; command.buffer = nullptr;
    auto* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(submitted);
    Require(fence != nullptr, "submit Vulkan readback");
    const bool waited = SDL_WaitForGPUFences(device_, true, &fence, 1);
    SDL_ReleaseGPUFence(device_, fence);
    Require(waited, "wait Vulkan readback fence");
    mapped = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device_, download_, false));
    Require(mapped != nullptr, "map completed Vulkan readback");
    auto* surface = SDL_CreateSurface(int(w), int(h), SDL_PIXELFORMAT_RGBA32);
    if (surface) for (Uint32 y=0; y<h; ++y)
        std::memcpy(static_cast<Uint8*>(surface->pixels) + y*surface->pitch, mapped + y*w*4, w*4);
    SDL_UnmapGPUTransferBuffer(device_, download_);
    Require(surface != nullptr, "allocate Vulkan screenshot");
    ++frames; ++readbacks;
    return surface;
}
