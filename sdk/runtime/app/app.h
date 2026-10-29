#pragma once
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace input {
struct Pad;
}

// the host side: window, settings
namespace app {

enum class DisplayMode : uint32_t { Fullscreen, Borderless, Windowed };
// how the console's frame fills the window:
//   Auto      16:9 when the output is 3:2 or wider (the console reports a widescreen TV), else 4:3
//   Ratio4x3  4:3 with bars, a 4:3 TV; for games that never draw widescreen
//   Ratio16x9 16:9, a widescreen TV
//   Stretch   the whole window, the console told 4:3
enum class Aspect : uint32_t { Auto, Ratio4x3, Ratio16x9, Stretch };

struct Settings {
    DisplayMode display_mode = DisplayMode::Borderless;
    uint32_t width = 0, height = 0;  // 0 is the desktop mode
    uint32_t render_percent = 100;   // of the output height
    uint32_t fps_cap = 60;           // 0 unlimited
    bool vsync = true;
    uint32_t aniso = 0;              // 0 as the game asks
    Aspect aspect = Aspect::Auto;    // GameInfo::aspect before the settings file
    float overscan = 0;              // percent of the frame cut from each edge, as a TV's overscan hid; GameInfo::overscan
    bool precise_depth = true;       // vertex programs' depth without the z / w cancellation (gpu/shaders.cpp)
    bool point_nudge = true;         // point sampled textures take the same texel at edges when upscaled
    bool hd_textures = true;         // replacements from the textures folder (gpu/replace.h)
    std::string join_ip;                   // co-op: a host past the LAN the system link search also asks
};

Settings& Get();
void Load();
void Save();
void LogSettings();  // every setting, as the settings file has them
void LogSystem();    // the machine and the settings, at a run's start
void ApplyDisplay();  // any thread
struct Mode {
    uint32_t width, height;
};
Mode Desktop();
Mode Output();
float RenderScale(uint32_t guest_height);
// the console reports a widescreen TV: Aspect 16:9, or Auto with an output 3:2 or wider
bool Widescreen();
// the window area the frame fills, width / height; 0 for the whole window (Stretch)
float FrameAspect();

// the settings menu (menu.cpp), F1: display, aspect, render scale, pacing, filtering, port fixes, game options
bool MenuOpen();
void ToggleMenu();          // window thread
void AskQuit();             // window thread, Esc: the quit confirmation, or closes it when it is up
void DrawMenu(double fps);  // inside the ImGui frame, UiLock held

std::filesystem::path ExeDir();
// The packaged Linux launcher selects writable XDG paths; development and Windows retain their layout.
inline std::filesystem::path WritableDir(const char* name) {
#ifndef _WIN32
    if (const char* path = std::getenv(name); path && *path) return std::filesystem::path(path);
#endif
    return ExeDir();
}
inline std::filesystem::path DataDir() { return WritableDir("XBE_DATA_DIR"); }
inline std::filesystem::path ConfigDir() { return WritableDir("XBE_CONFIG_DIR"); }
inline std::filesystem::path CacheDir() { return WritableDir("XBE_CACHE_DIR"); }
// the game in assets (game_files.cpp): its folder and this executable's xbe, from the extracted files or a disc
// image (.iso), which it mounts as D:; false after telling the player what is missing
struct GameFiles {
    std::filesystem::path dir;
    std::vector<uint8_t> xbe;
};
bool FindGame(GameFiles& game);
int Instance();  // --instance N on the command line, 0 without: a second copy on this PC for system link tests
void* Window();  // HWND
// boot splash: GameInfo::splash .bmp or .png beside the exe, up before the game window
bool OpenSplash();        // window thread, at launch; false without one, and the game window opens at once
void SplashFirstFrame();  // the game's first Swap: waits out the splash's minimum, then opens the game window
void CloseSplash();       // window thread
void OpenGameWindow(bool apply = true);  // any thread but the window thread's; returns once the window is up
// relaunched: the previous instance keeps its window up until this one has shown its first frames (handover.cpp)
bool StartHandover();   // window thread, at launch; true: the game window starts hidden
bool HandingOver();
void FinishHandover();  // any thread but the window thread's: opens the game window, lets the old instance go

// held around ImGui: the window thread's messages and the renderer's frame
std::recursive_mutex& UiLock();

// what the game repo defines once
struct Option {  // a setting in the settings file, flag or value
    const char* key;
    bool* flag;
    uint32_t* value;
};
struct GameInfo {
    const wchar_t* title;        // window and splash
    const wchar_t* short_title;  // message boxes
    const wchar_t* name;         // window classes, crash dumps, handover event
    const char* log;             // log prefix
    const char* settings;        // settings file beside the exe
    const wchar_t* splash;       // .bmp or .png beside the exe
    const wchar_t* missing;      // message when neither the game files nor a disc image are found
    std::vector<std::pair<const char*, const char*>> images;  // xbe file name, executable name
    std::vector<Option> options;
    // the settings file's toml::table, for what options cannot hold
    void (*load)(const void* table);
    void (*save)(void* table);
    void (*init)();                // after the settings load, before the window and boot
    void (*loaded)();              // image in memory, before its entry runs
    void (*window)(void* hwnd);    // the game window is up, before boot
    // window thread, each message after ImGui's; true returns result instead of the host's handling
    bool (*message)(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);
    void (*ui)(double fps);        // each frame inside the ImGui frame, UiLock held
    void (*pad)(int port, input::Pad& pad);  // a polled pad before the game gets it
    void (*rumble)(int port, uint16_t& left, uint16_t& right);
    Aspect aspect = Aspect::Auto;  // the default before the settings file; Ratio4x3 for games without widescreen
    float overscan = 0;            // the default overscan cut, percent per edge: a game's black frame for TV overscan
};
extern const GameInfo kGame;

// sleeps out the rest of the frame for the fps cap
void Pace();

}  // namespace app
