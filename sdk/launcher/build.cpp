// The player's build: xbe_recomp on the extracted xbe, the generated sources compiled in parallel with the bundled
// clang, then one link against the prebuilt runtime. The flags come from sdk/compile.txt and sdk/link.txt, written by
// add_xbe_release, so they match the developer build. Output of every step goes to work/build.log.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>

#include "launcher.h"

namespace launcher {

namespace {

std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), {});
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

std::wstring Quote(const fs::path& p) { return L"\"" + p.wstring() + L"\""; }

// a started child process writing to its own log file
struct Child {
    HANDLE process = nullptr;
    fs::path log;
    std::wstring what;
};

bool Start(const std::wstring& exe, const std::wstring& args, const fs::path& cwd, const fs::path& log, Child& out) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE f = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = f;
    si.hStdError = f;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                             nullptr, cwd.c_str(), &si, &pi);
    CloseHandle(f);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    out.process = pi.hProcess;
    out.log = log;
    return true;
}

DWORD Finish(Child& c) {
    DWORD code = 1;
    GetExitCodeProcess(c.process, &code);
    CloseHandle(c.process);
    c.process = nullptr;
    return code;
}

// the build log: every step's command and output
struct Log {
    std::ofstream f;
    void Line(const std::string& s) {
        f << s << "\n";
        f.flush();
    }
    void Append(const fs::path& p) {
        std::string s = ReadText(p);
        if (!s.empty()) Line(s);
    }
};

// one process to the end, its output appended to the log; false when it fails or the build is cancelled
bool RunOne(const std::wstring& exe, const std::wstring& args, const fs::path& cwd, const fs::path& out_log, Log& log,
            Progress& progress, std::wstring& error, const std::wstring& what) {
    log.Line(Narrow(L"> \"" + exe + L"\" " + args));
    Child c;
    if (!Start(exe, args, cwd, out_log, c)) {
        error = L"Could not start " + exe;
        return false;
    }
    while (WaitForSingleObject(c.process, 200) == WAIT_TIMEOUT) {
        if (progress.Cancelled()) {
            TerminateProcess(c.process, 1);
            WaitForSingleObject(c.process, INFINITE);
            Finish(c);
            error = L"Cancelled.";
            return false;
        }
    }
    DWORD code = Finish(c);
    log.Append(out_log);
    if (code) {
        error = what + L" failed (exit code " + std::to_wstring(code) + L").";
        return false;
    }
    return true;
}

// parallel jobs: one per hardware thread, and at most one per 1.5 GB of physical memory, clang's peak on the
// largest partitions
unsigned Jobs() {
    unsigned n = std::max(1u, std::thread::hardware_concurrency());
    MEMORYSTATUSEX ms{sizeof(ms)};
    if (GlobalMemoryStatusEx(&ms)) {
        unsigned by_memory = unsigned(ms.ullTotalPhys / (1536ull << 20));
        n = std::min(n, std::max(1u, by_memory));
    }
    return n;
}

}  // namespace

bool Build(const Paths& p, Progress& progress, std::wstring& error) {
    std::error_code ec;
    fs::create_directories(p.work / "obj", ec);
    fs::create_directories(p.game_dir, ec);
    Log log;
    log.f.open(p.work / "build.log", std::ios::binary | std::ios::trunc);
    log.Line(std::string("release ") + kInfo.version + ", xbe sha1 " + kInfo.sha1);

    const fs::path gen = p.work / "gen";
    const std::wstring clang = (p.toolchain / "bin" / "x86_64-w64-mingw32-clang++.exe").wstring();
    if (!fs::exists(clang, ec)) {
        error = L"The bundled compiler is missing: " + clang + L"\nExtract the whole release again.";
        return false;
    }

    // 1. recompile
    progress.Status(L"Recompiling the game's code (step 1 of 3)...");
    progress.Fraction(-1);
    std::wstring recomp_args = Quote(p.sdk / "config" / kInfo.config) + L" " + Quote(p.work / Widen(kInfo.xbe)) + L" " +
                               Quote(gen) + L" --xdk " + Quote(p.sdk / "xdk");
    if (!RunOne((p.sdk / "bin" / "xbe_recomp.exe").wstring(), recomp_args, p.work, p.work / "recomp.log", log, progress,
                error, L"Recompiling"))
        return false;
    // the runtime was built against the config's symbols; generated code with others would not fit it
    if (ReadText(gen / "image_symbols.h") != ReadText(p.sdk / "include" / "image_symbols.h")) {
        error = L"The recompiled symbols do not match this release's runtime. The release is damaged or incomplete.";
        return false;
    }

    // 2. compile, biggest first so the long ones start early
    std::vector<fs::path> sources;
    for (auto& e : fs::directory_iterator(gen, ec))
        if (e.path().extension() == ".cpp") sources.push_back(e.path());
    std::sort(sources.begin(), sources.end(), [](const fs::path& a, const fs::path& b) {
        std::error_code e1, e2;
        return fs::file_size(a, e1) > fs::file_size(b, e2);
    });
    const std::wstring flags = Widen(ReadText(p.sdk / "compile.txt"));
    const std::wstring includes = L" -I" + Quote(p.sdk / "include") + L" -I" + Quote(gen);
    std::vector<fs::path> objects;
    std::vector<Child> running;
    size_t next = 0, done = 0;
    const unsigned jobs = Jobs();
    log.Line("compiling " + std::to_string(sources.size()) + " files, " + std::to_string(jobs) + " at a time");
    auto status = [&] {
        progress.Status(L"Compiling the game for your PC (step 2 of 3): " + std::to_wstring(done) + L" of " +
                        std::to_wstring(sources.size()) + L" parts. This can take a while.");
        progress.Fraction(double(done) / std::max<size_t>(1, sources.size()));
    };
    status();
    bool failed = false;
    while ((next < sources.size() && !failed) || !running.empty()) {
        while (!failed && !progress.Cancelled() && next < sources.size() && running.size() < jobs) {
            const fs::path& src = sources[next++];
            fs::path obj = p.work / "obj" / (src.stem().wstring() + L".o");
            objects.push_back(obj);
            // a finished object newer than its source stays, so a cancelled build goes on where it stopped
            if (fs::exists(obj, ec) && fs::last_write_time(obj, ec) > fs::last_write_time(src, ec)) {
                done++;
                status();
                continue;
            }
            std::wstring args = flags + includes + L" -c " + Quote(src) + L" -o " + Quote(obj);
            log.Line(Narrow(L"> clang++ " + args));
            Child c;
            c.what = src.filename().wstring();
            if (!Start(clang, args, p.work, p.work / "obj" / (src.stem().wstring() + L".log"), c)) {
                error = L"Could not start the compiler.";
                failed = true;
                break;
            }
            running.push_back(c);
        }
        if (running.empty()) break;
        std::vector<HANDLE> handles;
        for (auto& c : running) handles.push_back(c.process);
        DWORD w = WaitForMultipleObjects(DWORD(handles.size()), handles.data(), FALSE, 200);
        if (progress.Cancelled() && !failed) {
            for (auto& c : running) TerminateProcess(c.process, 1);
            failed = true;
            error = L"Cancelled.";
        }
        if (w >= WAIT_OBJECT_0 && w < WAIT_OBJECT_0 + handles.size()) {
            Child c = running[w - WAIT_OBJECT_0];
            running.erase(running.begin() + (w - WAIT_OBJECT_0));
            DWORD code = Finish(c);
            log.Append(c.log);
            if (code) {
                // a half written object must not count as done next time
                fs::remove(p.work / "obj" / (fs::path(c.what).stem().wstring() + L".o"), ec);
                if (!failed) {
                    failed = true;
                    error = L"Compiling " + c.what + L" failed (exit code " + std::to_wstring(code) + L").";
                    for (auto& r : running) TerminateProcess(r.process, 1);  // their objects are removed as they end
                }
            } else {
                done++;
                status();
            }
        }
    }
    if (failed) return false;

    // 3. link: the runtime whole, so the host functions that replace recompiled ones are all taken
    progress.Status(L"Linking the game (step 3 of 3)...");
    progress.Fraction(-1);
    std::wstring rsp;
    for (auto& o : objects) rsp += Quote(o) + L"\n";
    rsp += L"-Wl,--whole-archive\n" + Quote(p.sdk / "lib" / kInfo.runtime) + L"\n-Wl,--no-whole-archive\n";
    rsp += Quote(p.sdk / "lib" / "libSDL3.a") + L"\n";
    // forward slashes: the response file is read with GNU quoting, where a backslash escapes
    for (auto& ch : rsp)
        if (ch == L'\\') ch = L'/';
    std::wstring link_flags = Widen(ReadText(p.sdk / "link.txt"));
    fs::path tmp_exe = p.game_dir / (std::wstring(kInfo.exe) + L".tmp.exe");
    {
        std::ofstream r(p.work / "link.rsp", std::ios::binary | std::ios::trunc);
        r << Narrow(rsp);
    }
    std::wstring link_args = L"@" + Quote(p.work / "link.rsp") + L" " + link_flags + L" -o " + Quote(tmp_exe);
    if (!RunOne(clang, link_args, p.work, p.work / "link.log", log, progress, error, L"Linking")) return false;
    fs::remove(p.game_exe, ec);
    fs::rename(tmp_exe, p.game_exe, ec);
    if (ec) {
        error = L"Could not write " + p.game_exe.wstring();
        return false;
    }
    log.Line("built " + Narrow(p.game_exe.wstring()));
    return true;
}

}  // namespace launcher
