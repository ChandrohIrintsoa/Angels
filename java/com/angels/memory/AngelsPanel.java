package com.angels.memory;

/**
 * AngelsPanel — Pont Java ↔ libangels.so pour l'état du panel de contrôle.
 *
 * BUG FIX #11 : loadLibrary protégé — chargement idempotent via try/catch.
 * La lib est partagée dans le même process : charger deux fois est inoffensif
 * mais le try/catch évite le crash si NativeCore l'a déjà chargée.
 */
public final class AngelsPanel {

    // BUG FIX #11 : loadLibrary sécurisé (idempotent, pas de crash si déjà chargé)
    static {
        try {
            System.loadLibrary("angels");
        } catch (UnsatisfiedLinkError ignored) {
            // Déjà chargé par NativeCore dans le même ClassLoader — OK
        }
    }

    // ── Constantes onglets ────────────────────────────────────────────────────
    public static final int TAB_SCAN   = 0;
    public static final int TAB_WRITE  = 1;
    public static final int TAB_FREEZE = 2;
    public static final int TAB_WATCH  = 3;
    public static final int TAB_SPEED  = 4;
    public static final int TAB_STRING = 5;
    public static final int TAB_XOR    = 6;

    public static final String[] TAB_NAMES = {
        "SCAN", "WRITE", "FREEZE", "WATCH", "SPEED", "STRING", "XOR"
    };

    // ═════════════════════════════════════════════════════════════════════════
    //  JNI natifs — mappés vers jni_panel.cpp
    // ═════════════════════════════════════════════════════════════════════════
    public static native String  nativeGetVersion();
    public static native int     nativeGetPid();
    public static native void    nativeSetPid(int pid);
    public static native int     nativeGetActiveTab();
    public static native void    nativeSetActiveTab(int tab);
    public static native int     nativeGetScanValueType();
    public static native void    nativeSetScanValueType(int type);
    public static native int     nativeGetScanMode();
    public static native void    nativeSetScanMode(int mode);
    public static native int     nativeGetXorKey();
    public static native void    nativeSetXorKey(int key);
    public static native boolean nativeGetWritableOnly();
    public static native void    nativeSetWritableOnly(boolean v);
    public static native int     nativeGetFreezeInterval();
    public static native void    nativeSetFreezeInterval(int ms);
    public static native double  nativeGetSpeedFactor();
    public static native void    nativeSetSpeedFactor(double f);
    public static native boolean nativeIsSpeedActive();
    public static native void    nativeSetSpeedActive(boolean v);
    public static native void    nativeLogMessage(String msg);
    public static native String  nativeGetLog();
    public static native void    nativeClearLog();
    // BUG FIX #13 : ajout de la déclaration manquante
    public static native int     nativeGetScanCount();

    // ═════════════════════════════════════════════════════════════════════════
    //  API Java haut niveau
    // ═════════════════════════════════════════════════════════════════════════
    public static String  getVersion()         { return nativeGetVersion(); }
    public static int     getTargetPid()       { return nativeGetPid(); }
    public static void    setTargetPid(int p)  { nativeSetPid(p); }
    public static int     getActiveTab()       { return nativeGetActiveTab(); }
    public static void    setActiveTab(int t)  { nativeSetActiveTab(t); }
    public static String  getActiveTabName()   {
        int t = nativeGetActiveTab();
        return (t >= 0 && t < TAB_NAMES.length) ? TAB_NAMES[t] : "?";
    }
    public static int     getScanValueType()        { return nativeGetScanValueType(); }
    public static void    setScanValueType(int vt)  { nativeSetScanValueType(vt); }
    public static int     getScanMode()             { return nativeGetScanMode(); }
    public static void    setScanMode(int m)        { nativeSetScanMode(m); }
    public static int     getXorKey()               { return nativeGetXorKey(); }
    public static void    setXorKey(int k)          { nativeSetXorKey(k); }
    public static boolean isWritableOnly()          { return nativeGetWritableOnly(); }
    public static void    setWritableOnly(boolean v){ nativeSetWritableOnly(v); }
    public static int     getFreezeInterval()       { return nativeGetFreezeInterval(); }
    public static void    setFreezeInterval(int ms) { nativeSetFreezeInterval(ms); }
    public static double  getSpeedFactor()          { return nativeGetSpeedFactor(); }
    public static void    setSpeedFactor(double f)  { nativeSetSpeedFactor(f); }
    public static boolean isSpeedActive()           { return nativeIsSpeedActive(); }
    public static void    setSpeedActive(boolean v) { nativeSetSpeedActive(v); }
    public static void    log(String msg)           { nativeLogMessage(msg); }
    public static String  getLog()                  { return nativeGetLog(); }
    public static void    clearLog()                { nativeClearLog(); }
    // BUG FIX #13 : wrapper pour getScanCount
    public static int     getScanCount()            { return nativeGetScanCount(); }

    public static String valueTypeName(int t) {
        switch (t) {
            case NativeCore.TYPE_BYTE:   return "BYTE";
            case NativeCore.TYPE_WORD:   return "WORD";
            case NativeCore.TYPE_DWORD:  return "DWORD";
            case NativeCore.TYPE_QWORD:  return "QWORD";
            case NativeCore.TYPE_FLOAT:  return "FLOAT";
            case NativeCore.TYPE_DOUBLE: return "DOUBLE";
            case NativeCore.TYPE_XOR:    return "XOR";
            default:                     return "?";
        }
    }

    public static String scanModeName(int m) {
        switch (m) {
            case NativeCore.MODE_EXACT:        return "EXACT";
            case NativeCore.MODE_UNKNOWN:      return "UNKNOWN";
            case NativeCore.MODE_INCREASED:    return "INCR";
            case NativeCore.MODE_DECREASED:    return "DECR";
            case NativeCore.MODE_CHANGED:      return "CHANGED";
            case NativeCore.MODE_UNCHANGED:    return "UNCH";
            case NativeCore.MODE_GREATER:      return ">";
            case NativeCore.MODE_LESS:         return "<";
            case NativeCore.MODE_BETWEEN:      return "BTWN";
            case NativeCore.MODE_INCREASED_BY: return "+BY";
            case NativeCore.MODE_DECREASED_BY: return "-BY";
            default:                           return "?";
        }
    }

    private AngelsPanel() {}
}
