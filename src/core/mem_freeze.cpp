// ─────────────────────────────────────────────────────────────────────────────
//  mem_freeze.cpp  —  Gel de valeurs : freeze individuel + gel en masse
//
//  CORRECTIONS v3 (crash fixes) :
//   - freeze_remove / freeze_clear : timeout sur join() via
//     pthread_timedjoin_np (Android Bionic) pour éviter le blocage
//     infini si le thread worker est bloqué dans write_mem
//     (processus cible mort ou zombie).
//   - freeze_add : validation renforcée des paramètres.
//   - freeze_all_results : limite du nombre d'adresses pour éviter OOM.
// ─────────────────────────────────────────────────────────────────────────────
#include "../../include/mi_core.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <android/log.h>
#include <pthread.h>      // pthread_timedjoin_np
#include <time.h>         // clock_gettime

#define LOG_TAG "Angels/Freeze"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

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

// Constantes de sécurité
static constexpr int     FREEZE_JOIN_TIMEOUT_MS = 2000;   // 2 secondes max
static constexpr size_t  FREEZE_MAX_ADDRS       = 100000; // max 100k adresses

// ─────────────────────────────────────────────────────────────────────────────
//  Helper : join avec timeout (Android Bionic : pthread_timedjoin_np)
//  Retourne true si le thread s'est terminé, false si timeout.
// ─────────────────────────────────────────────────────────────────────────────
static bool join_with_timeout(std::thread& t, int timeout_ms) {
    if (!t.joinable()) return true;

    pthread_t handle = t.native_handle();

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec  += 1;
        ts.tv_nsec -= 1000000000L;
    }

    int rc = pthread_timedjoin_np(handle, nullptr, &ts);
    if (rc == 0) {
        return true;  // Thread terminé normalement
    }
    // Timeout ou erreur → detacher pour éviter fuite
    pthread_detach(handle);
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  freeze_add
// ─────────────────────────────────────────────────────────────────────────────
int freeze_add(pid_t pid, uintptr_t addr,
               const void* value, size_t size, int interval_ms)
{
    if (!value || size == 0 || size > 1024 || pid <= 0 || interval_ms <= 0)
        return -1;

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
        delete entry;
        return -1;
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    s_freezes[id] = entry;
    return id;
}

// ─────────────────────────────────────────────────────────────────────────────
//  freeze_remove  —  CORRIGÉ : join avec timeout via pthread_timedjoin_np
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

    // CORRECTION v3 : join avec timeout pour éviter le blocage infini
    // si le thread est bloqué dans write_mem (processus cible mort/zombie).
    bool joined = join_with_timeout(entry->worker, FREEZE_JOIN_TIMEOUT_MS);
    if (!joined) {
        LOGW("freeze_remove id=%d : join timeout (%d ms) — thread detache",
             id, FREEZE_JOIN_TIMEOUT_MS);
    }
    delete entry;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  freeze_clear  —  CORRIGÉ : join avec timeout pour chaque thread
// ─────────────────────────────────────────────────────────────────────────────
void freeze_clear() {
    std::unordered_map<int, FreezeEntry*> snap;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        snap.swap(s_freezes);
    }
    for (auto& [id, entry] : snap) {
        entry->active.store(false, std::memory_order_release);
        bool joined = join_with_timeout(entry->worker, FREEZE_JOIN_TIMEOUT_MS);
        if (!joined) {
            LOGW("freeze_clear id=%d : join timeout — thread detache", id);
        }
        delete entry;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  freeze_all_results — gèle toutes les adresses d'un jeu de résultats
//  Un thread unique réécrit toutes les adresses en boucle.
//  CORRECTION v3 : limite du nombre d'adresses pour éviter OOM.
// ─────────────────────────────────────────────────────────────────────────────
int freeze_all_results(pid_t pid,
                       const std::vector<ScanResult>& results,
                       const void* value, size_t vsz,
                       int interval_ms)
{
    if (!value || vsz == 0 || vsz > 1024 || pid <= 0 || results.empty()
        || interval_ms <= 0)
        return 0;

    // CORRECTION v3 : limiter le nombre d'adresses pour éviter OOM
    // et des freezes trop lourds qui feraient laguer le système.
    size_t addr_count = results.size();
    if (addr_count > FREEZE_MAX_ADDRS) {
        LOGW("freeze_all_results : limite %zu -> %zu adresses",
             addr_count, FREEZE_MAX_ADDRS);
        addr_count = FREEZE_MAX_ADDRS;
    }

    // Snapshot des adresses + valeur figée
    std::vector<uintptr_t> addrs;
    addrs.reserve(addr_count);
    for (size_t i = 0; i < addr_count; ++i)
        addrs.push_back(results[i].address);

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
        delete entry;
        return 0;
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    s_freezes[id] = entry;
    return id;
}

} // namespace mi
