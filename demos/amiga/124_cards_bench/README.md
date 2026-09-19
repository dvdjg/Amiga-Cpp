# `demos/amiga/124_cards_bench` — benchmark de `eng::cards` en Amiga

Mide la **tasa real de trabajo** del motor de naipes en hardware emulado: repite una
unidad ("1 mano completa + N muestras de equity") hasta agotar un presupuesto de
tiempo y la cronometra con el contador **TOD de la CIA-A** (50 Hz PAL), no con el
bucle de frames: así mide también las unidades que **no caben** en un frame.

Publica en `g_eng_run_status.detail`:

```text
bits 31..16  unidades/s
bits 15..0   ms por unidad
```

y lo dibuja en pantalla. El perfil se elige en compilación con
`-DCARDS_BENCH_PROFILE=<0..4>` (`0`=N20 … `4`=N512).

## Evidencia medida (18/09/2026, A500/68000, release `-O2`, `--warp`)

Unidad = 1 mano heads-up + 1 muestra de equity. Tiempo **emulado** (TOD a 50 Hz):

| Perfil | Muestras MC/jugada | Unidades/s | ms/unidad | ¿Jugable en A500? |
|---|---:|---:|---:|---|
| `N20` | 0 (heurística) | **30** | 32 | sí |
| `N64` | 8 | **1** | 685 | sí (≈0,1 s/decisión) |
| `N128` | 16 | 0 | 6000 | no (muy lento) |
| `N256` | 32 | — | — | no completó la ventana |
| `N512` | 64 | — | — | no completó la ventana |

Conclusión: en un A500 base son viables `N20` y `N64`; `N128` y superiores apuntan a
máquinas ampliadas (A1200/030). Las muestras por perfil de `core/budget.hpp` se
calibraron con esta tabla (antes `N64`=32 tardaba ~6,5 s/mano).

Evidencia: `out/run/124_cards_bench/A500_release/run-report.json` (`status=ok`).

Comandos:

```bash
bash tools/build/build-demo.sh demos/amiga/124_cards_bench --release --clean
bash tools/run/run-demo.sh demos/amiga/124_cards_bench --warp
# variante por perfil:
EXTRA_DEFINES="-DCARDS_BENCH_PROFILE=2" bash tools/build/build-demo.sh demos/amiga/124_cards_bench --release
```

Lectura del resultado (el runner lo guarda en `run-report.json`):

```text
detail = (unidades_s << 16) | ms_por_unidad
```

## Estado

- **Build OK** (toolchain m68k) y **run → READY OK** en WinUAE con `--warp`.
- Matriz A500 de los cinco perfiles: N20/N64 medidos y calibrados; N128 medido (no
  jugable); N256/N512 agotan la ventana de captura (pendiente una ventana mayor o
  telemetría IRQ).
- Pendiente: comparar CPU (68000 vs 020/030) con builds `-m68020` y config de
  emulador A1200 (el `build-demo.sh` fija `-m68000`).
