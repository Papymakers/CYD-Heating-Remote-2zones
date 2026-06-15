/*
 * remoteCtrl2_CYD_merged.cpp
 *
 * Fusion de :
 *   - remoteCtrl2_CYD_next_TFT_MQTT  (commandes chauffage)
 *   - 0_TempoClockWs_display          (affichage Tempo / Horloge / Ecowitt)
 *
 * Page 0 (accueil) : heure, date, Tempo aujourd'hui/demain, températures
 *                    ext/Z1/Z2, Iinst, commande courante Z1/Z2
 *                    → touche n'importe où = bascule vers page 1
 *
 * Page 1 (commandes) : buttonmatrix STOP/HG/ECO/CONF/-2 × 2 zones
 *                    → touche sur un bouton = publie la commande
 *                    → bouton retour (ou timeout 30 s) = bascule vers page 0
 *
 * Topics MQTT abonnés :
 *   ecowittDatas          → tempExt, tempZ1, tempZ2
 *   newHeating/linkyValues → PTEC, DEMAIN
 *   newHeating/IinstVal    → Iinst
 *   newHeating/cmdStatus  → zone, cmd
 *   astroClock/#          → hours, minutes, day → gHeure, gDate
 *
 * Topic MQTT publié :
 *   heatingCmd            → {"zone":1,"cmd":"ECO"}
 *
 * Board : ESP32 Dev Module (CYD — Cheap Yellow Display)
 * Version : 1.0 — 07/06/2026
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include "esp_task_wdt.h"

// ─── Version ────────────────────────────────────────────────────────────────
static const char* FW_VERSION = "remoteCtrl2_CYD_merged  07/06/2026";

// ─── Matériel écran / touch ─────────────────────────────────────────────────
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320
#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))

#define BACKLIGHT_PIN 22
#define PWM_FREQ      5000
#define PWM_RESOLUTION 8

// ─── WiFi / MQTT ─────────────────────────────────────────────────────────────
static const char* SSID        = "FG14";
static const char* WIFI_PASS   = "FontainesGaillard";
static const char* MQTT_SERVER = "192.168.1.20";
static const int   MQTT_PORT   = 1883;
static const char* CLIENT_ID   = "cydMerged01";

// ─── Watchdog ────────────────────────────────────────────────────────────────
constexpr uint32_t WDT_TIMEOUT_MS = 10000;
esp_task_wdt_config_t wdt_config = {
    .timeout_ms  = WDT_TIMEOUT_MS,
    .idle_core_mask = 0,
    .trigger_panic  = true
};

// ─── Timeout auto-retour page 0 (ms) ────────────────────────────────────────
#define CMD_PAGE_TIMEOUT_MS 30000UL

// ─── Objet SPI / touch ───────────────────────────────────────────────────────
SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

// ─── MQTT ────────────────────────────────────────────────────────────────────
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// ─── État WiFi ────────────────────────────────────────────────────────────────
static bool          wifiConnected    = false;
static unsigned long lastWifiAttempt  = 0;
static const unsigned long WIFI_RETRY = 10000;

// ─── État MQTT ───────────────────────────────────────────────────────────────
static unsigned long lastMqttAttempt  = 0;
static const unsigned long MQTT_RETRY = 2000;

// ─── Données affichées ────────────────────────────────────────────────────────
static String  gHeure   = "--:--";   // "14:32"
static String  gDate    = "---";     // "Lun 02/06"
static String  gPtec    = "---";     // "Jour ROUGE" / "Jour BLEU" / "Jour BLANC"
static String  gDemain  = "---";
static String  gTempExt = "--.-";
static String  gTempZ1  = "--.-";
static String  gTempZ2  = "--.-";
static String  gIinst   = "--";
static bool    gOverload = false;  // surcharge courant
static String  gCmdZ1   = "---";
static String  gCmdZ2   = "---";

// couleurs Tempo (pour la cellule)
static bool flagRA = false, flagBA = false, flagWA = false;  // aujourd'hui
static bool flagRD = false, flagBD = false, flagWD = false;  // demain

static volatile bool iinstChanged = false;
static uint8_t lastIinst = 0;

// ─── Buffers draw LVGL ───────────────────────────────────────────────────────
static uint32_t draw_buf[DRAW_BUF_SIZE / 4];

// ─── Pages LVGL ──────────────────────────────────────────────────────────────
static lv_obj_t* pageHome = NULL;
static lv_obj_t* pageCmd  = NULL;
static int        currentPage = 0;   // 0 = accueil, 1 = commandes
static unsigned long cmdPageEntered  = 0;  // horodatage entrée sur page 1
static unsigned long cmdPageReadyAt  = 0;  // garde anti-fantôme (800 ms)
static unsigned long cmdSentAt       = 0;  // horodatage dernier envoi commande
#define CMD_BACK_DELAY_MS 3000UL             // délai retour page 0 après commande

// ─── Labels page 0 (accueil) ─────────────────────────────────────────────────
static lv_obj_t* lblHeure    = NULL;
static lv_obj_t* lblDate     = NULL;
static lv_obj_t* lblPtec     = NULL;
static lv_obj_t* lblDemain   = NULL;
static lv_obj_t* lblTempExt  = NULL;
static lv_obj_t* lblTempZ1   = NULL;
static lv_obj_t* lblTempZ2   = NULL;
static lv_obj_t* lblIinst    = NULL;
static lv_obj_t* lblCmdZ1    = NULL;
static lv_obj_t* lblCmdZ2    = NULL;

// ─── Indicateur WiFi (page 0) ────────────────────────────────────────────────
static lv_obj_t* lblWifi0 = NULL;

// ─── Commandes chauffage ─────────────────────────────────────────────────────
static const char* btnm_map[] = {
    "S", "HG", "E", "C", "-2", "\n",
    "S", "HG", "E", "C", "-2", ""
};
static const char* CmdeTx[] = {
    "STOP", "HG", "ECO", "CONF", "293S",
    "STOP", "HG", "ECO", "CONF", "293S"
};

// ─── Symbole degré ───────────────────────────────────────────────────────────
static const char DEGREE_C[] = "\u00B0C";

// ═══════════════════════════════════════════════════════════════════════════
//  Prototypes
// ═══════════════════════════════════════════════════════════════════════════
static void goToPage(int page);
static void updateHomePage(void);
static void updateTempoColor(void);

// ═══════════════════════════════════════════════════════════════════════════
//  Touchscreen
// ═══════════════════════════════════════════════════════════════════════════
static void touchscreen_read(lv_indev_t* indev, lv_indev_data_t* data) {
    if (touchscreen.tirqTouched() && touchscreen.touched()) {
        TS_Point p = touchscreen.getPoint();
        int tx = map(p.y, 240, 3800, 0, SCREEN_WIDTH);
        int ty = map(p.x, 200, 3700, SCREEN_HEIGHT, 0);
        data->point.x = tx;
        data->point.y = ty;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Navigation entre pages
// ═══════════════════════════════════════════════════════════════════════════
static void goToPage(int page) {
    if (page == currentPage) return;
    currentPage = page;
    if (page == 0) {
        lv_scr_load(pageHome);
        updateHomePage();
    } else {
        cmdPageEntered = millis();
        cmdPageReadyAt = millis() + 800UL;  // garde anti-fantôme
        lv_scr_load(pageCmd);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Event handler : page 0 → toute touche → page 1
// ═══════════════════════════════════════════════════════════════════════════
static void home_touch_cb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        goToPage(1);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Event handler : buttonmatrix commandes (page 1)
// ═══════════════════════════════════════════════════════════════════════════
static void btnm_event_handler(lv_event_t* e) {
    // Filtre strict : uniquement VALUE_CHANGED (appui réel)
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    if (millis() < cmdPageReadyAt) return;  // ignore events fantômes au chargement

    lv_obj_t* obj = (lv_obj_t*)lv_event_get_target(e);
    uint32_t id   = lv_buttonmatrix_get_selected_button(obj);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;

    int zone = (id <= 4) ? 1 : 2;
    const char* cmd = CmdeTx[id];

    JsonDocument doc;
    doc["zone"] = zone;
    doc["cmd"]  = cmd;
    char buffer[128];
    serializeJson(doc, buffer, sizeof(buffer));

    if (mqttClient.connected()) {
        mqttClient.publish("heatingCmd", buffer);
        Serial.printf("[MQTT] Published heatingCmd: zone=%d cmd=%s\n", zone, cmd);
    } else {
        Serial.println("[MQTT] Not connected — command lost");
    }
    memset(buffer, 0, sizeof(buffer));

    // Retour différé vers page 0 (3 s) — sans bloquer LVGL
    cmdSentAt = millis();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Event handler : bouton retour (page 1 → page 0)
// ═══════════════════════════════════════════════════════════════════════════
static void back_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        goToPage(0);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Timer LVGL : rafraîchit les labels page 0 toutes les secondes
// ═══════════════════════════════════════════════════════════════════════════
static void timer_cb(lv_timer_t* timer) {
    LV_UNUSED(timer);
    if (currentPage == 0) {
        updateHomePage();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Mise à jour couleur fond Tempo (labels Ptec / Demain)
// ═══════════════════════════════════════════════════════════════════════════
static void updateTempoColor(void) {
    // Ptec
    lv_color_t cA;
    if      (flagRA) cA = lv_palette_main(LV_PALETTE_RED);
    else if (flagBA) cA = lv_palette_main(LV_PALETTE_BLUE);
    else if (flagWA) cA = lv_color_hex(0xC0C0C0);
    else             cA = lv_color_hex(0x444444);
    lv_obj_set_style_text_color(lblPtec, cA, 0);

    // Demain
    lv_color_t cD;
    if      (flagRD) cD = lv_palette_main(LV_PALETTE_RED);
    else if (flagBD) cD = lv_palette_main(LV_PALETTE_BLUE);
    else if (flagWD) cD = lv_color_hex(0xC0C0C0);
    else             cD = lv_color_hex(0x444444);
    lv_obj_set_style_text_color(lblDemain, cD, 0);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Mise à jour de tous les labels de la page accueil
// ═══════════════════════════════════════════════════════════════════════════
static void updateHomePage(void) {
    lv_label_set_text(lblHeure,   gHeure.c_str());
    lv_label_set_text(lblDate,    gDate.c_str());
    lv_label_set_text(lblPtec,    gPtec.c_str());
    lv_label_set_text(lblDemain,  gDemain.c_str());

    String sExt = gTempExt + DEGREE_C;
    String sZ1  = gTempZ1  + DEGREE_C;
    String sZ2  = gTempZ2  + DEGREE_C;
    lv_label_set_text(lblTempExt, sExt.c_str());
    lv_label_set_text(lblTempZ1,  sZ1.c_str());
    lv_label_set_text(lblTempZ2,  sZ2.c_str());
    lv_label_set_text(lblIinst,   (gIinst + " A").c_str());
    lv_label_set_text(lblCmdZ1,   gCmdZ1.c_str());
    lv_label_set_text(lblCmdZ2,   gCmdZ2.c_str());
    updateTempoColor();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Création page 0 : ACCUEIL
//
//  Layout (écran 320×240 en rotation 270°) :
//
//  ┌──────────────────────────────────┐
//  │  14:32      Lun 02/06      WiFi │  y=12
//  │─────────────────────────────────│
//  │  Aujourd'hui:   Jour ROUGE      │  y=50
//  │  Demain:        Jour BLEU       │  y=72
//  │─────────────────────────────────│
//  │  Ext.  18.5°C   Courant  12 A   │  y=110
//  │  Zone1 21.3°C   Cmd Z1   ECO    │  y=135
//  │  Zone2 20.1°C   Cmd Z2   HG     │  y=160
//  │─────────────────────────────────│
//  │        [Toucher pour commander] │  y=200
//  └──────────────────────────────────┘
// ═══════════════════════════════════════════════════════════════════════════
static void create_page_home(void) {
    pageHome = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(pageHome, lv_color_hex(0x101018), 0);
    lv_obj_set_style_bg_opa(pageHome, LV_OPA_COVER, 0);

    // Touche partout → page commandes
    lv_obj_add_event_cb(pageHome, home_touch_cb, LV_EVENT_CLICKED, NULL);

    const lv_color_t COL_TITLE  = lv_color_hex(0x8080FF);
    const lv_color_t COL_VALUE  = lv_color_hex(0xE0E0E0);
    const lv_color_t COL_HINT   = lv_color_hex(0x505060);

    // ── Heure ──────────────────────────────────────────────
    lblHeure = lv_label_create(pageHome);
    lv_label_set_text(lblHeure, "--:--");
    lv_obj_set_style_text_font(lblHeure, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lblHeure, lv_color_hex(0xFFFFAA), 0);
    lv_obj_set_pos(lblHeure, 8, 6);

    // ── Date ───────────────────────────────────────────────
    lblDate = lv_label_create(pageHome);
    lv_label_set_text(lblDate, "---");
    lv_obj_set_style_text_font(lblDate, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblDate, COL_VALUE, 0);
    lv_obj_set_pos(lblDate, 110, 16);

    // ── Indicateur WiFi ────────────────────────────────────
    lblWifi0 = lv_label_create(pageHome);
    lv_label_set_text(lblWifi0, "WiFi");
    lv_obj_set_style_text_font(lblWifi0, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lblWifi0, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_align(lblWifi0, LV_ALIGN_TOP_RIGHT, -12, 6);
    lv_obj_add_flag(lblWifi0, LV_OBJ_FLAG_HIDDEN);

    // ── Séparateur ─────────────────────────────────────────
    lv_obj_t* sep1 = lv_obj_create(pageHome);
    lv_obj_set_size(sep1, 312, 1);
    lv_obj_set_pos(sep1, 4, 42);
    lv_obj_set_style_bg_color(sep1, COL_HINT, 0);
    lv_obj_set_style_border_width(sep1, 0, 0);

    // ── Tempo : Aujourd'hui ────────────────────────────────
    lv_obj_t* lbTA = lv_label_create(pageHome);
    lv_label_set_text(lbTA, "Aujourd'hui:");
    lv_obj_set_style_text_font(lbTA, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbTA, COL_TITLE, 0);
    lv_obj_set_pos(lbTA, 8, 50);

    lblPtec = lv_label_create(pageHome);
    lv_label_set_text(lblPtec, "---");
    lv_obj_set_style_text_font(lblPtec, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lblPtec, 130, 50);

    // ── Tempo : Demain ─────────────────────────────────────
    lv_obj_t* lbTD = lv_label_create(pageHome);
    lv_label_set_text(lbTD, "Demain:");
    lv_obj_set_style_text_font(lbTD, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbTD, COL_TITLE, 0);
    lv_obj_set_pos(lbTD, 8, 72);

    lblDemain = lv_label_create(pageHome);
    lv_label_set_text(lblDemain, "---");
    lv_obj_set_style_text_font(lblDemain, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lblDemain, 130, 72);

    // ── Séparateur ─────────────────────────────────────────
    lv_obj_t* sep2 = lv_obj_create(pageHome);
    lv_obj_set_size(sep2, 312, 1);
    lv_obj_set_pos(sep2, 4, 98);
    lv_obj_set_style_bg_color(sep2, COL_HINT, 0);
    lv_obj_set_style_border_width(sep2, 0, 0);

    // ── Températures col gauche ────────────────────────────
    struct { const char* lbl; lv_obj_t** val; int y; } tempRows[] = {
        { "Ext.",  &lblTempExt, 108 },
        { "Zone 1", &lblTempZ1, 133 },
        { "Zone 2", &lblTempZ2, 158 },
    };
    for (auto& r : tempRows) {
        lv_obj_t* t = lv_label_create(pageHome);
        lv_label_set_text(t, r.lbl);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(t, COL_TITLE, 0);
        lv_obj_set_pos(t, 8, r.y);

        *r.val = lv_label_create(pageHome);
        lv_label_set_text(*r.val, "--.-°C");
        lv_obj_set_style_text_font(*r.val, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(*r.val, COL_VALUE, 0);
        lv_obj_set_pos(*r.val, 72, r.y);
    }

    // ── Col droite : Courant + Commandes ──────────────────
    // Courant
    lv_obj_t* lbI = lv_label_create(pageHome);
    lv_label_set_text(lbI, "Courant:");
    lv_obj_set_style_text_font(lbI, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbI, COL_TITLE, 0);
    lv_obj_set_pos(lbI, 172, 108);

    lblIinst = lv_label_create(pageHome);
    lv_label_set_text(lblIinst, "-- A");
    lv_obj_set_style_text_font(lblIinst, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblIinst, COL_VALUE, 0);
    lv_obj_set_pos(lblIinst, 260, 108);

    // Cmd Z1
    lv_obj_t* lbC1 = lv_label_create(pageHome);
    lv_label_set_text(lbC1, "Cmd Z1:");
    lv_obj_set_style_text_font(lbC1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbC1, COL_TITLE, 0);
    lv_obj_set_pos(lbC1, 172, 133);

    lblCmdZ1 = lv_label_create(pageHome);
    lv_label_set_text(lblCmdZ1, "---");
    lv_obj_set_style_text_font(lblCmdZ1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblCmdZ1, lv_palette_main(LV_PALETTE_INDIGO), 0);
    lv_obj_set_pos(lblCmdZ1, 260, 133);

    // Cmd Z2
    lv_obj_t* lbC2 = lv_label_create(pageHome);
    lv_label_set_text(lbC2, "Cmd Z2:");
    lv_obj_set_style_text_font(lbC2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbC2, COL_TITLE, 0);
    lv_obj_set_pos(lbC2, 172, 158);

    lblCmdZ2 = lv_label_create(pageHome);
    lv_label_set_text(lblCmdZ2, "---");
    lv_obj_set_style_text_font(lblCmdZ2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblCmdZ2, lv_palette_main(LV_PALETTE_INDIGO), 0);
    lv_obj_set_pos(lblCmdZ2, 260, 158);

    // ── Séparateur ─────────────────────────────────────────
    lv_obj_t* sep3 = lv_obj_create(pageHome);
    lv_obj_set_size(sep3, 312, 1);
    lv_obj_set_pos(sep3, 4, 188);
    lv_obj_set_style_bg_color(sep3, COL_HINT, 0);
    lv_obj_set_style_border_width(sep3, 0, 0);

    // ── Hint "toucher pour commander" ─────────────────────
    lv_obj_t* lbHint = lv_label_create(pageHome);
    lv_label_set_text(lbHint, LV_SYMBOL_SETTINGS "  Toucher pour commander");
    lv_obj_set_style_text_font(lbHint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbHint, COL_HINT, 0);
    lv_obj_align(lbHint, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Création page 1 : COMMANDES
// ═══════════════════════════════════════════════════════════════════════════
static void create_page_cmd(void) {
    pageCmd = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(pageCmd, lv_color_hex(0x101018), 0);
    lv_obj_set_style_bg_opa(pageCmd, LV_OPA_COVER, 0);

    // ── Titre ──────────────────────────────────────────────
    lv_obj_t* lbTitle = lv_label_create(pageCmd);
    lv_label_set_text(lbTitle, "Commandes chauffage");
    lv_obj_set_style_text_font(lbTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbTitle, lv_color_hex(0x8080FF), 0);
    lv_obj_align(lbTitle, LV_ALIGN_TOP_MID, 0, 8);

    // ── Labels Zone 1 / Zone 2 ─────────────────────────────
    lv_obj_t* lbZ1 = lv_label_create(pageCmd);
    lv_label_set_text(lbZ1, "ZONE 1");
    lv_obj_set_style_text_font(lbZ1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbZ1, lv_color_hex(0x8080FF), 0);
    lv_obj_set_pos(lbZ1, 4, 30);

    // Zone 2 : positionné sous la buttonmatrix (hors de vue immédiate)
    lv_obj_t* lbZ2 = lv_label_create(pageCmd);
    lv_label_set_text(lbZ2, "ZONE 2");
    lv_obj_set_style_text_font(lbZ2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbZ2, lv_color_hex(0x8080FF), 0);
    lv_obj_set_pos(lbZ2, 4, 195);

    // ── Buttonmatrix ───────────────────────────────────────
    lv_obj_t* btnm = lv_buttonmatrix_create(pageCmd);
    lv_buttonmatrix_set_map(btnm, btnm_map);
    lv_buttonmatrix_set_one_checked(btnm, false);  // pas de maintien d'état
    lv_obj_set_size(btnm, 310, 155);
    lv_obj_align(btnm, LV_ALIGN_CENTER, 0, -5);
    lv_obj_add_event_cb(btnm, btnm_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

    // ── Bouton Retour ──────────────────────────────────────
    lv_obj_t* btnBack = lv_button_create(pageCmd);
    lv_obj_set_size(btnBack, 100, 28);
    lv_obj_align(btnBack, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(btnBack, lv_color_hex(0x333360), 0);
    lv_obj_add_event_cb(btnBack, back_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbBack = lv_label_create(btnBack);
    lv_label_set_text(lbBack, LV_SYMBOL_LEFT " Retour");
    lv_obj_set_style_text_font(lbBack, &lv_font_montserrat_12, 0);
    lv_obj_center(lbBack);
}

// ═══════════════════════════════════════════════════════════════════════════
//  MQTT callback
// ═══════════════════════════════════════════════════════════════════════════
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // Buffer sécurisé
    char buf[257];
    if (length > 256) length = 256;
    memcpy(buf, payload, length);
    buf[length] = '\0';

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, buf);
    if (err) {
        Serial.printf("[MQTT] JSON error on %s: %s\n", topic, err.f_str());
        return;
    }

    String t = topic;

    // ── ecowittDatas ────────────────────────────────────────
    if (t == "ecowittDatas") {
        if (doc.containsKey("tempExt")) {
            float v = doc["tempExt"];
            gTempExt = String(v, 1);
        }
        if (doc.containsKey("tempZ1")) gTempZ1 = doc["tempZ1"].as<String>();
        if (doc.containsKey("tempZ2")) gTempZ2 = doc["tempZ2"].as<String>();
    }

    // ── astroClock → heure + date ──────────────────────────
    // Format JSON : {"hours":23,"minutes":16,"day":7,"month":6,"weekday":7}
    // weekday : 1=Lun ... 7=Dim (ISO)
    else if (t.startsWith("astroClock")) {
        int h  = doc["hours"]   | -1;
        int m  = doc["minutes"] | -1;
        int d  = doc["day"]     | 0;
        int mo = doc["month"]   | 0;
        int wd = doc["weekday"] | 0;

        if (h >= 0 && m >= 0) {
            char hhmm[6];
            snprintf(hhmm, sizeof(hhmm), "%02d:%02d", h, m);
            gHeure = hhmm;
        }
        if (d > 0 && mo > 0 && wd > 0) {
            static const char* jours[] = {"", "Lun","Mar","Mer","Jeu","Ven","Sam","Dim"};
            char date[14];
            snprintf(date, sizeof(date), "%s %02d/%02d", jours[wd], d, mo);
            gDate = date;
        }
        Serial.printf("[astro] Heure: %s  Date: %s\n", gHeure.c_str(), gDate.c_str());
    }

    // ── newHeating/linkyValues ─────────────────────────────
    else if (t == "newHeating/linkyValues") {
        flagRA = flagBA = flagWA = false;
        flagRD = flagBD = flagWD = false;

        if (doc["PTEC"].is<const char*>()) {
            String s = doc["PTEC"].as<String>();
            s.trim();
            if      (s == "HPJR" || s == "HCJR") { flagRA = true;  gPtec = "Jour ROUGE"; }
            else if (s == "HPJB" || s == "HCJB") { flagBA = true;  gPtec = "Jour BLEU";  }
            else if (s == "HPJW" || s == "HCJW") { flagWA = true;  gPtec = "Jour BLANC"; }
            else                                  {                 gPtec = s;             }
        }
        if (doc["DEMAIN"].is<const char*>()) {
            String s = doc["DEMAIN"].as<String>();
            s.trim();
            if      (s == "ROUG") { flagRD = true;  gDemain = "Jour ROUGE"; }
            else if (s == "BLEU") { flagBD = true;  gDemain = "Jour BLEU";  }
            else if (s == "BLAN") { flagWD = true;  gDemain = "Jour BLANC"; }
            else                  {                 gDemain = "indefini";    }
        }
    }

    // ── newHeating/IinstVal ────────────────────────────────
    else if (t == "newHeating/IinstVal") {
        if (doc["Iinst"].is<int>()) {
            uint8_t v = doc["Iinst"].as<int>();
            if (v != lastIinst) {
                lastIinst = v;
                gIinst = String(v);
                iinstChanged = true;
            }
        }
        if (doc["overload"].is<bool>()) {
            bool ov = doc["overload"].as<bool>();
            if (ov != gOverload) {
                gOverload = ov;
                iinstChanged = true;  // forcer rafraîchissement couleur
            }
        }
    }

    // ── newHeating/cmdStatus ───────────────────────────────
    else if (t == "newHeating/cmdStatus") {
        int zone    = doc["zone"] | -1;
        String cmd  = doc["cmd"]  | "---";
        if (cmd == "293S") cmd = "-2";
        if      (zone == 1) gCmdZ1 = cmd;
        else if (zone == 2) gCmdZ2 = cmd;
        Serial.printf("[MQTT] cmdStatus zone=%d cmd=%s\n", zone, cmd.c_str());
    }

}

// ═══════════════════════════════════════════════════════════════════════════
//  Reconnexion MQTT (non bloquante)
// ═══════════════════════════════════════════════════════════════════════════
static void mqttReconnect(void) {
    if (mqttClient.connected()) return;
    unsigned long now = millis();
    if (now - lastMqttAttempt < MQTT_RETRY) return;
    lastMqttAttempt = now;

    esp_task_wdt_reset();

    if (mqttClient.connect(CLIENT_ID)) {
        static const char* topics[] = {
            "ecowittDatas",
            "astroClock/#",
            "newHeating/linkyValues",
            "newHeating/IinstVal",
            "newHeating/cmdStatus",
        };
        for (const char* tp : topics) {
            bool ok = mqttClient.subscribe(tp);
            Serial.printf("[MQTT] Subscribe %s : %s\n", tp, ok ? "OK" : "FAIL");
        }
        Serial.println("[MQTT] Connected");
    } else {
        Serial.printf("[MQTT] Connect failed, rc=%d\n", mqttClient.state());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Gestion WiFi (non bloquante)
// ═══════════════════════════════════════════════════════════════════════════
static void handleWiFi(void) {
    bool connected = (WiFi.status() == WL_CONNECTED);

    if (connected && !wifiConnected) {
        wifiConnected = true;
        Serial.println("[WiFi] Connected");
    } else if (!connected && wifiConnected) {
        wifiConnected = false;
        Serial.println("[WiFi] Disconnected");
    }

    if (connected) return;

    unsigned long now = millis();
    if (now - lastWifiAttempt >= WIFI_RETRY) {
        lastWifiAttempt = now;
        WiFi.begin(SSID, WIFI_PASS);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println(FW_VERSION);

    // Rétroéclairage
    ledcAttach(BACKLIGHT_PIN, PWM_FREQ, PWM_RESOLUTION);
    ledcWrite(BACKLIGHT_PIN, 120);

    // WiFi
    WiFi.setAutoReconnect(true);
    WiFi.begin(SSID, WIFI_PASS);

    // MQTT
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setKeepAlive(60);

    // Touchscreen SPI
    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touchscreen.begin(touchscreenSPI);
    touchscreen.setRotation(1);

    // LVGL
    lv_init();
    lv_display_t* disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT,
                                             draw_buf, sizeof(draw_buf));
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchscreen_read);

    // Création des deux pages
    create_page_home();
    create_page_cmd();

    // Affichage initial : page accueil
    lv_scr_load(pageHome);
    currentPage = 0;

    // Timer de rafraîchissement (1 s)
    lv_timer_t* tmr = lv_timer_create(timer_cb, 1000, NULL);
    lv_timer_ready(tmr);

    // Watchdog
    delay(10);
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);

    Serial.println("[SETUP] Done");
}

// ═══════════════════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════════════════
void loop() {
    esp_task_wdt_reset();

    lv_task_handler();
    lv_tick_inc(5);
    delay(5);

    // Mise à jour Iinst hors timer si elle a changé
    if (iinstChanged) {
        iinstChanged = false;
        if (currentPage == 0 && lblIinst) {
            lv_label_set_text(lblIinst, (gIinst + " A").c_str());
            lv_obj_set_style_text_color(lblIinst,
                gOverload ? lv_palette_main(LV_PALETTE_RED)
                          : lv_color_hex(0xE0E0E0), 0);
        }
    }

    // Retour différé après envoi commande (3 s)
    if (currentPage == 1 && cmdSentAt > 0) {
        if (millis() - cmdSentAt > CMD_BACK_DELAY_MS) {
            cmdSentAt = 0;
            goToPage(0);
        }
    }
    // Timeout auto-retour depuis la page commandes (30 s sans action)
    else if (currentPage == 1) {
        if (millis() - cmdPageEntered > CMD_PAGE_TIMEOUT_MS) {
            goToPage(0);
        }
    }

    handleWiFi();

    // Indicateur WiFi
    if (currentPage == 0 && lblWifi0) {
        if (wifiConnected)
            lv_obj_clear_flag(lblWifi0, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(lblWifi0, LV_OBJ_FLAG_HIDDEN);
    }

    if (!wifiConnected) return;

    mqttReconnect();
    mqttClient.loop();
}
