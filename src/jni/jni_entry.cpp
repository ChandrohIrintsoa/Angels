// ─────────────────────────────────────────────────────────────────────────────
//  jni_entry.cpp  —  Angels Memory Engine v2 — Pont JNI complet
//  Package : com.angels.memory  /  Classe : NativeCore
//
//  CORRECTIONS v2 :
//   - getScanResults + scanClear manquants → ajoutés
//   - DeleteLocalRef manquant dans scanGroup → ajouté
//   - XOR key passée en paramètre (plus hardcodée)
//   - scan_fuzzy exposé via scanFuzzyFirst / scanFuzzyRefine
//   - scan_xor_keys exposé
//   - GroupResult complet retourné (adresses de chaque item)
//   - freezeAllResults exposé
//   - writeAllOffset exposé
//
//  CORRECTION CRITIQUE v3 (crash fix) :
//   - Ajout de angels_get_scan_count() exportée pour jni_panel.cpp
//     (évite l'appel JNI réflexif bogué qui causait UnsatisfiedLinkError)
// ─────────────────────────────────────────────────────────────────────────────
#include "../../include/mi_core.h"
#include <jni.h>
#include <android/log.h>
#include <cstring>
#include <vector>
#include <string>
#include <mutex>

#define LOG_TAG "Angels"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────
static std::vector<uint8_t> jbytes_to_vec(JNIEnv* env, jbyteArray arr) {
    if (!arr) return {};
    jsize len = env->GetArrayLength(arr);
    if (len <= 0) return {};
    std::vector<uint8_t> v(static_cast<size_t>(len));
    env->GetByteArrayRegion(arr, 0, len,
                            reinterpret_cast<jbyte*>(v.data()));
    return v;
}

static jbyteArray vec_to_jbytes(JNIEnv* env,
                                 const std::vector<uint8_t>& v) {
    if (v.empty()) return nullptr;
    jbyteArray arr = env->NewByteArray(static_cast<jsize>(v.size()));
    if (!arr) { LOGE("Failed to allocate jbyteArray"); return nullptr; }
    if (!arr) return nullptr;
    env->SetByteArrayRegion(arr, 0, static_cast<jsize>(v.size()),
                            reinterpret_cast<const jbyte*>(v.data()));
    return arr;
}

static jlongArray addrs_to_jlongs(JNIEnv* env,
                                   const std::vector<mi::ScanResult>& res) {
    jlongArray arr = env->NewLongArray(static_cast<jsize>(res.size()));
    if (!arr) return nullptr;
    std::vector<jlong> tmp;
    tmp.reserve(res.size());
    for (const auto& r : res) tmp.push_back(static_cast<jlong>(r.address));
    env->SetLongArrayRegion(arr, 0, static_cast<jsize>(tmp.size()),
                            tmp.data());
    return arr;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Cache natif du dernier scan
// ─────────────────────────────────────────────────────────────────────────────
static std::mutex                  s_scan_mutex;
static std::vector<mi::ScanResult> s_scan_results;

// ═════════════════════════════════════════════════════════════════════════════
//  CORRECTION CRITIQUE v3 : Fonction C exportée pour jni_panel.cpp
//  Permet d'accéder au scan count sans passer par un appel JNI réflexif
//  (l'appel réflexif causait des crashs selon le contexte du thread).
// ═════════════════════════════════════════════════════════════════════════════
extern "C" int angels_get_scan_count(void) {
    std::lock_guard<std::mutex> lock(s_scan_mutex);
    return static_cast<int>(s_scan_results.size());
}

extern "C" {

// ─────────────────────────────────────────────────────────────────────────────
//  Maps
// ─────────────────────────────────────────────────────────────────────────────
// long[] getMaps(int pid, boolean writableOnly)
JNIEXPORT jlongArray JNICALL
Java_com_angels_memory_NativeCore_getMaps(JNIEnv* env, jclass,
                                          jint pid, jboolean writable_only)
{
    auto regions = mi::get_maps(static_cast<pid_t>(pid),
                                static_cast<bool>(writable_only));
    jlongArray arr = env->NewLongArray(
        static_cast<jsize>(regions.size() * 2));
    if (!arr) return nullptr;
    std::vector<jlong> tmp;
    tmp.reserve(regions.size() * 2);
    for (const auto& r : regions) {
        tmp.push_back(static_cast<jlong>(r.start));
        tmp.push_back(static_cast<jlong>(r.end));
    }
    env->SetLongArrayRegion(arr, 0, static_cast<jsize>(tmp.size()),
                            tmp.data());
    return arr;
}

// ─────────────────────────────────────────────────────────────────────────────
//  R/W
// ─────────────────────────────────────────────────────────────────────────────
// byte[] readMem(int pid, long addr, int len)
JNIEXPORT jbyteArray JNICALL
Java_com_angels_memory_NativeCore_readMem(JNIEnv* env, jclass,
                                          jint pid, jlong addr, jint len)
{
    if (len <= 0 || len > (1 << 22)) return nullptr; // max 4 Mo
    std::vector<uint8_t> buf(static_cast<size_t>(len));
    if (!mi::read_mem(static_cast<pid_t>(pid),
                      static_cast<uintptr_t>(addr),
                      buf.data(), buf.size())) return nullptr;
    return vec_to_jbytes(env, buf);
}

// boolean writeMem(int pid, long addr, byte[] data)
JNIEXPORT jboolean JNICALL
Java_com_angels_memory_NativeCore_writeMem(JNIEnv* env, jclass,
                                           jint pid, jlong addr,
                                           jbyteArray data)
{
    auto v = jbytes_to_vec(env, data);
    if (v.empty()) return JNI_FALSE;
    return mi::write_mem(static_cast<pid_t>(pid),
                         static_cast<uintptr_t>(addr),
                         v.data(), v.size())
           ? JNI_TRUE : JNI_FALSE;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Scan exact
// ─────────────────────────────────────────────────────────────────────────────
// int scanFirst(int pid, boolean writableOnly, int valueType,
//               int scanMode, byte[] target, int xorKey)
JNIEXPORT jint JNICALL
Java_com_angels_memory_NativeCore_scanFirst(JNIEnv* env, jclass,
                                            jint pid, jboolean writable_only,
                                            jint value_type, jint scan_mode,
                                            jbyteArray target, jint xor_key)
{
    auto regions = mi::get_maps(static_cast<pid_t>(pid),
                                static_cast<bool>(writable_only));
    auto tgt = jbytes_to_vec(env, target);
    auto results = mi::scan_first(
        static_cast<pid_t>(pid), regions,
        static_cast<mi::ValueType>(value_type),
        static_cast<mi::ScanMode>(scan_mode),
        tgt.empty() ? nullptr : tgt.data(),
        static_cast<uint32_t>(xor_key));

    std::lock_guard<std::mutex> lock(s_scan_mutex);
    s_scan_results = std::move(results);
    LOGI("scanFirst: %zu results", s_scan_results.size());
    return static_cast<jint>(s_scan_results.size());
}

// int scanRefine(int pid, int valueType, int scanMode,
//                byte[] target, int xorKey)
JNIEXPORT jint JNICALL
Java_com_angels_memory_NativeCore_scanRefine(JNIEnv* env, jclass,
                                             jint pid, jint value_type,
                                             jint scan_mode,
                                             jbyteArray target, jint xor_key)
{
    auto tgt = jbytes_to_vec(env, target);
    std::lock_guard<std::mutex> lock(s_scan_mutex);
    s_scan_results = mi::scan_refine(
        static_cast<pid_t>(pid), s_scan_results,
        static_cast<mi::ValueType>(value_type),
        static_cast<mi::ScanMode>(scan_mode),
        tgt.empty() ? nullptr : tgt.data(),
        static_cast<uint32_t>(xor_key));
    LOGI("scanRefine: %zu remaining", s_scan_results.size());
    return static_cast<jint>(s_scan_results.size());
}

// ─────────────────────────────────────────────────────────────────────────────
//  Scan flou (Fuzzy)
// ─────────────────────────────────────────────────────────────────────────────
// int scanFuzzyFirst(int pid, boolean writableOnly, int valueType,
//                   double targetVal, double tolerance)
JNIEXPORT jint JNICALL
Java_com_angels_memory_NativeCore_scanFuzzyFirst(JNIEnv*, jclass,
                                                  jint pid,
                                                  jboolean writable_only,
                                                  jint value_type,
                                                  jdouble target_val,
                                                  jdouble tolerance)
{
    auto regions = mi::get_maps(static_cast<pid_t>(pid),
                                static_cast<bool>(writable_only));
    auto results = mi::scan_fuzzy_first(
        static_cast<pid_t>(pid), regions,
        static_cast<mi::ValueType>(value_type),
        static_cast<double>(target_val),
        static_cast<double>(tolerance));

    std::lock_guard<std::mutex> lock(s_scan_mutex);
    s_scan_results = std::move(results);
    LOGI("scanFuzzyFirst: %zu results", s_scan_results.size());
    return static_cast<jint>(s_scan_results.size());
}

// int scanFuzzyRefine(int pid, int valueType, double targetVal,
//                    double tolerance)
JNIEXPORT jint JNICALL
Java_com_angels_memory_NativeCore_scanFuzzyRefine(JNIEnv*, jclass,
                                                   jint pid, jint value_type,
                                                   jdouble target_val,
                                                   jdouble tolerance)
{
    std::lock_guard<std::mutex> lock(s_scan_mutex);
    s_scan_results = mi::scan_fuzzy_refine(
        static_cast<pid_t>(pid), s_scan_results,
        static_cast<mi::ValueType>(value_type),
        static_cast<double>(target_val),
        static_cast<double>(tolerance));
    LOGI("scanFuzzyRefine: %zu remaining", s_scan_results.size());
    return static_cast<jint>(s_scan_results.size());
}

// ─────────────────────────────────────────────────────────────────────────────
//  Découverte de clé XOR
// ─────────────────────────────────────────────────────────────────────────────
// int[] scanXorKeys(int pid, boolean writableOnly, int knownValue)
JNIEXPORT jintArray JNICALL
Java_com_angels_memory_NativeCore_scanXorKeys(JNIEnv* env, jclass,
                                               jint pid,
                                               jboolean writable_only,
                                               jint known_value)
{
    auto regions = mi::get_maps(static_cast<pid_t>(pid),
                                static_cast<bool>(writable_only));
    auto keys = mi::scan_xor_keys(
        static_cast<pid_t>(pid), regions,
        static_cast<int32_t>(known_value), 16);

    jintArray arr = env->NewIntArray(static_cast<jsize>(keys.size()));
    if (!arr) { LOGE("Failed to allocate jintArray"); return nullptr; }
    if (!arr) return nullptr;
    std::vector<jint> tmp(keys.begin(), keys.end());
    env->SetIntArrayRegion(arr, 0, static_cast<jsize>(tmp.size()),
                           tmp.data());
    LOGI("scanXorKeys: %zu candidates", keys.size());
    return arr;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Scan de groupe — CORRIGÉ : DeleteLocalRef + GroupResult complet
// ─────────────────────────────────────────────────────────────────────────────
// int scanGroup(int pid, boolean writableOnly, int[] types,
//               byte[][] values, int maxGap)
JNIEXPORT jint JNICALL
Java_com_angels_memory_NativeCore_scanGroup(JNIEnv* env, jclass,
                                            jint pid, jboolean writable_only,
                                            jintArray types,
                                            jobjectArray values,
                                            jint max_gap)
{
    if (!types || !values) return 0;
    jsize n = env->GetArrayLength(types);
    if (n <= 0) return 0;

    std::vector<mi::GroupItem> items;
    items.reserve(static_cast<size_t>(n));

    for (jsize i = 0; i < n; ++i) {
        jint t;
        env->GetIntArrayRegion(types, i, 1, &t);

        auto* v_arr = static_cast<jbyteArray>(
            env->GetObjectArrayElement(values, i));
        mi::GroupItem item;
        item.type          = static_cast<mi::ValueType>(t);
        item.value         = jbytes_to_vec(env, v_arr);
        item.offset_hint   = 0;
        item.search_window = 0; // utilise max_gap
        items.push_back(std::move(item));

        if (v_arr) env->DeleteLocalRef(v_arr); // ← CORRECTION : évite fuite JNI
    }

    auto regions = mi::get_maps(static_cast<pid_t>(pid),
                                static_cast<bool>(writable_only));
    auto group_results = mi::scan_group(
        static_cast<pid_t>(pid), regions, items,
        static_cast<size_t>(max_gap));

    // Convertir GroupResult → ScanResult pour le cache
    std::vector<mi::ScanResult> flat;
    flat.reserve(group_results.size());
    for (const auto& gr : group_results) {
        mi::ScanResult sr;
        sr.address = gr.base_address;
        flat.push_back(std::move(sr));
    }

    std::lock_guard<std::mutex> lock(s_scan_mutex);
    s_scan_results = std::move(flat);
    LOGI("scanGroup: %zu groups found", s_scan_results.size());
    return static_cast<jint>(s_scan_results.size());
}

// ─────────────────────────────────────────────────────────────────────────────
//  Accès au cache de résultats  ← MANQUAIT dans v1
// ─────────────────────────────────────────────────────────────────────────────
// long[] getScanResults(int offset, int count)
JNIEXPORT jlongArray JNICALL
Java_com_angels_memory_NativeCore_getScanResults(JNIEnv* env, jclass,
                                                  jint offset, jint count)
{
    std::lock_guard<std::mutex> lock(s_scan_mutex);
    size_t total = s_scan_results.size();
    size_t start = std::min(static_cast<size_t>(offset), total);
    size_t end   = std::min(start + static_cast<size_t>(count), total);
    size_t n     = end - start;

    jlongArray arr = env->NewLongArray(static_cast<jsize>(n));
    if (!arr || n == 0) return arr;
    std::vector<jlong> tmp(n);
    for (size_t i = 0; i < n; ++i)
        tmp[i] = static_cast<jlong>(s_scan_results[start + i].address);
    env->SetLongArrayRegion(arr, 0, static_cast<jsize>(n), tmp.data());
    return arr;
}

// int getScanCount()
JNIEXPORT jint JNICALL
Java_com_angels_memory_NativeCore_getScanCount(JNIEnv*, jclass)
{
    std::lock_guard<std::mutex> lock(s_scan_mutex);
    return static_cast<jint>(s_scan_results.size());
}

// void scanClear()  ← MANQUAIT dans v1
JNIEXPORT void JNICALL
Java_com_angels_memory_NativeCore_scanClear(JNIEnv*, jclass)
{
    std::lock_guard<std::mutex> lock(s_scan_mutex);
    s_scan_results.clear();
    s_scan_results.shrink_to_fit();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Écriture en masse
// ─────────────────────────────────────────────────────────────────────────────
// int writeAll(int pid, byte[] value)
JNIEXPORT jint JNICALL
Java_com_angels_memory_NativeCore_writeAll(JNIEnv* env, jclass,
                                           jint pid, jbyteArray value)
{
    auto v = jbytes_to_vec(env, value);
    if (v.empty()) return 0;
    std::lock_guard<std::mutex> lock(s_scan_mutex);
    int n = mi::write_all(static_cast<pid_t>(pid), s_scan_results,
                          v.data(), v.size());
    LOGI("writeAll: %d/%zu written", n, s_scan_results.size());
    return n;
}

// int writeAllOffset(int pid, long addrOffset, byte[] value)
JNIEXPORT jint JNICALL
Java_com_angels_memory_NativeCore_writeAllOffset(JNIEnv* env, jclass,
                                                  jint pid, jlong addr_offset,
                                                  jbyteArray value)
{
    auto v = jbytes_to_vec(env, value);
    if (v.empty()) return 0;
    std::lock_guard<std::mutex> lock(s_scan_mutex);
    int n = mi::write_all_offset(
        static_cast<pid_t>(pid), s_scan_results,
        static_cast<ptrdiff_t>(addr_offset),
        v.data(), v.size());
    LOGI("writeAllOffset: %d/%zu written", n, s_scan_results.size());
    return n;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Freeze
// ─────────────────────────────────────────────────────────────────────────────
// int freezeAdd(int pid, long addr, byte[] value, int intervalMs)
JNIEXPORT jint JNICALL
Java_com_angels_memory_NativeCore_freezeAdd(JNIEnv* env, jclass,
                                            jint pid, jlong addr,
                                            jbyteArray value, jint interval_ms)
{
    auto v = jbytes_to_vec(env, value);
    if (v.empty()) return -1;
    return mi::freeze_add(static_cast<pid_t>(pid),
                          static_cast<uintptr_t>(addr),
                          v.data(), v.size(),
                          static_cast<int>(interval_ms));
}

// boolean freezeRemove(int id)
JNIEXPORT jboolean JNICALL
Java_com_angels_memory_NativeCore_freezeRemove(JNIEnv*, jclass, jint id) {
    return mi::freeze_remove(id) ? JNI_TRUE : JNI_FALSE;
}

// void freezeClear()
JNIEXPORT void JNICALL
Java_com_angels_memory_NativeCore_freezeClear(JNIEnv*, jclass) {
    mi::freeze_clear();
}

// int freezeAllResults(int pid, byte[] value, int intervalMs)
JNIEXPORT jint JNICALL
Java_com_angels_memory_NativeCore_freezeAllResults(JNIEnv* env, jclass,
                                                    jint pid,
                                                    jbyteArray value,
                                                    jint interval_ms)
{
    auto v = jbytes_to_vec(env, value);
    if (v.empty()) return -1;
    std::lock_guard<std::mutex> lock(s_scan_mutex);
    int id = mi::freeze_all_results(
        static_cast<pid_t>(pid), s_scan_results,
        v.data(), v.size(),
        static_cast<int>(interval_ms));
    LOGI("freezeAllResults: id=%d, %zu addrs", id, s_scan_results.size());
    return id;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Recherche de chaînes
// ─────────────────────────────────────────────────────────────────────────────
// long[] scanString(int pid, boolean writableOnly, String needle,
//                   boolean caseSensitive)
JNIEXPORT jlongArray JNICALL
Java_com_angels_memory_NativeCore_scanString(JNIEnv* env, jclass,
                                             jint pid,
                                             jboolean writable_only,
                                             jstring needle,
                                             jboolean case_sensitive)
{
    if (!needle) return env->NewLongArray(0);
    const char* c = env->GetStringUTFChars(needle, nullptr);
    std::string s = c ? c : "";
    if (c) env->ReleaseStringUTFChars(needle, c);

    auto regions = mi::get_maps(static_cast<pid_t>(pid),
                                static_cast<bool>(writable_only));
    auto results = mi::scan_string(static_cast<pid_t>(pid), regions, s,
                                   static_cast<bool>(case_sensitive));

    jlongArray arr = env->NewLongArray(static_cast<jsize>(results.size()));
    if (!arr) { LOGE("Failed to allocate jlongArray"); return nullptr; }
    if (!arr) return nullptr;
    std::vector<jlong> tmp;
    tmp.reserve(results.size());
    for (const auto& r : results)
        tmp.push_back(static_cast<jlong>(r.address));
    env->SetLongArrayRegion(arr, 0, static_cast<jsize>(tmp.size()),
                            tmp.data());
    LOGI("scanString '%s': %zu hits", s.c_str(), results.size());
    return arr;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Watch List
// ─────────────────────────────────────────────────────────────────────────────
// int watchAdd(int pid, long addr, int valueType, String label)
JNIEXPORT jint JNICALL
Java_com_angels_memory_NativeCore_watchAdd(JNIEnv* env, jclass,
                                           jint pid, jlong addr,
                                           jint value_type, jstring label)
{
    std::string lbl;
    if (label) {
        const char* c = env->GetStringUTFChars(label, nullptr);
        if (c) { lbl = c; env->ReleaseStringUTFChars(label, c); }
    }
    return mi::watch_add(static_cast<pid_t>(pid),
                         static_cast<uintptr_t>(addr),
                         static_cast<mi::ValueType>(value_type), lbl);
}

// boolean watchRemove(int id)
JNIEXPORT jboolean JNICALL
Java_com_angels_memory_NativeCore_watchRemove(JNIEnv*, jclass, jint id) {
    return mi::watch_remove(id) ? JNI_TRUE : JNI_FALSE;
}

// byte[] watchRead(int id)
JNIEXPORT jbyteArray JNICALL
Java_com_angels_memory_NativeCore_watchRead(JNIEnv* env, jclass, jint id) {
    return vec_to_jbytes(env, mi::watch_read(id));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Speed Hack
// ─────────────────────────────────────────────────────────────────────────────
// boolean speedHackSet(int pid, double factor)
JNIEXPORT jboolean JNICALL
Java_com_angels_memory_NativeCore_speedHackSet(JNIEnv*, jclass,
                                               jint pid, jdouble factor) {
    return mi::speed_hack_set(static_cast<pid_t>(pid),
                              static_cast<double>(factor))
           ? JNI_TRUE : JNI_FALSE;
}

// boolean speedHackReset(int pid)
JNIEXPORT jboolean JNICALL
Java_com_angels_memory_NativeCore_speedHackReset(JNIEnv*, jclass, jint pid) {
    return mi::speed_hack_reset(static_cast<pid_t>(pid))
           ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"
