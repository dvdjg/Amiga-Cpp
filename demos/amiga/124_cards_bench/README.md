# `demos/amiga/124_cards_bench` — benchmark de `eng::cards` en Amiga

Mide el coste de una unidad de trabajo del motor de naipes en hardware real
(emulado): **1 mano completa** (reglas + showdown) **+ 1 muestra de equity**, leído
como **líneas de raster** de un frame PAL (313 líneas) con `current_raster_line()`
(patrón de polling, igual que `000` y `games/100_chess`).

Publica el resultado en `g_eng_run_status.detail`:

```text
bits 31..16  líneas del tick más caro (max_lines)
bits 15..0   unidades/s si la unidad cabe en un frame (0 si no cabe)
```

y lo dibuja en pantalla. El perfil se elige en compilación con
`-DCARDS_BENCH_PROFILE=<0..4>` (`0`=N20 … `4`=N512, por defecto `0`).

## Evidencia medida (18/09/2026)

Perfil **N20** en **A500 (68000), release `-O2`**:

| Magnitud | Valor |
|---|---|
| Líneas de raster por unidad (1 mano + 1 muestra) | **290** |
| Presupuesto de frame PAL | 313 |
| Unidades/s (derivado: `50·313/290`) | **≈53** |

Conclusión: en un A500 base, el perfil `N20` **cabe en un frame** (290 < 313) con
margen; el perfil **N64 no llega a READY** en la ventana de 40 s con la misma carga
(el coste del Monte Carlo por decisión lo saca del presupuesto). La matriz completa
por perfil y CPU es el paso siguiente (C4.3 del roadmap).

Evidencia: `out/run/124_cards_bench/A500_release/run-report.json` (`status=ok`,
`detail=0x01220035`). Comandos:

```bash
bash tools/build/build-demo.sh demos/amiga/124_cards_bench --release --clean
bash tools/run/run-demo.sh demos/amiga/124_cards_bench --warp
# variante por perfil:
EXTRA_DEFINES="-DCARDS_BENCH_PROFILE=1" bash tools/build/build-demo.sh demos/amiga/124_cards_bench --release
```

## Estado

- **Build OK** (toolchain m68k) y **run → READY OK** en WinUAE con `--warp`.
- Medida N20 válida; N64 medido como "no cabe" (timeout de la ventana).
- Pendiente: matriz completa perfil × CPU (68000/020/030) y volcado a
  `docs/guides/roadmap/ROADMAP_CARD_GAMES.md` §C4.3.
