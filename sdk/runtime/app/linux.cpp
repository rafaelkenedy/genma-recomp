// Experimental native Linux host. SDL events belong to this thread.
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <unistd.h>
#include "app/app.h"
#include "core/input.h"
#ifndef XBE_DISABLE_CONTROLLER_BRIDGE
#include "core/wsl_controller_bridge.h"
#endif
#include "core/log.h"
#include "gpu/backend.h"
#include "image_symbols.h"
#include "kernel/kernel.h"

namespace app {
namespace {
SDL_Window *g_window;
std::atomic<uint64_t> g_size{uint64_t(1280) << 32 | 800};
std::recursive_mutex g_ui;
} // namespace
void *Window() {
    return g_window;
}
Mode LinuxWindowSize() {
    uint64_t n = g_size.load();
    return {uint32_t(n >> 32), uint32_t(n)};
}
std::filesystem::path ExeDir() {
    char path[4096];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    return n > 0 ? std::filesystem::path(std::string(path, size_t(n))).parent_path() : std::filesystem::current_path();
}
int Instance() {
    return 0;
}
bool MenuOpen() {
    return false;
}
void ToggleMenu() {
    LOGR("linux: F1 settings menu is not implemented yet; use settings.toml");
}
void AskQuit() {
    SDL_Event e{};
    e.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&e);
}
void DrawMenu(double) {}
std::recursive_mutex &UiLock() {
    return g_ui;
}
void ApplyDisplay() {
    LOGR("linux: display settings apply on next launch; F11 toggles fullscreen");
}
void OpenGameWindow(bool) {}
void SplashFirstFrame() {}
bool OpenSplash() {
    return false;
}
void CloseSplash() {}
bool StartHandover() {
    return false;
}
bool HandingOver() {
    return false;
}
void FinishHandover() {}
void LogSystem() {
    LOGR("Experimental native Linux SDL/Vulkan runtime");
    LogSettings();
}
void Pace() {
    using C = std::chrono::steady_clock;
    static C::time_point next = C::now();
    uint32_t cap = Get().fps_cap;
    if (!cap) {
        next = C::now();
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    next += period;
    if (next < C::now() - period)
        next = C::now();
    std::this_thread::sleep_until(next);
}
} // namespace app

int main() {
    logx::Init((app::CacheDir() / "logs").string(), app::kGame.log, false);
    app::Load();
    app::LogSystem();
    if (app::kGame.init)
        app::kGame.init();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        LOGE("SDL: %s", SDL_GetError());
        return 1;
    }
    app::g_window = SDL_CreateWindow("Genma Onimusha — Linux Vulkan (experimental)", 1280, 800,
                                     SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!app::g_window) {
        LOGE("window: %s", SDL_GetError());
        return 1;
    }
    app::GameFiles game;
    if (!app::FindGame(game))
        return 1;
    input::Init();
#ifndef XBE_DISABLE_CONTROLLER_BRIDGE
    input::WslControllerBridge controller_bridge;
    if(const char* helper=std::getenv("XBE_CONTROLLER_BRIDGE")) {
        if(!controller_bridge.Start(helper))return 1;
    }
#endif
    // Keep writable user data outside a packaged installation.
    if (!kernel::Boot(std::move(game.xbe), IMAGE_XBE, game.dir.string(), (app::DataDir() / "hdd").string()))
        return 1;
    uint64_t until = 0;
    if (const char *s = std::getenv("XBE_RUN_SECONDS"))
        until = SDL_GetTicks() + std::strtoull(s, nullptr, 10) * 1000;
    for (;;) {
        SDL_PumpEvents();
#ifndef XBE_DISABLE_CONTROLLER_BRIDGE
        controller_bridge.Pump();
#endif
        input::Pump();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE))
                kernel::Shutdown(0);
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                if (e.key.key == SDLK_F11)
                    SDL_SetWindowFullscreen(app::g_window,
                                            !(SDL_GetWindowFlags(app::g_window) & SDL_WINDOW_FULLSCREEN));
                if (e.key.key == SDLK_F1)
                    app::ToggleMenu();
                if (e.key.key == SDLK_F3)
                    gpu::g_capture = true;
            }
        }
        input::Pad keyboard;
        keyboard.connected = true;
        if (SDL_GetWindowFlags(app::g_window) & SDL_WINDOW_INPUT_FOCUS) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            keyboard.buttons = uint16_t(keys[SDL_SCANCODE_UP] | (keys[SDL_SCANCODE_DOWN] << 1) |
                                        (keys[SDL_SCANCODE_LEFT] << 2) | (keys[SDL_SCANCODE_RIGHT] << 3) |
                                        (keys[SDL_SCANCODE_RETURN] << 4) | (keys[SDL_SCANCODE_BACKSPACE] << 5));
            constexpr SDL_Scancode analog[] = {SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V,
                                               SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_Q, SDL_SCANCODE_E};
            for (int i = 0; i < 8; i++)
                keyboard.analog[i] = keys[analog[i]] ? 255 : 0;
            keyboard.lx = int16_t((int(keys[SDL_SCANCODE_D]) - int(keys[SDL_SCANCODE_A])) * 32767);
            keyboard.ly = int16_t((int(keys[SDL_SCANCODE_W]) - int(keys[SDL_SCANCODE_S])) * 32767);
        }
        input::SetKeyboardPad(keyboard);
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(app::g_window, &w, &h);
        app::g_size = uint64_t(uint32_t(w)) << 32 | uint32_t(h);
        if (until && SDL_GetTicks() >= until)
            kernel::Shutdown(0);
        SDL_Delay(2);
    }
}
