// the settings file beside the exe
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <fstream>
#include <sstream>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "core/log.h"

namespace app {

namespace {

const char* kModes[] = {"fullscreen", "borderless", "windowed"};
const char* kAspects[] = {"auto", "4:3", "16:9", "stretch"};

// XBE_PRECISE_DEPTH / XBE_POINT_NUDGE = 0 or 1 win over the file, for A/B runs
void EnvFlag(const char* name, bool& v) {
    if (const char* e = std::getenv(name)) v = e[0] != '0';
}

std::filesystem::path File() { return ExeDir() / kGame.settings; }

}  // namespace

Settings& Get() {
    static Settings s;
    return s;
}

void Load() {
    Settings& s = Get();
    s.aspect = kGame.aspect;
    s.overscan = kGame.overscan;
    toml::table t;
    try {
        t = toml::parse_file(File().string());
    } catch (...) {
        EnvFlag("XBE_PRECISE_DEPTH", s.precise_depth);
        EnvFlag("XBE_POINT_NUDGE", s.point_nudge);
        return;
    }
    std::string mode = t["display_mode"].value_or(std::string(kModes[uint32_t(s.display_mode)]));
    for (uint32_t i = 0; i < 3; i++)
        if (mode == kModes[i]) s.display_mode = DisplayMode(i);
    s.width = t["width"].value_or(s.width);
    s.height = t["height"].value_or(s.height);
    s.render_percent = t["render_percent"].value_or(s.render_percent);
    s.fps_cap = t["fps_cap"].value_or(s.fps_cap);
    s.vsync = t["vsync"].value_or(s.vsync);
    s.aniso = t["anisotropic_filtering"].value_or(s.aniso);
    std::string aspect = t["aspect"].value_or(std::string(kAspects[uint32_t(s.aspect)]));
    for (uint32_t i = 0; i < 4; i++)
        if (aspect == kAspects[i]) s.aspect = Aspect(i);
    s.overscan = std::clamp(float(t["overscan"].value_or(double(s.overscan))), 0.0f, 15.0f);
    s.precise_depth = t["precise_depth"].value_or(s.precise_depth);
    s.point_nudge = t["point_nudge"].value_or(s.point_nudge);
    s.hd_textures = t["hd_textures"].value_or(s.hd_textures);
    EnvFlag("XBE_PRECISE_DEPTH", s.precise_depth);
    EnvFlag("XBE_POINT_NUDGE", s.point_nudge);
    s.join_ip = t["join_ip"].value_or(s.join_ip);
    for (const Option& o : kGame.options) {
        if (o.flag) *o.flag = t[o.key].value_or(*o.flag);
        else *o.value = t[o.key].value_or(*o.value);
    }
    if (kGame.load) kGame.load(&t);
}

namespace {

toml::table Table() {
    const Settings& s = Get();
    toml::table t{{"display_mode", kModes[uint32_t(s.display_mode)]},
                  {"width", int64_t(s.width)},
                  {"height", int64_t(s.height)},
                  {"render_percent", int64_t(s.render_percent)},
                  {"fps_cap", int64_t(s.fps_cap)},
                  {"vsync", s.vsync},
                  {"anisotropic_filtering", int64_t(s.aniso)},
                  {"aspect", kAspects[uint32_t(s.aspect)]},
                  {"overscan", double(s.overscan)},
                  {"precise_depth", s.precise_depth},
                  {"point_nudge", s.point_nudge},
                  {"hd_textures", s.hd_textures},
                  {"join_ip", s.join_ip}};
    for (const Option& o : kGame.options) {
        if (o.flag) t.insert(o.key, *o.flag);
        else t.insert(o.key, int64_t(*o.value));
    }
    if (kGame.save) kGame.save(&t);
    return t;
}

}  // namespace

void Save() {
    std::ofstream f(File());
    f << Table() << "\n";
}

void LogSettings() {
    std::ostringstream text;
    text << Table();
    std::istringstream lines(text.str());
    LOGR("Settings:");
    for (std::string line; std::getline(lines, line);)
        if (!line.empty()) LOGR("  %s", line.c_str());
}

Mode Desktop() {
    HMONITOR mon = MonitorFromWindow(static_cast<HWND>(Window()), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (GetMonitorInfoW(mon, &mi) && EnumDisplaySettingsW(mi.szDevice, ENUM_REGISTRY_SETTINGS, &dm))
        return {dm.dmPelsWidth, dm.dmPelsHeight};
    return {uint32_t(GetSystemMetrics(SM_CXSCREEN)), uint32_t(GetSystemMetrics(SM_CYSCREEN))};
}

Mode Output() {
    const Settings& s = Get();
    if (s.width && s.height) return {s.width, s.height};
    return Desktop();
}

bool Widescreen() {
    const Settings& s = Get();
    if (s.aspect == Aspect::Ratio16x9) return true;
    if (s.aspect != Aspect::Auto) return false;
    // asked per frame and per HUD quad: the desktop mode lookup only runs when the resolution setting changes
    static std::atomic<uint64_t> key{~0ull};
    static std::atomic<bool> wide{true};
    uint64_t k = uint64_t(s.width) << 32 | s.height;
    if (key.exchange(k) != k) {
        Mode m = Output();
        wide = m.width * 2 >= m.height * 3;  // 3:2 or wider
    }
    return wide;
}

float FrameAspect() {
    if (Get().aspect == Aspect::Stretch) return 0;
    return Widescreen() ? 16.0f / 9.0f : 4.0f / 3.0f;
}

float RenderScale(uint32_t guest_height) {
    float h = Output().height * (Get().render_percent / 100.0f);
    return guest_height ? std::max(h / guest_height, 0.1f) : 1.0f;
}

}  // namespace app
