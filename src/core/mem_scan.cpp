#include <android/log.h>
#define LOG_TAG "Angels/Scan"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
// ─────────────────────────────────────────────────────────────────────────────
//  mem_scan.cpp  —  Angels Memory Engine v2
//  Scan exact, fuzzy, XOR dynamique, groupe, écriture en masse
// ─────────────────────────────────────────────────────────────────────────────
#include "../../include/mi_core.h"
#include <sys/uio.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace mi {

static constexpr size_t CHUNK = 65536; // 64 Ko par lecture

// ─────────────────────────────────────────────────────────────────────────────
//  Lecture de chunk via process_vm_readv (sans passer par read_mem pour perf)
// ─────────────────────────────────────────────────────────────────────────────
static ssize_t read_chunk(pid_t pid, uintptr_t addr,
                          void* buf, size_t len) {
    struct iovec local  = { buf,                             len };
    struct iovec remote = { reinterpret_cast<void*>(addr),  len };
    return process_vm_readv(pid, &local, 1, &remote, 1, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Comparateur de valeurs — cœur du scan
//  cur    : valeur actuelle (brute)
//  prev   : valeur précédente (peut être nullptr)
//  target : cible(s) (peut être nullptr)
//  Pour BETWEEN : target = [min || max] (2x value_size octets)
//  Pour XOR     : cur est déjà XOR-décodé avant appel
// ─────────────────────────────────────────────────────────────────────────────
static bool value_match(const uint8_t* cur,
                        const uint8_t* prev,
                        const uint8_t* target,
                        ValueType      type,
                        ScanMode       mode)
{
// Macro générique pour types scalaires
#define DO_CMP(T) {                                                         \
    T c = 0, p = 0, t = 0, t2 = 0;                                        \
    memcpy(&c, cur, sizeof(T));                                             \
    if (prev)   memcpy(&p, prev,           sizeof(T));                     \
    if (target) memcpy(&t, target,         sizeof(T));                     \
    if (target && mode == ScanMode::BETWEEN)                               \
        memcpy(&t2, target + sizeof(T),    sizeof(T));                     \
    if constexpr (std::is_floating_point_v<T>) {                           \
        if (!std::isfinite(c)) return false;                               \
    }                                                                       \
    switch (mode) {                                                         \
        case ScanMode::EXACT:        return c == t;                        \
        case ScanMode::UNKNOWN:      return true;                          \
        case ScanMode::INCREASED:    return c >  p;                        \
        case ScanMode::DECREASED:    return c <  p;                        \
        case ScanMode::CHANGED:      return c != p;                        \
        case ScanMode::UNCHANGED:    return c == p;                        \
        case ScanMode::GREATER:      return c >  t;                        \
        case ScanMode::LESS:         return c <  t;                        \
        case ScanMode::BETWEEN:      return c >= t && c <= t2;             \
        case ScanMode::INCREASED_BY: return c == p + t;                   \
        case ScanMode::DECREASED_BY: return c == p - t;                   \
    }                                                                       \
    return false;                                                           \
}

    switch (type) {
        case ValueType::BYTE:   DO_CMP(uint8_t); break;
        case ValueType::WORD:   DO_CMP(int16_t); break;
        case ValueType::DWORD:  DO_CMP(int32_t); break;
        case ValueType::QWORD:  DO_CMP(int64_t); break;
        case ValueType::FLOAT:  DO_CMP(float); break;
        case ValueType::DOUBLE: DO_CMP(double); break;
        case ValueType::XOR:    DO_CMP(int32_t); break; // déjà décodé avant appel
    }
#undef DO_CMP
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Décode un entier XOR depuis des octets bruts
// ─────────────────────────────────────────────────────────────────────────────
static int32_t decode_xor(const uint8_t* raw, uint32_t key) {
    uint32_t enc;
    memcpy(&enc, raw, 4);
    return static_cast<int32_t>(enc ^ key);
}

// ─────────────────────────────────────────────────────────────────────────────
//  scan_first
// ─────────────────────────────────────────────────────────────────────────────
std::vector<ScanResult> scan_first(
    pid_t                          pid,
    const std::vector<MemRegion>&  regions,
    ValueType                      type,
    ScanMode                       mode,
    const void*                    target,
    uint32_t                       xor_key)
{
    std::vector<ScanResult> results;
    if (pid <= 0) return results;

    const size_t sz    = value_size(type);
    const size_t align = value_align(type);
    const auto*  tgt   = static_cast<const uint8_t*>(target);

    std::vector<uint8_t> chunk(CHUNK);

    for (const auto& region : regions) {
        if (!region.readable) continue;
        uintptr_t base = region.start;
        const uintptr_t rend = region.end;

        while (base < rend) {
            size_t to_read = std::min(static_cast<size_t>(rend - base), CHUNK);
            ssize_t got = read_chunk(pid, base, chunk.data(), to_read);
            if (got <= 0) { base += CHUNK; continue; }

            const size_t readable = static_cast<size_t>(got);

            // Calcul de l'offset de départ aligné dans ce chunk
            size_t off_start = 0;
            if (align > 1) {
                size_t mis = base % align;
                if (mis) off_start = align - mis;
            }

            for (size_t off = off_start; off + sz <= readable; off += align) {
                const uint8_t* cur = chunk.data() + off;

                bool matched = false;
                if (type == ValueType::XOR) {
                    // Décoder à la volée et comparer
                    int32_t decoded = decode_xor(cur, xor_key);
                    matched = value_match(
                        reinterpret_cast<const uint8_t*>(&decoded),
                        nullptr, tgt, ValueType::DWORD, mode);
                } else {
                    matched = value_match(cur, nullptr, tgt, type, mode);
                }

                if (matched) {
                    ScanResult sr;
                    sr.address = base + off;
                    sr.value.assign(cur, cur + sz);
                    results.push_back(std::move(sr));
                }
            }
            base += readable;
        }
    }
    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
//  scan_refine
// ─────────────────────────────────────────────────────────────────────────────
std::vector<ScanResult> scan_refine(
    pid_t                          pid,
    const std::vector<ScanResult>& prev,
    ValueType                      type,
    ScanMode                       mode,
    const void*                    target,
    uint32_t                       xor_key)
{
    std::vector<ScanResult> results;
    if (pid <= 0) return results;
    results.reserve(prev.size());

    const size_t sz  = value_size(type);
    const auto*  tgt = static_cast<const uint8_t*>(target);
    std::vector<uint8_t> cur_buf(sz);

    for (const auto& entry : prev) {
        if (!read_mem(pid, entry.address, cur_buf.data(), sz)) continue;

        const uint8_t* prev_val = (entry.value.size() >= sz)
                                  ? entry.value.data() : nullptr;
        bool matched = false;

        if (type == ValueType::XOR) {
            int32_t decoded = decode_xor(cur_buf.data(), xor_key);
            int32_t prev_dec = 0;
            if (prev_val) prev_dec = decode_xor(prev_val, xor_key);
            matched = value_match(
                reinterpret_cast<const uint8_t*>(&decoded),
                prev_val ? reinterpret_cast<const uint8_t*>(&prev_dec) : nullptr,
                tgt, ValueType::DWORD, mode);
        } else {
            matched = value_match(cur_buf.data(), prev_val, tgt, type, mode);
        }

        if (matched) {
            ScanResult sr;
            sr.address = entry.address;
            sr.value   = cur_buf;
            results.push_back(std::move(sr));
        }
    }
    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
//  scan_fuzzy_first — scan flou avec tolérance (FLOAT / DOUBLE)
// ─────────────────────────────────────────────────────────────────────────────
std::vector<ScanResult> scan_fuzzy_first(
    pid_t                          pid,
    const std::vector<MemRegion>&  regions,
    ValueType                      type,
    double                         target_val,
    double                         tolerance)
{
    std::vector<ScanResult> results;
    if (pid <= 0) return results;
    if (type != ValueType::FLOAT && type != ValueType::DOUBLE) return results;

    const size_t sz    = value_size(type);
    const size_t align = value_align(type);
    const double lo    = target_val - std::abs(tolerance);
    const double hi    = target_val + std::abs(tolerance);

    std::vector<uint8_t> chunk(CHUNK);

    for (const auto& region : regions) {
        if (!region.readable) continue;
        uintptr_t base = region.start;
        const uintptr_t rend = region.end;

        while (base < rend) {
            size_t to_read = std::min(static_cast<size_t>(rend - base), CHUNK);
            ssize_t got = read_chunk(pid, base, chunk.data(), to_read);
            if (got <= 0) { base += CHUNK; continue; }

            const size_t readable = static_cast<size_t>(got);
            size_t off_start = 0;
            if (align > 1) {
                size_t mis = base % align;
                if (mis) off_start = align - mis;
            }

            for (size_t off = off_start; off + sz <= readable; off += align) {
                double v = 0.0;
                if (type == ValueType::FLOAT) {
                    float f; memcpy(&f, chunk.data() + off, 4);
                    if (!std::isfinite(f)) continue;
                    v = static_cast<double>(f);
                } else {
                    memcpy(&v, chunk.data() + off, 8);
                    if (!std::isfinite(v)) continue;
                }
                if (v >= lo && v <= hi) {
                    ScanResult sr;
                    sr.address = base + off;
                    sr.value.assign(chunk.data() + off,
                                    chunk.data() + off + sz);
                    results.push_back(std::move(sr));
                }
            }
            base += readable;
        }
    }
    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
//  scan_fuzzy_refine
// ─────────────────────────────────────────────────────────────────────────────
std::vector<ScanResult> scan_fuzzy_refine(
    pid_t                          pid,
    const std::vector<ScanResult>& prev,
    ValueType                      type,
    double                         target_val,
    double                         tolerance)
{
    std::vector<ScanResult> results;
    if (pid <= 0) return results;
    if (type != ValueType::FLOAT && type != ValueType::DOUBLE) return results;

    const size_t sz = value_size(type);
    const double lo = target_val - std::abs(tolerance);
    const double hi = target_val + std::abs(tolerance);
    std::vector<uint8_t> buf(sz);

    for (const auto& entry : prev) {
        if (!read_mem(pid, entry.address, buf.data(), sz)) continue;
        double v = 0.0;
        if (type == ValueType::FLOAT) {
            float f; memcpy(&f, buf.data(), 4);
            if (!std::isfinite(f)) continue;
            v = static_cast<double>(f);
        } else {
            memcpy(&v, buf.data(), 8);
            if (!std::isfinite(v)) continue;
        }
        if (v >= lo && v <= hi) {
            ScanResult sr;
            sr.address = entry.address;
            sr.value   = buf;
            results.push_back(std::move(sr));
        }
    }
    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
//  scan_xor_keys — découverte de clé XOR
//  Stratégie : scan DWORD et calcule clé = raw XOR known_value.
//  Compte les clés les plus fréquentes → candidates.
// ─────────────────────────────────────────────────────────────────────────────
std::vector<uint32_t> scan_xor_keys(
    pid_t                          pid,
    const std::vector<MemRegion>&  regions,
    int32_t                        known_value,
    size_t                         max_keys)
{
    std::unordered_map<uint32_t, size_t> freq;
    std::vector<uint8_t> chunk(CHUNK);

    for (const auto& region : regions) {
        if (!region.readable) continue;
        uintptr_t base = region.start;
        const uintptr_t rend = region.end;

        while (base < rend) {
            size_t to_read = std::min(static_cast<size_t>(rend - base), CHUNK);
            ssize_t got = read_chunk(pid, base, chunk.data(), to_read);
            if (got <= 0) { base += CHUNK; continue; }

            const size_t readable = static_cast<size_t>(got);
            // Alignement 4 octets
            size_t off_start = base % 4 ? (4 - base % 4) : 0;

            for (size_t off = off_start; off + 4 <= readable; off += 4) {
                uint32_t raw;
                memcpy(&raw, chunk.data() + off, 4);
                uint32_t key = raw ^ static_cast<uint32_t>(known_value);
                // Filtre : ignore les clés triviales et les valeurs qui ressemblent
                // à du code ou des pointeurs (heuristique simple)
                if (key != 0 && key != 0xFFFFFFFF) {
                    freq[key]++;
                }
            }
            base += readable;
        }
    }

    // Trier par fréquence décroissante
    std::vector<std::pair<uint32_t, size_t>> sorted(freq.begin(), freq.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b){ return a.second > b.second; });

    std::vector<uint32_t> keys;
    keys.reserve(std::min(max_keys, sorted.size()));
    for (size_t i = 0; i < sorted.size() && i < max_keys; ++i)
        keys.push_back(sorted[i].first);

    return keys;
}

// ─────────────────────────────────────────────────────────────────────────────
//  scan_group — recherche de groupe via buffer continu
//  Lit chaque région en entier (ou par chunks) et cherche les items.
//  Pour chaque occurrence du pivot (item[0]), vérifie les items suivants
//  dans le buffer déjà chargé → zéro appels read_mem supplémentaires.
// ─────────────────────────────────────────────────────────────────────────────
std::vector<GroupResult> scan_group(
    pid_t                          pid,
    const std::vector<MemRegion>&  regions,
    const std::vector<GroupItem>&  items,
    size_t                         max_gap)
{
    std::vector<GroupResult> results;
    if (pid <= 0 || items.empty()) return results;

    // Taille du buffer de lecture : assez grand pour couvrir le groupe entier
    // dans un seul chunk, avec une fenêtre de recherche.
    const size_t sz0      = value_size(items[0].type);
    const size_t align0   = value_align(items[0].type);
    // Overlap entre deux chunks pour ne pas rater les groupes à cheval
    const size_t max_span = max_gap * items.size() + 64; // marge
    const size_t buf_size = std::max(CHUNK, max_span * 2);

    std::vector<uint8_t> buf(buf_size + max_span); // +max_span = carry

    for (const auto& region : regions) {
        if (!region.readable) continue;
        uintptr_t base = region.start;
        const uintptr_t rend = region.end;
        // Carry d'un chunk à l'autre pour ne pas rater les groupes coupés
        size_t carry = 0;

        while (base < rend) {
            // Copier le carry au début du buffer
            // (déjà fait en fin de boucle sauf au 1er tour)

            size_t to_read = std::min(static_cast<size_t>(rend - base), buf_size);
            ssize_t got = read_chunk(pid, base,
                                     buf.data() + carry, to_read);
            if (got <= 0) {
                carry = 0;
                base += buf_size;
                continue;
            }

            const size_t total = carry + static_cast<size_t>(got);

            // Alignement du pivot
            size_t off_start = 0;
            if (align0 > 1) {
                uintptr_t abs_base = base - carry;
                size_t mis = abs_base % align0;
                if (mis) off_start = align0 - mis;
            }

            for (size_t off = off_start;
                 off + sz0 <= total;
                 off += align0)
            {
                // Vérifier le pivot (item[0])
                if (memcmp(buf.data() + off,
                           items[0].value.data(), sz0) != 0)
                    continue;

                // Pivot trouvé — chercher les items suivants
                GroupResult gr;
                gr.base_address = (base - carry) + off;
                gr.item_addresses.push_back(gr.base_address);

                bool group_ok = true;
                size_t search_from = off + sz0; // début de recherche du prochain item

                for (size_t i = 1; i < items.size() && group_ok; ++i) {
                    const size_t isz     = value_size(items[i].type);
                    const size_t ialign  = value_align(items[i].type);
                    const size_t iwindow = (items[i].search_window > 0)
                                          ? items[i].search_window
                                          : max_gap;

                    bool found_item = false;

                    // Fenêtre de recherche alignée
                    size_t win_start = search_from;
                    // Aligner win_start
                    if (ialign > 1 && win_start % ialign)
                        win_start += ialign - (win_start % ialign);

                    const size_t win_end = std::min(off + max_gap * (i + 1),
                                                    total);

                    for (size_t woff = win_start;
                         woff + isz <= win_end;
                         woff += ialign)
                    {
                        if (woff + isz > total) break;
                        if (memcmp(buf.data() + woff,
                                   items[i].value.data(), isz) == 0) {
                            gr.item_addresses.push_back((base - carry) + woff);
                            search_from = woff + isz;
                            found_item = true;
                            break;
                        }
                    }
                    if (!found_item) group_ok = false;
                }

                if (group_ok) results.push_back(std::move(gr));
            }

            // Préparer le carry pour le prochain chunk
            carry = std::min(max_span, total);
            if (carry > 0 && carry <= total)
                memmove(buf.data(), buf.data() + (total - carry), carry);
            else
                carry = 0;

            base += static_cast<size_t>(got);
        }
    }
    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
//  write_all / write_all_offset
// ─────────────────────────────────────────────────────────────────────────────
int write_all(pid_t pid, const std::vector<ScanResult>& results,
              const void* value, size_t vsz)
{
    if (!value || vsz == 0 || pid <= 0) return 0;
    int n = 0;
    auto start = std::chrono::high_resolution_clock::now();
    for (const auto& r : results)
        if (write_mem(pid, r.address, value, vsz)) ++n;
    auto end = std::chrono::high_resolution_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    LOGI("write_all: %d writes in %lld ms", n, dur);
    return n;
}

int write_all_offset(pid_t pid, const std::vector<ScanResult>& results,
                     ptrdiff_t addr_offset, const void* value, size_t vsz)
{
    if (!value || vsz == 0 || pid <= 0) return 0;
    int n = 0;
    auto start = std::chrono::high_resolution_clock::now();
    for (const auto& r : results) {
        uintptr_t target_addr = static_cast<uintptr_t>(
            static_cast<ptrdiff_t>(r.address) + addr_offset);
        if (write_mem(pid, target_addr, value, vsz)) ++n;
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    LOGI("write_all: %d writes in %lld ms", n, dur);
    return n;
}

} // namespace mi
