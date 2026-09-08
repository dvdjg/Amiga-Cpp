# Demo 060: self-check de eng::core en hardware (visible en pantalla real)

Valida en WinUAE los ports de `libmisc`/`libc` que cubre el test host
`HOST-000`. A diferencia de un overlay de depurador, **la demo dibuja en
bitplanes reales** (320x256 EHB) con una fuente 8x8 propia, de modo que el
resultado es visible en la ventana del Amiga normal (WinUAE, Coppenheimer,
hardware real), no solo en el overlay WinUAE-DBG.

| Fase | Cabecera | Origen | Validación |
|------|----------|--------|------------|
| 1 | `eng::core::isqrt` | `libmisc/fx.c` | `isqrt(n)` con muestras autenticadas del C original |
| 2 | `eng::core::crc32` | `libmisc/crc32.c` | CRC-32 de muestras (incluye `0xCBF43926` de "123456789") |
| 3 | `eng::core::Xoroshiro64pp` | `libc/stdlib/random.c` | 10 primeras salidas con estado {1,0} |
| 4 | `eng::core::quick_sort` + `sort_items` | `libmisc/sort.c` | orden ascendente + extremos |

Las fases se ejecutan en `init`. El resultado se publica en `g_eng_run_status`:

- estado `Ready` y `detail = 0x060100FF` si todas las fases pasan;
- estado `Failed` y `detail = 0x06000203` (etc.) codificando la primera fase
  fallida.

Así el runner valida la corrección sin depender del análisis visual, y la
pantalla muestra el `OK/FAIL` de cada fase + el cartel final.

## Dependencias nuevas

- `engine/include/eng/core/utf8.hpp` — decodificador UTF-8 freestanding (ASCI+1..4 bytes) para las funciones de texto.
- `engine/include/eng/graphics/font8.hpp` — fuente 8x8 con LATIN-1 completo (acentos/diéresis/ñ/Ñ/símbolos).
- API de texto: `Surface::draw_text(x, y, text, color)` (contexto de dispositivo sobre `Playfield`) + `CanvasPlayfield`.

## Cómo se dibuja (API, no punteros)

La demo usa `CanvasPlayfield` (320x256 EHB) como lienzo y dibuja TODO el texto a
través del contexto `Surface` (`surface.draw_text(...)`); el programador no ve
punteros a bitplanes, planos ni layouts. La cadena se pasa en UTF-8 y la fuente
cubre LATIN-1, así que acentos/diéresis/ñ se pintan igual que el resto.

## Build & run & analyze

```bash
tools/build/build-demo.sh demos/amiga/060_eng_core_selfcheck --clean
tools/run/run-demo.sh       demos/amiga/060_eng_core_selfcheck
tools/analyze/analyze-demo.sh demos/amiga/060_eng_core_selfcheck
```

## Criterio de aceptación

- Compila y llega a `Ready` por canal lateral (estado 3).
- `out/run/060_eng_core_selfcheck/run-report.json` reporta el detalle
  `0x060100FF`.
- En la captura se ve (bitplanes EHB): título, cuatro líneas `OK` y el cartel
  `SELF-CHECK: ALL PHASES OK` en texto blanco/amarillo sobre fondo azul.
- El analizador `analyze-demo` pasa (texto blanco + fondo no-Workbench).

## Por qué bitplanes reales y no solo overlay

El overlay `debug_*` (`debug_cmd` → `UaeLib` en `0xf0ff60`) solo existe en
WinUAE-DBG y no se ve en la ventana Amiga normal ni en hardware real
(ver `docs/debugging/diagnostico-adf-negro.md`). Una demo que debe "verse
ejecutándose" dibuja en el playfield; el overlay queda como complemento de
depuración, no como salida principal. Esta demo demuestra el patrón.

## Nota de portabilidad

`eng::u32` = `unsigned long`: 32 bits en m68k, 64 en host GCC x86. Por eso
`crc32`/`isqrt`/PRNG operan internamente con `__UINT32_TYPE__` y enmascaran a
32 bits; así los mismos headers dan el mismo resultado en host (tests) y en el
cruce (demo). La demo compila con el cruce m68k y verifica el resultado real de
32 bits tal como correrá en A500.