#include "audio/distance.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using audio::DistanceGain;
    assert(DistanceGain(0, 1, 100, 1) == 1);
    assert(DistanceGain(1, 1, 100, 1) == 1);
    assert(std::abs(DistanceGain(2, 1, 100, 1) - 0.5f) < 0.001f);
    assert(std::abs(DistanceGain(1000, 1, 100, 1) - 0.01f) < 0.0001f);
    assert(DistanceGain(1000, 1, 10000, 0) == 1);
    assert(std::abs(DistanceGain(4, 1, 100, 0.5f) - 0.5f) < 0.001f);
    assert(std::abs(DistanceGain(200, 1, 100, 0.5f) - 1.0f / 50.5f) < 0.0001f);
    // Genma passes float bits 0x391D4952 (0.00015), including in SetAllParameters.
    float before = DistanceGain(1000, 1, 1e9f, 1);
    float after = DistanceGain(1000, 1, 1e9f, 0.00015f);
    assert(std::abs(before - 0.001f) < 1e-7f);
    assert(after == 1.0f);
    assert(std::abs(DistanceGain(10000, 1, 1e9f, 0.00015f) - 2.0f / 3.0f) < 0.001f);
    const float right[3] = {1, 0, 0}, scaled_right[3] = {10000, 0, 0};
    const float at_right[3] = {100, 0, 0}, at_left[3] = {-100, 0, 0};
    const float ahead[3] = {0, 0, 100}, zero[3] = {};
    assert(audio::DirectionPan(at_right, right) == 1);
    assert(audio::DirectionPan(at_right, scaled_right) == 1);
    assert(audio::DirectionPan(at_left, scaled_right) == -1);
    assert(audio::DirectionPan(ahead, scaled_right) == 0);
    assert(audio::DirectionPan(zero, right) == 0);
    assert(audio::DirectionPan(at_right, zero) == 0);
    const float diagonal[3] = {100, 0, 100};
    float pan = audio::DirectionPan(diagonal, scaled_right);
    assert(std::abs(pan - std::sqrt(0.5f)) < 1e-6f);
    // Every speaker multiplier must stay positive and never amplify the voice.
    for (float p : {pan, -pan, 1.0f, -1.0f, 0.0f}) {
        float left = std::min(1.0f, 1.0f - p), right_gain = std::min(1.0f, 1.0f + p);
        assert(left >= 0 && left <= 1 && right_gain >= 0 && right_gain <= 1);
    }
    std::cout << "PASS: distance boundaries, max clamp, zero/default/custom rolloff\n"
              << "PASS: scaled orientation, left/right/centre, degenerate vectors and bounded gains\n"
              << "1000 units: old gain=" << before << ", Genma gain=" << after << '\n';
}
