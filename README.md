# Glizzarine v1.0.1

Synth (VST3 + Standalone, Windows 64-Bit) von NebulaAudio. Ersetzt die alte Glizzarine v3 (gleiche Plugin-Kennung).

## Installieren
1. Im GitHub-Repo unter **Actions** den letzten grünen Lauf öffnen und unten bei **Artifacts** `Glizzarine-Windows` herunterladen.
2. Entpacken. Den Ordner `VST3/Glizzarine.vst3` nach `C:\Program Files\Common Files\VST3\` kopieren (alte Version ersetzen).
3. In Sonar: Utilities → Plug-in Manager → VST-Plug-ins neu scannen. Glizzarine als Instrument einfügen.
4. Oben links im Plugin steht die Version mit Build-Datum. Daran siehst du, dass die neue Datei geladen ist.

## KeyLab einrichten (einmalig, gilt für alle Projekte)
1. KeyLab in den **User-Modus** schalten. MIDI-Spur in Sonar: Eingang = KeyLab, Ausgang = Glizzarine.
2. Im Plugin auf **SETUP** → **Learn KeyLab**, dann Knob 1 bis 8 drehen und Fader 1 bis 8 bewegen, nacheinander.
3. Die **Knobs** steuern E1–E8: die wichtigsten Regler des jeweiligen Sounds (wechseln mit dem Preset, Leiste auf PLAY).
   Die **Fader** sind immer gleich: Osc 1, Osc 2, Osc 3, Attack, Release, Delay, Reverb, Volume.
4. **Soft Takeover**: Ein Regler übernimmt erst, wenn er den aktuellen Wert passiert. Beim Preset-Wechsel springt nichts.
5. **Rechtsklick** auf jeden Regler = eigenes MIDI-Learn. Falls die Encoder beim Drehen springen: unter SETUP auf *Relative* stellen.
Mod-Rad = Vibrato, Aftertouch und Anschlag sind pro Sound belegt, Pitch-Bend ±2 Halbtöne, Sustain-Pedal geht (beim Arp = Hold).

## Bedienung
- **PLAY**: Wellenform, E1–E8, Fader-Leiste. **OSC**: 3 Oszillatoren (15 Modelle, die zwei Charakter-Regler heißen je nach Modell anders),
  Cross FM, Ring Mod, Mono/Legato/Poly, Glide. **FILTER & ENV**: 8 Filtertypen, Amp- und Filter-Hüllkurve.
  **MOD**: 2 LFOs (auch tempo-synchron), Mod-Matrix, Wheel/Aftertouch, Strange Physics. **ARP**: alle Einstellungen inkl. Oktaven,
  16-Step-Rhythmus, Top line. **EFFECTS**: jeder Effekt eine Zeile mit Schalter und allen Reglern; Regler aufdrehen schaltet ihn ein.
  **SETUP**: KeyLab, Tempo (Projekt oder eigenes mit Tap), Taktart.
- **Stereo-Stil** (EFFECTS → STEREO): Standard *Focused Soft* – Bass bleibt mittig, breit und klar, auch in Mono. *Classic* = alter Stil.
- Kopfzeile: **Undo/Redo**, **A/B** (zwei Klangstände vergleichen), **SAVE** (eigenes Preset, Name eingeben → `Dokumente/Glizzarine Presets`),
  Klick auf den Preset-Namen öffnet den **Browser** (Kategorien, Suche).
- Tooltips: Maus kurz über einen Regler halten. Fenster ist skalierbar (Ecke unten rechts ziehen).

## Presets
15 freigegebene Sounds: Lovers Pad, Soft Triangle Pad, Digital Warm Pad, Digital Shimmer, Grain Halo, Ping-Pong Arp Pad,
Glitter Grains, Tears in Rain, Robot Choir, Shimmer Cathedral, Dark Cinematic, Rift, Monolith, Haunted Bells Arp, Metal Ghost Arp.
Als Nächstes: Leads (Minimoog-Solo zuerst), Keys, Bass, Video – bis ca. 30.

## Technik
- Engine (`Engine.h`, `Voice.h`, `Osc.h`, `FX.h`, `Arp.h`) wird vom Plugin und vom Offline-Renderer (`render.cpp`) gleich benutzt.
- Klickschutz: min. 2 ms Attack / 40 ms Release, Voice-Stealing mit 4 ms Ausblenden, gleiche Note setzt sanft neu an,
  Velocity gleitet, alle Delays interpoliert und bounds-sicher, Look-ahead-Limiter 1,5 ms.
- Selbstheilung bei NaN/Inf (Stufe wird zurückgesetzt), beliebige Blockgrößen, keine geteilten Zustände zwischen Instanzen.
- Tests (`-DGLZ_TESTS=ON`): `test_fuzz` (ASan/UBSan mit `-DGLZ_SANITIZE=ON`), `test_bounce` (Offline-Export), `test_soak`
  (Editor + Audio-Thread), `test_cpu`, `test_screenshots`, `render` (Hörbeispiele).
