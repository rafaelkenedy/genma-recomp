#pragma once
namespace linux_launcher {
struct Info {
    const char *exe;
    const char *xbe;
    const char *sha1;
    const char *version;
};
extern const Info kInfo;
} // namespace linux_launcher
