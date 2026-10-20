// The settings menu, F1: the settings file's values changed while the game runs, and saved on request.
// Drawn in the renderer's ImGui frame (RenderUi) with UiLock held; the window thread opens and closes it.
// Most values apply on the next frame as the renderer and pacing read them each frame; the display mode and
// resolution go through ApplyDisplay. What the console was told at boot (a widescreen TV) needs a restart.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/app.h"
#include "core/log.h"

namespace app {

namespace {

std::atomic<bool> g_open{false};
std::atomic<bool> g_quit{false};  // the quit confirmation is up
std::chrono::steady_clock::time_point g_saved{};  // when Save was pressed, for the note beside it

// the monitor's modes, widest first, 640x480 and up
std::vector<Mode> Modes() {
    static std::vector<Mode> modes;
    if (!modes.empty()) return modes;
    std::set<std::pair<uint32_t, uint32_t>> seen;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); i++)
        if (dm.dmPelsWidth >= 640 && dm.dmPelsHeight >= 480) seen.insert({dm.dmPelsWidth, dm.dmPelsHeight});
    for (auto it = seen.rbegin(); it != seen.rend(); ++it) modes.push_back({it->first, it->second});
    return modes;
}

std::string ModeName(uint32_t w, uint32_t h) { return std::to_string(w) + " x " + std::to_string(h); }

std::string Narrow(const wchar_t* w) {
    if (!w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(std::max(n - 1, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

void Help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}

void Display(Settings& s) {
    ImGui::SeparatorText("Display");
    const char* modes[] = {"Fullscreen", "Borderless", "Windowed"};
    int mode = int(s.display_mode);
    if (ImGui::Combo("Mode", &mode, modes, 3)) {
        s.display_mode = DisplayMode(mode);
        ApplyDisplay();
    }
    std::string current = s.width && s.height ? ModeName(s.width, s.height) : "Desktop";
    if (ImGui::BeginCombo("Resolution", current.c_str())) {
        if (ImGui::Selectable("Desktop", !s.width)) {
            s.width = s.height = 0;
            ApplyDisplay();
        }
        for (const Mode& m : Modes()) {
            bool selected = s.width == m.width && s.height == m.height;
            if (ImGui::Selectable(ModeName(m.width, m.height).c_str(), selected)) {
                s.width = m.width;
                s.height = m.height;
                ApplyDisplay();
            }
        }
        ImGui::EndCombo();
    }
    // overscan presets: the console's whole frame, the game's own cut (GameInfo::overscan), or a chosen share
    static bool custom = false;
    const float fill = kGame.overscan > 0 ? kGame.overscan : 3.5f;  // a common TV's overscan without a game value
    int frame = custom ? 2 : s.overscan == 0 ? 0 : std::abs(s.overscan - fill) < 0.01f ? 1 : 2;
    const char* frames[] = {"Original (console frame)", "Fill screen (frame cut)", "Custom"};
    if (ImGui::Combo("Picture", &frame, frames, 3)) {
        custom = frame == 2;
        if (frame == 0) s.overscan = 0;
        if (frame == 1) s.overscan = fill;
    }
    Help("Games drew inside a black frame that a TV's overscan hid.\n"
         "Original: the whole console frame with that border, as the Xbox puts out.\n"
         "Fill screen: the frame cut and the rest enlarged, so the picture reaches the top and bottom.\n"
         "Custom: choose the share cut from every edge.");
    if (frame == 2) ImGui::SliderFloat("Overscan crop", &s.overscan, 0.0f, 10.0f, "%.1f%%");
    const char* aspects[] = {"Auto", "4:3", "16:9", "Stretch"};
    int aspect = int(s.aspect);
    if (ImGui::Combo("Aspect", &aspect, aspects, 4)) s.aspect = Aspect(aspect);
    Help("4:3: the console's picture with bars at the sides, for games without widescreen.\n"
         "16:9: for games that draw widescreen when the console has a widescreen TV.\n"
         "Auto: 16:9 on a wide screen, else 4:3.\n"
         "Stretch: fills the window, out of proportion.\n"
         "A game learns the TV's shape at start: switching between 4:3 and 16:9 for a widescreen game needs a restart.");
}

void Graphics(Settings& s) {
    ImGui::SeparatorText("Graphics");
    int percent = int(s.render_percent);
    if (ImGui::SliderInt("Render scale", &percent, 25, 200, "%d%%")) s.render_percent = uint32_t(percent);
    Help("Rendering height against the output's. Above 100% renders larger and scales down (supersampling).");
    const char* aniso[] = {"As the game", "2x", "4x", "8x", "16x"};
    const uint32_t aniso_v[] = {0, 2, 4, 8, 16};
    int ai = 0;
    for (int i = 0; i < 5; i++)
        if (aniso_v[i] == s.aniso) ai = i;
    if (ImGui::Combo("Anisotropic filtering", &ai, aniso, 5)) s.aniso = aniso_v[ai];
}

void Pacing(Settings& s) {
    ImGui::SeparatorText("Frame rate");
    const char* caps[] = {"30", "60", "120", "144", "165", "240", "Unlimited"};
    const uint32_t caps_v[] = {30, 60, 120, 144, 165, 240, 0};
    int ci = 1;
    for (int i = 0; i < 7; i++)
        if (caps_v[i] == s.fps_cap) ci = i;
    if (ImGui::Combo("FPS cap", &ci, caps, 7)) s.fps_cap = caps_v[ci];
    ImGui::Checkbox("V-Sync", &s.vsync);
}

void Fixes(Settings& s) {
    ImGui::SeparatorText("Port fixes");
    ImGui::Checkbox("Precise depth", &s.precise_depth);
    Help("Models' depth without the console's rounding: layers of a character (collar, armour, hands) stop "
         "flickering against each other.");
    ImGui::Checkbox("Stable point sampling", &s.point_nudge);
    Help("Unfiltered textures pick the same texel every frame when upscaled: no shimmering columns in "
         "animated backgrounds.");
    ImGui::Checkbox("HD textures", &s.hd_textures);
    Help("Replacements from the textures folder beside the exe, such as the high resolution font.");
}

void GameOptions() {
    if (kGame.options.empty()) return;
    ImGui::SeparatorText("Game");
    for (const Option& o : kGame.options) {
        if (o.flag) {
            ImGui::Checkbox(o.key, o.flag);
        } else if (o.value) {
            ImGui::InputScalar(o.key, ImGuiDataType_U32, o.value);
        }
    }
}

// "Quit the game?": Enter or Quit closes the window as its close button does, Esc or Cancel goes back
void DrawQuit() {
    const char* id = "Quit###quit";
    if (g_quit.load() && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) return;
    if (!g_quit.load()) {  // Esc again, from the window thread
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::Text("Quit %s?", Narrow(kGame.title).c_str());
    ImGui::TextDisabled("Progress since the last save is lost.");
    ImGui::Spacing();
    float w = ImGui::GetFontSize() * 6;
    bool quit = ImGui::Button("Quit", ImVec2(w, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(w, 0))) {
        g_quit = false;
        ImGui::CloseCurrentPopup();
    }
    if (quit) {
        LOGI("menu: quit");
        g_quit = false;
        ImGui::CloseCurrentPopup();
        PostMessageW(static_cast<HWND>(Window()), WM_CLOSE, 0, 0);
    }
    ImGui::EndPopup();
}

}  // namespace

bool MenuOpen() { return g_open.load() || g_quit.load(); }

void ToggleMenu() { g_open = !g_open.load(); }

void AskQuit() { g_quit = !g_quit.load(); }

void DrawMenu(double fps) {
    DrawQuit();
    if (!g_open.load()) return;
    Settings& s = Get();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    bool open = true;
    std::string title = Narrow(kGame.title) + " - Settings###settings";
    if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open) g_open = false;
        return;
    }
    ImGui::Text("%.0f FPS", fps);
    ImGui::SameLine();
    ImGui::TextDisabled("  F1 closes this menu");
    ImGui::PushItemWidth(ImGui::GetFontSize() * 14);
    Display(s);
    Graphics(s);
    Pacing(s);
    Fixes(s);
    GameOptions();
    ImGui::PopItemWidth();
    ImGui::Separator();
    if (ImGui::Button("Save")) {
        Save();
        g_saved = std::chrono::steady_clock::now();
        LOGI("menu: settings saved");
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) open = false;
    ImGui::SameLine();
    if (ImGui::Button("Quit game")) g_quit = true;
    if (std::chrono::steady_clock::now() - g_saved < std::chrono::seconds(3)) {
        ImGui::SameLine();
        ImGui::TextDisabled("Saved to %s", kGame.settings);
    }
    ImGui::End();
    if (!open) g_open = false;
}

}  // namespace app
