// ===========================================================================
//  webpages.h  –  HTML-Seiten für das Webinterface
// ---------------------------------------------------------------------------
//  Die Seiten werden direkt vom ESP32 ausgeliefert. Kein externer Server nötig.
//  - INDEX_HTML : Übersicht (Livebild, Zustand, letzter Wechsel)
//  - TRAIN_HTML : Trainingsmodus (Bild bewerten, Fortschritt, Kalibrierung)
//  Das Live-MJPEG-Bild läuft aus technischen Gründen auf Port 81
//  (eigener Server), die Adresse wird im Skript automatisch zusammengebaut.
// ===========================================================================
#pragma once

// ------------------------- ÜBERSICHTSSEITE  ("/") --------------------------
static const char INDEX_HTML[] = R"HTML(<!DOCTYPE html>
<html lang="de"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Garagentor-Ueberwachung</title>
<style>
 body{font-family:Arial,Helvetica,sans-serif;margin:0;background:#1e1e1e;color:#eee}
 header{background:#000;padding:14px 18px;font-size:20px;font-weight:bold}
 .wrap{display:flex;flex-wrap:wrap;gap:16px;padding:16px}
 .karte{background:#2a2a2a;border-radius:12px;padding:16px;flex:1 1 340px}
 h3{margin:4px 0 12px}
 img.live{width:100%;border-radius:8px;background:#000;min-height:120px}
 .status{font-size:36px;font-weight:bold;text-align:center;padding:26px 10px;border-radius:12px;margin:6px 0}
 .zu{background:#1b7f3b;color:#fff}
 .offen{background:#b3261e;color:#fff}
 .halb{background:#c9a700;color:#000}
 .unklar{background:#666;color:#fff}
 .konf{text-align:center;font-size:18px;margin-top:6px}
 .hinweis{background:#7a5c00;color:#fff;padding:10px;border-radius:8px;margin-top:12px;display:none}
 .thumb{width:100%;border-radius:8px;background:#000;min-height:60px}
 .klein{font-size:13px;color:#aaa;margin-top:6px}
 a{color:#7ab8ff;text-decoration:none}
</style></head><body>
<header>Garagentor-Ueberwachung</header>
<div class="wrap">
  <div class="karte">
    <h3>Livebild</h3>
    <img id="live" class="live" src="" alt="Livestream">
    <div class="klein" id="zeit">Letzte Analyse: -</div>
  </div>
  <div class="karte">
    <h3>Aktueller Zustand</h3>
    <div id="zustand" class="status unklar">-</div>
    <div class="konf" id="konfidenz">Konfidenz: - %</div>
    <div class="hinweis" id="kalhinweis">Noch nicht kalibriert - bitte zuerst den Trainingsmodus ausfuehren.</div>
    <h3 style="margin-top:22px">Letzter Zustandswechsel</h3>
    <img id="wechselBild" class="thumb" src="/letzter_wechsel.jpg" alt="kein Bild">
    <div class="klein" id="wechselText">-</div>
    <p style="margin-top:16px"><a href="/train">&#10142; Zum Trainingsmodus</a></p>
  </div>
</div>
<script>
 // Livebild kommt vom Stream-Server auf Port 81
 document.getElementById('live').src = 'http://' + location.hostname + ':81/stream';
 async function update(){
   try{
     const r = await fetch('/status'); const d = await r.json();
     const z = document.getElementById('zustand');
     z.textContent = d.zustandText; z.className = 'status ' + d.klasse;
     document.getElementById('konfidenz').textContent = 'Konfidenz: ' + d.konfidenz + ' %';
     document.getElementById('kalhinweis').style.display = d.kalibriert ? 'none' : 'block';
     document.getElementById('zeit').textContent = 'Letzte Analyse: ' + d.zeit;
     document.getElementById('wechselText').textContent = d.wechselZustand + '   |   ' + d.wechselZeit;
     if(d.wechselBild){ document.getElementById('wechselBild').src = '/letzter_wechsel.jpg?ts=' + Date.now(); }
   }catch(e){ /* Netzwerkfehler ignorieren, naechster Versuch folgt */ }
 }
 setInterval(update, 3000); update();
</script></body></html>)HTML";

// ------------------------- TRAININGSSEITE  ("/train") ----------------------
static const char TRAIN_HTML[] = R"HTML(<!DOCTYPE html>
<html lang="de"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Garagentor - Trainingsmodus</title>
<style>
 body{font-family:Arial,Helvetica,sans-serif;margin:0;background:#1e1e1e;color:#eee}
 header{background:#000;padding:14px 18px;font-size:20px;font-weight:bold}
 .wrap{display:flex;justify-content:center;padding:16px}
 .karte{background:#2a2a2a;border-radius:12px;padding:16px;flex:1 1 520px;max-width:560px}
 img.live{width:100%;border-radius:8px;background:#000;min-height:160px}
 .infobox{background:#264b7a;color:#fff;padding:12px;border-radius:8px;margin-bottom:12px;font-size:15px}
 #schaetzung{text-align:center;font-size:22px;font-weight:bold;margin:12px 0}
 .btn{padding:14px 16px;margin:6px;font-size:16px;border:none;border-radius:8px;cursor:pointer;color:#fff}
 .b-zu{background:#1b7f3b}.b-offen{background:#b3261e}.b-halb{background:#c9a700;color:#000}.b-skip{background:#555}
 .b-reset{background:#333;color:#ccc;border:1px solid #777}
 .mitte{text-align:center}
 #fortschritt{text-align:center;margin-top:16px;font-size:16px}
 #fertig{display:none;background:#1b7f3b;color:#fff;padding:14px;border-radius:8px;font-size:18px;margin-top:14px;text-align:center}
 a{color:#7ab8ff;text-decoration:none}
</style></head><body>
<header>Trainingsmodus</header>
<div class="wrap"><div class="karte">
  <div class="infobox">
    Bitte auch bei verschiedenen Lichtverhaeltnissen trainieren
    (Nacht, Tag, Gegenlicht), damit die Erkennung robust wird.
  </div>
  <img id="tbild" class="live" src="" alt="Trainingsbild">
  <div id="schaetzung">Ich meine: -</div>
  <div class="mitte">
    <button class="btn b-zu"    onclick="label('zu')">Tor zu</button>
    <button class="btn b-offen" onclick="label('offen')">Tor offen</button>
    <button class="btn b-halb"  onclick="label('halb')">Tor halb</button>
    <button class="btn b-skip"  onclick="label('skip')">Bild ueberspringen</button>
  </div>
  <div id="fortschritt">-</div>
  <div id="fertig">Training abgeschlossen - Kalibrierung gespeichert!</div>
  <div class="mitte" style="margin-top:18px">
    <button class="btn b-reset" onclick="reset()">Training zuruecksetzen</button>
    <a href="/" style="margin-left:14px">&#10142; Zur Uebersicht</a>
  </div>
</div></div>
<script>
 let timer = null;
 async function poll(){
   try{
     const r = await fetch('/train/poll'); const d = await r.json();
     document.getElementById('tbild').src = '/train/bild.jpg?ts=' + Date.now();
     document.getElementById('schaetzung').textContent =
        d.gueltig ? ('Ich meine: ' + d.zustandText + ' - ' + d.konfidenz + ' %')
                  : 'Kein verwertbares Bild';
     document.getElementById('fortschritt').textContent =
        'Tor zu: ' + d.zu + '/' + d.ziel +
        '   -   Tor offen: ' + d.offen + '/' + d.ziel +
        '   -   Tor halb: ' + d.halb + '/' + d.ziel;
     document.getElementById('fertig').style.display = d.fertig ? 'block' : 'none';
   }catch(e){}
 }
 async function label(z){
   try{ await fetch('/train/label?zustand=' + z); }catch(e){}
   poll(); start();   // sofort naechstes Bild holen, Timer neu starten
 }
 async function reset(){
   if(!confirm('Training wirklich zuruecksetzen?')) return;
   try{ await fetch('/train/reset'); }catch(e){}
   poll();
 }
 function start(){ if(timer) clearInterval(timer); timer = setInterval(poll, 3000); }
 start(); poll();
</script></body></html>)HTML";
