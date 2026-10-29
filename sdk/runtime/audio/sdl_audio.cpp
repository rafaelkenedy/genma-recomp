// Linux output for the existing 48 kHz stereo float mixer.
#include <SDL3/SDL.h>
#include <algorithm>
#include <vector>
#include "audio/audio.h"
#include "core/log.h"

namespace audio {
namespace {
std::function<void(float *, uint32_t)> g_render;
void Feed(void *, SDL_AudioStream *stream, int additional, int) {
    if (additional <= 0)
        return;
    constexpr uint32_t chunk = kRate / 100;
    float samples[chunk * 2];
    uint32_t frames = (uint32_t(additional) + 7) / 8;
    while (frames) {
        uint32_t n = std::min(frames, chunk);
        g_render(samples, n);
        if (!SDL_PutAudioStreamData(stream, samples, int(n * 8))) {
            LOGE("audio: SDL stream write: %s", SDL_GetError());
            return;
        }
        frames -= n;
    }
}
} // namespace
void StartOutput(std::function<void(float *, uint32_t)> render) {
    g_render = std::move(render);
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LOGE("audio: SDL initialization: %s", SDL_GetError());
        return;
    }
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, int(kRate)};
    auto *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, nullptr);
    if (!stream || !SDL_ResumeAudioStreamDevice(stream)) {
        LOGE("audio: SDL output unavailable: %s", SDL_GetError());
        if (stream)
            SDL_DestroyAudioStream(stream);
        return;
    }
    LOGR("audio: SDL output, stereo float %u Hz", kRate);
}
} // namespace audio
