#pragma once
// The release launcher: a release ships no game code. On the first start it takes the player's own copy of the game,
// recompiles its xbe, compiles and links the code with the bundled LLVM-MinGW toolchain, then starts the game; later
// starts go straight to the game. CMake's add_xbe_release writes kInfo.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace launcher {

namespace fs = std::filesystem;

struct Info {
    const wchar_t* title;    // window titles and messages
    const wchar_t* exe;      // the launcher's name, and the built game's in game/
    const char* xbe;         // the image on the disc, default.xbe
    const char* sha1;        // of the xbe the release is made for, lower case hex
    const wchar_t* config;   // sdk/config/<config>
    const wchar_t* runtime;  // sdk/lib/<runtime>
    const char* version;     // release name; a game built by another release is built again
};
extern const Info kInfo;

// where everything is, from the launcher's folder
struct Paths {
    fs::path root;       // the launcher's folder
    fs::path sdk;        // root/sdk
    fs::path toolchain;  // root/toolchain
    fs::path work;       // root/work: the extracted xbe, generated code, objects, logs
    fs::path game_dir;   // root/game
    fs::path game_exe;   // root/game/<exe>.exe
    fs::path stamp;      // root/game/build.txt: the release and xbe the game was built from
};
Paths MakePaths();

// a progress window on the calling thread's message loop; the build reports from a worker thread
class Progress {
public:
    bool Open(const std::wstring& title);
    void Status(const std::wstring& text);  // any thread
    void Fraction(double f);                // 0..1, negative for an unknown amount of work (marquee)
    void Finish();                          // any thread: closes the window, ends Run
    void Run();                             // the message loop, until Finish or the window is closed
    bool Cancelled() const { return cancelled_.load(); }
    void Cancel() { cancelled_ = true; }

private:
    void* hwnd_ = nullptr;
    std::atomic<bool> cancelled_{false};
};

// recompiles work/<xbe> into work/gen, compiles it and links game/<exe>.exe; false with a message on failure
bool Build(const Paths& p, Progress& progress, std::wstring& error);

// text helpers
std::wstring Widen(const std::string& s);
std::string Narrow(const std::wstring& s);

}  // namespace launcher
