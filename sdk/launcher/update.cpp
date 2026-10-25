// Release updates are staged and verified before a separate worker replaces the
// launcher. Failed/offline checks never prevent the installed game from starting.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <fstream>
#include <thread>
#include "launcher.h"

namespace launcher {
namespace {
std::string Read(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(f)), {});
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) text.pop_back();
    return text;
}

std::wstring PowerShell() {
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    return (fs::path(system) / L"WindowsPowerShell/v1.0/powershell.exe").wstring();
}

HANDLE StartWorker(const Paths& p, const fs::path& script, const wchar_t* mode) {
    const std::wstring exe = PowerShell();
    std::wstring cmd = QuoteArgument(exe) + L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
        QuoteArgument(script.wstring()) + L" -Mode " + mode + L" -Root " + QuoteArgument(p.root.wstring()) +
        L" -Version " + QuoteArgument(Widen(kInfo.version)) + L" -Repository " + QuoteArgument(Widen(kInfo.update_repository)) +
        L" -Exe " + QuoteArgument(kInfo.exe) + L" -ParentProcessId " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        p.root.c_str(), &si, &pi)) return nullptr;
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

bool RunWorker(const Paths& p, const fs::path& script, const wchar_t* mode, DWORD timeout, Progress* progress = nullptr) {
    HANDLE child = StartWorker(p, script, mode);
    if (!child) return false;
    const ULONGLONG start = GetTickCount64();
    bool stopped = false;
    while (WaitForSingleObject(child, 100) == WAIT_TIMEOUT) {
        if (progress) {
            std::string status = Read(p.root / ".update/status.txt");
            if (!status.empty()) progress->Status(Widen(status));
        }
        if ((progress && progress->Cancelled()) || GetTickCount64() - start > timeout) {
            TerminateProcess(child, 1);  // Check/Prepare/Recover never spawn other processes.
            WaitForSingleObject(child, INFINITE);
            stopped = true;
            break;
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(child, &code);
    CloseHandle(child);
    return !stopped && code == 0;
}

bool Handoff(const Paths& p, const fs::path& script, const wchar_t* mode) {
    std::error_code ec;
    const fs::path ready = p.root / ".update/ready.txt";
    fs::remove(ready, ec);
    if (ec || !fs::is_regular_file(script, ec)) return false;
    HANDLE child = StartWorker(p, script, mode);
    if (!child) return false;
    const ULONGLONG start = GetTickCount64();
    while (WaitForSingleObject(child, 100) == WAIT_TIMEOUT) {
        if (Read(ready) == std::to_string(GetCurrentProcessId())) {
            CloseHandle(child);
            return true;
        }
        if (GetTickCount64() - start > 8000) break;
    }
    // The worker cannot mutate the installation until this launcher has exited.
    TerminateProcess(child, 1);
    WaitForSingleObject(child, INFINITE);
    CloseHandle(child);
    return false;
}
}  // namespace

bool CheckForUpdates(const Paths& p) {
    if (!kInfo.update_repository || !*kInfo.update_repository) return false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool skip = false;
    for (int i = 1; argv && i < argc; i++)
        if (std::wstring(argv[i]) == L"--no-update" || std::wstring(argv[i]) == L"--skip-update-once") skip = true;
    if (argv) LocalFree(argv);

    std::error_code ec;
    const fs::path dir = p.root / ".update", script = p.sdk / "update.ps1";
    fs::create_directory(dir, ec);
    if (ec || (GetFileAttributesW(dir.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    HANDLE lock = CreateFileW((dir / "lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE) {
        MessageBoxW(nullptr, L"Another launcher or update is already using this installation. Try again when it finishes.",
                    kInfo.title, MB_OK | MB_ICONINFORMATION);
        return true;
    }
    struct Unlock { HANDLE handle; ~Unlock() { CloseHandle(handle); } } unlock{lock};

    // A journal always takes precedence over network checks, even with --no-update.
    if (fs::exists(dir / "transaction.json", ec)) {
        const fs::path recovery = dir / "worker.ps1";
        if (!Handoff(p, recovery, L"Recover")) {
            MessageBoxW(nullptr, L"The interrupted update could not be restored. Close other copies of the game and try again. Details: .update/error.txt",
                        kInfo.title, MB_OK | MB_ICONERROR);
        }
        return true;  // Recovery may replace this executable too; wait until it exits.
    }
    if (skip || !fs::exists(script, ec)) return false;
    if (!RunWorker(p, script, L"Check", 8000)) return false;
    std::string version = Read(dir / "available.txt");
    if (version.empty()) return false;
    std::wstring text = std::wstring(kInfo.title) + L" " + Widen(version) + L" is available (installed: " + Widen(kInfo.version) +
        L").\n\nDownload and install it now? Your saves, settings and textures will be kept. The game will rebuild once after the update.";
    if (MessageBoxW(nullptr, text.c_str(), kInfo.title, MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2) != IDYES) return false;

    // Preserve the original arguments for the restarted launcher without evaluating a shell command.
    std::wstring arguments;
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++)
        if (std::wstring(argv[i]) != L"--skip-update-once") arguments += L" " + QuoteArgument(argv[i]);
    if (argv) LocalFree(argv);
    { std::ofstream f(dir / "restart.txt", std::ios::binary); f << Narrow(arguments); if (!f) return false; }

    Progress progress;
    if (!progress.Open(std::wstring(kInfo.title) + L" - update")) return false;
    progress.Status(L"Downloading the update...");
    progress.Fraction(-1);
    bool ready = false;
    std::thread worker([&] { ready = RunWorker(p, script, L"Prepare", 15 * 60 * 1000, &progress); progress.Finish(); });
    progress.Run();
    worker.join();
    if (!ready || progress.Cancelled()) {
        if (!progress.Cancelled()) MessageBoxW(nullptr,
            L"The update could not be downloaded or verified. Your installed version will start. Details: .update/error.txt",
            kInfo.title, MB_OK | MB_ICONWARNING);
        return false;
    }
    if (!Handoff(p, dir / "worker.ps1", L"Apply")) {
        MessageBoxW(nullptr, L"Could not start the update installer. Your installed version will start.", kInfo.title, MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;  // Apply waits for this process to exit before replacing its executable.
}
}  // namespace launcher
