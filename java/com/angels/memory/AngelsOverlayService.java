package com.angels.memory;

import android.app.Service;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.provider.Settings;
import android.text.InputType;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * AngelsOverlayService v4 — Panel de contrôle overlay complet et corrigé.
 *
 * CORRECTIONS v3 (crash fixes) :
 *  CRITIQUE #1 : nativeGetScanCount en dehors de extern "C" → UnsatisfiedLinkError
 *                (corrigé dans jni_panel.cpp + ajout de angels_get_scan_count)
 *  MAJEUR #2  : wm.updateViewLayout sans try-catch → IllegalArgumentException
 *               → Ajout de safeUpdateLayout() partout
 *  MAJEUR #3  : Callback refresh() après onDestroy → ajout flag destroyed
 *  MAJEUR #4  : Double-clic rapide sur boutons d'action → ajout debounce
 *  MODERE #5  : Vérification permission SYSTEM_ALERT_WINDOW avant addView
 *  MODERE #6  : Vérification null sur toutes les vues avant accès
 */
public class AngelsOverlayService extends Service {

    // ── Couleurs ──────────────────────────────────────────────────────────────
    private static final int C_BG         = 0xEE0D1117;
    private static final int C_HEADER     = 0xFF161B22;
    private static final int C_CARD       = 0xFF1C2128;
    private static final int C_TAB_ON     = 0xFF00B4D8;
    private static final int C_TAB_OFF    = 0xFF21262D;
    private static final int C_BTN_POS    = 0xFF1A7A4A;
    private static final int C_BTN_NEG    = 0xFF8B1A1A;
    private static final int C_BTN_NEU    = 0xFF2D333B;
    private static final int C_BTN_CYAN   = 0xFF006080;
    private static final int C_ACCENT     = 0xFF00E5FF;
    private static final int C_ACCENT2    = 0xFFFF4081;
    private static final int C_TEXT       = 0xFFE6EDF3;
    private static final int C_DIM        = 0xFF8B949E;
    private static final int C_MONO       = 0xFFCDD9E5;
    private static final int C_LOG_BG     = 0xFF090D12;
    private static final int C_DIVIDER    = 0xFF30363D;
    private static final int C_BORDER     = 0xFF444C56;

    private static final int PANEL_W_DP   = 320;
    private static final int TAB_MAX_H_DP = 280;
    private static final int REFRESH_MS   = 600;

    // ── Android ───────────────────────────────────────────────────────────────
    private WindowManager              wm;
    private LinearLayout               rootView;
    private WindowManager.LayoutParams lp;
    private View                       headerView;

    // ── État panel ────────────────────────────────────────────────────────────
    private boolean      panelCollapsed = false;
    private int          activeTab      = AngelsPanel.TAB_SCAN;
    private LinearLayout panelBody;
    private TextView     btnCollapse;

    // CORRECTION v3 #3 : flag destroyed pour arrêter les callbacks
    private final AtomicBoolean destroyed = new AtomicBoolean(false);

    // ── Widgets partagés ──────────────────────────────────────────────────────
    private EditText etPid;
    private TextView tvResultCount;
    private TextView tvLog;
    private ScrollView svLog;

    // ── Onglets ───────────────────────────────────────────────────────────────
    private Button[] tabButtons;
    private View[]   tabPanels;

    // ── Scan ──────────────────────────────────────────────────────────────────
    private int      selType = NativeCore.TYPE_DWORD;
    private int      selMode = NativeCore.MODE_EXACT;
    private EditText etVal1, etVal2;
    private TextView tvBetweenLabel;
    private Spinner  spType, spMode;

    // ── Write ─────────────────────────────────────────────────────────────────
    private EditText etWriteVal, etWriteOffset;

    // ── Freeze ────────────────────────────────────────────────────────────────
    private EditText etFreezeVal, etFreezeMs;
    private TextView tvFreezeStatus;
    private List<Integer> freezeIds = new ArrayList<>();

    // ── Watch ─────────────────────────────────────────────────────────────────
    private static class WatchItem {
        int id, type;  long addr;  String label;
        WatchItem(int id, long addr, int type, String label) {
            this.id = id;  this.addr = addr;
            this.type = type;  this.label = label;
        }
    }
    private final List<WatchItem> watchItems   = new ArrayList<>();
    private       LinearLayout    llWatchList;
    private       EditText        etWatchAddr, etWatchLabel;
    private       Spinner         spWatchType;

    // ── Speed ─────────────────────────────────────────────────────────────────
    private SeekBar  sbSpeed;
    private TextView tvSpeedVal;

    // ── String ────────────────────────────────────────────────────────────────
    private EditText etNeedle;
    private CheckBox cbCase;
    private TextView tvStrRes;

    // ── XOR ───────────────────────────────────────────────────────────────────
    private EditText etXorKnown;
    private TextView tvXorKeys;

    // ── Refresh ───────────────────────────────────────────────────────────────
    private final Handler  handler     = new Handler(Looper.getMainLooper());
    private final Runnable refreshTask = this::refresh;

    // CORRECTION v3 #4 : debounce flags pour éviter double-clic
    private final AtomicBoolean scanBusy   = new AtomicBoolean(false);
    private final AtomicBoolean writeBusy  = new AtomicBoolean(false);
    private final AtomicBoolean freezeBusy = new AtomicBoolean(false);

    // ═════════════════════════════════════════════════════════════════════════
    //  Lifecycle
    // ═════════════════════════════════════════════════════════════════════════

    @Override public IBinder onBind(Intent i) { return null; }
    @Override public int onStartCommand(Intent i, int f, int s) { return START_STICKY; }

    @Override
    public void onCreate() {
        super.onCreate();
        destroyed.set(false);
        wm = (WindowManager) getSystemService(WINDOW_SERVICE);

        // CORRECTION v3 #5 : Vérifier la permission SYSTEM_ALERT_WINDOW
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (!Settings.canDrawOverlays(this)) {
                AngelsPanel.log("ERREUR : Permission SYSTEM_ALERT_WINDOW manquante !");
                AngelsPanel.log("Accordez la permission dans Parametres > Applications speciales");
                stopSelf();
                return;
            }
        }

        try {
            buildOverlay();
        } catch (Exception e) {
            AngelsPanel.log("ERREUR buildOverlay : " + e.getMessage());
            stopSelf();
            return;
        }

        handler.postDelayed(refreshTask, REFRESH_MS);
        AngelsPanel.log("Panel v4 demarre — 6 crash fixes appliques");
    }

    @Override
    public void onDestroy() {
        destroyed.set(true);  // CORRECTION v3 #3 : arrêter tous les callbacks
        handler.removeCallbacksAndMessages(null); // Supprimer TOUT les callbacks
        if (rootView != null) {
            try {
                if (rootView.isAttachedToWindow()) wm.removeView(rootView);
            } catch (Exception ignored) {}
        }
        super.onDestroy();
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  CORRECTION v3 #2 : safeUpdateLayout — try-catch obligatoire
    // ═════════════════════════════════════════════════════════════════════════
    private void safeUpdateLayout() {
        if (destroyed.get()) return;
        if (rootView == null || lp == null) return;
        try {
            if (rootView.isAttachedToWindow()) {
                wm.updateViewLayout(rootView, lp);
            }
        } catch (IllegalArgumentException e) {
            // Vue detachee entre le test et l'appel — ignorer silencieusement
        } catch (Exception e) {
            AngelsPanel.log("updateLayout error: " + e.getMessage());
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Construction overlay
    // ═════════════════════════════════════════════════════════════════════════

    private void buildOverlay() {
        int type = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                 ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                 : WindowManager.LayoutParams.TYPE_PHONE;

        lp = new WindowManager.LayoutParams(
            dp(PANEL_W_DP),
            WindowManager.LayoutParams.WRAP_CONTENT,
            type,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
            | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            PixelFormat.TRANSLUCENT
        );
        lp.gravity = Gravity.TOP | Gravity.START;
        lp.x = 20;  lp.y = 80;

        rootView = new LinearLayout(this);
        rootView.setOrientation(LinearLayout.VERTICAL);
        rootView.setBackgroundColor(C_BG);

        buildHeader();
        buildPidBar();
        buildTabBar();
        buildTabPanels();
        buildLogSection();
        setupDrag();

        wm.addView(rootView, lp);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Header
    // ─────────────────────────────────────────────────────────────────────────
    private void buildHeader() {
        LinearLayout h = new LinearLayout(this);
        h.setOrientation(LinearLayout.HORIZONTAL);
        h.setBackgroundColor(C_HEADER);
        h.setPadding(dp(10), dp(7), dp(10), dp(7));
        h.setGravity(Gravity.CENTER_VERTICAL);
        h.setLayoutParams(matchW());

        TextView ico = tv("\u26A1", 15, C_ACCENT, true);
        ico.setPadding(0, 0, dp(6), 0);
        h.addView(ico);

        TextView title = tv(AngelsPanel.getVersion(), 12, C_TEXT, true);
        title.setLayoutParams(new LinearLayout.LayoutParams(0,
            LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        h.addView(title);

        btnCollapse = tv("\u25BC", 14, C_ACCENT, true);
        btnCollapse.setPadding(dp(10), dp(2), 0, dp(2));
        btnCollapse.setOnClickListener(v -> toggleCollapse());
        h.addView(btnCollapse);

        rootView.addView(h);
        headerView = h;
    }

    private void toggleCollapse() {
        if (destroyed.get()) return;
        panelCollapsed = !panelCollapsed;
        if (panelBody != null) {
            panelBody.setVisibility(panelCollapsed ? View.GONE : View.VISIBLE);
        }
        btnCollapse.setText(panelCollapsed ? "\u25B6" : "\u25BC");
        if (panelCollapsed) {
            lp.flags |= WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
            safeUpdateLayout();
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Barre PID
    // ─────────────────────────────────────────────────────────────────────────
    private void buildPidBar() {
        panelBody = new LinearLayout(this);
        panelBody.setOrientation(LinearLayout.VERTICAL);
        panelBody.setLayoutParams(matchW());
        panelBody.setPadding(dp(8), dp(4), dp(8), 0);

        LinearLayout row = hRow();
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(4), 0, dp(4));

        row.addView(tv("PID:", 11, C_DIM, false));
        etPid = et("", InputType.TYPE_CLASS_NUMBER, dp(70));
        etPid.setHint("ex: 1234");
        etPid.setHintTextColor(C_DIM);
        row.addView(etPid);

        Button bSet = btn("\u2714 SET", C_BTN_NEU);
        bSet.setOnClickListener(v -> applyPid());
        row.addView(bSet);

        tvResultCount = tv("0 resultats", 11, C_DIM, false);
        tvResultCount.setPadding(dp(8), 0, 0, 0);
        row.addView(tvResultCount);

        panelBody.addView(row);
        panelBody.addView(divider());
        rootView.addView(panelBody);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Tab bar
    // ─────────────────────────────────────────────────────────────────────────
    private void buildTabBar() {
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setBackgroundColor(0xFF090D12);
        bar.setLayoutParams(matchW());
        bar.setPadding(0, dp(2), 0, dp(2));

        tabButtons = new Button[AngelsPanel.TAB_NAMES.length];
        for (int i = 0; i < AngelsPanel.TAB_NAMES.length; i++) {
            final int idx = i;
            Button b = new Button(this);
            b.setText(AngelsPanel.TAB_NAMES[i]);
            b.setTextSize(9f);
            b.setAllCaps(false);
            b.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
            b.setPadding(dp(2), dp(2), dp(2), dp(2));
            LinearLayout.LayoutParams tlp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            b.setLayoutParams(tlp);
            b.setOnClickListener(v -> switchTab(idx));
            bar.addView(b);
            tabButtons[i] = b;
        }
        panelBody.addView(bar);
        updateTabStyles();
    }

    private void updateTabStyles() {
        if (tabButtons == null) return;
        for (int i = 0; i < tabButtons.length; i++) {
            if (tabButtons[i] == null) continue;
            if (i == activeTab) {
                tabButtons[i].setBackgroundColor(C_TAB_ON);
                tabButtons[i].setTextColor(0xFF000000);
            } else {
                tabButtons[i].setBackgroundColor(C_TAB_OFF);
                tabButtons[i].setTextColor(C_TEXT);
            }
        }
    }

    private void switchTab(int idx) {
        if (destroyed.get()) return;
        activeTab = idx;
        AngelsPanel.setActiveTab(idx);
        if (tabPanels != null) {
            for (View p : tabPanels) {
                if (p != null) p.setVisibility(View.GONE);
            }
            if (idx < tabPanels.length && tabPanels[idx] != null) {
                tabPanels[idx].setVisibility(View.VISIBLE);
            }
        }
        updateTabStyles();
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Tab panels
    // ─────────────────────────────────────────────────────────────────────────
    private void buildTabPanels() {
        tabPanels = new View[AngelsPanel.TAB_NAMES.length];
        tabPanels[AngelsPanel.TAB_SCAN]   = scroll(buildScanContent());
        tabPanels[AngelsPanel.TAB_WRITE]  = scroll(buildWriteContent());
        tabPanels[AngelsPanel.TAB_FREEZE] = scroll(buildFreezeContent());
        tabPanels[AngelsPanel.TAB_WATCH]  = scroll(buildWatchContent());
        tabPanels[AngelsPanel.TAB_SPEED]  = scroll(buildSpeedContent());
        tabPanels[AngelsPanel.TAB_STRING] = scroll(buildStringContent());
        tabPanels[AngelsPanel.TAB_XOR]    = scroll(buildXorContent());

        for (int i = 0; i < tabPanels.length; i++) {
            if (tabPanels[i] != null) {
                tabPanels[i].setVisibility(i == 0 ? View.VISIBLE : View.GONE);
                panelBody.addView(tabPanels[i]);
            }
        }
    }

    private ScrollView scroll(LinearLayout content) {
        ScrollView sv = new ScrollView(this);
        LinearLayout.LayoutParams slp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, dp(TAB_MAX_H_DP));
        sv.setLayoutParams(slp);
        sv.addView(content);
        return sv;
    }

    // ── Contenu onglet SCAN ──────────────────────────────────────────────────
    private LinearLayout buildScanContent() {
        LinearLayout p = vCol();

        p.addView(sectionLabel("> Type de valeur"));
        String[] types = {"BYTE","WORD","DWORD","QWORD","FLOAT","DOUBLE","XOR"};
        spType = styledSpinner(types);
        spType.setSelection(2);
        spType.setOnItemSelectedListener(sel(pos -> {
            selType = pos;
            AngelsPanel.setScanValueType(pos);
        }));
        p.addView(spType);

        p.addView(sectionLabel("> Mode de scan"));
        String[] modes = {
            "EXACT","UNKNOWN","INCREASED","DECREASED","CHANGED",
            "UNCHANGED","> GREATER","< LESS","BETWEEN","INCREASED_BY","DECREASED_BY"
        };
        spMode = styledSpinner(modes);
        spMode.setOnItemSelectedListener(sel(pos -> {
            selMode = pos;
            AngelsPanel.setScanMode(pos);
            boolean between = (pos == NativeCore.MODE_BETWEEN);
            if (tvBetweenLabel != null) tvBetweenLabel.setVisibility(between ? View.VISIBLE : View.GONE);
            if (etVal2 != null) etVal2.setVisibility(between ? View.VISIBLE : View.GONE);
        }));
        p.addView(spMode);

        p.addView(sectionLabel("> Valeur cible (min si BETWEEN)"));
        etVal1 = et("", InputType.TYPE_CLASS_TEXT, -1);
        etVal1.setHint("valeur...");
        etVal1.setHintTextColor(C_DIM);
        p.addView(etVal1);

        tvBetweenLabel = sectionLabel("> Valeur max (BETWEEN)");
        tvBetweenLabel.setVisibility(View.GONE);
        p.addView(tvBetweenLabel);
        etVal2 = et("", InputType.TYPE_CLASS_TEXT, -1);
        etVal2.setHint("valeur max...");
        etVal2.setHintTextColor(C_DIM);
        etVal2.setVisibility(View.GONE);
        p.addView(etVal2);

        LinearLayout bRow = hRow();
        Button bFirst = btn("\u26A1 FIRST", C_BTN_POS);
        bFirst.setOnClickListener(v -> doFirstScan());
        Button bRefine = btn("\u21BB REFINE", C_BTN_NEU);
        bRefine.setOnClickListener(v -> doRefine());
        Button bClear = btn("\u2716 CLEAR", C_BTN_NEG);
        bClear.setOnClickListener(v -> doClear());
        bRow.addView(bFirst);  bRow.addView(bRefine);  bRow.addView(bClear);
        p.addView(bRow);
        p.addView(divider());

        p.addView(sectionLabel("> Resultats (50 max)"));
        TextView tvRes = tv("", 9, C_MONO, false);
        tvRes.setTypeface(Typeface.MONOSPACE);
        tvRes.setPadding(dp(4), dp(2), dp(4), dp(2));
        tvRes.setTag("scan_results");
        p.addView(tvRes);

        return p;
    }

    // ── Contenu onglet WRITE ─────────────────────────────────────────────────
    private LinearLayout buildWriteContent() {
        LinearLayout p = vCol();
        p.addView(sectionLabel("> Valeur a ecrire"));
        etWriteVal = et("", InputType.TYPE_CLASS_TEXT, -1);
        etWriteVal.setHint("ex: 9999");
        etWriteVal.setHintTextColor(C_DIM);
        p.addView(etWriteVal);

        Button bAll = btn("\u270D WRITE ALL", C_BTN_POS);
        bAll.setOnClickListener(v -> doWriteAll());
        p.addView(bAll);

        p.addView(divider());
        p.addView(sectionLabel("> Offset d'adresse (optionnel, decimal)"));
        etWriteOffset = et("0", InputType.TYPE_CLASS_NUMBER
            | InputType.TYPE_NUMBER_FLAG_SIGNED, -1);
        p.addView(etWriteOffset);
        Button bOffset = btn("\u270D WRITE ALL + OFFSET", C_BTN_NEU);
        bOffset.setOnClickListener(v -> doWriteAllOffset());
        p.addView(bOffset);
        return p;
    }

    // ── Contenu onglet FREEZE ────────────────────────────────────────────────
    private LinearLayout buildFreezeContent() {
        LinearLayout p = vCol();
        p.addView(sectionLabel("> Valeur a geler"));
        etFreezeVal = et("", InputType.TYPE_CLASS_TEXT, -1);
        etFreezeVal.setHint("ex: 9999");
        etFreezeVal.setHintTextColor(C_DIM);
        p.addView(etFreezeVal);

        p.addView(sectionLabel("> Intervalle (ms, min 10)"));
        etFreezeMs = et("50", InputType.TYPE_CLASS_NUMBER, -1);
        p.addView(etFreezeMs);

        LinearLayout bRow = hRow();
        Button bFreeze = btn("\u2744 FREEZE ALL", C_BTN_CYAN);
        bFreeze.setTextColor(C_ACCENT);
        bFreeze.setOnClickListener(v -> doFreezeAll());
        Button bUnfreeze = btn("\uD83D\uDD25 UNFREEZE ALL", C_BTN_NEG);
        bUnfreeze.setOnClickListener(v -> doUnfreezeAll());
        bRow.addView(bFreeze);  bRow.addView(bUnfreeze);
        p.addView(bRow);

        tvFreezeStatus = tv("Aucun freeze actif", 10, C_DIM, false);
        tvFreezeStatus.setPadding(0, dp(4), 0, 0);
        p.addView(tvFreezeStatus);
        return p;
    }

    // ── Contenu onglet WATCH ─────────────────────────────────────────────────
    private LinearLayout buildWatchContent() {
        LinearLayout p = vCol();
        p.addView(sectionLabel("> Adresse (hex, ex: 0x7f1234)"));
        etWatchAddr = et("", InputType.TYPE_CLASS_TEXT, -1);
        etWatchAddr.setHint("0x...");
        etWatchAddr.setHintTextColor(C_DIM);
        p.addView(etWatchAddr);

        p.addView(sectionLabel("> Label"));
        etWatchLabel = et("", InputType.TYPE_CLASS_TEXT, -1);
        etWatchLabel.setHint("nom");
        etWatchLabel.setHintTextColor(C_DIM);
        p.addView(etWatchLabel);

        p.addView(sectionLabel("> Type"));
        String[] types = {"BYTE","WORD","DWORD","QWORD","FLOAT","DOUBLE","XOR"};
        spWatchType = styledSpinner(types);
        spWatchType.setSelection(2);
        p.addView(spWatchType);

        LinearLayout bRow = hRow();
        Button bAdd = btn("+ ADD WATCH", C_BTN_POS);
        bAdd.setOnClickListener(v -> doWatchAdd());
        Button bClear = btn("\u2716 CLEAR ALL", C_BTN_NEG);
        bClear.setOnClickListener(v -> {
            for (WatchItem w : watchItems) NativeCore.watchRemove(w.id);
            watchItems.clear();
            refreshWatchList();
        });
        bRow.addView(bAdd);  bRow.addView(bClear);
        p.addView(bRow);

        p.addView(divider());
        p.addView(sectionLabel("> Watchpoints actifs"));
        llWatchList = new LinearLayout(this);
        llWatchList.setOrientation(LinearLayout.VERTICAL);
        llWatchList.setLayoutParams(matchW());
        p.addView(llWatchList);
        return p;
    }

    // ── Contenu onglet SPEED ─────────────────────────────────────────────────
    private LinearLayout buildSpeedContent() {
        LinearLayout p = vCol();
        p.addView(sectionLabel("> Facteur de vitesse (0.01x -> 10x)"));

        tvSpeedVal = tv("1.00x", 20, C_ACCENT, true);
        tvSpeedVal.setGravity(Gravity.CENTER);
        tvSpeedVal.setLayoutParams(matchW());
        p.addView(tvSpeedVal);

        sbSpeed = new SeekBar(this);
        sbSpeed.setMax(2000);
        sbSpeed.setProgress(200);
        sbSpeed.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar s, int prog, boolean user) {
                double f = Math.max(0.01, prog / 200.0);
                AngelsPanel.setSpeedFactor(f);
                tvSpeedVal.setText(String.format("%.2fx", f));
            }
            public void onStartTrackingTouch(SeekBar s) {}
            public void onStopTrackingTouch(SeekBar s) {}
        });
        sbSpeed.setLayoutParams(matchW());
        p.addView(sbSpeed);

        LinearLayout presets = hRow();
        int[] progs  = {10, 100, 200, 400, 1000, 2000};
        String[] lbs = {"0.1x","0.5x","1x","2x","5x","10x"};
        for (int i = 0; i < progs.length; i++) {
            final int pg = progs[i];
            Button b = btn(lbs[i], C_BTN_NEU);
            b.setOnClickListener(v -> sbSpeed.setProgress(pg));
            presets.addView(b);
        }
        p.addView(presets);

        LinearLayout bRow = hRow();
        Button bApply = btn("\u25B6 APPLIQUER", C_BTN_POS);
        bApply.setOnClickListener(v -> {
            int pid = AngelsPanel.getTargetPid();
            double f = AngelsPanel.getSpeedFactor();
            boolean ok = NativeCore.speedHackSet(pid, f);
            AngelsPanel.setSpeedActive(ok);
            tvSpeedVal.setTextColor(ok ? C_ACCENT : C_ACCENT2);
            AngelsPanel.log("SpeedHack " + (ok ? "actif" : "echoue") + " : " + String.format("%.2f", f) + "x");
        });
        Button bReset = btn("\u23F9 RESET", C_BTN_NEG);
        bReset.setOnClickListener(v -> {
            NativeCore.speedHackReset(AngelsPanel.getTargetPid());
            AngelsPanel.setSpeedActive(false);
            sbSpeed.setProgress(200);
            tvSpeedVal.setTextColor(C_ACCENT);
            AngelsPanel.log("SpeedHack desactive");
        });
        bRow.addView(bApply);  bRow.addView(bReset);
        p.addView(bRow);
        return p;
    }

    // ── Contenu onglet STRING ────────────────────────────────────────────────
    private LinearLayout buildStringContent() {
        LinearLayout p = vCol();
        p.addView(sectionLabel("> Chaine a rechercher"));
        etNeedle = et("", InputType.TYPE_CLASS_TEXT, -1);
        etNeedle.setHint("ex: Player");
        etNeedle.setHintTextColor(C_DIM);
        p.addView(etNeedle);

        cbCase = new CheckBox(this);
        cbCase.setText("Sensible a la casse");
        cbCase.setTextColor(C_TEXT);
        cbCase.setChecked(true);
        p.addView(cbCase);

        Button bScan = btn("\uD83D\uDD0D SCAN STRING", C_BTN_POS);
        bScan.setOnClickListener(v -> doStringScan());
        p.addView(bScan);
        p.addView(divider());
        tvStrRes = tv("", 9, C_MONO, false);
        tvStrRes.setTypeface(Typeface.MONOSPACE);
        p.addView(tvStrRes);
        return p;
    }

    // ── Contenu onglet XOR ───────────────────────────────────────────────────
    private LinearLayout buildXorContent() {
        LinearLayout p = vCol();
        p.addView(tv("Decouverte automatique de cle XOR", 12, C_ACCENT, true));
        p.addView(tv("Entrez la valeur en clair connue.\nLa cle la plus frequente sera selectionnee.", 10, C_DIM, false));
        p.addView(divider());

        p.addView(sectionLabel("> Valeur en clair connue"));
        etXorKnown = et("", InputType.TYPE_CLASS_NUMBER
            | InputType.TYPE_NUMBER_FLAG_SIGNED, -1);
        etXorKnown.setHint("ex: 500");
        etXorKnown.setHintTextColor(C_DIM);
        p.addView(etXorKnown);

        LinearLayout bRow = hRow();
        Button bFind = btn("\uD83D\uDD11 FIND KEYS", C_BTN_POS);
        bFind.setOnClickListener(v -> doXorDiscover());
        Button bAuto = btn("\u26A1 AUTO SCAN", C_BTN_CYAN);
        bAuto.setTextColor(C_ACCENT);
        bAuto.setOnClickListener(v -> doXorAutoScan());
        bRow.addView(bFind);  bRow.addView(bAuto);
        p.addView(bRow);

        tvXorKeys = tv("Cles candidates : -", 10, C_MONO, false);
        tvXorKeys.setTypeface(Typeface.MONOSPACE);
        p.addView(tvXorKeys);
        return p;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Section Log
    // ─────────────────────────────────────────────────────────────────────────
    private void buildLogSection() {
        LinearLayout logH = new LinearLayout(this);
        logH.setOrientation(LinearLayout.HORIZONTAL);
        logH.setBackgroundColor(0xFF090D12);
        logH.setLayoutParams(matchW());
        logH.setPadding(dp(8), dp(3), dp(8), dp(3));
        logH.setGravity(Gravity.CENTER_VERTICAL);
        logH.addView(tv("\u25CE LOG", 10, C_ACCENT2, true));

        LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        View space = new View(this);
        space.setLayoutParams(sp);
        logH.addView(space);

        Button bClr = new Button(this);
        bClr.setText("\u2716");
        bClr.setTextColor(C_DIM);
        bClr.setTextSize(9f);
        bClr.setBackgroundColor(Color.TRANSPARENT);
        bClr.setPadding(dp(8), 0, 0, 0);
        bClr.setOnClickListener(v -> {
            AngelsPanel.clearLog();
            if (tvLog != null) tvLog.setText("");
        });
        logH.addView(bClr);
        panelBody.addView(logH);

        svLog = new ScrollView(this);
        svLog.setBackgroundColor(C_LOG_BG);
        svLog.setLayoutParams(new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, dp(90)));
        tvLog = tv("", 9, C_DIM, false);
        tvLog.setTypeface(Typeface.MONOSPACE);
        tvLog.setPadding(dp(6), dp(4), dp(6), dp(4));
        svLog.addView(tvLog);
        panelBody.addView(svLog);
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Drag
    // ═════════════════════════════════════════════════════════════════════════
    private void setupDrag() {
        final int[] ix = {0}, iy = {0}, itx = {0}, ity = {0};
        headerView.setOnTouchListener((v, e) -> {
            switch (e.getAction()) {
                case MotionEvent.ACTION_DOWN:
                    ix[0] = lp.x;  iy[0] = lp.y;
                    itx[0] = (int) e.getRawX();  ity[0] = (int) e.getRawY();
                    return true;
                case MotionEvent.ACTION_MOVE:
                    lp.x = ix[0] + (int)(e.getRawX() - itx[0]);
                    lp.y = iy[0] + (int)(e.getRawY() - ity[0]);
                    safeUpdateLayout();
                    return true;
                case MotionEvent.ACTION_UP:
                    int dx = Math.abs(lp.x - ix[0]), dy = Math.abs(lp.y - iy[0]);
                    if (dx < dp(5) && dy < dp(5)) toggleCollapse();
                    return true;
            }
            return false;
        });
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Actions — SCAN  (avec debounce)
    // ═════════════════════════════════════════════════════════════════════════

    private void doFirstScan() {
        // CORRECTION v3 #4 : debounce
        if (!scanBusy.compareAndSet(false, true)) {
            toast("Scan en cours... patientez");
            return;
        }
        try {
            int pid = getPid();
            if (pid <= 0) { toast("PID invalide"); return; }
            byte[] tgt = buildTarget();
            int n = NativeCore.scanFirst(pid, AngelsPanel.isWritableOnly(),
                                         selType, selMode, tgt,
                                         AngelsPanel.getXorKey());
            AngelsPanel.log("First [" + AngelsPanel.valueTypeName(selType)
                + "/" + AngelsPanel.scanModeName(selMode) + "] -> " + n);
            updateResultCount(n);
            updateScanDisplay();
        } finally {
            scanBusy.set(false);
        }
    }

    private void doRefine() {
        if (!scanBusy.compareAndSet(false, true)) {
            toast("Scan en cours... patientez");
            return;
        }
        try {
            int pid = getPid();
            if (pid <= 0) { toast("PID invalide"); return; }
            byte[] tgt = buildTarget();
            int n = NativeCore.scanRefine(pid, selType, selMode, tgt,
                                          AngelsPanel.getXorKey());
            AngelsPanel.log("Refine -> " + n);
            updateResultCount(n);
            updateScanDisplay();
        } finally {
            scanBusy.set(false);
        }
    }

    private void doClear() {
        NativeCore.scanClear();
        updateResultCount(0);
        updateScanDisplay();
        AngelsPanel.log("Scan efface");
    }

    private byte[] buildTarget() {
        String s1 = etVal1.getText().toString().trim();
        if (s1.isEmpty()) return null;

        try {
            boolean between = (selMode == NativeCore.MODE_BETWEEN);
            String s2 = between ? etVal2.getText().toString().trim() : "";
            if (between && s2.isEmpty()) { toast("Entrez la valeur max"); return null; }

            switch (selType) {
                case NativeCore.TYPE_BYTE:
                    return NativeCore.encodeByte(Integer.parseInt(s1));

                case NativeCore.TYPE_WORD:
                    return NativeCore.encodeWord(Integer.parseInt(s1));

                case NativeCore.TYPE_DWORD:
                case NativeCore.TYPE_XOR:
                    if (between)
                        return NativeCore.encodeBetweenDword(
                            Integer.parseInt(s1), Integer.parseInt(s2));
                    return NativeCore.encodeDword(Integer.parseInt(s1));

                case NativeCore.TYPE_QWORD: {
                    if (between) {
                        ByteBuffer bb = ByteBuffer.allocate(16)
                            .order(ByteOrder.LITTLE_ENDIAN);
                        bb.putLong(Long.parseLong(s1));
                        bb.putLong(Long.parseLong(s2));
                        return bb.array();
                    }
                    return NativeCore.encodeQword(Long.parseLong(s1));
                }

                case NativeCore.TYPE_FLOAT:
                    if (between)
                        return NativeCore.encodeBetweenFloat(
                            Float.parseFloat(s1), Float.parseFloat(s2));
                    return NativeCore.encodeFloat(Float.parseFloat(s1));

                case NativeCore.TYPE_DOUBLE: {
                    if (between) {
                        ByteBuffer bb = ByteBuffer.allocate(16)
                            .order(ByteOrder.LITTLE_ENDIAN);
                        bb.putDouble(Double.parseDouble(s1));
                        bb.putDouble(Double.parseDouble(s2));
                        return bb.array();
                    }
                    return NativeCore.encodeDouble(Double.parseDouble(s1));
                }

                default:
                    return NativeCore.encodeDword(Integer.parseInt(s1));
            }
        } catch (NumberFormatException e) {
            toast("Valeur invalide"); return null;
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Actions — WRITE  (avec debounce)
    // ═════════════════════════════════════════════════════════════════════════

    private void doWriteAll() {
        if (!writeBusy.compareAndSet(false, true)) {
            toast("Ecriture en cours...");
            return;
        }
        try {
            int pid = getPid();
            if (pid <= 0) { toast("PID invalide"); return; }
            byte[] v = encodeWriteValue(etWriteVal.getText().toString().trim());
            if (v == null) return;
            int n = NativeCore.writeAll(pid, v);
            AngelsPanel.log("writeAll -> " + n + " ecrits");
        } finally {
            writeBusy.set(false);
        }
    }

    private void doWriteAllOffset() {
        if (!writeBusy.compareAndSet(false, true)) {
            toast("Ecriture en cours...");
            return;
        }
        try {
            int pid = getPid();
            if (pid <= 0) { toast("PID invalide"); return; }
            byte[] v = encodeWriteValue(etWriteVal.getText().toString().trim());
            if (v == null) return;
            long off = 0;
            try { off = Long.parseLong(etWriteOffset.getText().toString().trim()); }
            catch (Exception ignored) {}
            int n = NativeCore.writeAllOffset(pid, off, v);
            AngelsPanel.log("writeAllOffset(" + off + ") -> " + n);
        } finally {
            writeBusy.set(false);
        }
    }

    private byte[] encodeWriteValue(String s) {
        if (s.isEmpty()) { toast("Entrez une valeur"); return null; }
        try {
            switch (selType) {
                case NativeCore.TYPE_BYTE:   return NativeCore.encodeByte(Integer.parseInt(s));
                case NativeCore.TYPE_WORD:   return NativeCore.encodeWord(Integer.parseInt(s));
                case NativeCore.TYPE_DWORD:
                case NativeCore.TYPE_XOR:    return NativeCore.encodeDword(Integer.parseInt(s));
                case NativeCore.TYPE_QWORD:  return NativeCore.encodeQword(Long.parseLong(s));
                case NativeCore.TYPE_FLOAT:  return NativeCore.encodeFloat(Float.parseFloat(s));
                case NativeCore.TYPE_DOUBLE: return NativeCore.encodeDouble(Double.parseDouble(s));
                default:                     return NativeCore.encodeDword(Integer.parseInt(s));
            }
        } catch (NumberFormatException e) { toast("Valeur invalide"); return null; }
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Actions — FREEZE  (avec debounce)
    // ═════════════════════════════════════════════════════════════════════════

    private void doFreezeAll() {
        if (!freezeBusy.compareAndSet(false, true)) {
            toast("Freeze en cours...");
            return;
        }
        try {
            int pid = getPid();
            if (pid <= 0) { toast("PID invalide"); return; }
            String s = etFreezeVal.getText().toString().trim();
            if (s.isEmpty()) { toast("Entrez une valeur"); return; }
            byte[] v = encodeWriteValue(s);
            if (v == null) return;
            int ms = 50;
            try { ms = Math.max(10, Integer.parseInt(etFreezeMs.getText().toString().trim())); }
            catch (Exception ignored) {}
            AngelsPanel.setFreezeInterval(ms);
            int id = NativeCore.freezeAllResults(pid, v, ms);
            if (id >= 0) {
                freezeIds.add(id);
                tvFreezeStatus.setText("\u2744 Freeze actif — id=" + id
                    + " | " + NativeCore.getScanCount() + " addr | " + ms + "ms");
                tvFreezeStatus.setTextColor(C_ACCENT);
            } else {
                tvFreezeStatus.setText("Freeze echoue (aucun resultat ?)");
                tvFreezeStatus.setTextColor(C_ACCENT2);
            }
            AngelsPanel.log("freezeAllResults id=" + id);
        } finally {
            freezeBusy.set(false);
        }
    }

    private void doUnfreezeAll() {
        NativeCore.freezeClear();
        freezeIds.clear();
        tvFreezeStatus.setText("Aucun freeze actif");
        tvFreezeStatus.setTextColor(C_DIM);
        AngelsPanel.log("Tous freezes supprimes");
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Actions — WATCH
    // ═════════════════════════════════════════════════════════════════════════

    private void doWatchAdd() {
        int pid = getPid();
        if (pid <= 0) { toast("PID invalide"); return; }

        String addrStr = etWatchAddr.getText().toString().trim().toLowerCase();
        long addr;
        try {
            if (addrStr.startsWith("0x"))
                addr = Long.parseUnsignedLong(addrStr.substring(2), 16);
            else
                addr = Long.parseUnsignedLong(addrStr, 16);
        } catch (Exception e) { toast("Adresse hex invalide"); return; }

        String label = etWatchLabel.getText().toString().trim();
        if (label.isEmpty()) label = "w" + watchItems.size();
        int type = spWatchType.getSelectedItemPosition();
        int id = NativeCore.watchAdd(pid, addr, type, label);
        watchItems.add(new WatchItem(id, addr, type, label));
        AngelsPanel.log("watchAdd 0x" + Long.toHexString(addr) + " id=" + id);
        refreshWatchList();
    }

    private void refreshWatchList() {
        if (llWatchList == null) return;
        llWatchList.removeAllViews();
        if (watchItems.isEmpty()) {
            llWatchList.addView(tv("(aucun watchpoint)", 10, C_DIM, false));
            return;
        }
        for (final WatchItem w : new ArrayList<>(watchItems)) {
            byte[] raw = NativeCore.watchRead(w.id);
            String valStr = decodeValue(raw, w.type);

            LinearLayout row = hRow();
            row.setPadding(0, dp(2), 0, dp(2));
            row.setGravity(Gravity.CENTER_VERTICAL);

            String line = String.format("%-8s 0x%s -> %s",
                w.label, Long.toHexString(w.addr), valStr);
            TextView tvLine = tv(line, 9, C_MONO, false);
            tvLine.setTypeface(Typeface.MONOSPACE);
            tvLine.setLayoutParams(new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
            row.addView(tvLine);

            Button bDel = btn("\u2716", C_BTN_NEG);
            bDel.setOnClickListener(v -> {
                NativeCore.watchRemove(w.id);
                watchItems.remove(w);
                refreshWatchList();
            });
            row.addView(bDel);
            llWatchList.addView(row);
        }
    }

    private String decodeValue(byte[] raw, int type) {
        if (raw == null || raw.length == 0) return "---";
        try {
            switch (type) {
                case NativeCore.TYPE_BYTE:   return String.valueOf(raw[0] & 0xFF);
                case NativeCore.TYPE_WORD:
                    return String.valueOf(ByteBuffer.wrap(raw)
                        .order(ByteOrder.LITTLE_ENDIAN).getShort());
                case NativeCore.TYPE_DWORD:
                case NativeCore.TYPE_XOR:
                    return String.valueOf(NativeCore.decodeDword(raw));
                case NativeCore.TYPE_QWORD:
                    return String.valueOf(NativeCore.decodeQword(raw));
                case NativeCore.TYPE_FLOAT:
                    return String.format("%.4f", NativeCore.decodeFloat(raw));
                case NativeCore.TYPE_DOUBLE:
                    return String.format("%.6f", NativeCore.decodeDouble(raw));
                default: return "?";
            }
        } catch (Exception e) { return "err"; }
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Actions — STRING
    // ═════════════════════════════════════════════════════════════════════════

    private void doStringScan() {
        int pid = getPid();
        if (pid <= 0) { toast("PID invalide"); return; }
        String needle = etNeedle.getText().toString();
        if (needle.isEmpty()) { toast("Entrez une chaine"); return; }
        long[] addrs = NativeCore.scanString(pid, true, needle, cbCase.isChecked());
        int n = addrs != null ? addrs.length : 0;
        AngelsPanel.log("scanString \"" + needle + "\" -> " + n);
        StringBuilder sb = new StringBuilder(n + " resultats\n");
        int max = Math.min(n, 20);
        for (int i = 0; i < max; i++)
            sb.append("0x").append(Long.toHexString(addrs[i])).append("\n");
        if (n > 20) sb.append("... + ").append(n - 20).append(" autres");
        tvStrRes.setText(sb.toString());
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Actions — XOR
    // ═════════════════════════════════════════════════════════════════════════

    private void doXorDiscover() {
        int pid = getPid();
        if (pid <= 0) { toast("PID invalide"); return; }
        String s = etXorKnown.getText().toString().trim();
        int known;
        try { known = Integer.parseInt(s); } catch (Exception e) { toast("Valeur invalide"); return; }
        int[] keys = NativeCore.scanXorKeys(pid, true, known);
        StringBuilder sb = new StringBuilder("Cles candidates :\n");
        if (keys != null && keys.length > 0) {
            for (int k : keys)
                sb.append("  0x").append(Integer.toHexString(k)).append("\n");
            AngelsPanel.setXorKey(keys[0]);
            AngelsPanel.log("XOR cle : 0x" + Integer.toHexString(keys[0]));
        } else sb.append("  Aucune cle trouvee");
        tvXorKeys.setText(sb.toString());
    }

    private void doXorAutoScan() {
        int pid = getPid();
        if (pid <= 0) { toast("PID invalide"); return; }
        String s = etXorKnown.getText().toString().trim();
        int known;
        try { known = Integer.parseInt(s); } catch (Exception e) { toast("Valeur invalide"); return; }
        int n = NativeCore.scanXorAuto(pid, known);
        AngelsPanel.log("XOR auto -> " + n);
        updateResultCount(n);
        updateScanDisplay();
        switchTab(AngelsPanel.TAB_SCAN);
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Refresh auto  —  CORRIGE : flag destroyed
    // ═════════════════════════════════════════════════════════════════════════

    private void refresh() {
        // CORRECTION v3 #3 : ne rien faire si le service est detruit
        if (destroyed.get()) return;

        try {
            updateResultCount(NativeCore.getScanCount());
            updateScanDisplay();
            refreshWatchList();
            String log = AngelsPanel.getLog();
            if (log != null && !log.isEmpty() && tvLog != null) {
                tvLog.setText(log);
                if (svLog != null) {
                    svLog.post(() -> {
                        if (svLog != null) svLog.fullScroll(View.FOCUS_DOWN);
                    });
                }
            }
        } catch (Exception e) {
            // Ignorer les erreurs pendant le refresh (evite crash si le
            // processus cible est mort pendant la lecture)
        }

        // Replanifier seulement si pas detruit
        if (!destroyed.get()) {
            handler.postDelayed(refreshTask, REFRESH_MS);
        }
    }

    private void updateResultCount(int n) {
        if (tvResultCount == null) return;
        tvResultCount.setText(n + " resultats");
        tvResultCount.setTextColor(n > 0 ? C_ACCENT : C_DIM);
    }

    private void updateScanDisplay() {
        if (tabPanels == null) return;
        View root = tabPanels[AngelsPanel.TAB_SCAN];
        if (!(root instanceof ViewGroup)) return;
        TextView tvRes = ((ViewGroup) root).findViewWithTag("scan_results");
        if (tvRes == null) return;

        int total = NativeCore.getScanCount();
        if (total == 0) { tvRes.setText("Aucun resultat"); return; }

        int show = Math.min(total, 50);
        long[] addrs = NativeCore.getScanResults(0, show);
        if (addrs == null) { tvRes.setText("(erreur lecture)"); return; }

        int pid = AngelsPanel.getTargetPid();
        StringBuilder sb = new StringBuilder();
        sb.append(total).append(" resultats");
        if (total > show) sb.append(" (").append(show).append(" affiches)");
        sb.append("\n");
        for (long a : addrs) {
            sb.append("0x").append(Long.toHexString(a));
            byte[] raw = NativeCore.readMem(pid, a, valueSizeOf(selType));
            if (raw != null) sb.append("  ->  ").append(decodeValue(raw, selType));
            sb.append("\n");
        }
        tvRes.setText(sb.toString());
    }

    private int valueSizeOf(int type) {
        switch (type) {
            case NativeCore.TYPE_BYTE:   return 1;
            case NativeCore.TYPE_WORD:   return 2;
            case NativeCore.TYPE_DWORD:
            case NativeCore.TYPE_FLOAT:
            case NativeCore.TYPE_XOR:    return 4;
            case NativeCore.TYPE_QWORD:
            case NativeCore.TYPE_DOUBLE: return 8;
            default:                     return 4;
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Utilitaires
    // ═════════════════════════════════════════════════════════════════════════

    private void applyPid() {
        try {
            int pid = Integer.parseInt(etPid.getText().toString().trim());
            AngelsPanel.setTargetPid(pid);
            AngelsPanel.log("PID : " + pid);
            toast("PID : " + pid);
        } catch (Exception e) { toast("PID invalide"); }
    }

    private int getPid() { return AngelsPanel.getTargetPid(); }

    private void toast(String msg) {
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show();
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Helpers UI
    // ─────────────────────────────────────────────────────────────────────────

    private int dp(int v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP,
            v, getResources().getDisplayMetrics()));
    }

    private LinearLayout.LayoutParams matchW() {
        return new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
    }

    private TextView tv(String text, float sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(null, Typeface.BOLD);
        t.setLayoutParams(matchW());
        return t;
    }

    private TextView sectionLabel(String text) {
        TextView t = tv(text, 10, C_DIM, false);
        t.setPadding(0, dp(5), 0, dp(2));
        return t;
    }

    private LinearLayout hRow() {
        LinearLayout ll = new LinearLayout(this);
        ll.setOrientation(LinearLayout.HORIZONTAL);
        ll.setLayoutParams(matchW());
        return ll;
    }

    private LinearLayout vCol() {
        LinearLayout ll = new LinearLayout(this);
        ll.setOrientation(LinearLayout.VERTICAL);
        ll.setLayoutParams(matchW());
        ll.setPadding(dp(8), dp(6), dp(8), dp(6));
        return ll;
    }

    private Button btn(String text, int bgColor) {
        Button b = new Button(this);
        b.setText(text);
        b.setTextSize(10f);
        b.setAllCaps(false);
        b.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        b.setTextColor(C_TEXT);
        b.setPadding(dp(7), dp(4), dp(7), dp(4));
        b.setBackground(btnDrawable(bgColor));
        LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.WRAP_CONTENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        blp.setMargins(dp(2), dp(2), dp(2), dp(2));
        b.setLayoutParams(blp);
        return b;
    }

    private StateListDrawable btnDrawable(int bgColor) {
        GradientDrawable normal = new GradientDrawable();
        normal.setColor(bgColor);
        normal.setCornerRadius(dp(4));
        normal.setStroke(dp(1), blendColor(bgColor, 0xFFFFFFFF, 0.2f));

        GradientDrawable pressed = new GradientDrawable();
        pressed.setColor(blendColor(bgColor, 0xFFFFFFFF, 0.3f));
        pressed.setCornerRadius(dp(4));

        StateListDrawable sld = new StateListDrawable();
        sld.addState(new int[]{android.R.attr.state_pressed}, pressed);
        sld.addState(new int[]{}, normal);
        return sld;
    }

    private int blendColor(int base, int overlay, float ratio) {
        int r = (int)((Color.red(base)   * (1-ratio)) + (Color.red(overlay)   * ratio));
        int g = (int)((Color.green(base) * (1-ratio)) + (Color.green(overlay) * ratio));
        int bl= (int)((Color.blue(base)  * (1-ratio)) + (Color.blue(overlay)  * ratio));
        return Color.rgb(Math.min(r,255), Math.min(g,255), Math.min(bl,255));
    }

    private EditText et(String def, int inputType, int wDp) {
        EditText e = new EditText(this);
        e.setText(def);
        e.setInputType(inputType);
        e.setTextColor(C_TEXT);
        e.setTextSize(12f);
        e.setTypeface(Typeface.MONOSPACE);
        e.setPadding(dp(6), dp(5), dp(6), dp(5));

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            e.setTextCursorDrawable(null);
        }

        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xFF1A2230);
        bg.setCornerRadius(dp(4));
        bg.setStroke(dp(1), C_BORDER);
        e.setBackground(bg);

        LinearLayout.LayoutParams elp = wDp > 0
            ? new LinearLayout.LayoutParams(wDp, LinearLayout.LayoutParams.WRAP_CONTENT)
            : matchW();
        e.setLayoutParams(elp);

        // Toggle FLAG_NOT_FOCUSABLE au focus pour clavier
        e.setOnFocusChangeListener((v, hasFocus) -> {
            if (hasFocus) {
                lp.flags &= ~WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
            } else {
                lp.flags |= WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
            }
            safeUpdateLayout();
        });
        return e;
    }

    private Spinner styledSpinner(String[] items) {
        Spinner sp = new Spinner(this);
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(
            this, android.R.layout.simple_spinner_item, items)
        {
            @Override
            public View getView(int pos, View conv, android.view.ViewGroup parent) {
                View v = super.getView(pos, conv, parent);
                ((TextView) v).setTextColor(C_TEXT);
                ((TextView) v).setTypeface(Typeface.MONOSPACE);
                ((TextView) v).setTextSize(12f);
                v.setBackgroundColor(0xFF1A2230);
                return v;
            }
            @Override
            public View getDropDownView(int pos, View conv, android.view.ViewGroup parent) {
                View v = super.getDropDownView(pos, conv, parent);
                ((TextView) v).setTextColor(C_TEXT);
                ((TextView) v).setBackgroundColor(0xFF1C2128);
                ((TextView) v).setTypeface(Typeface.MONOSPACE);
                ((TextView) v).setTextSize(12f);
                ((TextView) v).setPadding(dp(10), dp(8), dp(10), dp(8));
                return v;
            }
        };
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        sp.setAdapter(adapter);
        sp.setPopupBackgroundDrawable(new ColorDrawable(0xFF1C2128));
        sp.setLayoutParams(matchW());
        return sp;
    }

    private interface OnPosition { void on(int pos); }

    private AdapterView.OnItemSelectedListener sel(OnPosition action) {
        return new AdapterView.OnItemSelectedListener() {
            public void onItemSelected(AdapterView<?> p, View v, int pos, long id) {
                action.on(pos);
            }
            public void onNothingSelected(AdapterView<?> p) {}
        };
    }

    private View divider() {
        View d = new View(this);
        d.setLayoutParams(new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, dp(1)));
        d.setBackgroundColor(C_DIVIDER);
        return d;
    }
}
