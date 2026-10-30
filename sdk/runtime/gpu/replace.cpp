// HD texture replacement and texture dumps (replace.h). PNG files through WIC, DDS read directly.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <thread>

#include "app/app.h"
#include "core/log.h"
#include "gpu/replace.h"
#include "gpu/shaders.h"
#include "gpu/xpr_index.h"
#include "core/resource_cache.h"
#include "core/file_observer.h"

using Microsoft::WRL::ComPtr;

namespace gpu {

namespace {

namespace fs = std::filesystem;

std::mutex g_lock;

IWICImagingFactory* Wic() {
    static thread_local ComPtr<IWICImagingFactory> wic = [] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ComPtr<IWICImagingFactory> f;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
        return f;
    }();
    return wic.Get();
}

// a PNG as 8-bit grey (indexed) or BGRA
bool ReadPng(const fs::path& path, bool indexed, Replacement& out) {
    IWICImagingFactory* wic = Wic();
    if (!wic) return false;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    HRESULT hr = wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(frame.Get(), indexed ? GUID_WICPixelFormat8bppGray : GUID_WICPixelFormat32bppBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) hr = converter->GetSize(&w, &h);
    if (FAILED(hr) || !w || !h) {
        LOGW("textures: %s could not be read (%08X)", path.string().c_str(), unsigned(hr));
        return false;
    }
    uint32_t bpp = indexed ? 1 : 4;
    out.w = w;
    out.h = h;
    out.kind = indexed ? Replacement::Kind::Indexed : Replacement::Kind::Bgra;
    out.levels.assign(1, std::vector<uint8_t>(size_t(w) * h * bpp));
    hr = converter->CopyPixels(nullptr, w * bpp, UINT(out.levels[0].size()), out.levels[0].data());
    return SUCCEEDED(hr);
}

bool WritePng(const fs::path& path, const uint8_t* px, uint32_t w, uint32_t h, bool grey) {
    IWICImagingFactory* wic = Wic();
    if (!wic) return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    HRESULT hr = wic->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
    WICPixelFormatGUID fmt = grey ? GUID_WICPixelFormat8bppGray : GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
    uint32_t bpp = grey ? 1 : 4;
    if (SUCCEEDED(hr)) hr = frame->WritePixels(h, w * bpp, w * h * bpp, const_cast<BYTE*>(px));
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    return SUCCEEDED(hr);
}

// a DDS with BC1, BC2, BC3 or BC7 blocks and its mip levels (legacy DXT1/3/5 FourCC or a DX10 header)
bool ReadDds(const fs::path& path, Replacement& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    std::vector<uint8_t> d;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 128) {
        d.resize(size_t(size));
        if (fread(d.data(), 1, d.size(), f) != d.size()) d.clear();
    }
    fclose(f);
    auto u32 = [&](size_t o) {
        uint32_t v;
        std::memcpy(&v, &d[o], 4);
        return v;
    };
    if (d.size() <= 128 || std::memcmp(d.data(), "DDS ", 4) != 0) {
        LOGW("textures: %s is no DDS", path.string().c_str());
        return false;
    }
    uint32_t h = u32(12), w = u32(16), mips = std::max(u32(28), 1u), fourcc = u32(84);
    size_t data = 128;
    using K = Replacement::Kind;
    K kind;
    if (fourcc == 0x31545844) {
        kind = K::BC1;  // DXT1
    } else if (fourcc == 0x33545844) {
        kind = K::BC2;  // DXT3
    } else if (fourcc == 0x35545844) {
        kind = K::BC3;  // DXT5
    } else if (fourcc == 0x30315844 && d.size() > 148) {  // DX10
        uint32_t dxgi = u32(128);
        data = 148;
        if (dxgi == 71 || dxgi == 72) kind = K::BC1;
        else if (dxgi == 74 || dxgi == 75) kind = K::BC2;
        else if (dxgi == 77 || dxgi == 78) kind = K::BC3;
        else if (dxgi == 98 || dxgi == 99) kind = K::BC7;
        else {
            LOGW("textures: %s has DXGI format %u, only BC1, BC2, BC3 and BC7 are read", path.string().c_str(), dxgi);
            return false;
        }
    } else {
        LOGW("textures: %s is not block compressed (BC1, BC2, BC3, BC7)", path.string().c_str());
        return false;
    }
    out.kind = kind;
    out.w = w;
    out.h = h;
    out.levels.clear();
    uint32_t bb = out.BlockBytes();
    for (uint32_t l = 0; l < mips; l++) {
        uint32_t lw = std::max(w >> l, 1u), lh = std::max(h >> l, 1u);
        size_t bytes = size_t((lw + 3) / 4) * ((lh + 3) / 4) * bb;
        if (data + bytes > d.size()) break;  // a chain cut short by the file is kept as far as it goes
        out.levels.emplace_back(d.begin() + data, d.begin() + data + bytes);
        data += bytes;
    }
    return !out.levels.empty();
}

// DXT1/3/5 blocks (as DecodeLevel leaves them, in block rows) to BGRA, for the dumps
void DecodeBlocks(uint32_t format, const uint8_t* src, uint32_t w, uint32_t h, std::vector<uint8_t>& out) {
    const bool dxt1 = format == 0x0C, dxt3 = format == 0x0E;
    const uint32_t bb = dxt1 ? 8 : 16, bw = (w + 3) / 4, bh = (h + 3) / 4;
    out.assign(size_t(w) * h * 4, 0);
    auto expand = [](uint16_t c, uint8_t* rgb) {
        rgb[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
        rgb[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
        rgb[2] = uint8_t((c & 31) * 255 / 31);
    };
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            const uint8_t* b = src + (size_t(by) * bw + bx) * bb;
            uint8_t alpha[16];
            std::fill(std::begin(alpha), std::end(alpha), uint8_t(255));
            if (dxt3) {
                for (int i = 0; i < 16; i++) alpha[i] = uint8_t(((b[i / 2] >> ((i & 1) * 4)) & 15) * 17);
            } else if (!dxt1) {
                uint8_t a[8] = {b[0], b[1]};
                if (a[0] > a[1]) {
                    for (int i = 0; i < 6; i++) a[2 + i] = uint8_t(((6 - i) * a[0] + (1 + i) * a[1]) / 7);
                } else {
                    for (int i = 0; i < 4; i++) a[2 + i] = uint8_t(((4 - i) * a[0] + (1 + i) * a[1]) / 5);
                    a[6] = 0;
                    a[7] = 255;
                }
                uint64_t bits = 0;
                for (int i = 0; i < 6; i++) bits |= uint64_t(b[2 + i]) << (8 * i);
                for (int i = 0; i < 16; i++) alpha[i] = a[(bits >> (3 * i)) & 7];
            }
            const uint8_t* c = dxt1 ? b : b + 8;
            uint16_t c0 = uint16_t(c[0] | c[1] << 8), c1 = uint16_t(c[2] | c[3] << 8);
            uint8_t col[4][4] = {};  // RGBA
            expand(c0, col[0]);
            expand(c1, col[1]);
            col[0][3] = col[1][3] = col[2][3] = col[3][3] = 255;
            if (!dxt1 || c0 > c1) {
                for (int k = 0; k < 3; k++) {
                    col[2][k] = uint8_t((2 * col[0][k] + col[1][k]) / 3);
                    col[3][k] = uint8_t((col[0][k] + 2 * col[1][k]) / 3);
                }
            } else {
                for (int k = 0; k < 3; k++) col[2][k] = uint8_t((col[0][k] + col[1][k]) / 2);
                col[3][3] = 0;  // transparent black
            }
            uint32_t idx = uint32_t(c[4] | c[5] << 8 | c[6] << 16 | uint32_t(c[7]) << 24);
            for (int i = 0; i < 16; i++) {
                uint32_t x = bx * 4 + i % 4, y = by * 4 + i / 4;
                if (x >= w || y >= h) continue;
                const uint8_t* p = col[(idx >> (2 * i)) & 3];
                uint8_t* o = &out[(size_t(y) * w + x) * 4];
                o[0] = p[2];
                o[1] = p[1];
                o[2] = p[0];
                o[3] = dxt1 ? p[3] : alpha[i];
            }
        }
}

struct Pack {
    bool listed = false;
    struct File {
        fs::path path;
        Replacement::Kind kind;  // Bgra, Indexed, or BC1 for any DDS (its format is read from the file)
    };
    struct Key {
        uint64_t hash, sig;  // sig: 0 untagged, else the .p<sig> palette signature
        bool operator==(const Key& o) const { return hash == o.hash && sig == o.sig; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const { return size_t(k.hash ^ (k.sig * 0x9E3779B97F4A7C15ull)); }
    };
    std::unordered_map<Key, File, KeyHash> files;    // .dds and .png
    std::unordered_map<uint64_t, fs::path> indexed;  // .idx.png
};
Pack g_pack;

bool Hex16(const std::wstring& t, uint64_t& v) {
    if (t.size() != 16 || !std::all_of(t.begin(), t.end(), [](wchar_t c) { return std::iswxdigit(c) != 0; })) return false;
    v = std::wcstoull(t.c_str(), nullptr, 16);
    return true;
}

// <hash>.dds|png, <hash>.p<sig>.dds|png, <hash>.idx.png
void List() {
    g_pack.listed = true;
    std::error_code ec;
    fs::path dir = app::ExeDir() / "textures";
    size_t n = 0;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        std::wstring name = e.path().filename().wstring();
        std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
        uint64_t hash = 0, sig = 0;
        if (name.size() < 20 || !Hex16(name.substr(0, 16), hash) || name[16] != L'.') continue;
        std::wstring rest = name.substr(17);
        if (rest == L"idx.png") {
            g_pack.indexed[hash] = e.path();
            n++;
            continue;
        }
        if (rest.size() > 18 && rest[0] == L'p' && rest[17] == L'.' && Hex16(rest.substr(1, 16), sig)) rest = rest.substr(18);
        using K = Replacement::Kind;
        if (rest != L"dds" && rest != L"png") continue;
        K kind = rest == L"dds" ? K::BC1 : K::Bgra;
        Pack::Key key{hash, sig};
        auto it = g_pack.files.find(key);
        if (it != g_pack.files.end() && it->second.kind == K::BC1 && kind != K::BC1) continue;  // the DDS stays
        if (it == g_pack.files.end()) n++;
        g_pack.files[key] = {e.path(), kind};
    }
    if (n) LOGI("textures: %zu replacements in %s", n, dir.string().c_str());
}

}  // namespace

uint64_t PaletteSignature(const uint8_t* indices, size_t count, const uint32_t* palette) {
    bool used[256] = {};
    for (size_t i = 0; i < count; i++) used[indices[i]] = true;
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint8_t b) { h = (h ^ b) * 1099511628211ull; };
    for (int i = 0; i < 256; i++) {
        if (!used[i]) continue;
        mix(uint8_t(i));
        uint32_t c = palette ? palette[i] : 0;
        for (int k = 0; k < 4; k++) mix(uint8_t(c >> (8 * k)));
    }
    return h;
}

namespace {
// the file a texture takes, false for none: a P8 one its palette's file, else its indices
bool Pick(uint64_t hash, uint64_t sig, Pack::File& out) {
    std::lock_guard lock(g_lock);
    if (!g_pack.listed) List();
    auto f = g_pack.files.find(Pack::Key{hash, sig});
    if (f != g_pack.files.end()) {
        out = f->second;
        return true;
    }
    if (sig) {
        auto i = g_pack.indexed.find(hash);
        if (i != g_pack.indexed.end()) {
            out = {i->second, Replacement::Kind::Indexed};
            return true;
        }
    }
    return false;
}
}  // namespace

bool HasReplacement(uint64_t hash, uint64_t palette_sig) {
    Pack::File file;
    return Pick(hash, palette_sig, file);
}

bool ReplacementUsesPalette(uint64_t hash, uint64_t palette_sig) {
    Pack::File file;
    return Pick(hash, palette_sig, file) && file.kind == Replacement::Kind::Indexed;
}

namespace {
cache::Resources<fs::path, Replacement>& Replacements() {
    static cache::Resources<fs::path, Replacement> cache(512ull << 20);
    return cache;
}

std::shared_ptr<const Replacement> ReadReplacement(const Pack::File& file) {
    auto read = [&]() -> std::shared_ptr<const Replacement> {
        auto out = std::make_shared<Replacement>();
        bool ok = file.kind == Replacement::Kind::BC1
                      ? ReadDds(file.path, *out)
                      : ReadPng(file.path, file.kind == Replacement::Kind::Indexed, *out);
        if (!ok)
            return {};
        static const char* kKinds[] = {"BGRA", "indexed", "BC1", "BC2", "BC3", "BC7"};
        LOGI("textures: read %s, %ux%u %s, %zu levels", file.path.filename().string().c_str(), out->w, out->h,
             kKinds[int(out->kind)], out->levels.size());
        return out;
    };
    return ReplacementCaching() ? Replacements().Get(file.path, read) : read();
}

std::atomic<bool> g_prefetch_enabled{false};
class Prefetch {
    struct Job {
        fs::path path, key;
        uint64_t generation, after_frame;
    };
    std::mutex mutex_;
    std::condition_variable work_;
    std::deque<Job> queue_;
    std::unordered_set<fs::path> pending_;
    bool stop_ = false;
    fs::path room_;
    uint64_t generation_ = 0;
    uint64_t frame_ = 0;
    std::thread thread_;
    void Run() {
        std::unordered_map<fs::path, std::vector<uint64_t>> index;
        for (;;) {
            fs::path path;
            fs::path key;
            uint64_t generation;
            {
                std::unique_lock lock(mutex_);
                work_.wait(lock, [&] { return stop_ || (!queue_.empty() && queue_.front().after_frame <= frame_); });
                if (stop_)
                    return;
                path = queue_.front().path;
                key = queue_.front().key;
                generation = queue_.front().generation;
                queue_.pop_front();
            }
            if (!g_prefetch_enabled.load()) {
                std::lock_guard lock(mutex_);
                if (generation == generation_)
                    pending_.erase(key);
                continue;
            }
            try {
                auto found = index.find(key);
                if (found == index.end()) {
                    // Original room containers are small. Never read an unbounded file.
                    std::error_code error;
                    auto bytes = fs::file_size(path, error);
                    if (!error && bytes >= 12 && bytes <= (64ull << 20)) {
                        std::vector<uint8_t> data(size_t(bytes), uint8_t{});
                        std::ifstream file(path, std::ios::binary);
                        if (file.read(reinterpret_cast<char*>(data.data()), data.size())) {
                            std::vector<uint64_t> hashes;
                            for (const auto& level : IndexXpr(data))
                                hashes.push_back(Hash(data.data() + level.offset, level.bytes));
                            // Bound metadata too; re-indexing an old file remains safe.
                            if (index.size() >= 128)
                                index.clear();
                            found = index.emplace(key, std::move(hashes)).first;
                        }
                    }
                }
                size_t loaded = 0;
                if (found != index.end())
                    for (uint64_t hash : found->second) {
                        {
                            std::lock_guard lock(mutex_);
                            if (stop_)
                                return;
                            if (generation != generation_)
                                break;
                        }
                        if (!g_prefetch_enabled.load())
                            break;
                        Pack::File file;
                        if (Pick(hash, 0, file) && ReadReplacement(file))
                            ++loaded;
                    }
                auto stats = Replacements().Snapshot();
                LOGI("textures: prefetched %zu from %s | CPU cache %.1f MiB, %zu hits, %zu misses, %zu evictions",
                     loaded, path.filename().string().c_str(), stats.bytes / 1048576.0, stats.hits, stats.misses,
                     stats.evictions);
            } catch (const std::exception& e) {
                LOGW("textures: prefetch skipped: %s", e.what());
            }
            {
                std::lock_guard lock(mutex_);
                if (generation == generation_)
                    pending_.erase(key);
            }
        }
    }

  public:
    Prefetch() : thread_([this] { Run(); }) {}
    ~Prefetch() {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        work_.notify_all();
        thread_.join();
    }
    void Frame() {
        {
            std::lock_guard lock(mutex_);
            ++frame_;
        }
        work_.notify_one();
    }
    void Queue(const fs::path& path) {
        if (!g_prefetch_enabled.load())
            return;
        auto extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](wchar_t c) { return std::towlower(c); });
        if (extension != L".bgs" && extension != L".xpr")
            return;
        auto directory = path.parent_path().filename().wstring();
        std::transform(directory.begin(), directory.end(), directory.begin(),
                       [](wchar_t c) { return std::towlower(c); });
        if (directory != L"bgs")
            return;
        auto normalized = path.lexically_normal().wstring();
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](wchar_t c) { return std::towlower(c); });
        fs::path key(normalized);
        std::lock_guard lock(mutex_);
        if (extension == L".bgs" && key.filename() != room_) {
            room_ = key.filename(); // D: and the game's Z: copy identify the same room
            ++generation_;
            queue_.clear();
            pending_.clear(); // obsolete room jobs stop between individual reads
        }
        if (queue_.size() >= 16 || !pending_.insert(key).second)
            return;
        // Let the current view reach presentation before speculative reads
        // compete with its load. The renderer can be one frame behind the title.
        queue_.push_back({path, key, generation_, frame_ + 2});
        work_.notify_one();
    }
};
Prefetch& Backgrounds() {
    static Prefetch prefetch;
    return prefetch;
}
} // namespace

std::shared_ptr<const Replacement> LoadReplacement(uint64_t hash, uint64_t palette_sig) {
    Pack::File file;
    return Pick(hash, palette_sig, file) ? ReadReplacement(file) : nullptr;
}

bool ReplacementCaching() {
    static bool enabled = [] {
        const char* value = std::getenv("XBE_TEXTURE_CACHE");
        return !value || std::strcmp(value, "0");
    }();
    return enabled;
}

void StartReplacementPrefetch(bool enabled) {
    g_prefetch_enabled.store(enabled && ReplacementCaching());
    if (!g_prefetch_enabled.load())
        return;
    // Initialize before the worker so cache destruction follows worker shutdown.
    (void)Replacements();
    Backgrounds().Frame();
    assets::on_open.store([](const fs::path& path) { Backgrounds().Queue(path); });
}

bool DumpTextures() {
    static const bool on = [] {
        const char* e = std::getenv("XBE_DUMP_TEXTURES");
        return e && e[0] == '1';
    }();
    return on;
}

void DumpTexture(uint64_t hash, uint64_t palette_sig, const TextureDesc& d, const uint8_t* guest,
                 const uint32_t* palette) {
    static std::unordered_set<uint64_t> done, done_idx;
    bool indices_done;
    {
        std::lock_guard lock(g_lock);
        if (!done.insert(hash ^ (palette_sig * 0x9E3779B97F4A7C15ull)).second)
            return;
        indices_done = !done_idx.insert(hash).second;
    }
    if (d.cube || d.depth > 1)
        return;
    std::error_code ec;
    fs::path dir = app::ExeDir() / "textures_dump";
    fs::create_directories(dir, ec);
    char name[48];
    if (d.format == 0x0B)
        std::snprintf(name, sizeof(name), "%016llX.p%016llX", (unsigned long long)hash, (unsigned long long)palette_sig);
    else
        std::snprintf(name, sizeof(name), "%016llX", (unsigned long long)hash);
    char base_name[32];
    std::snprintf(base_name, sizeof(base_name), "%016llX", (unsigned long long)hash);
    std::vector<uint8_t> px;
    uint32_t pitch = 0;
    DecodeLevel(d, 0, 0, guest, palette, px, pitch);
    uint32_t w = d.width, h = d.height;
    if (Format(d.format).compressed) {
        std::vector<uint8_t> bgra;
        DecodeBlocks(d.format, px.data(), w, h, bgra);
        px.swap(bgra);
        pitch = w * 4;
    }
    if (pitch == w * 4 && px.size() >= size_t(w) * h * 4) {
        WritePng(dir / (std::string(name) + ".png"), px.data(), w, h, false);
    } else {
        LOGW("textures: %s (%ux%u format %X) not dumped, its decoded layout is not BGRA", name, w, h, d.format);
        return;
    }
    if (d.format == 0x0B && !indices_done) {
        // the indices: a palette of i -> (i, i, i)
        uint32_t grey[256];
        for (uint32_t i = 0; i < 256; i++) grey[i] = 0xFF000000u | i << 16 | i << 8 | i;
        DecodeLevel(d, 0, 0, guest, grey, px, pitch);
        std::vector<uint8_t> idx(size_t(w) * h);
        for (size_t i = 0; i < idx.size(); i++) idx[i] = px[i * 4];
        WritePng(dir / (std::string(base_name) + ".idx.png"), idx.data(), w, h, true);
    }
    LOGI("textures: dumped %s, %ux%u format %X", name, w, h, d.format);
}

void PrepareLevels(Replacement& r, const uint32_t* palette) {
    if (r.Compressed() || r.levels.empty()) return;
    if (r.kind == Replacement::Kind::Indexed) {
        std::vector<uint8_t> bgra(size_t(r.w) * r.h * 4);
        const std::vector<uint8_t>& idx = r.levels[0];
        for (size_t i = 0; i < size_t(r.w) * r.h; i++) {
            uint8_t v = idx[i];
            uint32_t c = palette ? palette[v] : (0xFF000000u | uint32_t(v) << 16 | uint32_t(v) << 8 | v);
            std::memcpy(&bgra[i * 4], &c, 4);
        }
        r.levels.assign(1, std::move(bgra));
        r.kind = Replacement::Kind::Bgra;
    }
    r.levels.resize(1);
    uint32_t w = r.w, h = r.h;
    while (w > 1 || h > 1) {
        uint32_t nw = std::max(w / 2, 1u), nh = std::max(h / 2, 1u);
        const std::vector<uint8_t>& src = r.levels.back();
        std::vector<uint8_t> dst(size_t(nw) * nh * 4);
        for (uint32_t y = 0; y < nh; y++)
            for (uint32_t x = 0; x < nw; x++)
                for (int c = 0; c < 4; c++) {
                    uint32_t x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1);
                    uint32_t y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
                    uint32_t sum = src[(size_t(y0) * w + x0) * 4 + c] + src[(size_t(y0) * w + x1) * 4 + c] +
                                   src[(size_t(y1) * w + x0) * 4 + c] + src[(size_t(y1) * w + x1) * 4 + c];
                    dst[(size_t(y) * nw + x) * 4 + c] = uint8_t((sum + 2) / 4);
                }
        r.levels.push_back(std::move(dst));
        w = nw;
        h = nh;
    }
}

}  // namespace gpu
