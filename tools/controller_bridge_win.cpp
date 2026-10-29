// Development helper: stream a Windows XInput controller over redirected stdio to WSL.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include "core/controller_bridge_protocol.h"

int main(int argc, char **argv) {
    HMODULE dll = LoadLibraryW(L"xinput1_4.dll");
    if (!dll)
        dll = LoadLibraryW(L"xinput9_1_0.dll");
    if (!dll)
        return 2;
    auto get = reinterpret_cast<DWORD(WINAPI *)(DWORD, XINPUT_STATE *)>(GetProcAddress(dll, "XInputGetState"));
    auto set = reinterpret_cast<DWORD(WINAPI *)(DWORD, XINPUT_VIBRATION *)>(GetProcAddress(dll, "XInputSetState"));
    if (!get || !set)
        return 2;
    if (argc == 2 && std::strcmp(argv[1], "--probe") == 0) {
        unsigned count = 0;
        for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
            XINPUT_STATE s{};
            if (get(i, &s) != ERROR_SUCCESS)
                continue;
            std::printf("XInput port %lu: connected, buttons=%04X left=(%d,%d) right=(%d,%d) triggers=(%u,%u)\n",
                        static_cast<unsigned long>(i), s.Gamepad.wButtons, s.Gamepad.sThumbLX, s.Gamepad.sThumbLY,
                        s.Gamepad.sThumbRX, s.Gamepad.sThumbRY, s.Gamepad.bLeftTrigger, s.Gamepad.bRightTrigger);
            count++;
        }
        FreeLibrary(dll);
        return count ? 0 : 1;
    }
    if (argc != 2 || std::strcmp(argv[1], "--pipe") != 0)
        return 2;
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE), input = GetStdHandle(STD_INPUT_HANDLE);
    // Keep the selected physical port until unplugged; another controller may then take over.
    DWORD selected = XUSER_MAX_COUNT;
    uint32_t sequence = 0;
    for (;;) {
        XINPUT_STATE s{};
        if (selected == XUSER_MAX_COUNT || get(selected, &s) != ERROR_SUCCESS) {
            selected = XUSER_MAX_COUNT;
            for (DWORD i = 0; i < XUSER_MAX_COUNT; i++)
                if (get(i, &s) == ERROR_SUCCESS) {
                    selected = i;
                    break;
                }
        }
        controller_bridge::State p;
        p.sequence = ++sequence;
        p.connected = selected != XUSER_MAX_COUNT;
        if (p.connected) {
            const auto &g = s.Gamepad;
            p.buttons = g.wButtons;
            p.lt = g.bLeftTrigger;
            p.rt = g.bRightTrigger;
            p.lx = g.sThumbLX;
            p.ly = g.sThumbLY;
            p.rx = g.sThumbRX;
            p.ry = g.sThumbRY;
        }
        DWORD written = 0;
        if (!WriteFile(output, &p, sizeof(p), &written, nullptr) || written != sizeof(p))
            break;
        DWORD available = 0;
        while (PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr) &&
               available >= sizeof(controller_bridge::Rumble)) {
            controller_bridge::Rumble r;
            DWORD read = 0;
            if (!ReadFile(input, &r, sizeof(r), &read, nullptr) || read != sizeof(r))
                break;
            if (r.magic == controller_bridge::kRumbleMagic && selected != XUSER_MAX_COUNT) {
                XINPUT_VIBRATION vibration{r.left, r.right};
                set(selected, &vibration);
            }
        }
        Sleep(8);
    }
    if (selected != XUSER_MAX_COUNT) {
        XINPUT_VIBRATION zero{};
        set(selected, &zero);
    }
    FreeLibrary(dll);
    return 0;
}
