#include "kernel/memory.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
static size_t OpenDescriptors() {
    size_t count = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) ++count;
    return count;
}
static size_t GuestSizedMappings() {
    std::ifstream maps("/proc/self/maps");
    assert(maps.good());
    size_t count = 0;
    std::string line;
    while (std::getline(maps, line)) {
        unsigned long long begin, end;
        if (std::sscanf(line.c_str(), "%llx-%llx", &begin, &end) == 2 && end - begin >= (1ull << 32)) ++count;
    }
    return count;
}
static void FailedInitReleasesReservation() {
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        const auto mappings = GuestSizedMappings();
        struct rlimit original{};
        assert(getrlimit(RLIMIT_NOFILE, &original) == 0);
        auto limit = original;
        limit.rlim_cur = 0; // reserve succeeds, but creation of the backing file fails
        assert(setrlimit(RLIMIT_NOFILE, &limit) == 0);
        assert(!mem::Init() && mem::Base() == nullptr);
        assert(setrlimit(RLIMIT_NOFILE, &original) == 0);
        assert(GuestSizedMappings() == mappings);
        assert(mem::Init()); // a failed attempt must permit a later retry
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
#endif

int main() {
#ifndef _WIN32
    FailedInitReleasesReservation();
    const auto descriptors = OpenDescriptors();
#endif
    assert(mem::Init());
#ifndef _WIN32
    assert(OpenDescriptors() == descriptors); // mappings keep their backing alive after close(fd)
#endif
    auto* base = mem::Base();
    const auto physical = mem::AllocContiguous(0x4000, 0, mem::kPhysicalSize - 1, 0x1000);
    assert(physical && physical % 0x1000 == 0);
    const auto alias = mem::kWriteCombineBase | mem::PhysicalFromVirtual(physical);
    assert(alias != physical && mem::VirtualFromPhysical(mem::PhysicalFromVirtual(physical)) == physical);
    base[physical + 37] = 0x73;
    assert(base[alias + 37] == 0x73);
    base[alias + 37] = 0x42;
    assert(base[physical + 37] == 0x42);
    std::vector<std::thread> writers;
    for (unsigned i = 0; i < 4; ++i) writers.emplace_back([=] {
        for (unsigned j = 0; j < 1000; ++j) {
            base[physical + i * 0x1000 + j] = uint8_t(i + j);
            assert(base[alias + i * 0x1000 + j] == uint8_t(i + j));
        }
    });
    for (auto& writer : writers) writer.join();
    mem::FreeContiguous(physical);

    uint32_t address = 0, size = 0x2000;
    assert(mem::AllocateVirtual(address, size, mem::kMemReserve | mem::kMemCommit, 4) == 0);
    assert(address && mem::IsCommitted(address));
    base[address] = 0x9a;
    mem::RegionInfo info{};
    assert(mem::QueryVirtual(address, info) && info.state == mem::kMemCommit);
    uint32_t page = address, pageSize = mem::kPage;
    assert(mem::FreeVirtual(page, pageSize, mem::kMemDecommit) == 0);
    assert(!mem::IsCommitted(address));
    page = address; pageSize = mem::kPage;
    assert(mem::AllocateVirtual(page, pageSize, mem::kMemCommit, 4) == 0);
    assert(base[address] == 0); // recommitted anonymous memory must be zero-filled
    assert(mem::FreeVirtual(address, size, mem::kMemRelease) == 0);
    const auto stack = mem::AllocStack(0x10000);
    assert(stack && mem::IsCommitted(stack - 1));
    assert(!mem::IsCommitted(stack - 0x10000 - 1));
    base[stack - 1] = 0xab;
    mem::FreeStack(stack);
    assert(!mem::IsCommitted(stack - 1));
    std::cout << "PASS: physical aliases, concurrent reads, reserve/commit/decommit, zero-fill and stack guard\n";
}
