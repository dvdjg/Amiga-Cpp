# Depuración de sonido — procedimiento

Procedimiento para verificar que el sonido generado es el que se pretende, desde
la onda en host hasta la salida de Paula. El principio: **analizar la onda antes
de llevarla a Paula** (host), y **verificar el hardware por el canal lateral**
(emulador). Así se aísla dónde se rompe el sonido (muestra, períodos, registros o
mezcla).

## 1. Pipeline de depuración

```
┌─ HOST (Node) ──────────────────────────────────────────────┐
│ generateWave() -> .raw/.wav  +  analyzeWave() (amplitud,   │
│ DC, RMS, frecuencia)                                        │
│   -> verifica la onda ANTES de Paula                        │
├─ PUENTE ────────────────────────────────────────────────────┤
│ raw-to-header.js -> cabecera C++ (misma onda, byte a byte)  │
├─ AMIGA (demo) ─────────────────────────────────────────────┤
│ la demo incrusta la muestra y la reproduce; expone por el   │
│ canal lateral el estado real (DMACONR, registros, checksum) │
├─ VERIFICACIÓN (canal lateral / watchpoints) ────────────────┤
│ leer DMACONR + registros AUDx + volcar la muestra en Chip   │
└─────────────────────────────────────────────────────────────┘
```

## 2. Herramientas host (`tools/audio/`)

| Herramienta | Uso |
|---|---|
| `wave.ts` | `generateWave(tipo, Hz, rate, seg, amp)` y `analyzeWave(data, rate)`. Puro, host-testable. |
| `gen-wave.ts` | Genera `<salida>.raw` + `<salida>.wav` y por stdout el informe (amplitud/DC/RMS/frecuencia). |
| `raw-to-header.ts` | `.raw` -> cabecera C++ con el array `u8`, para incrustar la onda exacta en una demo. |
| `test-wave.ts` | Test host del generador/analizador (`node dist/tools/audio/test-wave.js`). |

Ejemplo:

```bash
node dist/tools/audio/gen-wave.js sine 440 11025 1.0 out/sine440
# -> out/sine440.raw/.wav + informe: min=-127 max=127 dc=0 rms=90 dominant=440Hz
node dist/tools/audio/raw-to-header.js out/sine440.raw demo.h sine440
```

## 3. Prueba 1 (hecha): seno por UN canal

`demos/amiga/064_audio_debug`: genera un ciclo de seno de 64 muestras (tabla de
cuarto de onda, **entero, sin float** — el engine es freestanding y no enlaza
libgcc soft-float) y lo reproduce en bucle en AUD0 con período 127 (~440 Hz).

Verificación (canal lateral / `run-report.json`):

- `DMACONR = 0x381` -> `AUD0EN` (bit 0) + `DMAEN` (bit 9) activos.
- `detail` codifica `sample_max=127`, `sample_min=-127` (seno correcto).

La demo también expone `g_audio_dbg` (struct `volatile`) con el puntero de
muestra, `AUD0LEN/PER/VOL` (espejo RAM: los registros de audio son write-only) y
el checksum, para leerlo con `winuae_side_read` / `mem <addr> <len>`.

**Conclusión**: el camino de Paula funciona; el sonido "roto" anterior venía de
la muestra (cuadrada de 128 muestras a período 428 = zumbido de ~65 Hz) y de los
períodos, no del hardware.

## 4. Pruebas siguientes (escalera)

1. **Cambiar tono**: misma muestra, distinto `AUD0PER` (124..65535). Verificar que
   `DMACONR` sigue activo y que el período llega al registro (espejo RAM).
2. **Distintas formas**: cuadrado/triangular/sierra con `gen-wave.js` + análisis
   (frecuencia por cruces por cero).
3. **Dos canales**: reproducir seno en AUD0 y otra onda en AUD1 (o música por
   ptplayer). Verificar `AUD0EN`+`AUD1EN`.
4. **Mixer**: `SfxMixer` (4 voces software en AUD0) + música. Verificar que las
   muestras están preprocesadas (±32) y que la mezcla no desborda.
5. **Armonía completa**: demo 063 (3 canales + mixer), afinando períodos y timbre.

## 5. Verificación en WinUAE

- **Registros de audio** (`AUDxLCH/LCL/LEN/PER/VOL`): write-only. Usar un espejo
  RAM en la demo (`g_audio_dbg`) o watchpoints (`winuae_watchpoint_set_ext` con
  `src=audio`) para ver qué escribe el CPU/Copper.
- **DMA de audio**: leer `DMACONR` (`$dff002`); bit 0..3 = AUD0..3EN, bit 9 = DMAEN.
- **Muestra en Chip RAM**: `winuae_side_read` / `mem <addr> <len>` para volcar la
  muestra y compararla con el `.raw` del host (checksum o byte a byte).
- **Salida analógica** (opcional): grabación de audio de WinUAE (menú *Sound* ->
  *record*), para inspeccionar la forma de onda final.

## 6. Reglas para que el sonido sea correcto

- **Muestra 8-bit con signo**, frecuencia ~11 kHz (coincidir con el período).
- **Amplitud preprocesada** para el mixer (±32 con 4 voces; ±127 para música).
- **Períodos Protracker** para melodías (C-2=428..B-2=226; con una muestra de 32
  muestras suena en ~C4..B4).
- **Sin float** en el engine (freestanding sin libgcc soft-float): senos por
  tabla entera.
