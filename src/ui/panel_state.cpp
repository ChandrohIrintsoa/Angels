// ─────────────────────────────────────────────────────────────────────────────
//  panel_state.cpp  —  Angels Memory Engine v2 — Implémentation PanelState
// ─────────────────────────────────────────────────────────────────────────────
#include "../../include/panel_state.h"
#include <android/log.h>
#include <sstream>

#define LOG_TAG "Angels/Panel"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)

namespace mi {
namespace panel {

// ─────────────────────────────────────────────────────────────────────────────
//  Singleton
// ─────────────────────────────────────────────────────────────────────────────
PanelState& PanelState::get() {
    static PanelState instance;
    return instance;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Log circulaire
// ─────────────────────────────────────────────────────────────────────────────
void PanelState::log(const std::string& msg) {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (log_entries.size() >= LOG_MAX) {
        log_entries.erase(log_entries.begin());
    }
    log_entries.push_back(msg);
    LOGI("[Panel] %s", msg.c_str());
}

std::vector<std::string> PanelState::get_log() const {
    std::lock_guard<std::mutex> lock(log_mutex);
    return log_entries;   // copie intentionnelle
}

void PanelState::clear_log() {
    std::lock_guard<std::mutex> lock(log_mutex);
    log_entries.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Fonctions utilitaires libres
// ─────────────────────────────────────────────────────────────────────────────
const char* get_version() {
    return ENGINE_VERSION;
}

void log_message(const std::string& msg) {
    PanelState::get().log(msg);
}

std::string get_log_string() {
    auto entries = PanelState::get().get_log();
    std::ostringstream oss;
    // Du plus récent au plus ancien
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        oss << *it << "\n";
    }
    return oss.str();
}

} // namespace panel
} // namespace mi
