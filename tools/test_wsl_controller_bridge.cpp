#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include "core/controller_bridge_protocol.h"
#include "core/wsl_controller_bridge.h"

int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--pipe") == 0) {
        controller_bridge::State packet;
        packet.connected = 1;
        packet.sequence = 1;
        packet.buttons = 0x1211;
        packet.lx = 32767;
        packet.ly = -32768;
        packet.rx = -32768;
        packet.ry = 32767;
        packet.lt = 0;
        packet.rt = 255;
        // Fragment a wire message deliberately: pipe reads need not preserve message boundaries.
        const auto *bytes = reinterpret_cast<const uint8_t *>(&packet);
        for (size_t i = 0; i < sizeof(packet); i += 3)
            assert(write(STDOUT_FILENO, bytes + i, 3) == 3);
        controller_bridge::Rumble effect;
        size_t used = 0;
        while (used < sizeof(effect)) {
            ssize_t n = read(STDIN_FILENO, reinterpret_cast<uint8_t *>(&effect) + used, sizeof(effect) - used);
            if (n <= 0)
                return 1;
            used += size_t(n);
        }
        if (effect.magic != controller_bridge::kRumbleMagic || effect.left != 0x1234 || effect.right != 0x5678)
            return 1;
        packet.connected = 0;
        packet.sequence = 2;
        assert(write(STDOUT_FILENO, &packet, sizeof(packet)) == sizeof(packet));
        SDL_Delay(100);
        return 0;
    }
    assert(SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS));
    {
        input::WslControllerBridge bridge;
        assert(bridge.Start("/proc/self/exe"));
        SDL_Gamepad *pad = nullptr;
        bool checked = false, detached = false;
        uint64_t until = SDL_GetTicks() + 5000;
        while (SDL_GetTicks() < until) {
            SDL_PumpEvents();
            bridge.Pump();
            if (!pad) {
                int count = 0;
                auto *ids = SDL_GetGamepads(&count);
                for (int i = 0; i < count; i++)
                    if (std::strstr(SDL_GetGamepadNameForID(ids[i]), "WSL test bridge"))
                        pad = SDL_OpenGamepad(ids[i]);
                SDL_free(ids);
            }
            SDL_PumpEvents();
            if (pad && !checked && SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) == 32767) {
                assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) == 32767);
                assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX) == -32768);
                assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY) == -32768);
                assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) == 0);
                assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) == 32767);
                assert(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH));
                assert(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START));
                assert(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP));
                assert(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
                assert(SDL_RumbleGamepad(pad, 0x1234, 0x5678, 1000));
                checked = true;
            }
            if (checked && !SDL_GamepadConnected(pad)) {
                detached = true;
                break;
            }
            SDL_Delay(1);
        }
        assert(checked && detached);
        SDL_CloseGamepad(pad);
    }
    SDL_Quit();
    std::puts("PASS: bridge framing, buttons, signed axes, trigger endpoints, rumble and disconnect");
}
