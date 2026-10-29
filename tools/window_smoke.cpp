// Native host-window diagnostic: software reference or explicit SDL GPU Vulkan presentation.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <cstring>
#include "window_vulkan.h"

namespace {
void Check(bool ok, const char* operation) {
    if (!ok) throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
struct QuitSDL { ~QuitSDL() { SDL_Quit(); } };
struct Pads {
    std::map<SDL_JoystickID, SDL_Gamepad*> devices;
    void Add(SDL_JoystickID id) {
        if (devices.contains(id) || !SDL_IsGamepad(id)) return;
        if (auto* pad = SDL_OpenGamepad(id)) {
            devices[id] = pad;
            SDL_Log("gamepad added: id=%u name=%s", id, SDL_GetGamepadName(pad));
        } else SDL_Log("gamepad open failed: %s", SDL_GetError());
    }
    void Remove(SDL_JoystickID id) {
        if (auto it = devices.find(id); it != devices.end()) {
            SDL_CloseGamepad(it->second); devices.erase(it);
            SDL_Log("gamepad removed: id=%u", id);
        }
    }
    ~Pads() { for (auto [id, pad] : devices) { (void)id; SDL_CloseGamepad(pad); } }
};
struct VirtualPad {
    SDL_JoystickID id = 0;
    SDL_Joystick* joystick = nullptr;
    void Attach() {
        SDL_VirtualJoystickDesc d{};
        SDL_INIT_INTERFACE(&d);
        d.name = "Genma window self-test (virtual)";
        d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        d.naxes = SDL_GAMEPAD_AXIS_COUNT; d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        d.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        id = SDL_AttachVirtualJoystick(&d);
        Check(id != 0, "attach virtual gamepad");
        joystick = SDL_OpenJoystick(id);
        Check(joystick != nullptr, "open virtual joystick");
    }
    void Detach() {
        if (joystick) SDL_CloseJoystick(joystick);
        joystick = nullptr;
        if (id) Check(SDL_DetachVirtualJoystick(id), "detach virtual gamepad");
        id = 0;
    }
    ~VirtualPad() {
        if (joystick) SDL_CloseJoystick(joystick);
        if (id) SDL_DetachVirtualJoystick(id);
    }
};
void Key(SDL_WindowID window, SDL_Keycode key) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = window; event.key.key = key; event.key.down = true;
    Check(SDL_PushEvent(&event), "push self-test key event");
}
void Draw(SDL_Renderer* renderer, float x, float y, size_t pads, unsigned keys, bool vulkan) {
    Check(SDL_SetRenderDrawColor(renderer, 12, 17, 25, 255), "background color");
    Check(SDL_RenderClear(renderer), "clear");
    SDL_SetRenderDrawColor(renderer, 45, 61, 80, 255);
    for (int i = 0; i <= 960; i += 40) SDL_RenderLine(renderer, float(i), 140, float(i), 640);
    for (int i = 160; i <= 640; i += 40) SDL_RenderLine(renderer, 0, float(i), 960, float(i));
    SDL_SetRenderDrawColor(renderer, 77, 193, 165, 255);
    SDL_FRect border{16, 140, 928, 500};
    Check(SDL_RenderRect(renderer, &border), "draw 4:3 test frame");
    for (int i = 0; i < 4; ++i) {
        const Uint8 colors[4][3] = {{221,76,88},{64,162,216},{227,184,67},{108,179,110}};
        SDL_SetRenderDrawColor(renderer, colors[i][0], colors[i][1], colors[i][2], 255);
        SDL_FRect swatch{float(36+i*100), 170, 80, 38}; SDL_RenderFillRect(renderer, &swatch);
    }
    SDL_SetRenderDrawColor(renderer, 237, 241, 247, 255);
    for (int i = 0; i < 120; ++i) {
        const float a = float(i) * 6.2831853f / 120;
        const float b = float(i+1) * 6.2831853f / 120;
        SDL_RenderLine(renderer, 480+100*std::cos(a), 410+100*std::sin(a),
                      480+100*std::cos(b), 410+100*std::sin(b));
    }
    SDL_SetRenderDrawColor(renderer, 77, 193, 165, 255);
    SDL_FRect marker{470+x*160, 400+y*100, 20, 20}; SDL_RenderFillRect(renderer, &marker);
    SDL_SetRenderDrawColor(renderer, 237, 241, 247, 255);
    Check(SDL_RenderDebugText(renderer, 28, 32, "GENMA ONIMUSHA / LINUX WINDOW TEST"), "draw text");
    SDL_RenderDebugText(renderer, 28, 56, vulkan ? "SDL GPU VULKAN PRESENTATION / CPU TEST PATTERN" : "SDL3 SOFTWARE PRESENTATION - GAME RENDERER NOT LOADED");
    SDL_RenderDebugText(renderer, 28, 80, "1280x800 window / 960x720 logical image / preserved 4:3");
    SDL_RenderDebugText(renderer, 28, 104, "F11: fullscreen  R: resize  Arrows / left stick: move  Esc: quit");
    SDL_RenderDebugTextFormat(renderer, 28, 664, "Gamepads: %zu   Keyboard events: %u   Input: %.2f, %.2f", pads, keys, x, y);
    SDL_RenderDebugText(renderer, 28, 688, "Circle must stay round; resizing adds letterbox bars.");
}
} // namespace

int main(int argc, char** argv) {
    bool selfTest = false, vulkan = false;
    double seconds = 0;
    std::string screenshot;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--self-test") selfTest = true;
        else if (arg == "--backend" && i+1 < argc) {
            const std::string backend = argv[++i];
            if (backend != "software" && backend != "vulkan") return 2;
            vulkan = backend == "vulkan";
        }
        else if (arg == "--seconds" && i+1 < argc) {
            char* end = nullptr;
            seconds = std::strtod(argv[++i], &end);
            if (!end || *end || !std::isfinite(seconds) || seconds <= 0 || seconds > 3600) return 2;
        } else if (arg == "--screenshot" && i+1 < argc) screenshot = argv[++i];
        else { std::fprintf(stderr, "usage: genma_window_smoke [--backend software|vulkan] [--self-test] [--seconds 1..3600] [--screenshot file.bmp]\n"); return 2; }
    }
    try {
        SDL_SetMainReady();
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        SDL_Log("initializing SDL video and gamepads");
        Check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "SDL_Init");
        QuitSDL quit;
        SDL_Log("creating SDL test window");
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Genma Linux window test | F11 fullscreen | Esc quit", 1280, 800,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY), SDL_DestroyWindow);
        Check(bool(window), "create window");
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pattern(nullptr, SDL_DestroySurface);
        std::unique_ptr<VulkanPresenter> gpu;
        if (vulkan) {
            pattern.reset(SDL_CreateSurface(960, 720, SDL_PIXELFORMAT_RGBA32));
            Check(bool(pattern), "create Vulkan reference pattern");
            gpu = std::make_unique<VulkanPresenter>(); gpu->Init(window.get());
        }
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
            vulkan ? SDL_CreateSoftwareRenderer(pattern.get()) : SDL_CreateRenderer(window.get(), "software"), SDL_DestroyRenderer);
        Check(bool(renderer), "create software renderer");
        Check(SDL_SetRenderLogicalPresentation(renderer.get(), 960, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX), "set 4:3 presentation");
        SDL_Log("started: SDL=%d video=%s presentation=%s pattern-renderer=%s", SDL_GetVersion(), SDL_GetCurrentVideoDriver(), vulkan ? "vulkan" : "software", SDL_GetRendererName(renderer.get()));
        Pads pads;
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        for (int i = 0; i < count; ++i) pads.Add(ids[i]);
        SDL_free(ids);
        VirtualPad virtualPad;
        SDL_JoystickID testId = 0;
        bool running = true, sawVirtualAxis = false, sawVirtualButton = false, sawVirtualRemoval = false, initialReadback = false;
        unsigned keys = 0, resizeEvents = 0;
        float x = 0, y = 0;
        unsigned stage = 0;
        const Uint64 start = SDL_GetTicks();
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
                else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    ++keys;
                    SDL_Log("keyboard: key=%s", SDL_GetKeyName(event.key.key));
                    switch (event.key.key) {
                        case SDLK_ESCAPE: running = false; break;
                        case SDLK_F11: Check(SDL_SetWindowFullscreen(window.get(), !(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN)), "toggle fullscreen"); break;
                        case SDLK_R: Check(SDL_SetWindowSize(window.get(), 960, 720), "resize"); break;
                        case SDLK_RIGHT: x += 0.1f; break;
                        case SDLK_LEFT: x -= 0.1f; break;
                        case SDLK_DOWN: y += 0.1f; break;
                        case SDLK_UP: y -= 0.1f; break;
                        default: break;
                    }
                } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                    ++resizeEvents;
                    SDL_Log("window pixels: %dx%d", event.window.data1, event.window.data2);
                } else if (event.type == SDL_EVENT_GAMEPAD_ADDED) pads.Add(event.gdevice.which);
                else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                    if (event.gdevice.which == testId) sawVirtualRemoval = true;
                    pads.Remove(event.gdevice.which);
                } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                    if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) x = event.gaxis.value / 32767.f;
                    if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) y = event.gaxis.value / 32767.f;
                    if (event.gaxis.which == testId && event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX && event.gaxis.value == 24576) sawVirtualAxis = true;
                } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                    if (event.gbutton.which == testId && event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) sawVirtualButton = true;
                    SDL_Log("gamepad button: id=%u button=%u", event.gbutton.which, unsigned(event.gbutton.button));
                } else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                    SDL_Log("window focus: %s", event.type == SDL_EVENT_WINDOW_FOCUS_GAINED ? "gained" : "lost");
                }
            }
            const double elapsed = double(SDL_GetTicks()-start)/1000;
            if (selfTest && elapsed >= double(stage+1)*0.4) {
                switch (stage++) {
                    case 0: virtualPad.Attach(); testId = virtualPad.id; break;
                    case 1: Check(pads.devices.contains(testId), "virtual gamepad hotplug event");
                            Check(SDL_SetJoystickVirtualAxis(virtualPad.joystick, SDL_GAMEPAD_AXIS_LEFTX, 24576), "set virtual axis");
                            Check(SDL_SetJoystickVirtualButton(virtualPad.joystick, SDL_GAMEPAD_BUTTON_SOUTH, true), "set virtual button"); break;
                    case 2: Check(sawVirtualAxis && sawVirtualButton, "virtual gamepad axis/button events"); Key(SDL_GetWindowID(window.get()), SDLK_RIGHT); break;
                    case 3: Check(keys > 0, "keyboard event"); Key(SDL_GetWindowID(window.get()), SDLK_R); break;
                    case 4: { int w=0,h=0; Check(SDL_GetWindowSize(window.get(), &w, &h) && w == 960 && h == 720, "window resize result");
                              Key(SDL_GetWindowID(window.get()), SDLK_F11); break; }
                    case 5: Check((SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) != 0, "fullscreen enabled");
                            Key(SDL_GetWindowID(window.get()), SDLK_F11); break;
                    case 6: Check((SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) == 0, "fullscreen disabled");
                            virtualPad.Detach(); break;
                    case 7: Check(sawVirtualRemoval && !pads.devices.contains(testId), "virtual gamepad removal event");
                            Check(resizeEvents > 0, "pixel resize event");
                            Key(SDL_GetWindowID(window.get()), SDLK_ESCAPE);
                            break;
                }
            }
            x = std::clamp(x, -1.f, 1.f); y = std::clamp(y, -1.f, 1.f);
            Draw(renderer.get(), x, y, pads.devices.size(), keys, vulkan);
            const bool finishing = !running || (seconds && elapsed >= seconds);
            const bool capture = (finishing && (selfTest || !screenshot.empty())) || (vulkan && selfTest && !initialReadback);
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> gpuSurface(nullptr, SDL_DestroySurface);
            if (vulkan) {
                Check(SDL_FlushRenderer(renderer.get()), "finish CPU reference pattern");
                gpuSurface.reset(gpu->Present(pattern.get(), capture));
            }
            if (capture && (!vulkan || gpuSurface || finishing)) {
                std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
                    vulkan ? gpuSurface.release() : SDL_RenderReadPixels(renderer.get(), nullptr), SDL_DestroySurface);
                Check(bool(surface), "read diagnostic frame");
                if (selfTest) {
                    SDL_FRect presentation{};
                    if (vulkan) presentation = gpu->viewport;
                    else Check(SDL_GetRenderLogicalPresentationRect(renderer.get(), &presentation), "query presentation");
                    Check(presentation.h > 0 && std::abs(presentation.w / presentation.h - 4.f/3) < 0.005f, "4:3 presentation ratio");
                    Uint8 r=0,g=0,b=0,a=0;
                    const int px = int(presentation.x + presentation.w*80/960);
                    const int py = int(presentation.y + presentation.h*188/720);
                    Check(SDL_ReadSurfacePixel(surface.get(), px, py, &r, &g, &b, &a), "read test color");
                    Check(r == 221 && g == 76 && b == 88, "rendered test color");
                    if (vulkan) {
                        if (presentation.x >= 2) {
                            Check(SDL_ReadSurfacePixel(surface.get(), 0, surface->h/2, &r, &g, &b, &a), "read letterbox");
                            Check(r == 0 && g == 0 && b == 0, "Vulkan black letterbox pixels");
                        }
                        if (surface->w == 960 && surface->h == 720) {
                            unsigned different = 0;
                            for (int row=0; row<720; ++row) {
                                const auto* actual = static_cast<const Uint8*>(surface->pixels)+row*surface->pitch;
                                const auto* expected = static_cast<const Uint8*>(pattern->pixels)+row*pattern->pitch;
                                for (int col=0; col<960; ++col)
                                    if (std::memcmp(actual+col*4, expected+col*4, 4)) ++different;
                            }
                            SDL_Log("Vulkan readback comparison: %u differing pixels out of 691200", different);
                            Check(different == 0, "Vulkan readback matches CPU reference");
                        }
                        initialReadback = true;
                    }
                }
                if (finishing && !screenshot.empty()) {
                    Check(SDL_SaveBMP(surface.get(), screenshot.c_str()), "save diagnostic screenshot");
                    SDL_Log("screenshot: %s", screenshot.c_str());
                }
                if (finishing) screenshot.clear();
            }
            if (!vulkan) Check(SDL_RenderPresent(renderer.get()), "present");
            if (seconds && elapsed >= seconds) running = false;
            if (selfTest && elapsed > 15) throw std::runtime_error("self-test timed out");
            SDL_Delay(16);
        }
        if (selfTest) {
            Check(stage >= 8, "self-test completed before close");
            if (vulkan) Check(initialReadback && gpu->frames > 1 && gpu->readbacks >= 2, "Vulkan presentation/readbacks completed");
            SDL_Log("PASS: window, keyboard events, resize, fullscreen, virtual gamepad attach/axis/button/remove, 4:3 ratio and rendered pixels; presentation=%s", vulkan ? "vulkan" : "software");
        }
        if (vulkan) SDL_Log("Vulkan totals: frames=%u readbacks=%u", gpu->frames, gpu->readbacks);
        SDL_Log("exit: clean");
        return 0;
    } catch (const std::exception& e) {
        SDL_Log("FAIL: %s", e.what()); return 1;
    }
}
