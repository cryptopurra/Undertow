# UNDERTOW: sidechain, phase e space per kick e basso

Un plugin VST3 (più app Standalone) che fa tre cose sul basso, guidato dalla cassa:

1. **SIDECHAIN**: una curva di volume disegnata a mano che riparte a ogni kick.
2. **PHASE**: ruota la fase del solo sub per farlo sommare con la coda della cassa.
3. **SPACE**: un EQ dinamico con campane strette che tagliano il basso solo dove e quando suona la cassa.

---

## Installazione (Windows)

1. Copia la cartella **`Undertow.vst3`** in `C:\Program Files\Common Files\VST3\`.
2. Nella DAW rifai la scansione dei plugin.
3. Inserisci Undertow **sulla traccia del basso**.

`Undertow.exe` è la versione Standalone: serve per provarlo senza DAW.

> Il plugin non è firmato. Se Windows SmartScreen mostra un avviso sull'exe, scegli "Ulteriori informazioni → Esegui comunque".

## Come dargli la cassa

Hai tre possibilità e il plugin sceglie da solo, nell'ordine:

| Sorgente | Come | Indicatore in alto |
|---|---|---|
| **Kick instradato** | manda la traccia della cassa all'ingresso **sidechain** di Undertow | `KICK · ROUTED` |
| **Sample** | trascina un .wav/.aif/.flac della cassa sul plugin, oppure "LOAD KICK…" | `SAMPLE · ON GRID` |
| **Griglia** | niente: il plugin segue il tempo della DAW | `GRID · NO KICK` |

* Con il kick instradato, ogni colpo viene rilevato e la curva riparte esattamente dal **primo campione** della cassa (il plugin guarda avanti di ~64 ms, compensati automaticamente dalla DAW).
* Se la cassa si ferma (per esempio quando esporti solo il basso), entra in **HOLD**: l'ultima cassa sentita continua in silenzio con lo stesso timing, quindi il ducking resta identico.
* Cliccando l'indicatore si spegne la griglia (`GRID OFF`): senza cassa, nessun ducking.
* Il sample caricato viene salvato dentro il progetto.

## Scheda SIDECHAIN

* **Curva**: il tempo va da sinistra a destra, un ciclo per ogni kick; il livello del basso va dal basso verso l'alto.
  * trascina un **punto** per spostarlo;
  * trascina una **linea** orizzontale per spostare su/giù tutto il gradino (una linea verticale si sposta a destra/sinistra);
  * trascina il **cerchietto** a metà di una pendenza per curvarla;
  * **doppio clic** per aggiungere o togliere un punto.
* **FREE / QUANTIZE**: con QUANTIZE i punti si agganciano a 1/16 del ciclo e a 1/8 dell'altezza.
* **GRID**: mostra o nasconde la griglia. **LANES / OVERLAY**: cassa (grigio) e basso (verde acqua) su due corsie separate o sovrapposti sotto la curva.
* In alto a destra trovi il livello **sotto la cassa** (−∞ dB = silenzio) e **dopo quanti ms** il basso si riapre.
* **Rate** (1/16 … 1 BAR): la durata di un ciclo della curva.
* **SHAPES**: 13 forme pronte. Clic per applicarne una; trascinale per riordinarle; **+ SAVE** salva la tua curva; tasto destro su una tua forma per eliminarla.
* **DEPTH**: quanto scende il basso (100% = silenzio dove la curva è a zero).
* **CLICK**: dissolvenza di 0–10 ms *prima* del colpo, per non fare click.
* **DUCK**: quale parte del basso si muove.
  * **FULL**: tutto il basso;
  * **SUB**: solo sotto i 120 Hz;
  * **BAND**: una banda a scelta, da regolare trascinando le due maniglie (per esempio 20–200 Hz per lasciare intatti i medi).

  Fuori dalla banda il segnale non viene toccato: la divisione è a fase lineare e si ricompone perfettamente.

## Scheda PHASE

Ruota la fase del **solo sub** (sotto **SPLIT**) per farlo continuare dalla coda della cassa invece di cancellarla.

* **AUTO**: a ogni ciclo misura la rotazione con cui coda della cassa e sub si sommano meglio, e la applica **solo mentre il duck è chiuso**, così il cambio non si sente. In basso vedi `MATCH` (quanto sono correlati) e `TARGET` (l'angolo misurato).
* **MANUAL**: la rotazione la scegli tu con la manopola PHASE.
* **Ø FLIP**: inverte la polarità del sub.
* **ON**: attiva o disattiva, per confrontare.
* Il grafico mostra i primi 150 ms dopo il kick: la parte bassa della cassa (grigio) e il sub del basso così come lo senti (verde acqua).

## Scheda SPACE

Un EQ dinamico di 20 campane strette fra **LOW** e **HIGH** (trascina le due linee). Una campana taglia solo se:
* la cassa è forte in quella banda (rispetto al suo picco),
* il basso ha energia in quella banda,
* e solo mentre la cassa suona.

**AMOUNT** regola il taglio massimo (100% = −24 dB) e **RELEASE** la velocità con cui ogni campana torna a 0 dB. "When they work" mostra in rosso in quale punto del ciclo le campane tagliano.

## Altro

* **Preset**: menu in alto, con 13 preset di fabbrica (Psytrance Rolling, Full-On, Handover, Techno, House, DnB…).
* **Undo / Redo**: frecce in alto a destra (anche Ctrl+Z / Ctrl+Shift+Z).
* **BYPASS**: bypass con latenza compensata.
* La finestra si ridimensiona dall'angolo in basso a destra.
* **Latenza**: 64 ms a 48 kHz (58 ms a 44.1 kHz), dichiarata alla DAW e compensata.

---

## Compilare dai sorgenti

Servono CMake ≥ 3.22 e JUCE 8.0.4:

```bash
git clone --branch 8.0.4 --depth 1 https://github.com/juce-framework/JUCE.git
cmake -B build -DJUCE_DIR=$PWD/JUCE -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

* **Windows (Visual Studio 2022)**: `cmake -B build -G "Visual Studio 17 2022" -DJUCE_DIR=...` e poi `cmake --build build --config Release`.
* **macOS**: produce anche la versione **AU**.
* **CI**: `.github/workflows/build.yml` compila Windows e macOS su GitHub Actions a ogni push.
* **Test offline (Linux)**: `cmake -B build -DUNDERTOW_TESTS=ON && cmake --build build --target UndertowTests`, poi esegui `UndertowTests` (sotto `xvfb-run` per gli screenshot).

I binari Windows inclusi sono cross-compilati con llvm-mingw (`cmake/toolchain-llvm-mingw.cmake`). Il JUCE usato ha tre piccole patch per MinGW; con Visual Studio non servono.

### Struttura

```
Source/PluginProcessor.*   motore audio: trigger, curva, fasi, space, stato
Source/LinearPhaseBank.h   filtri FIR a fase lineare + Hilbert (convoluzione FFT partizionata, PFFFT)
Source/SpaceEQ.h           EQ dinamico a 20 campane
Source/Curve.h             modello della curva e le 13 forme
Source/Presets.h           preset di fabbrica
Source/PluginEditor.*      interfaccia
tests/Tests.cpp            test offline del DSP
third_party/pffft          FFT (licenza BSD, vedi LICENSE.txt)
```
