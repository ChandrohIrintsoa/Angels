// ─────────────────────────────────────────────────────────────────────────────
//  string_scan.cpp  —  Recherche de chaînes UTF-8 dans la mémoire
//  Lecture par blocs 64Ko avec chevauchement (needle.size()-1) pour éviter
//  les coupures aux frontières de blocs.
// ─────────────────────────────────────────────────────────────────────────────

#include "../../include/mi_core.h"

#include <sys/uio.h>
#include <cctype>
#include <algorithm>

namespace mi {

static constexpr size_t STR_CHUNK = 65536;

// Conversion ASCII en minuscule (uniquement pour insensible à la casse ASCII)
static inline char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

// Recherche de `needle` dans `haystack` selon case_sensitive.
// Retourne l'offset dans haystack, ou std::string::npos si absent.
static size_t find_in_chunk(const std::vector<uint8_t>& haystack,
                            size_t                       hay_len,
                            const std::string&           needle,
                            bool                         case_sensitive)
{
    if (needle.empty() || hay_len < needle.size()) return std::string::npos;

    const size_t n = needle.size();

    if (case_sensitive) {
        // Boyer-Moore-Horspool simplifié (memmem-like)
        const uint8_t* h = haystack.data();
        const uint8_t* result = static_cast<const uint8_t*>(
            memmem(h, hay_len, needle.data(), n));
        return result ? static_cast<size_t>(result - h) : std::string::npos;
    }

    // Insensible à la casse : recherche naïve avec ascii_lower
    for (size_t i = 0; i + n <= hay_len; ++i) {
        bool match = true;
        for (size_t j = 0; j < n; ++j) {
            if (ascii_lower(static_cast<char>(haystack[i+j])) !=
                ascii_lower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return i;
    }
    return std::string::npos;
}

// ─────────────────────────────────────────────────────────────────────────────
//  scan_string
// ─────────────────────────────────────────────────────────────────────────────

std::vector<StringResult> scan_string(
    pid_t                          pid,
    const std::vector<MemRegion>&  regions,
    const std::string&             needle,
    bool                           case_sensitive)
{
    std::vector<StringResult> results;
    if (pid <= 0 || needle.empty()) return results;

    size_t nlen    = needle.size();
    size_t overlap = (nlen > 1) ? (nlen - 1) : 0;   // chevauchement inter-blocs

    std::vector<uint8_t> chunk(STR_CHUNK + overlap);

    for (const auto& region : regions) {
        if (!region.readable) continue;

        uintptr_t rstart = region.start;
        uintptr_t rend   = region.end;
        if (rend <= rstart) continue;

        // `carry` : octets résiduels du bloc précédent pour le chevauchement
        std::vector<uint8_t> carry;
        carry.reserve(overlap);

        uintptr_t base = rstart;

        while (base < rend) {
            size_t to_read = std::min(static_cast<size_t>(rend - base), STR_CHUNK);

            // Copier le carry au début du chunk
            size_t carry_size = carry.size();
            if (carry_size) memcpy(chunk.data(), carry.data(), carry_size);

            struct iovec local  = { chunk.data() + carry_size, to_read };
            struct iovec remote = { reinterpret_cast<void*>(base), to_read };
            ssize_t got = process_vm_readv(pid, &local, 1, &remote, 1, 0);

            if (got <= 0) {
                carry.clear();
                base += STR_CHUNK;
                continue;
            }

            size_t total = carry_size + static_cast<size_t>(got);

            // Chercher toutes les occurrences dans total octets
            size_t search_off = 0;
            while (true) {
                size_t rel = find_in_chunk(chunk, total - search_off,
                                           needle, case_sensitive);
                if (rel == std::string::npos) break;

                size_t abs_off = search_off + rel;
                // L'adresse réelle : base - carry_size + abs_off
                uintptr_t match_addr = (base - carry_size) + abs_off;
                // Ne pas rapporter les occurrences déjà couvertes par carry
                // (elles auraient été trouvées dans le bloc précédent)
                // → on rapporte seulement si match_addr >= base OU carry était vide
                if (carry_size == 0 || match_addr >= base) {
                    StringResult sr;
                    sr.address = match_addr;
                    sr.found   = needle; // on retourne le needle tel quel
                    results.push_back(std::move(sr));
                }
                search_off += rel + 1;  // avancer d'au moins 1
            }

            // Préparer le carry pour le prochain bloc
            if (overlap && static_cast<size_t>(got) >= overlap) {
                size_t carry_start = carry_size + static_cast<size_t>(got) - overlap;
                carry.assign(chunk.data() + carry_start,
                             chunk.data() + carry_start + overlap);
            } else {
                carry.clear();
            }

            base += static_cast<size_t>(got);
        }
    }

    return results;
}

} // namespace mi
