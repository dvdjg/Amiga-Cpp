# Demo 213 — Bartman "abyss" en el mini-SO (`eng::os`)

Port al engine de la demo clásica de **Bartman/vscode-amiga-debug** (`BartmanBasic/main.c`):
escena 320×256 de **5 planos interleaved** con la imagen *abyss*, **16 BOBs enmascarados** que se
desplazan por senos sobre la banda inferior y **fine-scroll** de `BPLCON1` movido por seno, con
**música P61** (ThePlayer) y salida al pulsar el **botón izquierdo del ratón**.

La diferencia de fondo con el original es que aquí **el juego no sondea hardware**: el latido del
**mini-SO** (`eng::os::tick`) latcha el VBlank y pollea la entrada, que llega como mensajes
(`MouseButton`/`KeyDown`); el bucle los drena y sale con `quit`. La música avanza una vez por frame.

## Qué muestra

- La imagen *abyss* (5 planos) tal cual, sobre fondo claro.
- Los 16 BOBs (cada `あ` de un color) recorriendo la banda inferior en seno, con **cookie-cut `$CA`**
  a nivel de píxel (no `copy`): el fondo se conserva fuera de la máscara.
- **Fine-scroll** horizontal del playfield por `BPLCON1` (`sin | sin<<4`), sin tocar los punteros.
- La música P61 sonando (ThePlayer, VBlank) mientras todo lo anterior se dibuja.
- El **mini-SO** como única vía de entrada/tiempo: no hay `while(!MouseLeft())` crudo.

## Invariantes / detalles que importan

- **Layout interleaved**: fila de 5 planos × 40 B = 200 B; `BPL1MOD=BPL2MOD=160`, `BPLxPT = base + p*40`.
  El bitmap se dibuja **in place** (el original no usa doble buffer; no hay `commit`).
- **BOB interleaved de una pasada**: el `bob.bpl` original guarda, por fila de plano, `[imagen]
  [máscara]` (8 B). Se reproduce con **un** `BlitJob` masked por BOB: `A=máscara`, `B=imagen`,
  `words_per_row=2`, `height=16*5=80`, `AMOD=BMOD=4`, `DMOD=36` (`40-4`), minterm `$CA`. No requiere
  reempaquetar el asset.
- **Borrado**: un solo `ClearRect` D-only sobre las filas 200..255 de los 5 planos (`DMOD=0`).
- **Assets en Chip**: se copian desde `.rodata` a un bloque de Chip en `init` (el Blitter y P61 solo
  ven Chip; así funciona también con Fast RAM).
- **Salida**: `MouseButton` con `buttons & 1` (bit 0 = izquierdo, como `MouseLeft()`); `ESC` también.
  No hay `FreeSystem()` fiel (el engine congela el sistema al hacer takeover); el runner cierra la
  instancia.

## Criterio de aceptación

- `state=3` (Ready) con `detail=0x22130` (bit 17 = música P61 iniciada).
- En la captura: la imagen *abyss* y los `あ` de colores repartidos por la banda inferior.

## Compilar / ejecutar / analizar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/os/213_bartman_abyss --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/os/213_bartman_abyss --warp
bash ./tools/run/run-demo.sh demos/techniques/amiga/os/213_bartman_abyss --warp --screenshot out/tmp/213.png
bash ./tools/analyze/analyze-demo.sh demos/techniques/amiga/os/213_bartman_abyss
```

## Evidencia de referencia (A500)

- `run-report.json`: `state=3`, `detail=0x22130`.
- Captura: imagen + bobs (ver `out/tmp/213.png`).
- **Audio**: `detail` bit 17 = 1 confirma que `P61_Init` tuvo éxito, pero el runner **no captura
  PCM** (no hay grabación de audio en el harness), así que la salida de Paula no se verifica aquí;
  el reproductor P61 ya está validado por las demos 060 (`music_pt`) y 272 (`audio_stream`).

## Rendimiento

Medido en `A500_debug` con `tools/debug/measure-fps.mjs` (el contador del periférico de depuración
marca ≈2× los ciclos de CPU; `142102` = **1 frame PAL**):

| Variante | fps | ciclos/iteración |
|---|---|---|
| Demo actual (`clear_box` + 16 BOBs + música) | **28,0** | 253093 |
| Sin `clear_box` (la banda de juego) | 34,7 | 204654 |
| Sin música (`play_music`) | 28,4 | 250133 |
| **Sin los 16 BOBs** (`Screen::sprite`) | **49,9** | 142102 |
| **Sin ejecutar el Blitter** (CPU sí; ver texto) | **48,8** | 145485 |

**Lectura**: sin los BOBs la demo cabe **exacta** en un frame PAL (49,9 fps) y, saltando **solo la
ejecución del Blitter** (manteniendo el CPU que encola los jobs), también (**48,8 fps**) → el cuello
es la **ejecución del Blitter** (los 17 blits ≈ 55k ciclos ≈ 0,8 frame), **no** el CPU que los
prepara. El `clear_box` cuesta ~24k (≈0,34 frame); la música (en la IRQ de VBlank) es
**despreciable**. El DMA de Blitter propio es **+43%** que el del original (22265 vs 15569) con los
mismos 17 blits y la misma geometría → margen en la ejecución (`amiga_blitter.cpp`: ~10-13
escrituras de registro por job, sin caché del estado de registros entre jobs).

## Assets

En `assets/amiga/sprites/abyss/` (origen `BartmanBasic`, uso interno de prueba):

| Archivo | Formato |
|---|---|
| `abyss.bpl` | Interleaved 320×256×5, 40 B por fila/plano (51200 B), sin cabecera. |
| `abyss.pal` | 32 colores Amiga `$0RGB` (64 B). |
| `bob.bpl` | 6 frames de 32×16; por fila de plano `[imagen 2 palabras][máscara 2 palabras]` (3840 B). |

Módulo: `assets/amiga/audio/testmod.p61` (mismo que usa la demo 276).

## Referencias

- Original: `BartmanBasic/main.c` del fork Bartman/vscode-amiga-debug.
- Técnica del BOB: `docs/reference/amiga/techniques/interleaved-bob-single-blit.md`.
- Mini-SO: `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md`, `ROADMAP_MINI_OS.md`.
- Música: `docs/engine/architecture/MUSIC_PLAYER.md`.

Tests: HOST-219 (núcleo del mini-SO), HOST-239/271 (P61/streaming), HOST-238 (tiempo).
