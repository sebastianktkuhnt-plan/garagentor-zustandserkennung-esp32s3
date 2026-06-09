# Garagentor-Zustandserkennung mit ESP32-S3-CAM

Erkennt automatisch, ob ein Sektionaltor **zu**, **offen** oder **halb offen** ist –
allein anhand des Kamerabildes. Läuft **komplett auf dem ESP32**, kein externer
Server oder Cloud-Dienst nötig. Die Erkennung wird im Browser auf die eigene
Garage **trainiert** und ist dadurch robust gegen schwieriges Licht und Gegenlicht.

## Inhalt

- [Hardware](#hardware)
- [Funktionsprinzip](#funktionsprinzip)
- [Benötigte Bibliotheken](#benötigte-bibliotheken)
- [Board-Einstellungen in der Arduino IDE](#board-einstellungen-in-der-arduino-ide)
- [WLAN-Zugang eintragen](#wlan-zugang-eintragen)
- [Flashen (Hochladen)](#flashen-hochladen)
- [Erstinbetriebnahme](#erstinbetriebnahme)
- [Trainingsmodus – Schritt für Schritt](#trainingsmodus--schritt-für-schritt)
- [Das Webinterface im Überblick](#das-webinterface-im-überblick)
- [Firmware-Update über WLAN (OTA)](#firmware-update-über-wlan-ota)
- [Optional: MQTT aktivieren](#optional-mqtt-aktivieren)
- [Troubleshooting](#troubleshooting)
- [Dateien im Projekt](#dateien-im-projekt)
- [Lizenz](#lizenz)

---

## Hardware

| Komponente        | Wert / Hinweis                                              |
|-------------------|------------------------------------------------------------|
| Board             | **Freenove ESP32-S3-WROOM CAM** (OV2640, USB-C, 8 MB PSRAM, 8 MB Flash) |
| SD-Karte          | 1 GB, eingesteckt (für CSV-Logs, optional)                 |
| Stromversorgung   | USB-C, 5 V / 2 A                                            |
| Montage           | seitlich, ca. 2,5 m vom Tor, leicht angewinkelt            |

> **Wichtig:** Der ESP32-S3 unterstützt nur **2,4-GHz-WLAN**, kein 5 GHz.

> **Montage-Tipp:** Die Kamera muss fest und unbeweglich montiert sein. Verändert
> sich der Blickwinkel nachträglich, passt die Kalibrierung nicht mehr und das
> Training muss wiederholt werden.

---

## Funktionsprinzip

Die Erkennung verlässt sich **nicht** auf die absolute Helligkeit (die bei
Gegenlicht stark schwankt), sondern auf das **Helligkeitsmuster** im Bild:

1. Jedes Bild wird in **drei horizontale Zonen** geteilt: oben / mitte / unten.
2. Daraus werden drei Merkmale berechnet:
   - **mittlere Helligkeit** (gesamt)
   - **Spreizung** = Unterschied zwischen hellster und dunkelster Zone
   - **Gradient** = Helligkeit unten minus oben (erkennt einen Lichtstreifen unten)
3. Grobe Vorab-Logik (vor dem Training):
   - **Tor zu** → gleichmäßige Verteilung, keine helle Unterzone
   - **Tor offen** → gleichmäßig hell / Außenbereich sichtbar
   - **Tor halb** → deutlicher Unterschied zwischen den Zonen (Lichtstreifen unten)
   - **Unklar** → kein verwertbares Bild oder Konfidenz zu niedrig
4. Nach dem **Training** werden statt fester Schwellen die **gelernten
   Referenzwerte** der eigenen Garage benutzt. Das macht die Erkennung
   deutlich treffsicherer – gerade bei der angewinkelten Montage und Gegenlicht.

Die Referenzwerte sind reine **Kennzahlen** (keine Bilder) und werden im SPIFFS
gespeichert – sie **überleben einen Neustart**.

---

## Benötigte Bibliotheken

Das meiste ist bereits im **ESP32-Boardpaket** enthalten (Kamera, WebServer,
SPIFFS, SD_MMC, WiFi, **ESPmDNS, ArduinoOTA**). Du brauchst nur:

1. **ESP32-Boardpaket von Espressif** (Version **3.x** empfohlen, mind. 2.0.6):
   - Arduino IDE → `Datei` → `Voreinstellungen` → *Zusätzliche Boardverwalter-URLs*:
     ```
     https://espressif.github.io/arduino-esp32/package_esp32_index.json
     ```
   - Dann `Werkzeuge` → `Board` → `Boardverwalter…` → nach **esp32** suchen → installieren.

2. **Nur falls MQTT genutzt wird (optional):** `PubSubClient` von Nick O'Leary
   (`Werkzeuge` → `Bibliotheken verwalten…` → „PubSubClient" suchen → installieren).

> Es werden **keine** weiteren Bibliotheken benötigt.

---

## Board-Einstellungen in der Arduino IDE

Diese Einstellungen müssen im Menü **`Werkzeuge`** exakt so gesetzt sein:

| Einstellung           | Wert                                         |
|-----------------------|----------------------------------------------|
| Board                 | **ESP32S3 Dev Module**                       |
| USB CDC On Boot       | **Enabled**                                  |
| PSRAM                 | **OPI PSRAM**                                |
| Flash Size            | **8MB (64Mb)**                               |
| Partition Scheme      | **8M with spiffs (3MB APP/1.5MB SPIFFS)**    |
| Upload Speed          | 921600 (bei Problemen auf 115200 senken)     |

> Die Einstellung **PSRAM = OPI PSRAM** und das Partition-Schema **mit SPIFFS**
> sind zwingend nötig – sonst startet die Kamera nicht bzw. die Kalibrierung
> lässt sich nicht speichern.

---

## WLAN-Zugang eintragen

In der Datei **`config.h`** ganz oben SSID und Passwort eintragen:

```cpp
const char* WLAN_SSID     = "MeinWLAN";          // Name des WLANs
const char* WLAN_PASSWORT = "MeinWLANPasswort";  // WLAN-Passwort
```

Alle weiteren Einstellungen (Intervalle, Auflösung, Schwellenwerte, MQTT) stehen
ebenfalls in `config.h` und sind dort auf Deutsch kommentiert.

---

## Flashen (Hochladen)

1. Den Ordner `Garagentor_Zustandserkennung` öffnen und die Datei
   **`Garagentor_Zustandserkennung.ino`** in der Arduino IDE laden
   (die `.h`-Dateien werden automatisch mitgeladen).
2. Board per **USB-C** anschließen.
3. Unter `Werkzeuge` → `Port` den richtigen **COM-Port** wählen.
4. Auf **Hochladen** (Pfeil-Symbol) klicken.
5. Sollte der Upload nicht starten: **BOOT-Taste** gedrückt halten, kurz
   **RESET** tippen, BOOT loslassen → erneut hochladen.

Nach dem Hochladen den **Seriellen Monitor** (`Werkzeuge` → `Serieller Monitor`,
**115200 Baud**) öffnen, um die IP-Adresse abzulesen.

---

## Erstinbetriebnahme

1. Kamera fest in der Garage montieren (ca. 2,5 m vom Tor, angewinkelt).
2. Strom über USB-C anschließen.
3. Seriellen Monitor öffnen (115200 Baud). Es erscheint z. B.:
   ```
   WLAN verbunden. IP-Adresse: 192.168.1.123
   Erreichbar auch ueber: http://garagentor.local
   Webserver laeuft.
   ```
4. Diese **IP-Adresse** im Browser öffnen (z. B. `http://192.168.1.123`) –
   alternativ `http://garagentor.local`.
5. Du siehst das **Livebild**, den aktuellen Zustand und den Hinweis
   **„Noch nicht kalibriert"**. Die Erkennung läuft bereits grob, sollte aber
   jetzt trainiert werden (siehe nächster Abschnitt).

---

## Trainingsmodus – Schritt für Schritt

Der Trainingsmodus bringt dem ESP32 bei, wie **deine** Garage in den drei
Zuständen aussieht. Das ist der wichtigste Schritt für zuverlässige Erkennung.

1. Im Browser **`http://<IP-Adresse>/train`** öffnen
   (z. B. `http://192.168.1.123/train`).
2. Der ESP32 zeigt alle **3 Sekunden** ein neues Bild und darunter seine
   eigene Einschätzung, z. B. *„Ich meine: TOR ZU – 73 %"*.
3. Bringe das **Tor in einen bekannten Zustand** (z. B. ganz zu) und klicke den
   passenden Knopf:
   - **Tor zu**
   - **Tor offen**
   - **Tor halb**
   - **Bild überspringen** (wenn das Bild unbrauchbar ist)
4. Wiederhole das für alle drei Zustände. **Wichtig – bewusst variieren:**
   > *Bitte auch bei verschiedenen Lichtverhältnissen trainieren
   > (Nacht, Tag, Gegenlicht).*
   Je unterschiedlicher die Lichtsituationen beim Training, desto robuster
   erkennt das System später.
5. Die **Fortschrittsanzeige** zeigt z. B.:
   ```
   Tor zu: 12/30   -   Tor offen: 8/30   -   Tor halb: 5/30
   ```
6. Sobald von **jedem** Zustand **30 Bestätigungen** vorliegen, erscheint
   **„Training abgeschlossen – Kalibrierung gespeichert!"**. Die Referenzwerte
   werden automatisch berechnet und im SPIFFS gespeichert.
7. Ab jetzt nutzt die automatische Erkennung die gelernten Werte; der Hinweis
   „Noch nicht kalibriert" auf der Übersichtsseite verschwindet.

**Training zurücksetzen:** Unten auf der Trainingsseite gibt es den Knopf
**„Training zurücksetzen"** – damit werden alle gelernten Werte gelöscht und du
kannst neu beginnen (z. B. nach einem Umbau oder Verschieben der Kamera).

> **Tipp:** Du musst nicht alle 90 Bilder am Stück machen. Die Zählerstände
> bleiben gespeichert. Komm einfach zu verschiedenen Tageszeiten zurück, um die
> fehlenden Lichtsituationen zu ergänzen.

---

## Das Webinterface im Überblick

**Übersichtsseite** `http://<IP>/`
- **Livebild** (MJPEG-Stream, läuft technisch auf Port 81 – wird automatisch eingebunden)
- **Aktueller Zustand** groß und farbig:
  - 🟩 grün = **zu**
  - 🟥 rot = **offen**
  - 🟨 gelb = **halb**
  - ⬜ grau = **unklar**
- **Konfidenz** der aktuellen Erkennung in Prozent
- **Thumbnail des letzten Zustandswechsels** mit Zeitstempel
- Hinweis „Noch nicht kalibriert", solange das Training nicht abgeschlossen ist

**Trainingsseite** `http://<IP>/train` – siehe Abschnitt oben.

Die automatische Analyse läuft **alle 20 Sekunden**. Bei einem bestätigten
Zustandswechsel wird das aktuelle Foto als „letzter Wechsel" im SPIFFS
gespeichert (inkl. Zeitstempel) und – falls eine SD-Karte steckt – eine Zeile
in die Logdatei `garagentor_log.csv` geschrieben.

---

## Firmware-Update über WLAN (OTA)

Ist das Gerät einmal montiert, muss man zum Aktualisieren nicht mehr ans
USB-Kabel: Neue Firmware lässt sich bequem **über WLAN** hochladen (OTA =
*Over The Air*).

### Voraussetzungen

- Das Gerät ist mit dem WLAN verbunden (im Seriellen Monitor erscheint beim
  Start `OTA bereit. Geraetename in der IDE: garagentor`).
- Dein Computer ist im **selben Netzwerk**.
- Das gewählte **Partition-Schema ist OTA-fähig** – das empfohlene
  **`8M with spiffs (3MB APP/1.5MB SPIFFS)`** ist es bereits (zwei App-Bereiche).

### Einstellungen in `config.h`

```cpp
const char* OTA_HOSTNAME = "garagentor";   // Name in der IDE
const char* OTA_PASSWORT = "";             // "" = kein Passwort, sonst eintragen
```

> **Empfehlung:** Ein OTA-Passwort eintragen, damit niemand sonst im Netzwerk
> ungefragt Firmware aufspielen kann.

### Schritt für Schritt in der Arduino IDE

1. **Erstes Hochladen muss noch per USB erfolgen** – erst danach ist OTA aktiv.
2. Gerät einschalten und ca. 10–20 s warten, bis es im WLAN ist.
3. In der Arduino IDE: `Werkzeuge` → `Port`. Unter **„Netzwerk-Ports"** sollte
   nun **`garagentor at 192.168.x.x`** erscheinen. Diesen Port auswählen.
   > Erscheint kein Netzwerk-Port: IDE neu starten, kurz warten, sicherstellen,
   > dass PC und ESP32 im selben (2,4-GHz-)Netz sind. mDNS/Bonjour muss erlaubt sein.
4. Normal auf **Hochladen** klicken. Die Firmware wird über WLAN übertragen.
5. Wurde ein `OTA_PASSWORT` gesetzt, fragt die IDE einmalig danach.
6. Nach dem Update startet das Gerät automatisch neu. Im Seriellen Monitor (über
   USB) bzw. an den OTA-Fortschrittsmeldungen lässt sich der Verlauf verfolgen.

> **Hinweis:** Die gespeicherte **Kalibrierung bleibt bei einem normalen
> Firmware-Update erhalten** (sie liegt im separaten SPIFFS-Bereich, der beim
> Firmware-Upload nicht überschrieben wird).

---

## Optional: MQTT aktivieren

Der Code enthält einen **vollständigen, aber auskommentierten** MQTT-Block.
So aktivierst du ihn:

1. Bibliothek **PubSubClient** installieren (siehe oben).
2. In `config.h` die MQTT-Konstanten anpassen:
   ```cpp
   const char* MQTT_BROKER    = "192.168.1.50";
   const int   MQTT_PORT      = 1883;
   const char* MQTT_TOPIC     = "garage/tor/zustand";
   const char* MQTT_CLIENT_ID = "garagentor-esp32s3";
   ```
3. In `Garagentor_Zustandserkennung.ino` die mit `//` markierten MQTT-Zeilen
   einkommentieren (Suchbegriff **„MQTT"**): den `#include`, die zwei Client-Zeilen,
   die Verbindung in `setup()`, das `mqttClient.loop()` in `loop()` und das
   `mqttClient.publish(...)` im Analysezyklus.

Bei jedem bestätigten Zustandswechsel wird dann der Zustand (`ZU`, `OFFEN`,
`HALB`) als „retained"-Nachricht an das Topic gesendet.

---

## Troubleshooting

### WLAN wird nicht gefunden / keine Verbindung
- **2,4 GHz prüfen:** Der ESP32-S3 kann **kein 5-GHz-WLAN**. Viele Router senden
  beide Bänder unter gleichem Namen – ggf. ein separates 2,4-GHz-Netz einrichten.
- **SSID/Passwort** in `config.h` exakt prüfen (Groß-/Kleinschreibung, Sonderzeichen).
- Im **Seriellen Monitor** (115200 Baud) erscheint die Meldung
  „WLAN NICHT verbunden!" – dann stimmen die Zugangsdaten oder das Band nicht.
- **Reichweite/Abschirmung:** Garagen sind oft WLAN-ungünstig. Näher an den
  Router gehen zum Testen, ggf. Repeater/Mesh nutzen.
- Nach Änderung der Zugangsdaten neu hochladen oder RESET drücken.

### Kein Bild / Stream bleibt schwarz
- **Board-Einstellungen** prüfen: **PSRAM = OPI PSRAM** ist Pflicht. Ohne PSRAM
  startet die Kamera nicht. Im Seriellen Monitor erscheint sonst
  „Kamera-Init fehlgeschlagen".
- **Richtiges Board** gewählt? „ESP32S3 Dev Module".
- **Kamerakabel/Flachbandkabel** am Modul fest und richtig herum eingesteckt?
- Nach dem Flashen einmal **RESET** drücken und 5–10 s warten.
- Stream wird über **Port 81** geladen. Wird die Seite über `http://...local`
  geöffnet und das Bild bleibt leer, stattdessen die **IP-Adresse direkt**
  verwenden (`http://192.168.1.123`).
- Stromversorgung: **5 V / 2 A** verwenden. Zu schwache USB-Quellen führen zu
  Bildaussetzern oder Neustarts.

### Zustand ist immer „unklar"
- **Noch nicht kalibriert?** Vor dem Training ist die Erkennung nur grob. Führe
  den **Trainingsmodus** vollständig durch (30 Bestätigungen je Zustand).
- **Bild zu dunkel/zu hell:** Bei extremem Gegenlicht kann die Automatik an
  Grenzen kommen. Trainiere bewusst **auch in dieser Lichtsituation**.
- **Kalibrierung passt nicht mehr:** Wurde die Kamera verschoben oder verdreht,
  stimmen die gelernten Werte nicht mehr → **Training zurücksetzen** und neu
  kalibrieren.
- **Konfidenzschwelle:** Liegt die Erkennung knapp unter der Schwelle, kann in
  `config.h` der Wert `KONFIDENZ_MIN_PROZENT` (Standard 50) leicht gesenkt werden.
- **Mehr/breiteres Training:** Wenn einzelne Zustände oft verwechselt werden,
  hilft es, von diesen Zuständen zusätzliche Bilder in verschiedenen Lichtlagen
  zu bestätigen (Training vorher zurücksetzen für einen sauberen Neustart).

### Kalibrierung „vergisst" nach Neustart
- **Partition Scheme** muss **mit SPIFFS** gewählt sein
  (`8M with spiffs (3MB APP/1.5MB SPIFFS)`), sonst gibt es keinen Speicherplatz.
- Im Seriellen Monitor auf die Meldung „SPIFFS bereit." achten.

### OTA-Update / Netzwerk-Port erscheint nicht in der IDE
- Das **erste** Hochladen muss per **USB** erfolgen – OTA ist erst danach aktiv.
- PC und ESP32 müssen im **selben Netzwerk** (2,4 GHz) sein.
- Im Seriellen Monitor muss `OTA bereit.` erscheinen; sonst war kein WLAN da.
- mDNS/Bonjour muss erlaubt sein; manche Firewalls/VPNs blockieren die
  Erkennung. Arduino IDE einmal **neu starten** und 1–2 Minuten warten.
- Bei „Authentifizierung fehlgeschlagen": gesetztes `OTA_PASSWORT` stimmt nicht.
- Bei „Start fehlgeschlagen" / zu wenig Platz: ein **OTA-fähiges Partition-Schema**
  wählen (z. B. `8M with spiffs (3MB APP/1.5MB SPIFFS)`).

### SD-Karte wird nicht erkannt
- Karte als **FAT32** formatieren (1 GB ist ideal).
- Meldung „Keine SD-Karte gefunden" → Karte neu einstecken, Kontakte prüfen.
- Logs sind **optional** – ohne SD-Karte funktioniert ansonsten alles normal.

---

## Dateien im Projekt

| Datei                                | Inhalt                                                      |
|--------------------------------------|-------------------------------------------------------------|
| `Garagentor_Zustandserkennung.ino`   | Hauptprogramm: Kamera, WLAN, Webserver, Analyse, Training   |
| `config.h`                           | **Alle Einstellungen** (WLAN, Kamera, Schwellen, MQTT)      |
| `camera_pins.h`                      | Pin-Belegung der Freenove-Kamera                            |
| `webpages.h`                         | HTML der Übersichts- und Trainingsseite                     |
| `README.md`                          | Diese Anleitung                                             |

**Im Betrieb angelegte Dateien (SPIFFS):**
`/kalibrierung.dat` (gelernte Referenzwerte), `/letzter_wechsel.jpg` (Foto des
letzten Wechsels), `/wechsel.txt` (Zeit + Zustand).
**Auf der SD-Karte:** `/garagentor_log.csv` (Verlaufsprotokoll).

---

## Lizenz

Dieses Projekt steht unter der **GNU General Public License v3.0 (GPL-3.0)** –
siehe Datei [`LICENSE`](LICENSE).

Kurz gesagt:

- Du darfst den Code **frei nutzen, verändern und weitergeben** – auch kommerziell.
- Veränderte oder darauf aufbauende Versionen müssen **ebenfalls unter der GPL-3.0
  offengelegt** werden (kein „Einsacken" als geschlossene, proprietäre Software).
- Es gibt **keine Gewährleistung** – Nutzung auf eigene Verantwortung.

© 2026 sebastianktkuhnt-plan. „GPL-3.0" ist der maßgebliche Lizenztext; diese
Zusammenfassung dient nur der Orientierung.
