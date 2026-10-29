// The game in assets: its files, or a disc image (.iso) with this executable's xbe on it. The xbe must be the one
// this executable was recompiled from; another release's would run its own data under this code.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cwctype>
#include <fstream>

#include "app/app.h"
#include "core/log.h"
#include "image_symbols.h"
#include "kernel/crypto.h"
#include "kernel/disc.h"

extern const char kImageSha1[];  // the xbe recompiled into this executable, written by CMake

namespace app {

namespace {

namespace fs = std::filesystem;

// assets beside the exe or in any folder above it (the build output)
fs::path FindAssets() {
    auto dir = ExeDir();
    std::error_code ec;
    for (int i = 0; i < 6; i++) {
        if (fs::is_directory(dir / "assets", ec)) return dir / "assets";
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return {};
}

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

bool ReadHostFile(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

}  // namespace

bool FindGame(GameFiles& game) {
#ifndef _WIN32
    if (const char* supplied = std::getenv("XBE_GAME_PATH"); supplied && *supplied) {
        fs::path path(supplied);
        std::unique_ptr<kernel::Disc> disc;
        std::vector<uint8_t> xbe;
        if (fs::is_directory(path)) {
            if (!ReadHostFile(path / IMAGE_XBE, xbe)) { LOGE("game: missing %s", IMAGE_XBE); return false; }
        } else {
            disc = std::make_unique<kernel::Disc>();
            std::string error;
            if (!disc->Open(path, error) || !disc->Load(IMAGE_XBE, xbe)) { LOGE("game: %s", error.c_str()); return false; }
        }
        if (Sha1(xbe) != kImageSha1) { LOGE("game: XBE SHA-1 does not match this release"); return false; }
        game = {fs::is_directory(path) ? path : path.parent_path(), std::move(xbe)};
        kernel::MountDisc(std::move(disc));
        return true;
    }
#endif
    fs::path assets = FindAssets();
    std::vector<fs::path> isos;
    std::error_code ec;
    if (!assets.empty())
        for (auto& e : fs::directory_iterator(assets, ec)) {
            std::wstring ext = e.path().extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
            if (ext == L".iso" && e.is_regular_file(ec)) isos.push_back(e.path());
        }
    std::sort(isos.begin(), isos.end());
    // assets/game_path.txt: a disc image or a game folder elsewhere, as a release's launcher remembers the one picked
    fs::path elsewhere;
    if (!assets.empty()) {
        std::ifstream f(assets / "game_path.txt", std::ios::binary);
        std::string line;
        if (f && std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty()) elsewhere = fs::path(std::u8string(line.begin(), line.end()));
        }
        if (!elsewhere.empty() && !fs::is_directory(elsewhere, ec)) isos.push_back(elsewhere);
    }

    // the first copy with this xbe wins, the extracted files before any image; another release is kept to offer
    GameFiles other;
    std::unique_ptr<kernel::Disc> other_disc;
    fs::path other_name;
    auto consider = [&](const fs::path& name, std::vector<uint8_t> xbe, std::unique_ptr<kernel::Disc> disc) {
        std::string sha = Sha1(xbe);
        bool match = sha == kImageSha1;
        LOGR("Game: %s in %s, sha1 %s%s", IMAGE_XBE, name.string().c_str(), sha.c_str(), match ? "" : ", another release");
        if (match) {
            game = {assets, std::move(xbe)};
            kernel::MountDisc(std::move(disc));
            return true;
        }
        if (other_name.empty()) {
            other = {assets, std::move(xbe)};
            other_disc = std::move(disc);
            other_name = name;
        }
        return false;
    };

    std::vector<uint8_t> xbe;
    if (!assets.empty() && ReadHostFile(assets / IMAGE_XBE, xbe) && consider(assets, std::move(xbe), nullptr)) return true;
    if (!elsewhere.empty() && fs::is_directory(elsewhere, ec) && ReadHostFile(elsewhere / IMAGE_XBE, xbe)) {
        fs::path keep = assets;
        assets = elsewhere;  // the game files are there
        if (consider(elsewhere, std::move(xbe), nullptr)) return true;
        assets = keep;
    }
    for (const fs::path& iso : isos) {
        auto disc = std::make_unique<kernel::Disc>();
        std::string error;
        if (!disc->Open(iso, error)) {
            LOGW("game: %s: %s", iso.string().c_str(), error.c_str());
            continue;
        }
        if (!disc->Load(IMAGE_XBE, xbe)) {
            LOGW("game: %s has no %s", iso.string().c_str(), IMAGE_XBE);
            continue;
        }
        if (consider(iso, std::move(xbe), std::move(disc))) return true;
    }

    if (other_name.empty()) {
#ifdef _WIN32
        MessageBoxW(nullptr, kGame.missing, kGame.short_title, MB_ICONERROR);
#else
        LOGE("game: assets missing; supply assets/default.xbe and its folders or an ISO");
#endif
        return false;
    }
#ifdef _WIN32
    std::wstring text = other_name.filename().wstring() +
                        L" is a different release of the game than this build was made from, and will most likely "
                        L"crash.\n\nStart it anyway?";
    if (MessageBoxW(nullptr, text.c_str(), kGame.short_title, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) return false;
    game = std::move(other);
    kernel::MountDisc(std::move(other_disc));
    return true;
#else
    LOGE("game: refusing a different XBE release; expected SHA-1 %s", kImageSha1);
    return false;
#endif
}

}  // namespace app
