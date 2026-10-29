#pragma once
#include <memory>

namespace input {
// Optional test transport. Native Linux gamepads continue using SDL's normal device drivers.
class WslControllerBridge {
  public:
    WslControllerBridge();
    ~WslControllerBridge();
    WslControllerBridge(const WslControllerBridge &) = delete;
    WslControllerBridge &operator=(const WslControllerBridge &) = delete;
    bool Start(const char *helper);
    void Pump(); // SDL window thread, before input::Pump()
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace input
