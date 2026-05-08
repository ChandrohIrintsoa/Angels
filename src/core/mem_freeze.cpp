#include "../../include/mi_core.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <android/log.h>
#include <vector>

#define LOG_TAG "Angels/Freeze"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

namespace mi {

struct FreezeEntry {
    std::atomic<bool> active { true };
    std::atomic<bool> finished { false };
    std::thread       worker;
};

static std::mutex                            s_mutex;
static std::unordered_map<int, FreezeEntry*> s_freezes;
static std::atomic<int>                      s_next_id { 0 };

static constexpr int     FREEZE_JOIN_TIMEOUT_MS = 2000;
static constexpr size_t  FREEZE_MAX_ADDRS       = 100000;

static bool join_with_timeout(FreezeEntry* entry, int timeout_ms) {
    if (!entry->worker.joinable()) return true;
    auto start = std::chrono::steady_clock::now();
    while (!entry->finished.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeout_ms)) {
            entry->worker.detach();
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (entry->worker.joinable()) entry->worker.join();
    return true;
}

int freeze_add(pid_t pid, uintptr_t addr, const void* value, size_t size, int interval_ms) {
    if (!value || size == 0 || size > 1024 || pid <= 0 || interval_ms <= 0)
        return -1;

    std::vector<uint8_t> val(static_cast<const uint8_t*>(value), static_cast<const uint8_t*>(value) + size);
    int id = s_next_id.fetch_add(1, std::memory_order_relaxed);
    FreezeEntry* entry = nullptr;
    try {
        entry = new FreezeEntry();
        entry->worker = std::thread([entry, pid, addr, val = std::move(val), interval_ms]() mutable {
            const auto interval = std::chrono::milliseconds(interval_ms);
            while (entry->active.load(std::memory_order_acquire)) {
                write_mem(pid, addr, val.data(), val.size());
                std::this_thread::sleep_for(interval);
            }
            entry->finished.store(true, std::memory_order_release);
        });
    } catch (const std::system_error& e) {
        LOGE("Thread creation failed for freeze_add: %s", e.what());
        delete entry;
        return -1;
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    s_freezes[id] = entry;
    return id;
}

bool freeze_remove(int id) {
    FreezeEntry* entry = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_freezes.find(id);
        if (it == s_freezes.end()) return false;
        entry = it->second;
        s_freezes.erase(it);
    }
    entry->active.store(false, std::memory_order_release);
    bool joined = join_with_timeout(entry, FREEZE_JOIN_TIMEOUT_MS);
    if (!joined) {
        LOGW("freeze_remove id=%d : join timeout", id);
    }
    delete entry;
    return true;
}

void freeze_clear() {
    std::unordered_map<int, FreezeEntry*> snap;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        snap.swap(s_freezes);
    }
    for (auto& [id, entry] : snap) {
        entry->active.store(false, std::memory_order_release);
        join_with_timeout(entry, FREEZE_JOIN_TIMEOUT_MS);
        delete entry;
    }
}

int freeze_all_results(pid_t pid, const std::vector<ScanResult>& results, const void* value, size_t vsz, int interval_ms) {
    if (!value || vsz == 0 || vsz > 1024 || pid <= 0 || results.empty() || interval_ms <= 0)
        return 0;

    size_t addr_count = std::min(results.size(), FREEZE_MAX_ADDRS);
    std::vector<uintptr_t> addrs;
    addrs.reserve(addr_count);
    for (size_t i = 0; i < addr_count; ++i)
        addrs.push_back(results[i].address);

    std::vector<uint8_t> val(static_cast<const uint8_t*>(value), static_cast<const uint8_t*>(value) + vsz);
    int id = s_next_id.fetch_add(1, std::memory_order_relaxed);
    FreezeEntry* entry = nullptr;
    try {
        entry = new FreezeEntry();
        entry->worker = std::thread([entry, pid, addrs = std::move(addrs), val = std::move(val), interval_ms]() mutable {
            const auto interval = std::chrono::milliseconds(interval_ms);
            while (entry->active.load(std::memory_order_acquire)) {
                for (uintptr_t addr : addrs)
                    write_mem(pid, addr, val.data(), val.size());
                std::this_thread::sleep_for(interval);
            }
            entry->finished.store(true, std::memory_order_release);
        });
    } catch (const std::system_error& e) {
        LOGE("Thread creation failed for freeze_all_results: %s", e.what());
        delete entry;
        return 0;
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    s_freezes[id] = entry;
    return id;
}

} // namespace mi
