# Demo 080 — fuego `fire-rgb` (HAM6 320×256, C2P por Blitter)

**Porte 1:1** de `effects/fire-rgb/fire-rgb.c` de `demoscene-repo-orig`. Es la demo
canónica del efecto: simulación de fuego **80×64**, conversión a color + escalado ×4 +
chunky en una sola pasada, **C2P 4bpp por Blitter** y display **HAM6 320×256** con
**cuadruplicado de líneas por Copper**.

## Qué muestra

```
 fuego 80×64 (u16)            una pasada de asm            4 planos 320×64        HAM6
 ─────────────────  ────────────────────────────────  ─────────────────────  ─────────────
  promedio de 4      MainLoop: 4 vecinos + dualtab      C2P 13 fases por        Copper repite
  vecinos de abajo   (color+escalado) + store chunky     Blitter (IRQ de blit)   cada línea ×4
```

- **`support/fire_loop.s`** (`MainLoop`): funde la simulación, el doble lookup de
  `dualtab` (que codifica color HAM **y** el escalado horizontal ×4) y la escritura del
  chunky, todo en una pasada sobre el buffer. `RandomizeBottom` + `fastrand` son asm
  verbatim del original. La ruta C++ equivalente (`MainLoopC`) queda como respaldo
  (`-DK_FIRE_ASM=0`).
- **C2P por Blitter**: 13 fases (`MinimalBackend::c2p_4bpp_step`) encadenadas por la
  **IRQ de blit** (`on_blit`): la CPU no espera al Blitter. Es el mecanismo del original
  (`ChunkyToPlanar` en la IRQ), es decir **CPU y Blitter en paralelo**.
- **Display**: `eng::graphics::drivers::HamScene` con `row_repeat = 4` (el escalado
  vertical es gratis por Copper) y doble buffer.

## Rendimiento medido (WinUAE-DBG, A500, `-O1`)

```
node tools/debug/measure-fps.mjs 080_fire_rgb
  080_fire_rgb/A500_debug | emulado=12.4-12.6 fps | 564.668-572.197 ciclos/frame (4.0 vblanks)
```

A/B con `EXTRA_DEFINES` (saltando el C2P) para aislar su coste:

| variante | fps | vblanks/frame |
|---|---|---|
| baseline | 12.4–12.6 | 4 |
| `-DK_DIAG_SKIP_C2P=1` | **16.70** | 3 |

Lectura: el frame está **cuantizado a vblanks** y el C2P cuesta ~1 vblanc del camino
crítico; los 3 vblancs restantes son el camino CPU (fuego + conversión + engine + DMA de
display). Para bajar de 4 a 3 vblancs hay que ocultar el C2P o amortiguar el bucle de
fuego (p. ej. actualizándolo cada 2 frames).

### Comparación con el original (por qué "igualarlo" = 3 campos)

El original se sincroniza igual (un `Render` **por tick de VBlank**, `system/effect.c`) y
su propio profiler le atribuye **788–968–976 líneas de raster** al
`RandomizeBottom + MainLoop` → **2.5–3.1 campos ≈ 16–20 fps**. Nuestro "sin C2P" son
**424.899 ciclos ≈ 936 líneas** → **el bucle de fuego ya está a la altura del original**;
el campo que nos sobra lo pone el C2P (33 KB de DMA compitiendo en el bus de Chip con el
display HAM6). Objetivo realista, por tanto, **16.7 fps (3 campos)**, no 25.

Detalle del análisis y de los experimentos (colocación en Slow RAM del `fire`/`dualtab`,
copia de la rutina a Slow —medida y revertida—, y el instrumento pendiente):
`docs/demos/effects/FIRE_RGB_PORT_PLAN.md` §"Rendimiento vs original".

### Ciclos medidos (instrumento `-DK_FIRE_PROF`)

La demo puede publicar el coste de un tramo en `g_eng_run_status.detail` (que
`measure-fps` imprime): `-DK_FIRE_PROF=1` = `RandomizeBottom+MainLoop`; `=2` = el
`update` completo. Medido (A500 `-O1`, ciclo-exacto):

```
frame                        570.303-572.197 ciclos  (4.03 campos, 12.4 fps)
RandomizeBottom+MainLoop     436.202 ciclos  (3.07 campos)
update completo              376.710 ciclos  (2.66 campos)
sin C2P (K_DIAG_SKIP_C2P=1)  424.899 ciclos  (3.00 campos, 16.7 fps)
```

El bucle de fuego (376–436k) cae **dentro del rango del original** (357–439k por su
propio profiler): el port está en paridad. El bucle del fuego y la sincronía de frame/el
C2P son las palancas que quedan (ver el plan de porte).

Flags de diagnóstico del build (`EXTRA_DEFINES="..."`):

- `-DK_FIRE_ASM=0` — usa la ruta C++ (`MainLoopC`) en vez del asm.
- `-DK_DIAG_SKIP_C2P=1` — no convierte (mide el camino CPU).
- `-DK_BLIT_NASTY=1` — da prioridad de bus al Blitter (`DMACON` BLTPRI).

## Referencias

- Plan de porte y análisis del original (hot verbatim, 13 fases, Copper, doble buffer,
  IRQ de blit): `docs/demos/effects/FIRE_RGB_PORT_PLAN.md`.
- `HamScene` (HAM + repetición de líneas): test **HOST-016**.
- `support/fire_loop.s`; C2P: `MinimalBackend::c2p_4bpp_step`/`c2p_4bpp_program`.
- Benchmark aislado de la simulación (C++ vs asm): `playground/fire-benchmark`.
- La variante simplificada `062_fire_c2p` (mismo origen, sin HAM y con C2P naive en CPU)
  se retiró por ser un subconjunto más lento de esta.

## Build / run

```bash
bash ./tools/build/build-demo.sh demos/amiga/080_fire_rgb --debug --clean
bash ./tools/run/run-demo.sh     demos/amiga/080_fire_rgb
```
