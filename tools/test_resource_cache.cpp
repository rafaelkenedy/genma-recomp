#include "core/resource_cache.h"
#include "core/file_observer.h"
#include <atomic>
#include <cassert>
#include <future>
#include <iostream>
#include <stdexcept>

struct Resource {
    int id;
    size_t bytes;
    size_t CacheBytes() const { return bytes; }
};
int main() {
    using namespace std::chrono_literals;
    cache::Resources<int, Resource> cache(8);
    int reads = 0;
    auto load = [&](int id, size_t bytes = 4) {
        ++reads;
        return std::make_shared<Resource>(Resource{id, bytes});
    };
    auto first = cache.Get(1, [&] { return load(1); });
    auto second = cache.Get(2, [&] { return load(2); });
    assert(cache.Get(1, [&] { return load(1); }) == first && reads == 2);
    cache.Get(3, [&] { return load(3); }); // evict 2, keeping its active lease valid
    assert(second->id == 2 && cache.Snapshot().bytes == 8 && cache.Snapshot().evictions == 1);
    cache.Get(2, [&] { return load(2); });
    assert(reads == 4);
    cache.Get(4, [&] { return load(4, 9); });
    cache.Get(4, [&] { return load(4, 9); });
    assert(reads == 6 && cache.Snapshot().bytes <= 8); // oversized values are not retained
    assert(!cache.Get(5, [] { return std::shared_ptr<Resource>{}; }));
    assert(cache.Get(5, [&] { return load(5); })); // failures can be retried
    try {
        cache.Get(6, []() -> std::shared_ptr<Resource> { throw std::runtime_error("load failed"); });
        assert(false);
    } catch (const std::runtime_error&) {
    }
    assert(cache.Get(6, [&] { return load(6); }));

    cache::Resources<int, Resource> concurrent(16);
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    std::atomic<int> calls{0};
    auto blocked = [&] {
        return concurrent.Get(1, [&] {
            ++calls;
            entered.set_value();
            released.wait();
            return std::make_shared<Resource>(Resource{1, 4});
        });
    };
    auto a = std::async(std::launch::async, blocked);
    entered.get_future().wait();
    auto b = std::async(std::launch::async, blocked);
    auto other = std::async(std::launch::async, [&] {
        return concurrent.Get(2, [] { return std::make_shared<Resource>(Resource{2, 4}); });
    });
    assert(other.wait_for(1s) == std::future_status::ready); // no global lock across I/O
    assert(b.wait_for(5ms) == std::future_status::timeout);
    release.set_value();
    assert(a.get() == b.get() && calls == 1);

    assets::on_open.store([](const std::filesystem::path&) { throw std::runtime_error("optional prefetch failed"); });
    assets::Opened("a.bgs"); // observer failure cannot change guest open success
    assets::on_open.store(nullptr);
    std::cout << "PASS: byte budget, LRU, active leases, retries, concurrent loads and optional observer failures\n";
}
