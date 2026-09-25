# Demo 213 — Bartman "abyss" en el mini-SO (`eng::os`)

Port al engine de la demo clásica de **Bartman/vscode-amiga-debug** (`BartmanBasic/main.c`):
escena 320×256 de **5 planos interleaved** con la imagen *abyss*, **16 BOBs enmascarados** que se
desplazan por senos sobre la banda inferior, **degradado de COLOR00** por las líneas `0x41..0x4f` y
**fine-scroll** de `BPLCON1` movido por seno, con **música P61** (ThePlayer) y salida al pulsar el
**botón izquierdo del ratón**.

La diferencia de fondo con el original es que aquí **el juego no sondea hardware**: el latido del
**mini-SO** (`eng::os::tick`) latcha el VBlank y pollea la entrada, que llega como mensajes
(`MouseButton`/`KeyDown`); el bucle los drena y sale con `quit`. La música avanza una vez por frame.

## Qué muestra

- La imagen *abyss* (5 planos) tal cual, con el **degradado de copper** en la banda superior.
- Los 16 BOBs (cada `あ` de un color) recorriendo la banda inferior en seno, con **cookie-cut `$CA`**
  a nivel de píxel (no `copy`): el fondo se conserva fuera de la máscara.
- **Fine-scroll** horizontal del playfield por `BPLCON1` (`sin | sin<<4`), sin tocar los punteros.
- La música P61 sonando (ThePlayer, VBlank) mientras todo lo anterior se dibuja.
- El **mini-SO** como única vía de entrada/tiempo: no hay `while(!MouseLeft())` crudo.

## Invariantes / detalles que importan

- **Layout interleaved**: fila de 5 planos × 40 B = 200 B; `BPL1MOD=BPL2MOD=160`, `BPLxPT = base + p*40`.
  El bitmap se dibuja **in place** (el original no usa doble buffer; no hay `commit`).
- **BOB interleaved de una pasada**: el `bob.bpl` original guarda, por fila de plano, `[máscara]
  [imagen]` (8 B). Se reproduce con **un** `BlitJob` masked por BOB: `A=máscara`, `B=imagen`,
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
- En la captura: la imagen *abyss*, el degradado de color en la banda superior y los `あ` de colores
  repartidos por la banda inferior.

## Compilar / ejecutar / analizar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/os/213_bartman_abyss --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/os/213_bartman_abyss --warp
bash ./tools/run/run-demo.sh demos/techniques/amiga/os/213_bartman_abyss --warp --screenshot out/tmp/213.png
bash ./tools/analyze/analyze-demo.sh demos/techniques/amiga/os/213_bartman_abyss
```

## Evidencia de referencia (A500)

- `run-report.json`: `state=3`, `detail=0x22130`.
- Captura: imagen + bobs + degradado (ver `out/tmp/213.png`).

## Assets

En `assets/amiga/sprites/abyss/` (origen `BartmanBasic`, uso interno de prueba):

| Archivo | Formato |
|---|---|
| `abyss.bpl` | Interleaved 320×256×5, 40 B por fila/plano (51200 B), sin cabecera. |
| `abyss.pal` | 32 colores Amiga `$0RGB` (64 B). |
| `bob.bpl` | 6 frames de 32×16; por fila de plano `[máscara 2 palabras][imagen 2 palabras]` (3840 B). |

Módulo: `assets/amiga/audio/testmod.p61` (mismo que usa la demo 276).

## Rendimiento

16 BOBs × 1 blit + 1 clear = 17 blits por frame (el original hace lo mismo). El coste dominante es el
bus del Blitter en la banda de 56 filas.

## Referencias

- Original: `BartmanBasic/main.c` del fork Bartman/vscode-amiga-debug.
- Mini-SO: `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md`, `ROADMAP_MINI_OS.md`.
- Música: `docs/engine/architecture/MUSIC_PLAYER.md`.
- Blitter/cookie-cut: `docs/reference/amiga/techniques/blitter_programming.md`.

Tests: HOST-219 (núcleo del mini-SO), HOST-239/271 (P61/streaming), HOST-238 (tiempo).
