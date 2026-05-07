// ─────────────────────────────────────────────────────────────────────────────
//  mem_maps.cpp  —  Lecture de /proc/<pid>/maps
// ─────────────────────────────────────────────────────────────────────────────

#include "../../include/mi_core.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <cerrno>

namespace mi {

std::vector<MemRegion> get_maps(pid_t pid, bool writable_only) {
    std::vector<MemRegion> result;
    if (pid <= 0) return result;

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", static_cast<int>(pid));

    FILE* fp = fopen(path, "re"); // 'e' = O_CLOEXEC
    if (!fp) return result;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        // Format:
        // start-end perms offset dev inode [pathname]
        // a3b00000-a3c00000 rw-p 00000000 00:00 0  [heap]

        uintptr_t start = 0, end = 0;
        char perms[8] = {};
        unsigned long long offset = 0;
        int dev_major = 0, dev_minor = 0;
        unsigned long inode = 0;
        char pathname[256] = {};

        int parsed = sscanf(line,
            "%lx-%lx %7s %llx %x:%x %lu %255s",
            &start, &end, perms, &offset,
            &dev_major, &dev_minor, &inode, pathname);

        if (parsed < 3) continue;           // ligne malformée
        if (parsed < 8) pathname[0] = '\0'; // s'assurer que pathname est vide si non présent
        if (start >= end) continue;         // région vide
        if (perms[0] != 'r') continue;      // non lisible → on ignore toujours

        // Ignorer les pseudo-régions noyau
        if (strcmp(pathname, "[vdso]")     == 0) continue;
        if (strcmp(pathname, "[vsyscall]") == 0) continue;

        bool w = (perms[1] == 'w');
        bool x = (perms[2] == 'x');

        if (writable_only && !w) continue;

        MemRegion r;
        r.start    = start;
        r.end      = end;
        r.readable = true;
        r.writable = w;
        r.exec     = x;
        r.path     = (parsed >= 8) ? pathname : "";
        result.push_back(std::move(r));
    }

    fclose(fp);
    return result;
}

} // namespace mi
