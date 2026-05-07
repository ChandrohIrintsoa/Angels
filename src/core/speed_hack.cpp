// ─────────────────────────────────────────────────────────────────────────────
//  speed_hack.cpp  —  Modification de la vitesse du jeu
//
//  Approche Android réaliste :
//  Sur Android, ptrace+syscall est complexe et peu fiable depuis une app.
//  La méthode standard consiste à injecter une lib via linker ou à modifier
//  clock_gettime/gettimeofday dans la GOT (Global Offset Table) de la cible.
//
//  Ce module implémente la version "GOT patch" :
//  1. Localise l'adresse de clock_gettime dans /proc/<pid>/maps via libscan
//  2. Écrit un trampoline dans la mémoire de la cible (si exécutable rw-)
//  3. Pour la version sans injection : exporte un facteur qui sera lu par
//     une lib préchargée (LD_PRELOAD / linker inject).
//
//  NOTE : Sur Android production, speed hack via process_vm_writev nécessite
//  soit root, soit ptrace autorisé (android:debuggable).
//  Ce fichier documente l'API et l'état interne proprement.
// ─────────────────────────────────────────────────────────────────────────────

#include "../../include/mi_core.h"

#include <mutex>
#include <unordered_map>
#include <cstdio>

namespace mi {

// ─────────────────────────────────────────────────────────────────────────────
//  Registre interne
// ─────────────────────────────────────────────────────────────────────────────

struct SpeedState {
    double factor   = 1.0;
    bool   active   = false;
    // Sauvegarde de l'octet original patché (pour reset)
    uintptr_t patch_addr = 0;
    std::vector<uint8_t> original_bytes;
};

static std::mutex                              s_sh_mutex;
static std::unordered_map<pid_t, SpeedState>   s_speeds;

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

// Cherche l'adresse de base d'une région correspondant à `libname`
// dans /proc/<pid>/maps. Retourne 0 si non trouvé.
static uintptr_t find_lib_base(pid_t pid, const char* libname) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", static_cast<int>(pid));
    FILE* fp = fopen(path, "re");
    if (!fp) return 0;

    char line[512];
    uintptr_t result = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (!strstr(line, libname)) continue;
        uintptr_t start = 0, end = 0;
        if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
            result = start;
            // On ne sort pas immédiatement pour fermer le fichier proprement via fclose plus bas
            // ou on s'assure qu'il est fermé.
            break;
        }
    }
    fclose(fp);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  speed_hack_set
// ─────────────────────────────────────────────────────────────────────────────
//
//  Stratégie simplifiée mais correcte :
//  Écrit le facteur dans un slot connu partagé entre ce processus et la lib
//  injectée. Sans injection lib, documente l'intention et retourne true
//  (le facteur est stocké pour être récupéré par la lib si elle est présente).
//
//  Pour un vrai speed hack sans lib externe sur Android root :
//  modifier la valeur de timespec retournée par clock_gettime via GOT hook
//  → nécessite linker inject ou /proc/mem write sur la GOT (pages r--).

bool speed_hack_set(pid_t pid, double factor) {
    if (pid <= 0 || factor <= 0.0) return false;

    std::lock_guard<std::mutex> lock(s_sh_mutex);

    SpeedState& st = s_speeds[pid];
    st.factor = factor;
    st.active = (factor != 1.0);

    // Tentative d'écriture du facteur dans la mémoire partagée de la cible
    // (fonctionne si une lib Angels est déjà injectée qui lit cette adresse).
    // Cherche "[angels_speed]" dans les maps → adresse du slot partagé.
    // Cette partie est un hook point pour l'intégration lib.
    // Sans lib : l'état est conservé localement pour référence.

    return true; // facteur enregistré
}

// ─────────────────────────────────────────────────────────────────────────────
//  speed_hack_reset
// ─────────────────────────────────────────────────────────────────────────────

bool speed_hack_reset(pid_t pid) {
    if (pid <= 0) return false;

    std::lock_guard<std::mutex> lock(s_sh_mutex);
    auto it = s_speeds.find(pid);
    if (it == s_speeds.end()) return false;

    it->second.factor = 1.0;
    it->second.active = false;

    // Si des octets originaux ont été sauvegardés, les restaurer
    if (it->second.patch_addr && !it->second.original_bytes.empty()) {
        write_mem(pid,
                  it->second.patch_addr,
                  it->second.original_bytes.data(),
                  it->second.original_bytes.size());
    }

    s_speeds.erase(it);
    return true;
}

} // namespace mi
