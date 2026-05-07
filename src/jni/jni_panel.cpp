// ─────────────────────────────────────────────────────────────────────────────
//  jni_panel.cpp  —  Angels Memory Engine v2 — JNI bridge du Panel
//  Classe Java : com.angels.memory.AngelsPanel
//
//  CORRECTIONS v3 (crash fixes) :
//   - nativeGetScanCount DÉPLACÉ dans extern "C" (fix linkage JNI)
//   - Remplacement de l'appel JNI réflexif par un appel direct C
//     via angels_get_scan_count() exportée par jni_entry.cpp
//   - Ajout de validations null sur les paramètres JNI
// ─────────────────────────────────────────────────────────────────────────────
#include "../../include/panel_state.h"
#include <jni.h>
#include <android/log.h>
#include <string>

#define LOG_TAG "Angels/PanelJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)

// ═════════════════════════════════════════════════════════════════════════════
//  CORRECTION CRITIQUE v3 : Déclaration de la fonction exportée par
//  jni_entry.cpp. Évite l'appel JNI réflexif bogué.
// ═════════════════════════════════════════════════════════════════════════════
extern "C" int angels_get_scan_count(void);

extern "C" {

using namespace mi::panel;

// ─────────────────────────────────────────────────────────────────────────────
//  Version
// ─────────────────────────────────────────────────────────────────────────────
JNIEXPORT jstring JNICALL
Java_com_angels_memory_AngelsPanel_nativeGetVersion(JNIEnv* env, jclass) {
    return env->NewStringUTF(get_version());
}

// ─────────────────────────────────────────────────────────────────────────────
//  PID cible
// ─────────────────────────────────────────────────────────────────────────────
JNIEXPORT jint JNICALL
Java_com_angels_memory_AngelsPanel_nativeGetPid(JNIEnv*, jclass) {
    return static_cast<jint>(PanelState::get().target_pid.load());
}

JNIEXPORT void JNICALL
Java_com_angels_memory_AngelsPanel_nativeSetPid(JNIEnv*, jclass, jint pid) {
    PanelState::get().target_pid.store(static_cast<int>(pid));
    log_message(std::string("PID cible : ") + std::to_string(static_cast<int>(pid)));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Onglet actif
// ─────────────────────────────────────────────────────────────────────────────
JNIEXPORT jint JNICALL
Java_com_angels_memory_AngelsPanel_nativeGetActiveTab(JNIEnv*, jclass) {
    return static_cast<jint>(PanelState::get().active_tab.load());
}

JNIEXPORT void JNICALL
Java_com_angels_memory_AngelsPanel_nativeSetActiveTab(JNIEnv*, jclass, jint tab) {
    PanelState::get().active_tab.store(static_cast<int>(tab));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Type de valeur du scan
// ─────────────────────────────────────────────────────────────────────────────
JNIEXPORT jint JNICALL
Java_com_angels_memory_AngelsPanel_nativeGetScanValueType(JNIEnv*, jclass) {
    return static_cast<jint>(PanelState::get().scan_value_type.load());
}

JNIEXPORT void JNICALL
Java_com_angels_memory_AngelsPanel_nativeSetScanValueType(JNIEnv*, jclass, jint vt) {
    PanelState::get().scan_value_type.store(static_cast<int>(vt));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Mode de scan
// ─────────────────────────────────────────────────────────────────────────────
JNIEXPORT jint JNICALL
Java_com_angels_memory_AngelsPanel_nativeGetScanMode(JNIEnv*, jclass) {
    return static_cast<jint>(PanelState::get().scan_mode.load());
}

JNIEXPORT void JNICALL
Java_com_angels_memory_AngelsPanel_nativeSetScanMode(JNIEnv*, jclass, jint mode) {
    PanelState::get().scan_mode.store(static_cast<int>(mode));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Clé XOR
// ─────────────────────────────────────────────────────────────────────────────
JNIEXPORT jint JNICALL
Java_com_angels_memory_AngelsPanel_nativeGetXorKey(JNIEnv*, jclass) {
    return static_cast<jint>(PanelState::get().xor_key.load());
}

JNIEXPORT void JNICALL
Java_com_angels_memory_AngelsPanel_nativeSetXorKey(JNIEnv*, jclass, jint key) {
    PanelState::get().xor_key.store(static_cast<int>(key));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Writable only
// ─────────────────────────────────────────────────────────────────────────────
JNIEXPORT jboolean JNICALL
Java_com_angels_memory_AngelsPanel_nativeGetWritableOnly(JNIEnv*, jclass) {
    return PanelState::get().writable_only.load() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_angels_memory_AngelsPanel_nativeSetWritableOnly(JNIEnv*, jclass, jboolean v) {
    PanelState::get().writable_only.store(static_cast<bool>(v));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Intervalle de freeze
// ─────────────────────────────────────────────────────────────────────────────
JNIEXPORT jint JNICALL
Java_com_angels_memory_AngelsPanel_nativeGetFreezeInterval(JNIEnv*, jclass) {
    return static_cast<jint>(PanelState::get().freeze_interval_ms.load());
}

JNIEXPORT void JNICALL
Java_com_angels_memory_AngelsPanel_nativeSetFreezeInterval(JNIEnv*, jclass, jint ms) {
    int v = static_cast<int>(ms);
    if (v < 10)  v = 10;
    if (v > 5000) v = 5000;
    PanelState::get().freeze_interval_ms.store(v);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Speed hack
// ─────────────────────────────────────────────────────────────────────────────
JNIEXPORT jdouble JNICALL
Java_com_angels_memory_AngelsPanel_nativeGetSpeedFactor(JNIEnv*, jclass) {
    return static_cast<jdouble>(PanelState::get().speed_factor.load());
}

JNIEXPORT void JNICALL
Java_com_angels_memory_AngelsPanel_nativeSetSpeedFactor(JNIEnv*, jclass, jdouble f) {
    double v = static_cast<double>(f);
    if (v < 0.01) v = 0.01;
    if (v > 20.0) v = 20.0;
    PanelState::get().speed_factor.store(v);
}

JNIEXPORT jboolean JNICALL
Java_com_angels_memory_AngelsPanel_nativeIsSpeedActive(JNIEnv*, jclass) {
    return PanelState::get().speed_active.load() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_angels_memory_AngelsPanel_nativeSetSpeedActive(JNIEnv*, jclass, jboolean v) {
    PanelState::get().speed_active.store(static_cast<bool>(v));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Log
// ─────────────────────────────────────────────────────────────────────────────
JNIEXPORT void JNICALL
Java_com_angels_memory_AngelsPanel_nativeLogMessage(JNIEnv* env, jclass,
                                                     jstring msg) {
    if (!msg) return;
    const char* c = env->GetStringUTFChars(msg, nullptr);
    if (c) {
        log_message(std::string(c));
        env->ReleaseStringUTFChars(msg, c);
    }
}

JNIEXPORT jstring JNICALL
Java_com_angels_memory_AngelsPanel_nativeGetLog(JNIEnv* env, jclass) {
    return env->NewStringUTF(get_log_string().c_str());
}

JNIEXPORT void JNICALL
Java_com_angels_memory_AngelsPanel_nativeClearLog(JNIEnv*, jclass) {
    PanelState::get().clear_log();
}

// ═════════════════════════════════════════════════════════════════════════════
//  CORRECTION CRITIQUE v3 : nativeGetScanCount DANS extern "C"
//
//  Problème original :
//    - La fonction était en DEHORS du bloc extern "C" (après ligne 195)
//    - Le compilateur C++ appliquait le name mangling
//    - La JVM ne trouvait jamais le symbole → UnsatisfiedLinkError → CRASH
//
//  Solution :
//    1. Déplacer la fonction À L'INTÉRIEUR du bloc extern "C"
//    2. Appeler directement angels_get_scan_count() (fonction C exportée
//       par jni_entry.cpp) au lieu de l'appel JNI réflexif FindClass +
//       CallStaticIntMethod qui était fragile et bogué.
// ═════════════════════════════════════════════════════════════════════════════
JNIEXPORT jint JNICALL
Java_com_angels_memory_AngelsPanel_nativeGetScanCount(JNIEnv*, jclass) {
    // Appel direct à la fonction C exportée par jni_entry.cpp.
    // Plus rapide, plus sûr, et ne dépend pas du ClassLoader.
    return static_cast<jint>(angels_get_scan_count());
}

} // extern "C"
