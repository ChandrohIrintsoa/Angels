// ─────────────────────────────────────────────────────────────────────────────
//  watch_list.cpp  —  Surveillance de valeurs en mémoire
//  Permet d'ajouter/supprimer/lire des adresses surveillées.
// ─────────────────────────────────────────────────────────────────────────────

#include "../../include/mi_core.h"

#include <mutex>
#include <unordered_map>
#include <atomic>

namespace mi {

// ─────────────────────────────────────────────────────────────────────────────
//  Registre global
// ─────────────────────────────────────────────────────────────────────────────

static std::mutex                              s_wl_mutex;
static std::unordered_map<int, WatchEntry>     s_watches;
static std::atomic<int>                        s_wl_next_id { 0 };

// ─────────────────────────────────────────────────────────────────────────────
//  watch_add
// ─────────────────────────────────────────────────────────────────────────────

int watch_add(pid_t pid, uintptr_t addr, ValueType type, const std::string& label) {
    if (pid <= 0) return -1;

    int id = s_wl_next_id.fetch_add(1, std::memory_order_relaxed);

    WatchEntry e;
    e.id      = id;
    e.pid     = pid;
    e.address = addr;
    e.type    = type;
    e.label   = label;

    std::lock_guard<std::mutex> lock(s_wl_mutex);
    s_watches[id] = std::move(e);
    return id;
}

// ─────────────────────────────────────────────────────────────────────────────
//  watch_remove
// ─────────────────────────────────────────────────────────────────────────────

bool watch_remove(int id) {
    std::lock_guard<std::mutex> lock(s_wl_mutex);
    return s_watches.erase(id) > 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  watch_read
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> watch_read(int id) {
    WatchEntry e;
    {
        std::lock_guard<std::mutex> lock(s_wl_mutex);
        auto it = s_watches.find(id);
        if (it == s_watches.end()) return {};
        e = it->second;
    }

    size_t sz = value_size(e.type);
    std::vector<uint8_t> buf(sz);
    if (!read_mem(e.pid, e.address, buf.data(), sz)) return {};
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
//  watch_list
// ─────────────────────────────────────────────────────────────────────────────

std::vector<WatchEntry> watch_list() {
    std::lock_guard<std::mutex> lock(s_wl_mutex);
    std::vector<WatchEntry> result;
    result.reserve(s_watches.size());
    for (const auto& [id, e] : s_watches) {
        result.push_back(e);
    }
    return result;
}

} // namespace mi
