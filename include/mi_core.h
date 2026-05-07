#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  mi_core.h  —  Angels Memory Engine v2 — API publique complète
//  Types    : BYTE WORD DWORD QWORD FLOAT DOUBLE XOR
//  Modes    : EXACT UNKNOWN INCREASED DECREASED CHANGED UNCHANGED
//             GREATER LESS BETWEEN INCREASED_BY DECREASED_BY
//  Extras   : scan_group, scan_fuzzy, scan_xor_key, freeze_all, write_all
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <optional>

namespace mi {

// ─────────────────────────────────────────────────────────────────────────────
//  ValueType
// ─────────────────────────────────────────────────────────────────────────────
enum class ValueType : int {
    BYTE   = 0,  // uint8_t  (1 octet)
    WORD   = 1,  // int16_t  (2 octets)
    DWORD  = 2,  // int32_t  (4 octets)
    QWORD  = 3,  // int64_t  (8 octets)
    FLOAT  = 4,  // float    (4 octets)
    DOUBLE = 5,  // double   (8 octets)
    XOR    = 6,  // int32_t XOR-encodé — clé découverte dynamiquement
};

inline size_t value_size(ValueType t) noexcept {
    switch (t) {
        case ValueType::BYTE:   return 1;
        case ValueType::WORD:   return 2;
        case ValueType::DWORD:  return 4;
        case ValueType::QWORD:  return 8;
        case ValueType::FLOAT:  return 4;
        case ValueType::DOUBLE: return 8;
        case ValueType::XOR:    return 4;
    }
    return 4;
}

inline size_t value_align(ValueType t) noexcept {
    size_t sz = value_size(t);
    if (sz >= 8) return 8;
    if (sz >= 4) return 4;
    if (sz >= 2) return 2;
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  ScanMode
// ─────────────────────────────────────────────────────────────────────────────
enum class ScanMode : int {
    EXACT        = 0,  // valeur == cible
    UNKNOWN      = 1,  // toute valeur (1er scan sans cible)
    INCREASED    = 2,  // valeur > précédent
    DECREASED    = 3,  // valeur < précédent
    CHANGED      = 4,  // valeur != précédent
    UNCHANGED    = 5,  // valeur == précédent
    GREATER      = 6,  // valeur > cible
    LESS         = 7,  // valeur < cible
    BETWEEN      = 8,  // cible_min <= valeur <= cible_max  (2 cibles)
    INCREASED_BY = 9,  // valeur == précédent + cible (delta exact)
    DECREASED_BY = 10, // valeur == précédent - cible (delta exact)
};

// ─────────────────────────────────────────────────────────────────────────────
//  MemRegion
// ─────────────────────────────────────────────────────────────────────────────
struct MemRegion {
    uintptr_t   start    = 0;
    uintptr_t   end      = 0;
    bool        readable = false;
    bool        writable = false;
    bool        exec     = false;
    std::string path;
};

std::vector<MemRegion> get_maps(pid_t pid, bool writable_only = true);

// ─────────────────────────────────────────────────────────────────────────────
//  ScanResult
// ─────────────────────────────────────────────────────────────────────────────
struct ScanResult {
    uintptr_t            address = 0;
    std::vector<uint8_t> value;   // dernière valeur lue (octets bruts LE)
};

// ─────────────────────────────────────────────────────────────────────────────
//  Mémoire R/W
// ─────────────────────────────────────────────────────────────────────────────
bool read_mem (pid_t pid, uintptr_t addr, void* dst,       size_t len);
bool write_mem(pid_t pid, uintptr_t addr, const void* src, size_t len);

// ─────────────────────────────────────────────────────────────────────────────
//  Scan numérique exact / flou
//
//  Pour BETWEEN   : target = [min(4/8o) || max(4/8o)]  (concaténés)
//  Pour INCREASED_BY / DECREASED_BY : target = delta encodé en LE
// ─────────────────────────────────────────────────────────────────────────────
std::vector<ScanResult> scan_first(
    pid_t                          pid,
    const std::vector<MemRegion>&  regions,
    ValueType                      type,
    ScanMode                       mode,
    const void*                    target,      // nullptr si UNKNOWN
    uint32_t                       xor_key = 0 // ignoré sauf ValueType::XOR
);

std::vector<ScanResult> scan_refine(
    pid_t                          pid,
    const std::vector<ScanResult>& prev,
    ValueType                      type,
    ScanMode                       mode,
    const void*                    target,
    uint32_t                       xor_key = 0
);

// ─────────────────────────────────────────────────────────────────────────────
//  Scan flou (Fuzzy) — pour valeurs inconnues avec tolérance float/double
//  tolerance : valeur absolue (ex: 0.01f pour float proche)
// ─────────────────────────────────────────────────────────────────────────────
std::vector<ScanResult> scan_fuzzy_first(
    pid_t                          pid,
    const std::vector<MemRegion>&  regions,
    ValueType                      type,       // FLOAT ou DOUBLE uniquement
    double                         target_val,
    double                         tolerance
);

std::vector<ScanResult> scan_fuzzy_refine(
    pid_t                          pid,
    const std::vector<ScanResult>& prev,
    ValueType                      type,
    double                         target_val,
    double                         tolerance
);

// ─────────────────────────────────────────────────────────────────────────────
//  Découverte de clé XOR
//  Cherche des paires (valeur_claire, valeur_XORée) dans la mémoire.
//  Retourne les clés XOR candidates les plus probables.
// ─────────────────────────────────────────────────────────────────────────────
std::vector<uint32_t> scan_xor_keys(
    pid_t                          pid,
    const std::vector<MemRegion>&  regions,
    int32_t                        known_value,   // valeur en clair connue
    size_t                         max_keys = 16  // nb max de clés retournées
);

// ─────────────────────────────────────────────────────────────────────────────
//  Scan de groupe
//  Recherche plusieurs valeurs proches en mémoire (ex: X, Y, Z d'une position).
//  Retourne l'adresse de base du premier item trouvé pour chaque groupe.
// ─────────────────────────────────────────────────────────────────────────────
struct GroupItem {
    ValueType            type;
    std::vector<uint8_t> value;         // valeur attendue (octets LE)
    size_t               offset_hint;   // offset attendu depuis le pivot (0 = inconnu)
    size_t               search_window; // fenêtre de recherche pour cet item (octets)
};

struct GroupResult {
    uintptr_t              base_address;   // adresse du pivot (item[0])
    std::vector<uintptr_t> item_addresses; // adresse de chaque item dans le groupe
};

std::vector<GroupResult> scan_group(
    pid_t                          pid,
    const std::vector<MemRegion>&  regions,
    const std::vector<GroupItem>&  items,
    size_t                         max_gap = 256  // distance max entre items (octets)
);

// ─────────────────────────────────────────────────────────────────────────────
//  Écriture en masse
// ─────────────────────────────────────────────────────────────────────────────

// Écrit value sur toutes les adresses de results. Retourne le nb de succès.
int write_all(
    pid_t                          pid,
    const std::vector<ScanResult>& results,
    const void*                    value,
    size_t                         value_size
);

// Écrit value sur toutes les adresses de results + un offset fixe.
int write_all_offset(
    pid_t                          pid,
    const std::vector<ScanResult>& results,
    ptrdiff_t                      addr_offset,
    const void*                    value,
    size_t                         value_size
);

// ─────────────────────────────────────────────────────────────────────────────
//  Recherche de chaînes
// ─────────────────────────────────────────────────────────────────────────────
struct StringResult {
    uintptr_t   address = 0;
    std::string found;
};

std::vector<StringResult> scan_string(
    pid_t                          pid,
    const std::vector<MemRegion>&  regions,
    const std::string&             needle,
    bool                           case_sensitive = true
);

// ─────────────────────────────────────────────────────────────────────────────
//  Freeze
// ─────────────────────────────────────────────────────────────────────────────
int  freeze_add        (pid_t pid, uintptr_t addr,
                        const void* value, size_t size, int interval_ms = 50);
bool freeze_remove     (int id);
void freeze_clear      ();

// Gèle toutes les adresses d'un jeu de résultats.
// Retourne le nombre de freezes créés.
int  freeze_all_results(pid_t pid,
                        const std::vector<ScanResult>& results,
                        const void* value, size_t value_size,
                        int interval_ms = 50);

// ─────────────────────────────────────────────────────────────────────────────
//  Watch List
// ─────────────────────────────────────────────────────────────────────────────
struct WatchEntry {
    int         id      = -1;
    pid_t       pid     = 0;
    uintptr_t   address = 0;
    ValueType   type    = ValueType::DWORD;
    std::string label;
};

int                     watch_add   (pid_t pid, uintptr_t addr,
                                     ValueType type, const std::string& label = "");
bool                    watch_remove(int id);
std::vector<uint8_t>    watch_read  (int id);
std::vector<WatchEntry> watch_list  ();

// ─────────────────────────────────────────────────────────────────────────────
//  Speed Hack
// ─────────────────────────────────────────────────────────────────────────────
bool speed_hack_set  (pid_t pid, double factor);
bool speed_hack_reset(pid_t pid);

} // namespace mi
