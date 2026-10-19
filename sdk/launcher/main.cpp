// Release launcher entry: start the built game, or build it first from the player's copy of the game.
//   1. game/<exe>.exe built by this release from this xbe: start it, passing the command line on
//   2. else find the game: assets/<xbe>, an .iso in assets, the path in assets/game_path.txt, or ask for one
//   3. check the xbe's SHA-1 against the release's, extract it to work/, and remember an .iso outside assets
//   4. build (build.cpp) behind the progress window, then start the game
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <thread>

#include "kernel/crypto.h"
#include "kernel/disc.h"
#include "launcher.h"

namespace launcher {

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

Paths MakePaths() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    Paths p;
    p.root = fs::path(std::wstring(buf, n)).parent_path();
    p.sdk = p.root / "sdk";
    p.toolchain = p.root / "toolchain";
    p.work = p.root / "work";
    p.game_dir = p.root / "game";
    p.game_exe = p.game_dir / (std::wstring(kInfo.exe) + L".exe");
    p.stamp = p.game_dir / "build.txt";
    return p;
}

namespace {

std::string Sha1(const std::vector<uint8_t>& data) {
    alignas(8) uint8_t ctx[128];
    uint8_t digest[20];
    crypto::ShaInit(ctx);
    crypto::ShaUpdate(ctx, data.data(), data.size());
    crypto::ShaFinal(ctx, digest);
    std::string hex;
    for (uint8_t b : digest) {
        hex += "0123456789abcdef"[b >> 4];
        hex += "0123456789abcdef"[b & 15];
    }
    return hex;
}

std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), {});
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

bool ReadFile(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
    return s;
}

void Message(const std::wstring& text, UINT icon = MB_ICONERROR) { MessageBoxW(nullptr, text.c_str(), kInfo.title, icon); }

// the xbe from a folder holding it, an .iso, or the xbe itself; false with the reason
bool LoadXbe(const fs::path& from, std::vector<uint8_t>& xbe, std::wstring& why) {
    std::error_code ec;
    if (fs::is_directory(from, ec)) {
        if (ReadFile(from / kInfo.xbe, xbe)) return true;
        why = from.wstring() + L" has no " + Widen(kInfo.xbe);
        return false;
    }
    std::wstring ext = Lower(from.extension().wstring());
    if (ext == L".xbe") {
        if (ReadFile(from, xbe)) return true;
        why = L"Cannot read " + from.wstring();
        return false;
    }
    kernel::Disc disc;
    std::string error;
    if (!disc.Open(from, error)) {
        why = from.filename().wstring() + L": " + Widen(error);
        return false;
    }
    if (!disc.Load(kInfo.xbe, xbe)) {
        why = from.filename().wstring() + L" has no " + Widen(kInfo.xbe);
        return false;
    }
    return true;
}

// candidates in the order tried: extracted files, disc images in assets, the remembered path
std::vector<fs::path> Candidates(const Paths& p) {
    std::vector<fs::path> out;
    fs::path assets = p.root / "assets";
    std::error_code ec;
    if (fs::exists(assets / kInfo.xbe, ec)) out.push_back(assets);
    std::vector<fs::path> isos;
    for (auto& e : fs::directory_iterator(assets, ec))
        if (Lower(e.path().extension().wstring()) == L".iso" && e.is_regular_file(ec)) isos.push_back(e.path());
    std::sort(isos.begin(), isos.end());
    out.insert(out.end(), isos.begin(), isos.end());
    std::string remembered = ReadText(assets / "game_path.txt");
    if (!remembered.empty()) out.push_back(fs::path(Widen(remembered)));
    return out;
}

fs::path AskForGame() {
    wchar_t file[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn{sizeof(ofn)};
    std::wstring title = L"Pick your copy of " + std::wstring(kInfo.title);
    ofn.lpstrTitle = title.c_str();
    ofn.lpstrFilter = L"Xbox disc image or executable (*.iso, *.xbe)\0*.iso;*.xbe\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = DWORD(std::size(file));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return {};
    fs::path picked = file;
    // an xbe stands for the folder of game files it sits in
    if (Lower(picked.extension().wstring()) == L".xbe") return picked.parent_path();
    return picked;
}

bool StartGame(const Paths& p) {
    std::wstring cmd = L"\"" + p.game_exe.wstring() + L"\"";
    // the launcher's own arguments go on to the game
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; i++) cmd += L" \"" + std::wstring(argv[i]) + L"\"";
        LocalFree(argv);
    }
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(p.game_exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, p.game_dir.c_str(), &si, &pi)) {
        Message(L"Could not start " + p.game_exe.wstring());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

std::string StampText() { return std::string(kInfo.version) + " " + kInfo.sha1; }

}  // namespace
}  // namespace launcher

using namespace launcher;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Paths p = MakePaths();
    std::error_code ec;

    // 1. already built by this release
    if (fs::exists(p.game_exe, ec) && ReadText(p.stamp) == StampText()) return StartGame(p) ? 0 : 1;

    // 2. the game
    std::vector<uint8_t> xbe;
    fs::path source;
    std::wstring problems;
    for (const fs::path& c : Candidates(p)) {
        std::wstring why;
        if (LoadXbe(c, xbe, why)) {
            source = c;
            break;
        }
        problems += why + L"\n";
    }
    if (source.empty()) {
        std::wstring intro = std::wstring(kInfo.title) +
                             L" needs your own copy of the game to build itself the first time. This takes a while and "
                             L"happens once.\n\nPick its disc image (.iso), or default.xbe in a folder with the game's "
                             L"files.";
        if (!problems.empty()) intro += L"\n\nNot usable:\n" + problems;
        if (MessageBoxW(nullptr, intro.c_str(), kInfo.title, MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) return 1;
        for (;;) {
            fs::path picked = AskForGame();
            if (picked.empty()) return 1;
            std::wstring why;
            if (LoadXbe(picked, xbe, why)) {
                source = picked;
                break;
            }
            Message(why + L"\n\nPick another file.", MB_ICONWARNING);
        }
    }

    // 3. the release it was made for
    std::string sha = Sha1(xbe);
    if (sha != kInfo.sha1) {
        std::wstring text = source.filename().wstring() +
                            L" holds a different release of the game than this build is made for (SHA-1 " + Widen(sha) +
                            L", expected " + Widen(kInfo.sha1) + L"). It will most likely not work.\n\nBuild it anyway?";
        if (MessageBoxW(nullptr, text.c_str(), kInfo.title, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) return 1;
    }
    fs::create_directories(p.work, ec);
    fs::create_directories(p.root / "assets", ec);
    {
        std::ofstream f(p.work / kInfo.xbe, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(xbe.data()), std::streamsize(xbe.size()));
        if (!f) {
            Message(L"Cannot write to " + p.work.wstring() + L". Put the release in a folder you can write to.");
            return 1;
        }
    }
    // the game finds an image outside assets through this file
    fs::path assets = fs::weakly_canonical(p.root / "assets", ec);
    if (fs::weakly_canonical(source, ec).parent_path() != assets && fs::weakly_canonical(source, ec) != assets) {
        std::ofstream f(p.root / "assets" / "game_path.txt", std::ios::binary | std::ios::trunc);
        f << Narrow(fs::absolute(source, ec).wstring()) << "\n";
    }

    // 4. build
    Progress progress;
    if (!progress.Open(std::wstring(kInfo.title) + L" - first start")) return 1;
    bool ok = false;
    std::wstring error;
    std::thread worker([&] {
        ok = Build(p, progress, error);
        progress.Finish();
    });
    progress.Run();
    worker.join();
    if (!ok) {
        if (error != L"Cancelled.")
            Message(L"Building the game failed:\n" + error + L"\n\nDetails are in " + (p.work / "build.log").wstring());
        return 1;
    }
    {
        std::ofstream f(p.stamp, std::ios::binary | std::ios::trunc);
        f << StampText() << "\n";
    }
    return StartGame(p) ? 0 : 1;
}
