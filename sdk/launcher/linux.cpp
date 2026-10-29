// Native release launcher: private XDG build cache, bundled compiler, no shell commands.
#include "linux_info.h"
#include "kernel/crypto.h"
#include "kernel/disc.h"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
extern char **environ;
namespace fs = std::filesystem;
using linux_launcher::kInfo;
namespace {
volatile sig_atomic_t cancelled = 0;
void Cancel(int) { cancelled = 1; }
std::string Read(const fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
void Write(const fs::path &path, const std::string &text) {
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
        f << text;
        if (!f)
            throw std::runtime_error("Cannot write " + path.string());
    }
    fs::rename(temporary, path);
}
fs::path Xdg(const char *name, const char *fallback) {
    if (const char *value = std::getenv(name); value && fs::path(value).is_absolute())
        return fs::path(value) / "genma-onimusha";
    const char *home = std::getenv("HOME");
    if (!home || !fs::path(home).is_absolute())
        throw std::runtime_error("HOME must be an absolute path");
    return fs::path(home) / fallback / "genma-onimusha";
}
struct Paths {
    fs::path root, sdk, compiler, dxc, config, data, cache, work, exe;
    std::string id;
    Paths() {
        root = fs::canonical("/proc/self/exe").parent_path();
        sdk = root / "sdk";
        compiler = root / "toolchain/bin/clang++";
        dxc = root / "shader-tools/bin/dxc";
        id = Read(sdk / "build-id.txt");
        while (!id.empty() && (id.back() == '\n' || id.back() == '\r'))
            id.pop_back();
        if (id.size() != 64 || id.find_first_not_of("0123456789abcdef") != std::string::npos)
            throw std::runtime_error("Package build ID missing or invalid");
        config = Xdg("XDG_CONFIG_HOME", ".config");
        data = Xdg("XDG_DATA_HOME", ".local/share");
        cache = Xdg("XDG_CACHE_HOME", ".cache") / "builds" / id;
        work = cache / "work";
        exe = cache / "game" / kInfo.exe;
    }
};
struct Process {
    pid_t pid = 0;
    fs::path object, temporary, log;
};
Process Start(const std::vector<std::string> &arguments, const fs::path &log) {
    Process p;
    p.log = log;
    std::vector<char *> argv;
    for (const auto &arg : arguments)
        argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t files;
    posix_spawn_file_actions_init(&files);
    posix_spawn_file_actions_addopen(&files, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    posix_spawn_file_actions_adddup2(&files, STDOUT_FILENO, STDERR_FILENO);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);
    int error = posix_spawn(&p.pid, argv[0], &files, &attr, argv.data(), environ);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&files);
    if (error)
        throw std::runtime_error("Cannot start " + arguments[0] + ": " + std::strerror(error));
    return p;
}
int Wait(Process &process, bool block) {
    int status = 0;
    pid_t done = waitpid(process.pid, &status, block ? 0 : WNOHANG);
    if (done == 0 || (done < 0 && errno == EINTR))
        return -1;
    if (done < 0)
        throw std::runtime_error("waitpid failed");
    process.pid = 0;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
void Stop(Process &p) {
    if (p.pid) {
        kill(-p.pid, SIGTERM);
        while (Wait(p, true) < 0) {
        }
    }
    if (!p.temporary.empty())
        fs::remove(p.temporary);
}
void Run(const std::vector<std::string> &args, const fs::path &log) {
    auto p = Start(args, log);
    while (p.pid) {
        if (cancelled) {
            Stop(p);
            throw std::runtime_error("Cancelled; completed objects remain reusable");
        }
        int result = Wait(p, false);
        if (result >= 0) {
            if (result)
                throw std::runtime_error("Command failed (" + std::to_string(result) + "). See " + log.string());
            return;
        }
        usleep(10000);
    }
}
std::vector<std::string> Arguments(const fs::path &file) {
    std::ifstream input(file);
    if (!input)
        throw std::runtime_error("Missing compiler arguments: " + file.string());
    std::vector<std::string> result;
    for (std::string line; std::getline(input, line);) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            result.push_back(line);
    }
    return result;
}
std::string Sha1(const std::vector<uint8_t> &bytes) {
    alignas(8) uint8_t context[128], hash[20];
    crypto::ShaInit(context);
    crypto::ShaUpdate(context, bytes.data(), bytes.size());
    crypto::ShaFinal(context, hash);
    std::string result;
    for (auto b : hash) {
        result += "0123456789abcdef"[b >> 4];
        result += "0123456789abcdef"[b & 15];
    }
    return result;
}
std::vector<uint8_t> Game(const fs::path &source) {
    std::vector<uint8_t> xbe;
    if (fs::is_directory(source)) {
        auto text = Read(source / kInfo.xbe);
        xbe.assign(text.begin(), text.end());
    } else {
        kernel::Disc disc;
        std::string error;
        if (!disc.Open(source, error) || !disc.Load(kInfo.xbe, xbe))
            throw std::runtime_error("Cannot read game image: " + error);
    }
    if (xbe.empty() || Sha1(xbe) != kInfo.sha1)
        throw std::runtime_error("Wrong game release: expected XBE SHA-1 " + std::string(kInfo.sha1));
    return xbe;
}
unsigned Jobs() {
    unsigned jobs = std::max(1u, std::thread::hardware_concurrency());
    struct sysinfo info{};
    if (!sysinfo(&info))
        jobs = std::min(jobs, std::max(1u, unsigned(uint64_t(info.freeram) * info.mem_unit / (1536ull << 20))));
    // Honor a cgroup memory limit as well as the host's available RAM.
    for (auto file : {"/sys/fs/cgroup/memory.max", "/sys/fs/cgroup/memory/memory.limit_in_bytes"}) {
        auto text = Read(file);
        try {
            uint64_t limit = std::stoull(text);
            jobs = std::min(jobs, std::max(1u, unsigned(std::min<uint64_t>(limit / (1536ull << 20), 64))));
        } catch (...) {
        }
    }
    return std::min(jobs, 8u);
}
void Build(const Paths &p, const std::vector<uint8_t> &xbe, unsigned jobs) {
    fs::create_directories(p.work / "obj");
    fs::create_directories(p.exe.parent_path());
    std::string identity = p.id + "\n" + kInfo.sha1 + "\n";
    auto gen = p.work / "gen";
    if (Read(p.work / "recompiled.txt") != identity || !fs::exists(gen / "image_symbols.h")) {
        std::ofstream f(p.work / kInfo.xbe, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char *>(xbe.data()), xbe.size());
        f.close();
        if (!f)
            throw std::runtime_error("Cannot extract XBE");
        std::cout << "[1/3] Recompiling your game..." << std::endl;
        Run({(p.sdk / "bin/xbe_recomp").string(), (p.sdk / "config/game.toml").string(), (p.work / kInfo.xbe).string(),
             gen.string(), "--xdk", (p.sdk / "xdk").string()},
            p.work / "recompile.log");
        if (Read(gen / "image_symbols.h") != Read(p.sdk / "include/image_symbols.h"))
            throw std::runtime_error("Generated symbols do not match the packaged runtime");
        Write(p.work / "recompiled.txt", identity);
    }
    std::vector<fs::path> sources;
    for (auto &entry : fs::directory_iterator(gen))
        if (entry.path().extension() == ".cpp")
            sources.push_back(entry.path());
    if (sources.empty())
        throw std::runtime_error("Recompiler produced no sources");
    std::sort(sources.begin(), sources.end(),
              [](const auto &a, const auto &b) { return fs::file_size(a) > fs::file_size(b); });
    auto flags = Arguments(p.sdk / "compile.args");
    std::vector<Process> running;
    std::vector<fs::path> objects;
    size_t next = 0, done = 0;
    std::cout << "[2/3] Compiling " << sources.size() << " parts, " << jobs << " at a time..." << std::endl;
    try {
        while (next < sources.size() || !running.empty()) {
            if (cancelled)
                throw std::runtime_error("Cancelled; completed objects remain reusable");
            while (next < sources.size() && running.size() < jobs) {
                auto source = sources[next++], object = p.work / "obj" / (source.stem().string() + ".o");
                objects.push_back(object);
                if (fs::exists(object) && fs::last_write_time(object) >= fs::last_write_time(source)) {
                    done++;
                    continue;
                }
                auto temporary = object;
                temporary += ".tmp";
                std::vector<std::string> args{p.compiler.string()};
                args.insert(args.end(), flags.begin(), flags.end());
                args.insert(args.end(), {"-I" + (p.sdk / "include").string(), "-I" + gen.string(), "-c",
                                         source.string(), "-o", temporary.string()});
                auto process = Start(args, p.work / "obj" / (source.stem().string() + ".log"));
                process.object = object;
                process.temporary = temporary;
                running.push_back(process);
            }
            for (size_t i = 0; i < running.size();) {
                int status = Wait(running[i], false);
                if (status < 0) {
                    i++;
                    continue;
                }
                if (status)
                    throw std::runtime_error("Compiler failed; see " + running[i].log.string());
                fs::rename(running[i].temporary, running[i].object);
                running.erase(running.begin() + i);
                done++;
                std::cout << "  " << done << "/" << sources.size() << std::endl;
            }
            if (!running.empty())
                usleep(10000);
        }
    } catch (...) {
        for (auto &process : running)
            Stop(process);
        throw;
    }
    std::cout << "[3/3] Linking..." << std::endl;
    std::vector<std::string> args{p.compiler.string()};
    for (auto &object : objects)
        args.push_back(object.string());
    args.insert(args.end(),
                {"-Wl,--whole-archive", (p.sdk / "lib/libGenmaOnimusha_runtime.a").string(), "-Wl,--no-whole-archive",
                 (p.sdk / "lib/libSDL3.a").string(), "-L" + (p.sdk / "lib").string(), "-l:libvulkan.so.1"});
    auto link = Arguments(p.sdk / "link.args");
    args.insert(args.end(), link.begin(), link.end());
    auto temporary = p.exe;
    temporary += ".tmp";
    args.insert(args.end(), {"-o", temporary.string()});
    Run(args, p.work / "link.log");
    fs::rename(temporary, p.exe);
    Write(p.cache / "build.txt", identity);
}
void Environment(const Paths &p, const fs::path &source) {
    for (auto item :
         {std::pair{"XBE_GAME_PATH", source}, std::pair{"XBE_DATA_DIR", p.data}, std::pair{"XBE_CONFIG_DIR", p.config},
          std::pair{"XBE_CACHE_DIR", p.cache}, std::pair{"XBE_DXC", p.dxc}}) {
        if (setenv(item.first, item.second.c_str(), 1))
            throw std::runtime_error("Cannot set runtime environment");
    }
    std::string libraries = (p.sdk / "lib").string();
    if (const char *old = std::getenv("LD_LIBRARY_PATH"); old && *old)
        libraries += ":" + std::string(old);
    setenv("LD_LIBRARY_PATH", libraries.c_str(), 1);
    unsetenv("XBE_CONTROLLER_BRIDGE"); // WSL helper is development-only and is not distributed.
}
} // namespace
int main(int argc, char **argv) {
    umask(0077);
    try {
        fs::path source;
        bool rebuild = false, build_only = false, check = false, print_paths = false;
        unsigned jobs = Jobs();
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "Genma Onimusha Linux (experimental)\nUsage: GenmaOnimusha [--game ISO-or-folder] "
                             "[--build-only] [--rebuild] [--jobs 1..64]\n       GenmaOnimusha --version | "
                             "--check-install | --print-paths\nUses your own game; no assets are included. Linux "
                             "updates are currently manual.\n";
                return 0;
            }
            if (arg == "--version") {
                std::cout << kInfo.version << " linux-x86_64\n";
                return 0;
            }
            if (arg == "--game" && i + 1 < argc)
                source = argv[++i];
            else if (arg == "--jobs" && i + 1 < argc) {
                std::string value = argv[++i];
                size_t used = 0;
                auto parsed = std::stoul(value, &used);
                if (used != value.size() || !parsed || parsed > 64)
                    throw std::runtime_error("Jobs must be 1..64");
                jobs = unsigned(parsed);
            } else if (arg == "--rebuild")
                rebuild = true;
            else if (arg == "--build-only")
                build_only = true;
            else if (arg == "--check-install")
                check = true;
            else if (arg == "--print-paths")
                print_paths = true;
            else
                throw std::runtime_error("Unknown/incomplete option: " + arg);
        }
        Paths p;
        if (print_paths) {
            std::cout << "config=" << p.config << "\ndata=" << p.data << "\ncache=" << p.cache << "\n";
            return 0;
        }
        if (check) {
            for (auto file : {p.compiler, p.dxc, p.sdk / "bin/xbe_recomp", p.sdk / "lib/libGenmaOnimusha_runtime.a",
                              p.sdk / "lib/libSDL3.a", p.sdk / "lib/libvulkan.so.1", p.sdk / "include/image_symbols.h",
                              p.sdk / "config/game.toml", p.sdk / "compile.args", p.sdk / "link.args"})
                if (!fs::is_regular_file(file))
                    throw std::runtime_error("Missing package file: " + file.string());
            std::string pattern = (fs::temp_directory_path() / "genma-check-XXXXXX").string();
            if (!mkdtemp(pattern.data()))
                throw std::runtime_error("Cannot create tool check directory");
            fs::path scratch = pattern;
            try {
                Run({p.compiler.string(), "--version"}, scratch / "compiler.log");
                Run({p.dxc.string(), "--version"}, scratch / "dxc.log");
                fs::remove_all(scratch);
            } catch (...) {
                throw;
            }
            std::cout << "Package tools verified; " << kInfo.version << ", Linux x86-64 (experimental)\n";
            return 0;
        }
        fs::create_directories(p.config);
        fs::create_directories(p.data);
        fs::create_directories(p.cache);
        if (source.empty()) {
            auto saved = Read(p.config / "game-path.txt");
            while (!saved.empty() && (saved.back() == '\n' || saved.back() == '\r'))
                saved.pop_back();
            source = saved;
        }
        if (source.empty())
            throw std::runtime_error("Supply your copy: GenmaOnimusha --game /path/to/game.iso (or extracted folder)");
        source = fs::canonical(source);
        if (fs::is_regular_file(source) && source.filename() == kInfo.xbe)
            source = source.parent_path();
        auto xbe = Game(source);
        Write(p.config / "game-path.txt", source.string() + "\n");
        int lock = open((p.cache / "build.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB))
            throw std::runtime_error("Another launcher is building this version");
        std::signal(SIGINT, Cancel);
        std::signal(SIGTERM, Cancel);
        std::string stamp = p.id + "\n" + kInfo.sha1 + "\n";
        if (rebuild || !fs::is_regular_file(p.exe) || Read(p.cache / "build.txt") != stamp) {
            if (rebuild)
                fs::remove(p.work / "recompiled.txt");
            Build(p, xbe, jobs);
        }
        if (build_only) {
            std::cout << "Built: " << p.exe << "\n";
            close(lock);
            return 0;
        }
        Environment(p, source);
        std::signal(SIGINT, SIG_DFL);
        std::signal(SIGTERM, SIG_DFL);
        execl(p.exe.c_str(), p.exe.c_str(), nullptr);
        throw std::runtime_error("Cannot start the built game: " + std::string(std::strerror(errno)));
    } catch (const std::exception &error) {
        std::cerr << "Genma Onimusha: " << error.what() << "\n";
        return cancelled ? 130 : 1;
    }
}
