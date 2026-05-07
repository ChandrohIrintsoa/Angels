#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  panel_state.h  —  Angels Memory Engine v2 — État interne du Panel UI
//
//  Ce module gère l'état côté natif du panel de contrôle :
//    - PID cible courant
//    - Onglet actif
//    - Paramètres du dernier scan (type, mode, valeur cible)
//    - Clé XOR active
//    - Intervalle de gel (freeze interval)
//    - Version du moteur
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <string>
#include <atomic>
#include <mutex>
#include <vector>

namespace mi {
namespace panel {

// ─────────────────────────────────────────────────────────────────────────────
//  Constantes
// ─────────────────────────────────────────────────────────────────────────────
constexpr const char* ENGINE_VERSION = "Angels Memory Engine v2.0";
constexpr int         TAB_SCAN       = 0;
constexpr int         TAB_WRITE      = 1;
constexpr int         TAB_FREEZE     = 2;
constexpr int         TAB_WATCH      = 3;
constexpr int         TAB_SPEED      = 4;
constexpr int         TAB_STRING     = 5;
constexpr int         TAB_XOR        = 6;
constexpr int         TAB_COUNT      = 7;

// ─────────────────────────────────────────────────────────────────────────────
//  État du panel
// ─────────────────────────────────────────────────────────────────────────────
struct PanelState {
    // ── Processus cible ──────────────────────────────────────────────────────
    std::atomic<int>      target_pid      { 0 };

    // ── Onglet actif ─────────────────────────────────────────────────────────
    std::atomic<int>      active_tab      { TAB_SCAN };

    // ── Paramètres scan ──────────────────────────────────────────────────────
    std::atomic<int>      scan_value_type { 2 };    // DWORD par défaut
    std::atomic<int>      scan_mode       { 0 };    // EXACT par défaut
    std::atomic<int>      xor_key         { 0 };
    std::atomic<bool>     writable_only   { true };

    // ── Freeze ───────────────────────────────────────────────────────────────
    std::atomic<int>      freeze_interval_ms { 50 };

    // ── Speed hack ───────────────────────────────────────────────────────────
    std::atomic<double>   speed_factor    { 1.0 };
    std::atomic<bool>     speed_active    { false };

    // ── Log circulaire (max 32 messages) ─────────────────────────────────────
    mutable std::mutex          log_mutex;
    std::vector<std::string>    log_entries;
    static constexpr size_t     LOG_MAX = 32;

    void log(const std::string& msg);
    std::vector<std::string> get_log() const;
    void clear_log();

    // ── Singleton ────────────────────────────────────────────────────────────
    static PanelState& get();

private:
    PanelState() = default;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Fonctions utilitaires
// ─────────────────────────────────────────────────────────────────────────────

/// Retourne la version du moteur.
const char* get_version();

/// Log un message dans l'état du panel.
void        log_message(const std::string& msg);

/// Récupère tous les messages de log (newline-separated).
std::string get_log_string();

} // namespace panel
} // namespace mi
