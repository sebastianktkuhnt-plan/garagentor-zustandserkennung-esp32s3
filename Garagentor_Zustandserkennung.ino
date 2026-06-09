// ===========================================================================
//  Garagentor-Zustandserkennung mit ESP32-S3-CAM (Freenove ESP32-S3-WROOM)
// ---------------------------------------------------------------------------
//  Copyright (C) 2026 sebastianktkuhnt-plan
//
//  Dieses Programm ist freie Software: Sie koennen es unter den Bedingungen
//  der GNU General Public License v3.0 (GPL-3.0), wie von der Free Software
//  Foundation veroeffentlicht, weitergeben und/oder veraendern. Es wird OHNE
//  JEDE GEWAEHRLEISTUNG bereitgestellt. Details stehen in der Datei LICENSE.
// ---------------------------------------------------------------------------
//  Funktionen:
//   - WLAN-Verbindung + Weboberflaeche direkt auf dem ESP32 (kein Server noetig)
//   - MJPEG-Livestream im Browser
//   - Alle 20 s automatische Bildanalyse -> Zustand (zu / offen / halb / unklar)
//   - Robuste Erkennung ueber Helligkeits-ZONEN und GRADIENTEN
//     (statt absoluter Helligkeit -> unempfindlicher gegen Gegenlicht)
//   - Trainingsmodus im Browser zum Kalibrieren auf die eigene Garage
//   - Speichern der Referenzwerte im SPIFFS (uebersteht Neustart)
//   - Foto + Zeitstempel des letzten Zustandswechsels
//   - Optionale Logs auf SD-Karte
//   - Optionale MQTT-Ausgabe (auskommentiert)
//
//  Board-Einstellungen in der Arduino IDE 2.x (Werkzeuge-Menue):
//   - Board:            "ESP32S3 Dev Module"
//   - USB CDC On Boot:  "Enabled"
//   - PSRAM:            "OPI PSRAM"
//   - Flash Size:       "8MB (64Mb)"
//   - Partition Scheme: "8M with spiffs (3MB APP/1.5MB SPIFFS)"
//
//  Alle Konfigurationswerte stehen in der Datei config.h.
// ===========================================================================

#include "esp_camera.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>        // OTA-Updates ueber WLAN
#include "esp_http_server.h"
#include "FS.h"
#include "SPIFFS.h"
#include "SD_MMC.h"
#include "img_converters.h"   // fmt2rgb888() zum Dekodieren der JPEGs
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "config.h"
#include "camera_pins.h"
#include "webpages.h"

// --- Optional fuer MQTT (Bibliothek "PubSubClient" installieren) -----------
// #include <PubSubClient.h>
// WiFiClient   mqttWifi;
// PubSubClient mqttClient(mqttWifi);

// ===========================================================================
//  GLOBALE ZUSTANDSVARIABLEN
// ===========================================================================
httpd_handle_t  gServer       = NULL;   // Hauptserver (Port 80): Seiten + API
httpd_handle_t  gStreamServer = NULL;   // Stream-Server (Port 81): nur MJPEG

SemaphoreHandle_t gCamMutex;   // schuetzt Kamerazugriffe (Stream vs. Analyse)
SemaphoreHandle_t gFsMutex;    // schuetzt SPIFFS-Zugriffe (Schreiben vs. Lesen)

// Ergebnis der letzten Analyse (fuer die Anzeige)
volatile Zustand gCurrentState = UNKLAR;
volatile int     gCurrentConf  = 0;
time_t           gLetzteAnalyseZeit = 0;

// Entprellter "stabiler" Zustand + Erkennung von Wechseln
Zustand  gStableState    = UNKLAR;
Zustand  gCandidate      = UNKLAR;
uint8_t  gCandidateCount = 0;

// Letzter erkannter Zustandswechsel
Zustand  gLetzterWechselState   = UNKLAR;
time_t   gLetzterWechselZeit    = 0;
bool     gWechselBildVorhanden  = false;

// Kalibrierung / gelerntes Modell
KalibrierDaten gKal;
bool     gKalibriert         = false;
uint16_t gAnzahl[3]          = {0, 0, 0};
float    gZentroid[3][3];     // [Zustand][Merkmal] -> gelernter Mittelpunkt
float    gRadius[3];          // typische Streuung je Zustand (fuer Konfidenz)

// Trainingsmodus: aktuell betrachtetes Bild + dessen Merkmale
float     gTrainFeatures[3] = {0, 0, 0};
bool      gTrainGueltig     = false;
uint8_t*  gTrainFrameBuf    = nullptr;   // JPEG-Kopie des aktuellen Trainingsbildes
size_t    gTrainFrameLen    = 0;

bool          gSDok          = false;     // ist eine SD-Karte verfuegbar?
unsigned long gLetzteAnalyse = 0;         // Zeitpunkt der letzten Analyse (millis)

// ===========================================================================
//  FUNKTIONS-PROTOTYPEN
// ===========================================================================
bool        analysiereFrame(camera_fb_t* fb, float feat[3]);
void        klassifiziere(const float f[3], Zustand* outState, int* outConf);
void        heuristik(const float f[3], Zustand* s, int* p);
void        berechneModell();
void        trainingLabel(int idx);
void        trainingReset();
void        speichereKalibrierung();
void        ladeKalibrierung();
void        speichereWechselBild(const uint8_t* d, size_t n);
void        speichereWechselInfo();
void        ladeLetzterWechsel();
void        logSD(Zustand st, int conf, bool wechsel);
String      zeitText(time_t t);
const char* zustandText(Zustand z);
const char* zustandKlasse(Zustand z);
const char* zustandKurz(Zustand z);
void        analyseZyklus();
void        starteWebserver();
void        initKamera();
void        initOTA();

// ===========================================================================
//  HILFSFUNKTIONEN: Zustand -> Text
// ===========================================================================
const char* zustandKurz(Zustand z) {
  switch (z) { case ZU: return "ZU"; case OFFEN: return "OFFEN";
               case HALB: return "HALB"; default: return "UNKLAR"; }
}
const char* zustandText(Zustand z) {
  switch (z) { case ZU: return "TOR ZU"; case OFFEN: return "TOR OFFEN";
               case HALB: return "TOR HALB OFFEN"; default: return "UNKLAR"; }
}
const char* zustandKlasse(Zustand z) {   // entspricht den CSS-Klassen
  switch (z) { case ZU: return "zu"; case OFFEN: return "offen";
               case HALB: return "halb"; default: return "unklar"; }
}

// Wandelt eine Epochenzeit in lesbaren deutschen Zeitstempel um.
String zeitText(time_t t) {
  if (t < 1700000000) return String("unbekannt");   // Zeit noch nicht per NTP gesetzt
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[32];
  strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", &tmv);
  return String(buf);
}

// ===========================================================================
//  BILDANALYSE
// ---------------------------------------------------------------------------
//  Das JPEG-Bild wird in RGB dekodiert und in drei horizontale Zonen
//  (oben / mitte / unten) aufgeteilt. Aus den Helligkeiten dieser Zonen
//  werden drei MERKMALE berechnet:
//    feat[0] = mittlere Helligkeit gesamt        (0..1)
//    feat[1] = Spreizung max-min zwischen Zonen   (0..1)  -> Gleichmaessigkeit
//    feat[2] = Gradient unten-oben                (-1..1) -> "Lichtstreifen unten"
//  Indem wir vor allem auf Spreizung und Gradient achten (statt auf die
//  absolute Helligkeit), wird die Erkennung deutlich robuster gegen Gegenlicht.
// ===========================================================================
bool analysiereFrame(camera_fb_t* fb, float feat[3]) {
  if (!fb || fb->format != PIXFORMAT_JPEG) return false;
  int w = fb->width, h = fb->height;
  if (w <= 0 || h <= 0) return false;

  // RGB-Puffer im PSRAM anlegen (3 Byte pro Pixel)
  uint8_t* rgb = (uint8_t*) ps_malloc((size_t)w * h * 3);
  if (!rgb) return false;

  bool ok = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb);
  if (!ok) { free(rgb); return false; }

  double sum[3] = {0, 0, 0};
  long   cnt[3] = {0, 0, 0};
  int z1 = h / 3, z2 = (2 * h) / 3;        // Zonengrenzen
  int step = ANALYSE_SUBSAMPLE;

  for (int y = 0; y < h; y += step) {
    int zone = (y < z1) ? 0 : ((y < z2) ? 1 : 2);   // 0=oben 1=mitte 2=unten
    const uint8_t* row = rgb + (size_t)y * w * 3;
    for (int x = 0; x < w; x += step) {
      const uint8_t* p = row + (size_t)x * 3;
      int lum = (p[0] + p[1] + p[2]) / 3;    // einfache Helligkeit
      sum[zone] += lum;
      cnt[zone] += 1;
    }
  }
  free(rgb);

  if (cnt[0] == 0 || cnt[1] == 0 || cnt[2] == 0) return false;
  float Lo = (float)(sum[0] / cnt[0]);   // Helligkeit oben
  float Lm = (float)(sum[1] / cnt[1]);   // Helligkeit mitte
  float Lu = (float)(sum[2] / cnt[2]);   // Helligkeit unten

  float mean   = (Lo + Lm + Lu) / 3.0f;
  float mx     = max(Lo, max(Lm, Lu));
  float mn     = min(Lo, min(Lm, Lu));
  float spread = mx - mn;
  float grad   = Lu - Lo;                // positiv = unten heller als oben

  feat[0] = mean   / 255.0f;             // 0..1
  feat[1] = spread / 255.0f;             // 0..1
  feat[2] = grad   / 255.0f;             // -1..1
  return true;
}

// ===========================================================================
//  KLASSIFIKATION
// ---------------------------------------------------------------------------
//  Ist das Geraet noch nicht kalibriert, wird eine grobe Heuristik benutzt.
//  Nach dem Training wird der naechste gelernte "Mittelpunkt" (Zentroid)
//  gesucht. Die Konfidenz ergibt sich aus dem Abstand zum besten und
//  zweitbesten Treffer.
// ===========================================================================
void heuristik(const float f[3], Zustand* s, int* p) {
  float mean = f[0], spread = f[1], grad = f[2];
  // Deutlicher Unterschied + heller Streifen unten -> halb offen
  if (spread > HEUR_SPREAD_DEUTL && grad > HEUR_GRAD_HALB) { *s = HALB; *p = 55; return; }
  // Sehr gleichmaessige Verteilung -> ueber Helligkeit zu/offen entscheiden
  if (spread < HEUR_SPREAD_UNIFORM) {
    if (mean > HEUR_HELL_OFFEN) { *s = OFFEN; *p = 55; }
    else                        { *s = ZU;    *p = 55; }
    return;
  }
  // Mittlere Lage: nur grob entscheiden
  if (mean > HEUR_HELL_OFFEN) { *s = OFFEN; *p = 45; return; }
  *s = UNKLAR; *p = 30;
}

void klassifiziere(const float f[3], Zustand* outState, int* outConf) {
  // --- noch nicht kalibriert: Heuristik ---
  if (!gKalibriert) { heuristik(f, outState, outConf); return; }

  // --- kalibriert: naechster Zentroid (Abstand durch Radius normiert) ---
  float bestD = 1e9f, secD = 1e9f;
  int   bestI = -1;
  for (int c = 0; c < 3; c++) {
    if (gAnzahl[c] == 0) continue;
    float d = 0;
    for (int k = 0; k < 3; k++) { float dd = f[k] - gZentroid[c][k]; d += dd * dd; }
    d = sqrtf(d);
    float dn = d / (gRadius[c] + 0.02f);   // auf typische Streuung normiert
    if (dn < bestD) { secD = bestD; bestD = dn; bestI = c; }
    else if (dn < secD) { secD = dn; }
  }
  if (bestI < 0) { *outState = UNKLAR; *outConf = 0; return; }

  float conf;
  if (secD >= 1e9f) conf = (bestD < 1.5f) ? 0.8f : 0.4f;   // nur eine Klasse vorhanden
  else              conf = secD / (bestD + secD);          // 0.5 .. 1.0
  if (bestD > 3.0f) conf *= 0.4f;                          // Bild passt zu keiner Referenz

  int proz = (int)(conf * 100.0f + 0.5f);   // kaufmaennisch runden
  if (proz < 0) proz = 0; if (proz > 100) proz = 100;

  if (proz < KONFIDENZ_MIN_PROZENT) { *outState = UNKLAR; *outConf = proz; return; }
  *outState = (Zustand)bestI;
  *outConf  = proz;
}

// ===========================================================================
//  MODELL BERECHNEN  (aus den gespeicherten Summen)
// ===========================================================================
void berechneModell() {
  gKalibriert = (gKal.kalibriert != 0);
  for (int c = 0; c < 3; c++) {
    gAnzahl[c] = gKal.anzahl[c];
    if (gKal.anzahl[c] > 0) {
      double n = (double)gKal.anzahl[c];
      double var = 0;
      for (int k = 0; k < 3; k++) {
        double m = gKal.summe[c][k] / n;
        gZentroid[c][k] = (float)m;
        double v = gKal.summeQuad[c][k] / n - m * m;   // Varianz je Merkmal
        if (v < 0) v = 0;
        var += v;
      }
      gRadius[c] = (float)sqrt(var);          // mittlere Streuung (RMS)
      if (gRadius[c] < 0.03f) gRadius[c] = 0.03f;   // Mindestradius
    } else {
      gZentroid[c][0] = gZentroid[c][1] = gZentroid[c][2] = 0;
      gRadius[c] = 0.1f;
    }
  }
}

// ===========================================================================
//  TRAINING: ein Bild bestaetigen / Training zuruecksetzen
// ===========================================================================
void trainingLabel(int idx) {
  if (idx < 0 || idx > 2) return;
  for (int k = 0; k < 3; k++) {
    gKal.summe[idx][k]     += gTrainFeatures[k];
    gKal.summeQuad[idx][k] += (double)gTrainFeatures[k] * gTrainFeatures[k];
  }
  gKal.anzahl[idx]++;
  // Sind alle drei Zustaende ausreichend trainiert -> Kalibrierung fertig
  if (gKal.anzahl[0] >= TRAINING_ZIEL &&
      gKal.anzahl[1] >= TRAINING_ZIEL &&
      gKal.anzahl[2] >= TRAINING_ZIEL) {
    gKal.kalibriert = 1;
  }
  berechneModell();
  speichereKalibrierung();
}

void trainingReset() {
  memset(&gKal, 0, sizeof(gKal));
  gKal.magic   = KAL_MAGIC;
  gKal.version = KAL_VERSION;
  gKal.kalibriert = 0;
  berechneModell();
  speichereKalibrierung();
}

// ===========================================================================
//  SPEICHERN / LADEN im SPIFFS
// ===========================================================================
void speichereKalibrierung() {
  xSemaphoreTake(gFsMutex, portMAX_DELAY);
  File f = SPIFFS.open(DATEI_KALIBRIERUNG, "w");
  if (f) { f.write((const uint8_t*)&gKal, sizeof(gKal)); f.close(); }
  xSemaphoreGive(gFsMutex);
}

void ladeKalibrierung() {
  bool ok = false;
  xSemaphoreTake(gFsMutex, portMAX_DELAY);
  File f = SPIFFS.open(DATEI_KALIBRIERUNG, "r");
  if (f && f.size() == sizeof(KalibrierDaten)) {
    f.read((uint8_t*)&gKal, sizeof(gKal));
    if (gKal.magic == KAL_MAGIC && gKal.version == KAL_VERSION) ok = true;
  }
  if (f) f.close();
  xSemaphoreGive(gFsMutex);

  if (!ok) {                       // keine/ungueltige Daten -> neu initialisieren
    memset(&gKal, 0, sizeof(gKal));
    gKal.magic = KAL_MAGIC; gKal.version = KAL_VERSION; gKal.kalibriert = 0;
  }
  berechneModell();
}

void speichereWechselBild(const uint8_t* d, size_t n) {
  xSemaphoreTake(gFsMutex, portMAX_DELAY);
  File f = SPIFFS.open(DATEI_WECHSEL_BILD, "w");
  if (f) { f.write(d, n); f.close(); gWechselBildVorhanden = true; }
  xSemaphoreGive(gFsMutex);
}

void speichereWechselInfo() {
  xSemaphoreTake(gFsMutex, portMAX_DELAY);
  File f = SPIFFS.open(DATEI_WECHSEL_INFO, "w");
  if (f) { f.printf("%ld;%d\n", (long)gLetzterWechselZeit, (int)gLetzterWechselState); f.close(); }
  xSemaphoreGive(gFsMutex);
}

void ladeLetzterWechsel() {
  xSemaphoreTake(gFsMutex, portMAX_DELAY);
  File f = SPIFFS.open(DATEI_WECHSEL_INFO, "r");
  if (f) {
    String line = f.readStringUntil('\n');
    f.close();
    int sep = line.indexOf(';');
    if (sep > 0) {
      gLetzterWechselZeit  = (time_t)line.substring(0, sep).toInt();
      gLetzterWechselState = (Zustand)line.substring(sep + 1).toInt();
    }
  }
  File b = SPIFFS.open(DATEI_WECHSEL_BILD, "r");
  if (b) { if (b.size() > 0) gWechselBildVorhanden = true; b.close(); }
  xSemaphoreGive(gFsMutex);
}

// ===========================================================================
//  LOG auf SD-Karte (CSV). Laeuft nur, wenn eine SD-Karte erkannt wurde.
// ===========================================================================
void logSD(Zustand st, int conf, bool wechsel) {
  if (!gSDok) return;
  File f = SD_MMC.open(SD_LOG_DATEI, FILE_APPEND);
  if (!f) return;
  f.printf("%s;%s;%d;%s;%s\n",
           zeitText(time(nullptr)).c_str(),
           zustandKurz(st), conf,
           gKalibriert ? "kalibriert" : "unkalibriert",
           wechsel ? "WECHSEL" : "");
  f.close();
}

// ===========================================================================
//  ANALYSEZYKLUS  (alle 20 s aus loop() aufgerufen)
// ===========================================================================
void analyseZyklus() {
  float feat[3]; bool ok = false;
  uint8_t* jpgCopy = nullptr; size_t jpgLen = 0;

  // --- Kamerazugriff (geschuetzt durch Mutex) ---
  xSemaphoreTake(gCamMutex, portMAX_DELAY);
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) {
    ok = analysiereFrame(fb, feat);
    // JPEG kopieren, um es spaeter (ausserhalb des Mutex) speichern zu koennen
    jpgLen  = fb->len;
    jpgCopy = (uint8_t*) ps_malloc(jpgLen);
    if (jpgCopy) memcpy(jpgCopy, fb->buf, jpgLen); else jpgLen = 0;
    esp_camera_fb_return(fb);
  }
  xSemaphoreGive(gCamMutex);

  // --- Klassifizieren ---
  Zustand st = UNKLAR; int conf = 0;
  if (ok) klassifiziere(feat, &st, &conf);
  gCurrentState = st;
  gCurrentConf  = conf;
  gLetzteAnalyseZeit = time(nullptr);

  // --- Zustandswechsel erkennen (mit Entprellung) ---
  bool wechsel = false;
  if (ok && st != UNKLAR && conf >= KONFIDENZ_MIN_PROZENT) {
    if (st == gStableState) {
      gCandidateCount = 0;                 // nichts Neues
    } else {
      if (st == gCandidate) gCandidateCount++;
      else { gCandidate = st; gCandidateCount = 1; }
      if (gCandidateCount >= WECHSEL_DEBOUNCE) {
        gStableState    = st;              // Wechsel bestaetigt
        gCandidateCount = 0;
        wechsel = true;
      }
    }
  }

  // --- Bei Wechsel: Foto + Info speichern ---
  if (wechsel && jpgCopy && jpgLen > 0) {
    gLetzterWechselState = gStableState;
    gLetzterWechselZeit  = time(nullptr);
    speichereWechselBild(jpgCopy, jpgLen);
    speichereWechselInfo();
    // --- Optional MQTT: hier den Zustand veroeffentlichen ---
    // if (mqttClient.connected())
    //   mqttClient.publish(MQTT_TOPIC, zustandKurz(gStableState), true);
  }
  if (jpgCopy) free(jpgCopy);

  logSD(st, conf, wechsel);
}

// ===========================================================================
//  WEB-HANDLER
// ===========================================================================

// ----- Uebersichtsseite "/" -----
static esp_err_t handleIndex(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

// ----- Trainingsseite "/train" -----
static esp_err_t handleTrain(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, TRAIN_HTML, HTTPD_RESP_USE_STRLEN);
}

// ----- Status als JSON "/status" -----
static esp_err_t handleStatus(httpd_req_t* req) {
  String j = "{";
  j += "\"zustandText\":\"" + String(zustandText((Zustand)gCurrentState)) + "\",";
  j += "\"klasse\":\""      + String(zustandKlasse((Zustand)gCurrentState)) + "\",";
  j += "\"konfidenz\":"     + String(gCurrentConf) + ",";
  j += "\"kalibriert\":"    + String(gKalibriert ? "true" : "false") + ",";
  j += "\"zeit\":\""        + zeitText(gLetzteAnalyseZeit) + "\",";
  j += "\"wechselZustand\":\"" + String(zustandText(gLetzterWechselState)) + "\",";
  j += "\"wechselZeit\":\""    + zeitText(gLetzterWechselZeit) + "\",";
  j += "\"wechselBild\":"      + String(gWechselBildVorhanden ? "true" : "false");
  j += "}";
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, j.c_str(), j.length());
}

// ----- Foto des letzten Wechsels aus SPIFFS "/letzter_wechsel.jpg" -----
static esp_err_t handleWechselBild(httpd_req_t* req) {
  xSemaphoreTake(gFsMutex, portMAX_DELAY);
  File f = SPIFFS.open(DATEI_WECHSEL_BILD, "r");
  if (!f || f.size() == 0) {
    if (f) f.close();
    xSemaphoreGive(gFsMutex);
    httpd_resp_send_404(req);
    return ESP_OK;
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  uint8_t buf[1024]; size_t n;
  while ((n = f.read(buf, sizeof(buf))) > 0) {
    if (httpd_resp_send_chunk(req, (const char*)buf, n) != ESP_OK) break;
  }
  f.close();
  httpd_resp_send_chunk(req, NULL, 0);    // Chunk-Ende
  xSemaphoreGive(gFsMutex);
  return ESP_OK;
}

// ----- Trainings-Poll: neues Bild aufnehmen + einschaetzen "/train/poll" -----
static esp_err_t handleTrainPoll(httpd_req_t* req) {
  float feat[3]; bool ok = false;

  xSemaphoreTake(gCamMutex, portMAX_DELAY);
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) {
    ok = analysiereFrame(fb, feat);
    // aktuelles Bild als JPEG-Kopie merken (fuer /train/bild.jpg)
    if (gTrainFrameBuf) { free(gTrainFrameBuf); gTrainFrameBuf = nullptr; gTrainFrameLen = 0; }
    gTrainFrameBuf = (uint8_t*) ps_malloc(fb->len);
    if (gTrainFrameBuf) { memcpy(gTrainFrameBuf, fb->buf, fb->len); gTrainFrameLen = fb->len; }
    esp_camera_fb_return(fb);
  }
  xSemaphoreGive(gCamMutex);

  Zustand st = UNKLAR; int conf = 0;
  if (ok) {
    klassifiziere(feat, &st, &conf);
    for (int k = 0; k < 3; k++) gTrainFeatures[k] = feat[k];
    gTrainGueltig = true;
  } else {
    gTrainGueltig = false;
  }

  bool fertig = (gKal.anzahl[0] >= TRAINING_ZIEL &&
                 gKal.anzahl[1] >= TRAINING_ZIEL &&
                 gKal.anzahl[2] >= TRAINING_ZIEL);

  String j = "{";
  j += "\"zustandText\":\"" + String(zustandText(st)) + "\",";
  j += "\"klasse\":\""      + String(zustandKlasse(st)) + "\",";
  j += "\"konfidenz\":"     + String(conf) + ",";
  j += "\"gueltig\":"       + String(gTrainGueltig ? "true" : "false") + ",";
  j += "\"zu\":"            + String(gKal.anzahl[0]) + ",";
  j += "\"offen\":"         + String(gKal.anzahl[1]) + ",";
  j += "\"halb\":"          + String(gKal.anzahl[2]) + ",";
  j += "\"ziel\":"          + String(TRAINING_ZIEL) + ",";
  j += "\"kalibriert\":"    + String(gKalibriert ? "true" : "false") + ",";
  j += "\"fertig\":"        + String(fertig ? "true" : "false");
  j += "}";
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, j.c_str(), j.length());
}

// ----- aktuelles Trainingsbild "/train/bild.jpg" -----
static esp_err_t handleTrainBild(httpd_req_t* req) {
  xSemaphoreTake(gCamMutex, portMAX_DELAY);
  esp_err_t r = ESP_OK;
  if (gTrainFrameBuf && gTrainFrameLen > 0) {
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    r = httpd_resp_send(req, (const char*)gTrainFrameBuf, gTrainFrameLen);
  } else {
    httpd_resp_send_404(req);
  }
  xSemaphoreGive(gCamMutex);
  return r;
}

// ----- Trainings-Label setzen "/train/label?zustand=zu|offen|halb|skip" -----
static esp_err_t handleTrainLabel(httpd_req_t* req) {
  char query[64]; char val[16];
  int idx = -1; bool skip = false;
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    if (httpd_query_key_value(query, "zustand", val, sizeof(val)) == ESP_OK) {
      if      (!strcmp(val, "zu"))    idx = 0;
      else if (!strcmp(val, "offen")) idx = 1;
      else if (!strcmp(val, "halb"))  idx = 2;
      else if (!strcmp(val, "skip"))  skip = true;
    }
  }
  if (!skip && idx >= 0 && gTrainGueltig) trainingLabel(idx);

  // einfache Bestaetigung zurueckgeben
  String j = String("{\"ok\":true,\"zu\":") + gKal.anzahl[0] +
             ",\"offen\":" + gKal.anzahl[1] +
             ",\"halb\":"  + gKal.anzahl[2] + "}";
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  return httpd_resp_send(req, j.c_str(), j.length());
}

// ----- Training zuruecksetzen "/train/reset" -----
static esp_err_t handleTrainReset(httpd_req_t* req) {
  trainingReset();
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

// ----- MJPEG-Livestream "/stream" (laeuft auf dem Stream-Server, Port 81) ----
static esp_err_t handleStream(httpd_req_t* req) {
  static const char* BOUNDARY  = "\r\n--frame\r\n";
  static const char* PART_HEAD = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
  char head[64];

  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  // Wiederverwendbarer Kopierpuffer im PSRAM (waechst bei Bedarf).
  // Wichtig: Wir kopieren das JPEG nur KURZ unter dem Mutex und senden es
  // danach OHNE Mutex. So kann ein langsamer Browser-Client niemals die
  // Bildanalyse oder die Trainings-Endpunkte blockieren.
  uint8_t* buf = nullptr;
  size_t   bufCap = 0;

  while (true) {
    // 1) Frame holen und kopieren (nur hier ist der Kamera-Mutex aktiv)
    size_t len = 0; bool ok = false;
    xSemaphoreTake(gCamMutex, portMAX_DELAY);
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      if (fb->len > bufCap) {
        uint8_t* nb = (uint8_t*) ps_realloc(buf, fb->len);
        if (nb) { buf = nb; bufCap = fb->len; }
      }
      if (buf && bufCap >= fb->len) { memcpy(buf, fb->buf, fb->len); len = fb->len; ok = true; }
      esp_camera_fb_return(fb);
    }
    xSemaphoreGive(gCamMutex);
    if (!ok) break;                      // Kamerafehler -> Stream beenden

    // 2) Senden ohne Mutex
    esp_err_t res = httpd_resp_send_chunk(req, BOUNDARY, strlen(BOUNDARY));
    if (res == ESP_OK) {
      int l = snprintf(head, sizeof(head), PART_HEAD, (unsigned)len);
      res = httpd_resp_send_chunk(req, head, l);
    }
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)buf, len);
    if (res != ESP_OK) break;            // Client hat die Verbindung beendet

    vTaskDelay(pdMS_TO_TICKS(60));       // ~15 fps und entlastet die Kamera
  }
  if (buf) free(buf);
  return ESP_OK;
}

// ===========================================================================
//  WEBSERVER STARTEN  (zwei Server: Port 80 = Seiten, Port 81 = Stream)
// ===========================================================================
void starteWebserver() {
  // --- Hauptserver auf Port 80 ---
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port      = 80;
  cfg.ctrl_port        = 32768;
  cfg.max_uri_handlers = 16;
  cfg.stack_size       = 8192;
  cfg.lru_purge_enable = true;

  if (httpd_start(&gServer, &cfg) == ESP_OK) {
    httpd_uri_t routen[] = {
      { "/",                    HTTP_GET, handleIndex,       NULL },
      { "/status",              HTTP_GET, handleStatus,      NULL },
      { "/letzter_wechsel.jpg", HTTP_GET, handleWechselBild, NULL },
      { "/train",               HTTP_GET, handleTrain,       NULL },
      { "/train/poll",          HTTP_GET, handleTrainPoll,   NULL },
      { "/train/bild.jpg",      HTTP_GET, handleTrainBild,   NULL },
      { "/train/label",         HTTP_GET, handleTrainLabel,  NULL },
      { "/train/reset",         HTTP_GET, handleTrainReset,  NULL },
    };
    for (auto& r : routen) httpd_register_uri_handler(gServer, &r);
  }

  // --- Stream-Server auf Port 81 (eigener Task, blockiert die Seiten nicht) ---
  httpd_config_t scfg = HTTPD_DEFAULT_CONFIG();
  scfg.server_port      = 81;
  scfg.ctrl_port        = 32769;            // muss sich vom Hauptserver unterscheiden
  scfg.max_uri_handlers = 2;
  scfg.stack_size       = 8192;

  if (httpd_start(&gStreamServer, &scfg) == ESP_OK) {
    httpd_uri_t s = { "/stream", HTTP_GET, handleStream, NULL };
    httpd_register_uri_handler(gStreamServer, &s);
  }
}

// ===========================================================================
//  KAMERA INITIALISIEREN
// ===========================================================================
void initKamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size   = KAMERA_FRAMESIZE;
  config.pixel_format = PIXFORMAT_JPEG;     // JPEG fuer den Stream
  config.grab_mode    = CAMERA_GRAB_LATEST;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = KAMERA_JPEG_QUALI;
  config.fb_count     = KAMERA_FB_COUNT;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("FEHLER: Kamera-Init fehlgeschlagen (0x%x). Verkabelung/Board pruefen!\n", err);
    return;
  }

  // Sensor-Feineinstellungen (helfen bei schwierigem Licht / Gegenlicht)
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_vflip(s, KAMERA_VFLIP);
    s->set_hmirror(s, KAMERA_HMIRROR);
    s->set_gain_ctrl(s, 1);        // automatische Verstaerkung
    s->set_exposure_ctrl(s, 1);    // automatische Belichtung
    s->set_whitebal(s, 1);         // automatischer Weissabgleich
  }
  Serial.println("Kamera bereit.");
}

// ===========================================================================
//  OTA-UPDATE INITIALISIEREN
// ---------------------------------------------------------------------------
//  Ermoeglicht das Hochladen neuer Firmware ueber WLAN (Arduino IDE ->
//  "Netzwerk-Port"). Wird nur aufgerufen, wenn das WLAN verbunden ist.
//  Voraussetzung: ein Partition-Schema mit zwei App-Bereichen (OTA-faehig),
//  z. B. "8M with spiffs (3MB APP/1.5MB SPIFFS)" (siehe README).
// ===========================================================================
void initOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  // Passwort nur setzen, wenn in secrets.h eines konfiguriert wurde
  if (strlen(OTA_PASSWORT) > 0) ArduinoOTA.setPassword(OTA_PASSWORT);

  ArduinoOTA.onStart([]() {
    // Wird das Dateisystem aktualisiert, muss SPIFFS vorher ausgehaengt werden
    if (ArduinoOTA.getCommand() == U_SPIFFS) SPIFFS.end();
    String typ = (ArduinoOTA.getCommand() == U_FLASH) ? "Firmware" : "Dateisystem";
    Serial.println("OTA-Update startet (" + typ + ") ...");
  });
  ArduinoOTA.onProgress([](unsigned int fortschritt, unsigned int gesamt) {
    Serial.printf("OTA: %u%%\r", (gesamt > 0) ? (fortschritt * 100) / gesamt : 0);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA-Update abgeschlossen. Geraet startet neu ...");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA-Fehler [%u]: ", error);
    if      (error == OTA_AUTH_ERROR)    Serial.println("Authentifizierung fehlgeschlagen");
    else if (error == OTA_BEGIN_ERROR)   Serial.println("Start fehlgeschlagen");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Verbindung fehlgeschlagen");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Empfang fehlgeschlagen");
    else if (error == OTA_END_ERROR)     Serial.println("Abschluss fehlgeschlagen");
  });

  ArduinoOTA.begin();
  Serial.printf("OTA bereit. Geraetename in der IDE: %s\n", OTA_HOSTNAME);
}

// ===========================================================================
//  SETUP
// ===========================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Garagentor-Zustandserkennung startet ===");

  // Mutexe anlegen
  gCamMutex = xSemaphoreCreateMutex();
  gFsMutex  = xSemaphoreCreateMutex();

  // SPIFFS starten (true = bei Bedarf formatieren)
  if (!SPIFFS.begin(true)) {
    Serial.println("WARNUNG: SPIFFS konnte nicht gestartet werden (Partition Scheme pruefen).");
  } else {
    Serial.println("SPIFFS bereit.");
  }

  // Gespeicherte Kalibrierung + letzten Wechsel laden
  ladeKalibrierung();
  ladeLetzterWechsel();
  Serial.printf("Kalibrierung: %s (zu:%u offen:%u halb:%u)\n",
                gKalibriert ? "vorhanden" : "noch keine",
                gKal.anzahl[0], gKal.anzahl[1], gKal.anzahl[2]);

  // Kamera
  initKamera();

  // WLAN verbinden
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);   // bei Verbindungsabbruch automatisch neu verbinden
  WiFi.begin(WLAN_SSID, WLAN_PASSWORT);
  Serial.printf("Verbinde mit WLAN \"%s\" ", WLAN_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WLAN_TIMEOUT_MS) {
    delay(500); Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WLAN verbunden. IP-Adresse: ");
    Serial.println(WiFi.localIP());
    if (MDNS.begin(HOSTNAME)) {
      Serial.printf("Erreichbar auch ueber: http://%s.local\n", HOSTNAME);
    }
    // Zeit per NTP holen (fuer die Zeitstempel)
    configTzTime(ZEITZONE, NTP_SERVER);
    // OTA-Updates ueber WLAN aktivieren
    initOTA();
  } else {
    Serial.println("WLAN NICHT verbunden! Bitte SSID/Passwort in secrets.h pruefen.");
    Serial.println("Das Geraet laeuft weiter, ist aber nicht im Netz erreichbar.");
  }

  // SD-Karte (optional, fuer Logs) – Freenove ESP32-S3 nutzt SD_MMC (1-Bit)
  SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN);
  if (SD_MMC.begin("/sdcard", true)) {     // true = 1-Bit-Modus
    gSDok = true;
    Serial.println("SD-Karte erkannt – Logs werden geschrieben.");
    // Kopfzeile schreiben, falls die Logdatei noch nicht existiert
    if (!SD_MMC.exists(SD_LOG_DATEI)) {
      File f = SD_MMC.open(SD_LOG_DATEI, FILE_WRITE);
      if (f) { f.println("Zeit;Zustand;Konfidenz;Status;Ereignis"); f.close(); }
    }
  } else {
    Serial.println("Keine SD-Karte gefunden (Logs deaktiviert).");
  }

  // Webserver starten
  starteWebserver();
  Serial.println("Webserver laeuft. Im Browser die oben angezeigte IP-Adresse oeffnen.");

  // Optional MQTT verbinden:
  // mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  // mqttClient.connect(MQTT_CLIENT_ID);

  // Erste Analyse leicht verzoegert, damit sich die Kamera einpegeln kann
  gLetzteAnalyse = millis() - ANALYSE_INTERVALL_MS + 3000;
}

// ===========================================================================
//  LOOP
// ===========================================================================
void loop() {
  // OTA-Anfragen bearbeiten (laeuft nur, wenn OTA initialisiert wurde)
  ArduinoOTA.handle();

  unsigned long now = millis();
  if (now - gLetzteAnalyse >= ANALYSE_INTERVALL_MS) {
    gLetzteAnalyse = now;
    analyseZyklus();
  }

  // Optional MQTT am Leben halten:
  // if (WiFi.status() == WL_CONNECTED) {
  //   if (!mqttClient.connected()) mqttClient.connect(MQTT_CLIENT_ID);
  //   mqttClient.loop();
  // }

  delay(10);
}
