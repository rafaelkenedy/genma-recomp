// HD texture replacement and texture dumps (replace.h). PNG files through WIC.
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
#include <cstdlib>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "app/app.h"
#include "core/log.h"
#include "gpu/replace.h"

using Microsoft::WRL::ComPtr;

namespace gpu {

namespace {

namespace fs = std::filesystem;

std::mutex g_lock;

IWICImagingFactory* Wic() {
    static ComPtr<IWICImagingFactory> wic = [] {
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
    out.indexed = indexed;
    out.px.resize(size_t(w) * h * bpp);
    hr = converter->CopyPixels(nullptr, w * bpp, UINT(out.px.size()), out.px.data());
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

struct Pack {
    bool listed = false;
    std::unordered_map<uint64_t, std::pair<fs::path, bool>> files;  // hash: file, indexed
    std::unordered_map<uint64_t, Replacement> loaded;
};
Pack g_pack;

void List() {
    g_pack.listed = true;
    std::error_code ec;
    fs::path dir = app::ExeDir() / "textures";
    for (auto& e : fs::directory_iterator(dir, ec)) {
        std::wstring name = e.path().filename().wstring();
        if (name.size() < 20 || e.path().extension() != L".png" || (name[16] != L'.')) continue;
        std::wstring digits = name.substr(0, 16);
        if (!std::all_of(digits.begin(), digits.end(), [](wchar_t c) { return std::iswxdigit(c) != 0; })) continue;
        uint64_t hash = std::wcstoull(digits.c_str(), nullptr, 16);
        bool indexed = name.size() >= 24 && name.compare(16, 8, L".idx.png") == 0;
        g_pack.files[hash] = {e.path(), indexed};
    }
    if (!g_pack.files.empty()) LOGI("textures: %zu replacements in %s", g_pack.files.size(), dir.string().c_str());
}

}  // namespace

const Replacement* FindReplacement(uint64_t hash) {
    std::lock_guard lock(g_lock);
    if (!g_pack.listed) List();
    auto f = g_pack.files.find(hash);
    if (f == g_pack.files.end()) return nullptr;
    auto it = g_pack.loaded.find(hash);
    if (it != g_pack.loaded.end()) return it->second.px.empty() ? nullptr : &it->second;
    Replacement& r = g_pack.loaded[hash];
    if (!ReadPng(f->second.first, f->second.second, r)) {
        r = {};
        return nullptr;
    }
    LOGI("textures: %016llX replaced by %s, %ux%u%s", (unsigned long long)hash, f->second.first.filename().string().c_str(),
         r.w, r.h, r.indexed ? " indexed" : "");
    return &r;
}

bool DumpTextures() {
    static const bool on = [] {
        const char* e = std::getenv("XBE_DUMP_TEXTURES");
        return e && e[0] == '1';
    }();
    return on;
}

void DumpTexture(uint64_t hash, const TextureDesc& d, const uint8_t* guest, const uint32_t* palette) {
    static std::unordered_set<uint64_t> done;
    {
        std::lock_guard lock(g_lock);
        if (!done.insert(hash).second) return;
    }
    if (d.cube || d.depth > 1) return;
    std::error_code ec;
    fs::path dir = app::ExeDir() / "textures_dump";
    fs::create_directories(dir, ec);
    char name[32];
    std::snprintf(name, sizeof(name), "%016llX", (unsigned long long)hash);
    std::vector<uint8_t> px;
    uint32_t pitch = 0;
    DecodeLevel(d, 0, 0, guest, palette, px, pitch);
    uint32_t w = d.linear ? d.width : d.width, h = d.height;
    if (pitch == w * 4 && px.size() >= size_t(w) * h * 4) WritePng(dir / (std::string(name) + ".png"), px.data(), w, h, false);
    if (d.format == 0x0B) {
        // the indices: a palette of i -> (i, i, i)
        uint32_t grey[256];
        for (uint32_t i = 0; i < 256; i++) grey[i] = 0xFF000000u | i << 16 | i << 8 | i;
        DecodeLevel(d, 0, 0, guest, grey, px, pitch);
        std::vector<uint8_t> idx(size_t(w) * h);
        for (size_t i = 0; i < idx.size(); i++) idx[i] = px[i * 4];
        WritePng(dir / (std::string(name) + ".idx.png"), idx.data(), w, h, true);
    }
    LOGI("textures: dumped %s, %ux%u format %X", name, w, h, d.format);
}

std::vector<std::vector<uint8_t>> ReplacementLevels(const Replacement& r, const uint32_t* palette) {
    std::vector<std::vector<uint8_t>> levels(1);
    std::vector<uint8_t>& top = levels[0];
    top.resize(size_t(r.w) * r.h * 4);
    if (r.indexed) {
        for (size_t i = 0; i < size_t(r.w) * r.h; i++) {
            uint8_t v = r.px[i];
            uint32_t c = palette ? palette[v] : (0xFF000000u | uint32_t(v) << 16 | uint32_t(v) << 8 | v);
            std::memcpy(&top[i * 4], &c, 4);
        }
    } else {
        top = r.px;
    }
    uint32_t w = r.w, h = r.h;
    while (w > 1 || h > 1) {
        uint32_t nw = std::max(w / 2, 1u), nh = std::max(h / 2, 1u);
        const std::vector<uint8_t>& src = levels.back();
        std::vector<uint8_t> dst(size_t(nw) * nh * 4);
        for (uint32_t y = 0; y < nh; y++)
            for (uint32_t x = 0; x < nw; x++)
                for (int c = 0; c < 4; c++) {
                    uint32_t x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1);
                    uint32_t y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
                    uint32_t s = src[(size_t(y0) * w + x0) * 4 + c] + src[(size_t(y0) * w + x1) * 4 + c] +
                                 src[(size_t(y1) * w + x0) * 4 + c] + src[(size_t(y1) * w + x1) * 4 + c];
                    dst[(size_t(y) * nw + x) * 4 + c] = uint8_t((s + 2) / 4);
                }
        levels.push_back(std::move(dst));
        w = nw;
        h = nh;
    }
    return levels;
}

}  // namespace gpu
