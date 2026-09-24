# Demo 080 — fuego `fire-rgb` (HAM6 320×256, C2P por Blitter)

**Porte 1:1** de `effects/fire-rgb/fire-rgb.c` de `demoscene-repo-orig`. Es la demo
canónica del efecto (la `062` se retiró por ser un subconjunto más lento; el benchmark
C++ vs asm de la `063` vive ahora en `playground/fire-benchmark`): simulación de fuego
**80×64**, conversión a color + escalado ×4 + chunky en una sola pasada, **C2P 4bpp por
Blitter** y display **HAM6 320×256** con **cuadruplicado de líneas por Copper**.

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
- **C2P por Blitter**: la tabla de estados del original tiene **13 fases = 12 blits**
  (dos pasadas de *swap* 8×4 + una de *swap* 4×4) y una última que **parchea la copperlist**
  con los punteros de los 4 planos nuevos; cada blit lo encadena la **IRQ de blit**
  (`on_blit`), de modo que la **CPU no espera al Blitter** (*CPU y Blitter en paralelo*: el
  mecanismo del original, `ChunkyToPlanar` en la IRQ).
- **Display**: `scene::compose` (HAM6 320×256) con `row_repeat = 4` (el escalado
  vertical es gratis por Copper) y doble buffer.

### Detalles del original que importan

- **`dualtab`: color + escalado en una indirección.** Cada entrada son 32 bits: la palabra
  alta es el **calor** (`val·4`, la realimentación que vuelve a `fire`) y la baja el
  **color**, empaquetado como 4 píxeles HAM `[r0 g0 b0 b0 r1 g1 b1 b1 …]` (el
  `scramblePixels` de `gen-dualtab.py`). Una sola indirección resuelve **color y el
  escalado horizontal ×4** (un valor de fuego de 80 de ancho se expande a 4 píxeles → 320).
- **Dos celdas por `u32`.** El bucle lee/suma vecinos como `u32` (= dos `u16`) y usa la
  palabra de 32 bits como **offset en bytes** de la tabla, lo que da gratis la **media** de
  los 4 vecinos (la suma es múltiplo de 4). Es **seguro** porque 4·252 = 1008 < 65536: la
  suma de dos celdas nunca acarrea de la palabra baja a la alta.
- **Escalado ≠ replicar el byte.** El escalado horizontal no es copiar el byte (eso produce
  columnas verticales espurias) sino **expandir cada bit ×4**; aquí ya viene hecho por la
  `dualtab`.

## Criterio de aceptación

- Compila, llega a `Ready` y el análisis visual pasa.
- La captura muestra **llamas pixeladas en bloques 4×4** con degradado suave
  rojo→amarillo, **subiendo desde abajo**, ocupando todo el ancho y **sin líneas verticales**
  ni costuras.
- Si el C2P fallara, la imagen sale corrupta (planos descolocados/no laterales) o con
  bandas; si fallara la visibilidad del fuego, la parte superior no se apaga.

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
`RandomizeBottom + MainLoop` → **2.5–3.1 campos ≈ 16–20 fps**. Nuestro tramo equivalente son
**436.202 ciclos = 961 líneas** → **el bucle de fuego ya está a la altura del original**
(dentro de su rango y en su media); el campo que nos sobra lo pone el C2P (33 KB de DMA
compitiendo en el bus de Chip con el display HAM6). Objetivo realista, por tanto,
**16.7 fps (3 campos)**, no 25.

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

El bucle de fuego cae **dentro del rango del original** (357–439k por su propio profiler):
el port está en paridad. Palancas que quedan: el **C2P** (~1 campo), la **espera de
sincronía** (~1.4 campos medidos) y el **tráfico de memoria del bucle** (reutilizar el long
`D` de una iteración como `B` de la siguiente ahorra ~25 % de las lecturas del fuego).
Detalle en `docs/demos/effects/FIRE_RGB_PORT_PLAN.md` §"Rendimiento vs original".

### La simulación: asm vs C++ (benchmark en `playground/fire-benchmark`)

El benchmark aislado de la simulación (32 pasadas, periférico de depuración) mide:
**C++ 787.792 ciclos/pasada vs asm 705.937** → el **asm es ~12 % más rápido**, por eso es
el camino por defecto (y el C++ el respaldo). El bucle interior es **memoria-dominante** y
`-O1` ya lo genera aceptablemente; la ganancia del asm viene de evitar `__mulsi3` por fila
y de usar post-incremento. Detalle y notas de ABI: `playground/fire-benchmark/README.md`.

Flags de diagnóstico del build (`EXTRA_DEFINES="..."`):

- `-DK_FIRE_ASM=0` — usa la ruta C++ (`MainLoopC`) en vez del asm.
- `-DK_DIAG_SKIP_C2P=1` — no convierte (mide el camino CPU).
- `-DK_BLIT_NASTY=1` — da prioridad de bus al Blitter (`DMACON` BLTPRI); **empeora**.
- `-DK_FIRE_PROF=1|2` — publica ciclos del fuego / del update en `detail`.

## Referencias

- Plan de porte y análisis del original (hot verbatim, fases del C2P, Copper, doble buffer,
  IRQ de blit, y el análisis de rendimiento): `docs/demos/effects/FIRE_RGB_PORT_PLAN.md`.
- `scene::compose` (HAM + repetición de líneas): test **HOST-016**; la simulación del fuego:
  `tests/host/graphics/015_fire_sim`.
- `support/fire_loop.s` (el bucle); C2P: `AmigaBackend::c2p_4bpp_step`/`c2p_4bpp_program`.
- Benchmark aislado de la simulación (C++ vs asm): `playground/fire-benchmark`.
- Demo del C2P **en CPU** (la otra vía, para buffers modestos): `061_c2p_chunky_4bpl`
  (`c2p_1x1_4` portable en `eng/graphics/c2p.hpp` + la asm de Kalms en `support/`).

## Build / run

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/080_fire_rgb --debug --clean
bash ./tools/run/run-demo.sh     demos/techniques/amiga/effects/080_fire_rgb
```
