# Demo 060: self-check de eng::core en hardware

Valida en WinUAE los four ports de `libmisc`/`libc` que cubre el test host
`HOST-000` y los promueve de "validado por test host" a "validado en hardware":

| Fase | Cabecera | Origen | Validación |
|------|----------|--------|------------|
| 1 | `eng::core::isqrt` | `libmisc/fx.c` | `isqrt(n)` con muestras autenticadas del C original |
| 2 | `eng::core::crc32` | `libmisc/crc32.c` | CRC-32 de muestras (incluye `0xCBF43926` de "123456789") |
| 3 | `eng::core::Xoroshiro64pp` | `libc/stdlib/random.c` | 10 primeras salidas con estado {1,0} |
| 4 | `eng::core::quick_sort` + `sort_items` | `libmisc/sort.c` | orden ascendente + extremos |

Las cuatro fases se ejecutan en `init`. El resultado se publica en
`g_eng_run_status`,

- estado `Ready` y `detail = 0x060100FF` si todas las fases pasan;
- estado `Failed` y `detail = 0x06000203` (etc.) codificando la primera fase
  fallida.

De ese modo el runner valida la correción sin depender del análisis visual: si
la demo llega a `Ready` con el detail esperado, el self-check pasó.

## Build & run & analyze

```bash
tools/build/build-demo.sh demos/amiga/060_eng_core_selfcheck --clean
tools/run/run-demo.sh       demos/amiga/060_eng_core_selfcheck
tools/analyze/analyze-demo.sh demos/amiga/060_eng_core_selfcheck
```

## Criterio de aceptación

- Compila y llega a `Ready` por canal lateral (estado 3).
- `out/run/060_eng_core_selfcheck/run-report.json` reporta el detalle
  `0x060100FF` (o se lee el símbolo `g_eng_run_status.detail` por GDB).
- En la captura se ve fondo verde y las cuatro líneas `OK`.
- El analizador `analyze-demo` pasa (fondo verde dominante + texto blanco).

## Nota de portabilidad

Los `eng::u32` del engine son `unsigned long`: de 32 bits en m68k pero de 64
en el host GCC x86. Por eso `crc32`, `isqrt` y el PRNG internamente operan con
`__UINT32_TYPE__` (uint32_t GNU freestanding) y enmascaran/truncan a 32 bits;
así los mismos headers dan el mismo resultado en host (tests) y en el cruce
(demo). La demo, al compilar con el cruce m68k, verifica el resultado real de
32 bits tal y como correrá en A500.