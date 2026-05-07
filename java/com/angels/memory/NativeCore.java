package com.angels.memory;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * NativeCore — Pont Java vers libangels.so (Angels Memory Engine v2)
 *
 * Usage :
 *   // 1. Scanner une valeur
 *   int count = NativeCore.scanFirst(pid, true, TYPE_DWORD,
 *                   MODE_EXACT, NativeCore.encodeDword(500), 0);
 *
 *   // 2. Affiner
 *   count = NativeCore.scanRefine(pid, TYPE_DWORD,
 *               MODE_EXACT, NativeCore.encodeDword(499), 0);
 *
 *   // 3. Modifier toutes les adresses trouvées
 *   NativeCore.writeAll(pid, NativeCore.encodeDword(9999));
 *
 *   // 4. Geler toutes les adresses
 *   int freezeId = NativeCore.freezeAllResults(pid,
 *                     NativeCore.encodeDword(9999), 50);
 */
public final class NativeCore {

    static { System.loadLibrary("angels"); }

    // ── ValueType ────────────────────────────────────────────────────────────
    public static final int TYPE_BYTE   = 0;
    public static final int TYPE_WORD   = 1;
    public static final int TYPE_DWORD  = 2;
    public static final int TYPE_QWORD  = 3;
    public static final int TYPE_FLOAT  = 4;
    public static final int TYPE_DOUBLE = 5;
    public static final int TYPE_XOR    = 6;

    // ── ScanMode ─────────────────────────────────────────────────────────────
    public static final int MODE_EXACT        = 0;
    public static final int MODE_UNKNOWN      = 1;
    public static final int MODE_INCREASED    = 2;
    public static final int MODE_DECREASED    = 3;
    public static final int MODE_CHANGED      = 4;
    public static final int MODE_UNCHANGED    = 5;
    public static final int MODE_GREATER      = 6;
    public static final int MODE_LESS         = 7;
    public static final int MODE_BETWEEN      = 8;
    public static final int MODE_INCREASED_BY = 9;
    public static final int MODE_DECREASED_BY = 10;

    // ── Maps ─────────────────────────────────────────────────────────────────
    /** [start0, end0, start1, end1, …] — adresses de régions mémoire */
    public static native long[] getMaps(int pid, boolean writableOnly);

    // ── R/W ──────────────────────────────────────────────────────────────────
    /** Lit len octets à addr. Retourne null si erreur. */
    public static native byte[] readMem(int pid, long addr, int len);

    /** Écrit data à addr. Retourne true si succès. */
    public static native boolean writeMem(int pid, long addr, byte[] data);

    // ── Scan exact ───────────────────────────────────────────────────────────
    /**
     * Premier scan. Résultats en cache natif.
     * @param target  Valeur LE. Pour BETWEEN : [min||max] (8 o pour DWORD).
     *                null si MODE_UNKNOWN.
     * @param xorKey  Clé XOR (ignorée si type != TYPE_XOR). 0 = pas de XOR.
     * @return Nombre de résultats.
     */
    public static native int scanFirst(int pid, boolean writableOnly,
                                       int valueType, int scanMode,
                                       byte[] target, int xorKey);

    /**
     * Affinage du scan.
     * @return Nombre de résultats restants.
     */
    public static native int scanRefine(int pid, int valueType,
                                        int scanMode, byte[] target,
                                        int xorKey);

    // ── Scan flou (Fuzzy) ────────────────────────────────────────────────────
    /**
     * Scan flou pour FLOAT/DOUBLE avec tolérance.
     * Ex: targetVal=100.0, tolerance=0.5 → trouve 99.5..100.5
     */
    public static native int scanFuzzyFirst(int pid, boolean writableOnly,
                                            int valueType,
                                            double targetVal,
                                            double tolerance);

    public static native int scanFuzzyRefine(int pid, int valueType,
                                             double targetVal,
                                             double tolerance);

    // ── Découverte de clé XOR ────────────────────────────────────────────────
    /**
     * Cherche des clés XOR candidates pour knownValue.
     * Utiliser la clé la plus fréquente (index 0) comme xorKey pour scanFirst.
     * @return Tableau de clés candidates triées par fréquence décroissante.
     */
    public static native int[] scanXorKeys(int pid, boolean writableOnly,
                                           int knownValue);

    // ── Scan de groupe ───────────────────────────────────────────────────────
    /**
     * Recherche plusieurs valeurs proches (ex: X, Y, Z d'une structure).
     * @param types   Types de chaque item (ex: {TYPE_FLOAT, TYPE_FLOAT, TYPE_FLOAT}).
     * @param values  Valeur encodée de chaque item.
     * @param maxGap  Distance max entre items en octets (ex: 256).
     * @return Nombre de groupes trouvés.
     */
    public static native int scanGroup(int pid, boolean writableOnly,
                                       int[] types, byte[][] values,
                                       int maxGap);

    // ── Accès au cache ───────────────────────────────────────────────────────
    /**
     * Récupère un sous-ensemble des adresses du dernier scan.
     * @param offset Index de départ (pagination).
     * @param count  Nombre max d'adresses à retourner.
     */
    public static native long[] getScanResults(int offset, int count);

    /** Nombre total de résultats dans le cache. */
    public static native int getScanCount();

    /** Vide le cache de résultats. */
    public static native void scanClear();

    // ── Écriture en masse ────────────────────────────────────────────────────
    /**
     * Écrit value sur toutes les adresses du dernier scan.
     * @return Nombre d'écritures réussies.
     */
    public static native int writeAll(int pid, byte[] value);

    /**
     * Écrit value sur toutes les adresses + addrOffset.
     * Utile pour cibler un champ à N octets du début de la structure.
     */
    public static native int writeAllOffset(int pid, long addrOffset,
                                            byte[] value);

    // ── Freeze ───────────────────────────────────────────────────────────────
    /** Gèle une adresse individuelle. Retourne id >= 0 ou -1. */
    public static native int freezeAdd(int pid, long addr,
                                       byte[] value, int intervalMs);

    /** Arrête un freeze individuel. */
    public static native boolean freezeRemove(int id);

    /** Arrête tous les freezes. */
    public static native void freezeClear();

    /**
     * Gèle TOUTES les adresses du dernier scan en un seul thread.
     * Plus efficace que N appels freezeAdd pour des milliers d'adresses.
     * @return Id du freeze groupé (utiliser freezeRemove(id) pour arrêter).
     */
    public static native int freezeAllResults(int pid, byte[] value,
                                              int intervalMs);

    // ── Chaînes ──────────────────────────────────────────────────────────────
    /** Retourne les adresses où needle a été trouvé. */
    public static native long[] scanString(int pid, boolean writableOnly,
                                           String needle,
                                           boolean caseSensitive);

    // ── Watch List ───────────────────────────────────────────────────────────
    public static native int     watchAdd   (int pid, long addr,
                                             int valueType, String label);
    public static native boolean watchRemove(int id);
    public static native byte[]  watchRead  (int id);

    // ── Speed Hack ───────────────────────────────────────────────────────────
    /** factor > 1.0 = rapide, < 1.0 = lent. Nécessite root. */
    public static native boolean speedHackSet  (int pid, double factor);
    public static native boolean speedHackReset(int pid);

    // ═════════════════════════════════════════════════════════════════════════
    //  Helpers encode / decode  (little-endian)
    // ═════════════════════════════════════════════════════════════════════════

    public static byte[] encodeByte(int v) {
        return new byte[]{ (byte)(v & 0xFF) };
    }

    public static byte[] encodeWord(int v) {
        return new byte[]{ (byte)v, (byte)(v >> 8) };
    }

    public static byte[] encodeDword(int v) {
        return ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN)
                         .putInt(v).array();
    }

    public static byte[] encodeQword(long v) {
        return ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)
                         .putLong(v).array();
    }

    public static byte[] encodeFloat(float v) {
        return ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN)
                         .putFloat(v).array();
    }

    public static byte[] encodeDouble(double v) {
        return ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)
                         .putDouble(v).array();
    }

    /**
     * Encode une plage pour MODE_BETWEEN : [min || max] concaténés.
     * Compatible avec DWORD (int) et FLOAT (float).
     */
    public static byte[] encodeBetweenDword(int min, int max) {
        return ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)
                         .putInt(min).putInt(max).array();
    }

    public static byte[] encodeBetweenFloat(float min, float max) {
        return ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)
                         .putFloat(min).putFloat(max).array();
    }

    public static int    decodeDword (byte[] b) {
        if (b == null || b.length < 4) return 0;
        return ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN).getInt();
    }

    public static long   decodeQword (byte[] b) {
        if (b == null || b.length < 8) return 0;
        return ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN).getLong();
    }

    public static float  decodeFloat (byte[] b) {
        if (b == null || b.length < 4) return 0f;
        return ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN).getFloat();
    }

    public static double decodeDouble(byte[] b) {
        if (b == null || b.length < 8) return 0.0;
        return ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN).getDouble();
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Helpers haut niveau
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * Scan de groupe simplifié pour des FLOAT (ex: coordonnées X, Y, Z).
     * @param pid       PID cible
     * @param values    Valeurs float à rechercher ensemble
     * @param maxGap    Distance max entre valeurs en octets
     */
    public static int scanGroupFloats(int pid, float[] values, int maxGap) {
        int[] types = new int[values.length];
        byte[][] encoded = new byte[values.length][];
        for (int i = 0; i < values.length; i++) {
            types[i]   = TYPE_FLOAT;
            encoded[i] = encodeFloat(values[i]);
        }
        return scanGroup(pid, true, types, encoded, maxGap);
    }

    /**
     * Scan XOR complet :
     * 1. Découvre la clé XOR pour knownValue
     * 2. Lance le scan avec la meilleure clé
     * @return Nombre de résultats, ou -1 si aucune clé trouvée.
     */
    public static int scanXorAuto(int pid, int knownValue) {
        int[] keys = scanXorKeys(pid, true, knownValue);
        if (keys == null || keys.length == 0) return -1;
        return scanFirst(pid, true, TYPE_XOR, MODE_EXACT,
                         encodeDword(knownValue), keys[0]);
    }

    private NativeCore() {}
}
