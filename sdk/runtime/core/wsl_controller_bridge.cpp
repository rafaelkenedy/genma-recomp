#ifdef __linux__
#include "core/wsl_controller_bridge.h"
#include "core/controller_bridge_protocol.h"
#include "core/log.h"
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;

namespace input {
struct WslControllerBridge::Impl {
    pid_t child = 0;
    int read_fd = -1, write_fd = -1;
    SDL_JoystickID id = 0;
    SDL_Joystick *joystick = nullptr;
    uint64_t received = 0, next_retry = 0;
    std::array<uint8_t, sizeof(controller_bridge::State)> bytes{};
    size_t used = 0;
    std::atomic<uint64_t> rumble{0};
    uint64_t sent_rumble = 0;
    bool reported_input = false;

    static bool Rumble(void *user, Uint16 left, Uint16 right) {
        auto *self = static_cast<Impl *>(user);
        // Generation in the high half also sends an explicit stop after an earlier nonzero effect.
        uint64_t old = self->rumble.load();
        self->rumble.store(((old + (1ull << 32)) & 0xffffffff00000000ull) | uint64_t(left) | uint64_t(right) << 16);
        return true;
    }
    void Detach() {
        if (joystick)
            SDL_CloseJoystick(joystick);
        joystick = nullptr;
        if (id)
            SDL_DetachVirtualJoystick(id);
        if (id)
            LOGR("input: Windows controller bridge disconnected");
        id = 0;
    }
    bool Attach() {
        if (joystick)
            return true;
        SDL_VirtualJoystickDesc desc{};
        SDL_INIT_INTERFACE(&desc);
        desc.name = "Windows XInput controller (WSL test bridge)";
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        desc.userdata = this;
        desc.Rumble = Rumble;
        id = SDL_AttachVirtualJoystick(&desc);
        if (id)
            joystick = SDL_OpenJoystick(id);
        if (!joystick) {
            LOGE("input: bridge virtual gamepad: %s", SDL_GetError());
            Detach();
            return false;
        }
        LOGR("input: physical Windows XInput controller connected through WSL test bridge");
        return true;
    }
    bool Apply(const controller_bridge::State &p) {
        if (p.magic != controller_bridge::kStateMagic || p.connected > 1)
            return false;
        received = SDL_GetTicks();
        if (!p.connected) {
            Detach();
            return true;
        }
        if (!Attach())
            return false;
        // XInput Y is up; SDL Y is down. input::Poll converts it back to Xbox convention.
        const Sint16 axes[] = {p.lx,
                               Sint16(-1 - int(p.ly)),
                               p.rx,
                               Sint16(-1 - int(p.ry)),
                               Sint16(int(p.lt) * 65535 / 255 - 32768),
                               Sint16(int(p.rt) * 65535 / 255 - 32768)};
        for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; i++)
            if (!SDL_SetJoystickVirtualAxis(joystick, i, axes[i]))
                return false;
        struct Button {
            uint16_t mask;
            SDL_GamepadButton button;
        };
        constexpr Button buttons[] = {
            {0x1000, SDL_GAMEPAD_BUTTON_SOUTH},         {0x2000, SDL_GAMEPAD_BUTTON_EAST},
            {0x4000, SDL_GAMEPAD_BUTTON_WEST},          {0x8000, SDL_GAMEPAD_BUTTON_NORTH},
            {0x0010, SDL_GAMEPAD_BUTTON_START},         {0x0020, SDL_GAMEPAD_BUTTON_BACK},
            {0x0040, SDL_GAMEPAD_BUTTON_LEFT_STICK},    {0x0080, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
            {0x0100, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {0x0200, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
            {0x0001, SDL_GAMEPAD_BUTTON_DPAD_UP},       {0x0002, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
            {0x0004, SDL_GAMEPAD_BUTTON_DPAD_LEFT},     {0x0008, SDL_GAMEPAD_BUTTON_DPAD_RIGHT}};
        for (auto b : buttons)
            if (!SDL_SetJoystickVirtualButton(joystick, b.button, (p.buttons & b.mask) != 0))
                return false;
        if (!reported_input &&
            (p.buttons || p.lt || p.rt || std::abs(int(p.lx)) > 8000 || std::abs(int(p.ly)) > 8000)) {
            reported_input = true;
            LOGR("input: bridge received physical buttons/axes (packet %u)", p.sequence);
        }
        return true;
    }
};
WslControllerBridge::WslControllerBridge() : impl_(std::make_unique<Impl>()) {}
WslControllerBridge::~WslControllerBridge() {
    auto &p = *impl_;
    p.Detach();
    if (p.read_fd >= 0)
        close(p.read_fd);
    if (p.write_fd >= 0)
        close(p.write_fd);
    if (p.child > 0) {
        kill(p.child, SIGTERM);
        while (waitpid(p.child, nullptr, 0) < 0 && errno == EINTR) {
        }
    }
}
bool WslControllerBridge::Start(const char *helper) {
    if (!helper || !*helper)
        return false;
    auto &p = *impl_;
    int from[2], to[2];
    if (pipe2(from, O_CLOEXEC))
        return false;
    if (pipe2(to, O_CLOEXEC)) {
        close(from[0]);
        close(from[1]);
        return false;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, to[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, from[1], STDOUT_FILENO);
    char option[] = "--pipe";
    char *args[] = {const_cast<char *>(helper), option, nullptr};
    int result = posix_spawn(&p.child, helper, &actions, nullptr, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(to[0]);
    close(from[1]);
    if (result) {
        close(to[1]);
        close(from[0]);
        p.child = 0;
        LOGE("input: bridge helper could not start: %s", strerror(result));
        return false;
    }
    p.read_fd = from[0];
    p.write_fd = to[1];
    fcntl(p.read_fd, F_SETFL, fcntl(p.read_fd, F_GETFL) | O_NONBLOCK);
    fcntl(p.write_fd, F_SETFL, fcntl(p.write_fd, F_GETFL) | O_NONBLOCK);
    std::signal(SIGPIPE, SIG_IGN);
    p.received = SDL_GetTicks();
    LOGR("input: Windows controller test bridge started: %s", helper);
    return true;
}
void WslControllerBridge::Pump() {
    auto &p = *impl_;
    if (p.read_fd < 0)
        return;
    // A bounded drain keeps a busy helper from starving window events.
    for (int i = 0; i < 128; i++) {
        ssize_t n = read(p.read_fd, p.bytes.data() + p.used, p.bytes.size() - p.used);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            p.Detach();
            close(p.read_fd);
            p.read_fd = -1;
            LOGR("input: Windows controller bridge ended");
            break;
        }
        p.used += size_t(n);
        if (p.used == p.bytes.size()) {
            controller_bridge::State packet;
            std::memcpy(&packet, p.bytes.data(), sizeof(packet));
            p.used = 0;
            if (!p.Apply(packet)) {
                p.Detach();
                close(p.read_fd);
                p.read_fd = -1;
                LOGE("input: bridge packet or SDL update failed");
                break;
            }
        }
    }
    if (p.joystick && SDL_GetTicks() - p.received > 1000)
        p.Detach();
    uint64_t effect = p.rumble.load();
    if (effect != p.sent_rumble && p.write_fd >= 0) {
        controller_bridge::Rumble r;
        r.left = uint16_t(effect);
        r.right = uint16_t(effect >> 16);
        ssize_t n = write(p.write_fd, &r, sizeof(r));
        if (n == sizeof(r))
            p.sent_rumble = effect;
        else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            close(p.write_fd);
            p.write_fd = -1;
        }
    }
}
} // namespace input
#endif
