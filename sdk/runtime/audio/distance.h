#pragma once
#include <algorithm>
#include <cmath>

namespace audio {

// Retail XDK 3911 (Genma 0x1988F9) scales distance/minimum by rolloff.
// Its beyond-maximum branch uses (maximum/minimum - 1)*rolloff + 1.
// DistanceFactor converts world units to metres (for Doppler), not attenuation.
inline float DistanceGain(float distance, float minimum, float maximum, float rolloff) {
    if (distance <= minimum || rolloff <= 0) return 1.0f;
    float scale = distance > maximum ? (maximum / minimum - 1.0f) * rolloff + 1.0f
                                     : (distance / minimum) * rolloff;
    if (scale <= 1.0f) return 1.0f;
    int millibels = int(-2000.0f * std::log10(scale));
    return millibels <= -10000 ? 0.0f : std::pow(10.0f, millibels / 2000.0f);
}

// Orientation vectors supplied by a title need not have unit length.
inline float DirectionPan(const float delta[3], const float right[3]) {
    float distance = std::hypot(delta[0], delta[1], delta[2]);
    float length = std::hypot(right[0], right[1], right[2]);
    if (distance <= 1e-3f || length <= 1e-6f) return 0.0f;
    float side = 0;
    for (int i = 0; i < 3; i++) side += (delta[i] / distance) * (right[i] / length);
    return std::isfinite(side) ? std::clamp(side, -1.0f, 1.0f) : 0.0f;
}

}  // namespace audio
