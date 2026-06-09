// ===========================================================================
//  config.h  –  ZENTRALE KONFIGURATION
// ---------------------------------------------------------------------------
//  Hier stehen ALLE einstellbaren Werte des Projekts.
//  Wer das Projekt anpassen will, muss normalerweise nur diese Datei ändern.
//  Alle Kommentare sind bewusst auf Deutsch gehalten.
// ===========================================================================
#pragma once

#include "esp_camera.h"   // wird für den Typ "framesize_t" benötigt
#include "secrets.h"      // ECHTE Zugangsdaten (liegt in .gitignore, nicht auf GitHub)

// ---------------------------------------------------------------------------
//  1) WLAN-ZUGANGSDATEN
// ---------------------------------------------------------------------------
//  Die ECHTEN WLAN-Daten stehen in der Datei "secrets.h" (nicht auf GitHub).
//  ACHTUNG: Der ESP32-S3 unterstützt nur 2,4-GHz-WLAN, KEIN 5 GHz!
const char* WLAN_SSID     = GEHEIM_WLAN_SSID;       // Name (SSID) – aus secrets.h
const char* WLAN_PASSWORT = GEHEIM_WLAN_PASSWORT;   // Passwort     – aus secrets.h

// Hostname für mDNS -> Aufruf im Browser auch über  http://garagentor.local
const char* HOSTNAME = "garagentor";

// Maximale Wartezeit beim WLAN-Verbinden, danach läuft das Gerät trotzdem weiter
const unsigned long WLAN_TIMEOUT_MS = 20000;     // 20 Sekunden

// ---------------------------------------------------------------------------
//  1b) OTA-UPDATE  (neue Firmware ueber WLAN hochladen, ohne USB-Kabel)
// ---------------------------------------------------------------------------
//  Name, unter dem das Geraet in der Arduino IDE als "Netzwerk-Port" erscheint.
//  Sollte mit HOSTNAME uebereinstimmen, damit der mDNS-Name eindeutig bleibt.
const char* OTA_HOSTNAME = "garagentor";
//  Optionales OTA-Passwort: schuetzt vor unbefugten Updates im Netzwerk.
//  Der echte Wert steht in "secrets.h" ("" = kein Passwort).
const char* OTA_PASSWORT = GEHEIM_OTA_PASSWORT;

// ---------------------------------------------------------------------------
//  2) ZEIT / NTP  (für die Zeitstempel der Zustandswechsel)
// ---------------------------------------------------------------------------
const char* NTP_SERVER = "pool.ntp.org";
// Zeitzone Deutschland inkl. automatischer Sommer-/Winterzeit-Umstellung
const char* ZEITZONE   = "CET-1CEST,M3.5.0,M10.5.0/3";

// ---------------------------------------------------------------------------
//  3) KAMERA-EINSTELLUNGEN
// ---------------------------------------------------------------------------
//  Auflösung: FRAMESIZE_VGA (640x480) ist ein guter Kompromiss aus Bildqualität,
//  Geschwindigkeit und Speicherbedarf bei der Analyse.
//  Mögliche Werte z.B.: FRAMESIZE_QVGA (320x240), FRAMESIZE_VGA (640x480),
//  FRAMESIZE_SVGA (800x600). Größer = schöneres Bild, aber langsamere Analyse.
const framesize_t KAMERA_FRAMESIZE   = FRAMESIZE_VGA;
const int         KAMERA_JPEG_QUALI  = 12;   // 10 (besser/größer) ... 30 (schlechter/kleiner)
const int         KAMERA_FB_COUNT    = 2;    // Anzahl Bildpuffer (2 = flüssiger Stream)

// Bild bei Bedarf drehen/spiegeln (je nach Montagelage der Kamera).
// Wenn das Tor "auf dem Kopf" oder seitenverkehrt erscheint, hier auf 1 setzen.
const int KAMERA_VFLIP   = 0;   // 0 = normal, 1 = vertikal spiegeln
const int KAMERA_HMIRROR = 0;   // 0 = normal, 1 = horizontal spiegeln

// ---------------------------------------------------------------------------
//  4) SD-KARTE (Freenove ESP32-S3, SD_MMC im 1-Bit-Modus)
// ---------------------------------------------------------------------------
#define SD_MMC_CLK_PIN 39
#define SD_MMC_CMD_PIN 38
#define SD_MMC_D0_PIN  40
const char* SD_LOG_DATEI = "/garagentor_log.csv";  // Logdatei auf der SD-Karte

// ---------------------------------------------------------------------------
//  5) ANALYSE / ZUSTANDSERKENNUNG
// ---------------------------------------------------------------------------
//  Im Normalbetrieb wird alle 20 Sekunden ein Bild analysiert.
const unsigned long ANALYSE_INTERVALL_MS = 20000;  // 20 Sekunden

//  Im Trainingsmodus holt der Browser alle 3 Sekunden ein neues Bild.
//  (Dieser Wert ist im Browser-Skript hinterlegt; hier nur zur Doku.)
const unsigned long TRAINING_INTERVALL_MS = 3000;  // 3 Sekunden

//  Beim Analysieren wird nicht jeder Pixel betrachtet, sondern nur jeder
//  n-te (spart Rechenzeit, Ergebnis bleibt praktisch identisch).
const int ANALYSE_SUBSAMPLE = 2;

//  Liegt die Konfidenz unter diesem Prozentwert, gilt der Zustand als "UNKLAR".
const int KONFIDENZ_MIN_PROZENT = 50;

//  Entprellung: Ein neuer Zustand muss so oft hintereinander erkannt werden,
//  bevor er als echter Zustandswechsel gilt (verhindert Flackern).
const uint8_t WECHSEL_DEBOUNCE = 2;

// ---------------------------------------------------------------------------
//  6) HEURISTIK (grobe Vor-Einschätzung, SOLANGE NOCH NICHT KALIBRIERT)
// ---------------------------------------------------------------------------
//  Diese Schwellenwerte sind nur eine grobe Startannahme. Nach dem Training
//  werden sie durch die gelernten Referenzwerte ersetzt. Alle Werte beziehen
//  sich auf normierte Helligkeit (0.0 = schwarz ... 1.0 = weiß).
const float HEUR_HELL_OFFEN     = 0.55f;  // ab dieser mittleren Helligkeit eher "offen"
const float HEUR_SPREAD_UNIFORM = 0.10f;  // darunter gilt die Verteilung als gleichmäßig
const float HEUR_SPREAD_DEUTL   = 0.18f;  // darüber gilt der Unterschied als deutlich
const float HEUR_GRAD_HALB      = 0.12f;  // heller Streifen unten -> "halb offen"

// ---------------------------------------------------------------------------
//  7) TRAINING
// ---------------------------------------------------------------------------
//  Anzahl benötigter Bestätigungen je Zustand, bis das Training abgeschlossen ist.
const uint16_t TRAINING_ZIEL = 30;

// ---------------------------------------------------------------------------
//  8) DATEINAMEN IM SPIFFS (interner Flash-Speicher, übersteht Neustart)
// ---------------------------------------------------------------------------
const char* DATEI_KALIBRIERUNG = "/kalibrierung.dat";   // gelernte Referenzwerte
const char* DATEI_WECHSEL_BILD = "/letzter_wechsel.jpg"; // Foto vom letzten Wechsel
const char* DATEI_WECHSEL_INFO = "/wechsel.txt";         // Zeit + Zustand des Wechsels

// ---------------------------------------------------------------------------
//  9) MQTT (OPTIONAL) – Konstanten hier, Code im .ino auskommentiert
// ---------------------------------------------------------------------------
//  Diese Werte werden nur gebraucht, wenn der MQTT-Block im Hauptprogramm
//  einkommentiert wird (siehe Garagentor_Zustandserkennung.ino).
const char*   MQTT_BROKER    = "192.168.1.50";       // IP-Adresse des MQTT-Brokers
const int     MQTT_PORT      = 1883;
const char*   MQTT_TOPIC     = "garage/tor/zustand";  // Topic für den Zustand
const char*   MQTT_CLIENT_ID = "garagentor-esp32s3";

// ===========================================================================
//  DATENTYPEN  (werden im ganzen Projekt verwendet)
// ===========================================================================

//  Die vier möglichen Zustände. Die Reihenfolge ZU/OFFEN/HALB entspricht
//  bewusst den Index-Nummern 0/1/2, die auch in der Kalibrierung benutzt werden.
enum Zustand { ZU = 0, OFFEN = 1, HALB = 2, UNKLAR = 3 };

//  Persistente Kalibrierungsdaten. Es werden KEINE Bilder gespeichert,
//  sondern nur Kennzahlen (Summen), aus denen die Referenzwerte berechnet werden.
//  "magic" und "version" dienen dazu, beim Laden ungültige/alte Daten zu erkennen.
#define KAL_MAGIC   0x47545A31UL   // "GTZ1"
#define KAL_VERSION 1

struct KalibrierDaten {
  uint32_t magic;            // Erkennungssignatur
  uint16_t version;          // Datenformat-Version
  uint8_t  kalibriert;       // 1 = Training abgeschlossen
  uint8_t  reserved;         // Platzhalter (Ausrichtung)
  uint16_t anzahl[3];        // Anzahl Bestätigungen je Zustand (zu, offen, halb)
  double   summe[3][3];      // Summe der Merkmale  [Zustand][Merkmal]
  double   summeQuad[3][3];  // Summe der Quadrate  (für die Streuung/Radius)
};
