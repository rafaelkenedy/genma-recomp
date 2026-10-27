#include "../src/game/modern_controls.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace modern;
    Movement movement;
    Frame f;
    assert(!movement.Update(f).active);
    f.x = 6000; f.y = 6000; // radial, not square, dead zone
    assert(movement.Update(f).active);
    f.x = 0; f.y = 7000;
    assert(movement.Update(f).active); // hysteresis
    f.y = 6400;
    assert(!movement.Update(f).active);
    f.y = 32767; f.cameraYaw = 0; f.yaw = 0;
    auto r = movement.Update(f);
    assert(r.target == 0 && r.directions == 1);
    f.camera = 1; f.cameraYaw = 2048;
    r = movement.Update(f);
    assert(r.target == 0); // 180 degree camera cut cannot reverse a held stick
    for (int i = 0; i < 600; ++i) assert(movement.Update(f).target == 0);
    f.camera = 2; f.cameraYaw = 1024;
    assert(movement.Update(f).target == 0); // repeated cuts keep the same heading
    // Gradual input changes are compared with the cut anchor, not each frame.
    for (int angle = 10; angle <= 130; angle += 10) {
        const double rad = angle * 6.283185307179586 / 4096;
        f.x = int(std::sin(rad) * 32767); f.y = int(std::cos(rad) * 32767);
        assert(movement.Update(f).target == 0); // small adjustments keep actual heading
    }
    f.x = 12000; f.y = 30000;
    r = movement.Update(f);
    assert(r.target == Wrap(1024 - Angle(float(f.x), float(f.y))));
    f.x = f.y = 0;
    assert(!movement.Update(f).active);
    f.y = 32767;
    assert(movement.Update(f).target == 1024); // release/re-engage uses new camera

    // A cut while still turning saves actual facing, not the old desired target.
    movement.Reset();
    f = {}; f.y = 32767; f.state = 2; f.cameraYaw = 1800;
    r = movement.Update(f);
    assert(r.yaw > 0 && r.yaw < r.target);
    f.yaw = r.yaw; f.camera = 1; f.cameraYaw = 3000;
    const int retainedYaw = f.yaw;
    r = movement.Update(f);
    assert(r.target == retainedYaw && r.yaw == retainedYaw && r.directions == 1);
    f.camera = 2; f.cameraYaw = 1000;
    assert(movement.Update(f).target == retainedYaw);
    // Combat interrupts the latch; returning to movement uses the current view.
    f.state = 17;
    assert(!movement.Update(f).steer);
    f.state = 2;
    assert(movement.Update(f).target == 1000);
    f.held = 0x10; f.state = 14;
    assert(movement.Update(f).dodge);
    f.held = 0; f.state = 2;
    assert(movement.Update(f).target == 1000);
    f.cameraYaw = 1024;

    // Hardware directions have priority, including during a retained camera.
    f.held = 8;
    r = movement.Update(f);
    assert(!r.active && r.directions == 8);
    f.held = 0; f.allowed = false;
    assert(!movement.Update(f).active);
    f.allowed = true;
    assert(movement.Update(f).target == 1024);
    // Protected/scripted states do not receive synthetic movement.
    for (int state : {8, 9, 10, 11, 15, 16, 18, 19, 255}) {
        f.state = uint8_t(state);
        r = movement.Update(f);
        assert(r.active && !r.steer && !r.dodge && r.directions == 0);
    }
    // Issues #19/#24: let the native handlers see movement intent while
    // absorbing souls or attacking. They own the cancel window and cleanup.
    for (int state : {12, 17}) {
        for (int angle : {0, 1024, 2048, 3072}) {
            f.state = uint8_t(state); f.cameraYaw = angle;
            r = movement.Update(f);
            assert(r.active && !r.steer && !r.dodge && r.directions == 1);
            assert(r.yaw == f.yaw);
            f.held = 0x10;
            assert(movement.Update(f).directions == 0); // guard takes priority
            f.held = 0; f.allowed = false;
            assert(!movement.Update(f).active); // magic/cutscene input locks
            f.allowed = true;
        }
    }
    f.state = 0; f.held = 0x10;
    assert(movement.Update(f).directions == 0); // guard beats forward
    // Camera-current guard directions, independent of retained movement reference.
    movement.Reset();
    f.state = 13; f.yaw = 0; f.cameraYaw = 0;
    for (int angle : {0, 1024, 2048, 3072}) {
        f.cameraYaw = angle;
        r = movement.Update(f);
        const uint32_t expected = angle == 0 ? 1 : angle == 1024 ? 8 : angle == 2048 ? 4 : 2;
        assert(r.dodge && !r.steer && r.directions == expected);
        assert(RawDirections(r.directions) == (angle == 0 ? 0x10000u :
            angle == 1024 ? 0x40000u : angle == 2048 ? 0x20000u : 0x80000u));
    }
    // State/animation timing remains native; steering is time-based and wrap-safe.
    movement.Reset();
    f = {}; f.y = 32767; f.yaw = 4090;
    r = movement.Update(f);
    assert(r.yaw == 0);
    Movement a, b;
    Frame fa; fa.y = 32767; fa.cameraYaw = 1800; fa.state = 2;
    Frame fb = fa;
    fa.seconds = 1.0f / 30; fb.seconds = 1.0f / 60;
    fa.yaw = a.Update(fa).yaw;
    for (int i = 0; i < 2; ++i) fb.yaw = b.Update(fb).yaw;
    assert(std::abs(Difference(fa.yaw, fb.yaw)) <= 1);
    fa.yaw = 0; fa.seconds = 10;
    assert(std::abs(Difference(a.Update(fa).yaw, 0)) <= 683);

    // Only direction bits change; repeated holds do not generate new presses.
    Actions original{0x12340, 0x56780, 0x9ABC0, 0xDEF00};
    auto actions = Substitute(original, 8, 0);
    assert(actions.held == 0x12348 && actions.previous == 0x56780);
    assert(actions.pressed == 0x9ABC8 && actions.repeat == 0xDEF08);
    actions = Substitute(original, 8, 8);
    assert(actions.previous == 0x56788 && actions.pressed == original.pressed);
    assert(actions.repeat == original.repeat);
    std::cout << "PASS: dead zone, camera cuts, release, D-pad, locks, soul/attack cancel intent, guard/dodge, timing and action edges\n";
}
