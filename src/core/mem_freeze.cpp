// ─────────────────────────────────────────────────────────────────────────────
//  mem_freeze.cpp  —  Gel de valeurs : freeze individuel + gel en masse
// ─────────────────────────────────────────────────────────────────────────────
#include "../../include/mi_core.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <android/log.h>
#define LOG_TAG "Angels/Freeze"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace mi {

// ─────────────────────────────────────────────────────────────────────────────
//  FreezeEntry : un thread par freeze
// ─────────────────────────────────────────────────────────────────────────────
struct FreezeEntry {
    std::atomic<bool> active { true };
    std::thread       worker;
};

static std::mutex                            s_mutex;
static std::unordered_map<int, FreezeEntry*> s_freezes;
static std::atomic<int>                      s_next_id { 0 };

// ─────────────────────────────────────────────────────────────────────────────
//  freeze_add
// ─────────────────────────────────────────────────────────────────────────────
int freeze_add(pid_t pid, uintptr_t addr,
               const void* value, size_t size, int interval_ms)
{
    if (!value || size == 0 || pid <= 0 || interval_ms <= 0) return -1;

    // Copie locale de la valeur — survit à l'appel
    std::vector<uint8_t> val(
        static_cast<const uint8_t*>(value),
        static_cast<const uint8_t*>(value) + size);

    int id = s_next_id.fetch_add(1, std::memory_order_relaxed);
    FreezeEntry* entry = nullptr;
    try {
        entry = new FreezeEntry();
        entry->worker = std::thread(
            [entry, pid, addr, val = std::move(val), interval_ms]() mutable {
                const auto interval = std::chrono::milliseconds(interval_ms);
                while (entry->active.load(std::memory_order_acquire)) {
                    write_mem(pid, addr, val.data(), val.size());
                    std::this_thread::sleep_for(interval);
                }
            });
    } catch (const std::system_error& e) {
        LOGE("Thread creation failed for freeze_add: %s", e.what());
        delete entry; // S'assurer que la mémoire est libérée en cas d'échec
        return -1;
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    s_freezes[id] = entry;
    return id;
}

// ─────────────────────────────────────────────────────────────────────────────
//  freeze_remove
// ─────────────────────────────────────────────────────────────────────────────
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
    if (entry->worker.joinable()) entry->worker.join();
    delete entry;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  freeze_clear
// ─────────────────────────────────────────────────────────────────────────────
void freeze_clear() {
    std::unordered_map<int, FreezeEntry*> snap;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        snap.swap(s_freezes);
    }
    for (auto& [id, entry] : snap) {
        entry->active.store(false, std::memory_order_release);
        if (entry->worker.joinable()) entry->worker.join();
        delete entry;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  freeze_all_results — gèle toutes les adresses d'un jeu de résultats
//  Un thread unique réécrit toutes les adresses en boucle (plus efficace
//  que N threads individuels pour des milliers d'adresses).
// ─────────────────────────────────────────────────────────────────────────────
int freeze_all_results(pid_t pid,
                       const std::vector<ScanResult>& results,
                       const void* value, size_t vsz,
                       int interval_ms)
{
    if (!value || vsz == 0 || pid <= 0 || results.empty()
        || interval_ms <= 0)
        return 0;

    // Snapshot des adresses + valeur figée
    std::vector<uintptr_t> addrs;
    addrs.reserve(results.size());
    for (const auto& r : results) addrs.push_back(r.address);

    std::vector<uint8_t> val(
        static_cast<const uint8_t*>(value),
        static_cast<const uint8_t*>(value) + vsz);

    int id = s_next_id.fetch_add(1, std::memory_order_relaxed);
    FreezeEntry* entry = nullptr;
    try {
        entry = new FreezeEntry();
        entry->worker = std::thread(
            [entry, pid, addrs = std::move(addrs),
             val = std::move(val), interval_ms]() mutable
        {
            const auto interval = std::chrono::milliseconds(interval_ms);
            while (entry->active.load(std::memory_order_acquire)) {
                for (uintptr_t addr : addrs)
                    write_mem(pid, addr, val.data(), val.size());
                std::this_thread::sleep_for(interval);
            }
        });
    } catch (const std::system_error& e) {
        LOGE("Thread creation failed for freeze_all_results: %s", e.what());
        delete entry; // S'assurer que la mémoire est libérée en cas d'échec
        return 0;
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    s_freezes[id] = entry;
    return id; // retourne l'id du freeze groupé (1 id pour tout le groupe)
}

} // namespace mi
