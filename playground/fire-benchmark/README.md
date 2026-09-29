# playground/fire-benchmark — fuego en **C++ vs asm** (benchmark, no demo)

Benchmark que compara dos implementaciones del MISMO algoritmo de fuego (promedio de 4
vecinos de abajo, buffer `u16[80×64]`):

- `fire_cpp()`: bucle C++ (el compilador calcula los offsets de fila `y*W` con
  `__mulsi3` y accede por índice).
- `fire_asm()`: rutina en `support/fire_asm.s` (punteros en registros y post-incremento,
  sin recalcular offsets), port del `MainLoop` de `fire-rgb` de `demoscene-repo-orig`.

**No es una demo**: no hay efecto que ver (el render es solo el fuego crudo, sin color ni
C2P) y por eso vive en `playground/` y **no** entra en el barrido de demos
(`tools/build/build-all-demos.sh`). La demo canónica del efecto es
`demos/techniques/amiga/effects/080_fire_rgb` (que sí funde simulación + color + escalado + chunky y usa el
C2P por Blitter); su plan de porte es `docs/demos/effects/FIRE_RGB_PORT_PLAN.md`.

Mide los **ciclos de CPU** de 32 iteraciones de cada versión con el periférico de
depuración, usando **checkpoints** (`debugperiph checkpoints`), el patrón de perfilado por
segmentos: `checkpoint(N)` antes y `checkpoint(N+1)` después; el `seg_*` del slot final es
el coste del tramo.

```
slot 10 = benchmark_start        (sin segmento)
slot 11 = fire_cpp_32x           (coste de fire_cpp × 32)
slot 12 = fire_asm_32x           (coste de fire_asm × 32)
```

## Cómo leer el resultado

```
node dist/tools/run/run-demo.js playground/fire-benchmark \
  --settle-ms 15000 --read-debugperiph checkpoints
```

O por canal lateral (`winuae_debugperiph checkpoints`). El benchmark es lento (decenas de
millones de ciclos para 32 iteraciones en un 68000), por lo que el runner necesita
`--settle-ms` alto para capturar el checkpoint final.

## Resultado medido (A500, 68000, -O1)

```
fire_cpp_32x  seg_avg = 25.209.342 ciclos   (787.792 ciclos / pasada)
fire_asm_32x  seg_avg = 22.589.976 ciclos   (705.937 ciclos / pasada)
```

La versión asm es **~12 % más rápida**. El acceso a memoria del bucle interior es el coste
dominante y el compilador a `-O1` ya lo genera aceptablemente; la ganancia del asm viene de
evitar `__mulsi3` por fila y de usar post-incremento en vez de recalcular offsets.

> Nota: este benchmark mide la **simulación** aislada (`fire_asm.s`, sin color ni C2P). El
> bucle que usa la demo 080 es `fire_loop.s`, que funde simulación + `dualtab` + chunky en
> una sola pasada; para medirlo, instrumentar 080 con checkpoints.

## Notas técnicas

- **Contador de ciclos**: la lectura del contador (`0xB7E928`, `cycle_counter`) no está
  disponible en contexto de SO en este build de WinUAE-DBG (la lectura no se intercepta y
  detiene el benchmark), por eso se usan checkpoints (escrituras de long, que sí funcionan).
  Las escrituras de long al periférico (`counter_value`, `checkpoint`) funcionan; la consola
  por byte (`0xB70000`) y las lecturas no están operativas en este modo de lanzamiento.
- **ABI de `fire_asm.s`**: los argumentos se empujan como long (4 bytes) de derecha a
  izquierda (`pea height; pea width; move.l fire,-(sp)`); tras `movem.l d2-d7/a2-a6`
  (11 registros = 44 bytes) quedan en `sp@(48)/sp@(52)/sp@(56)`. Se leen con `move.l` para
  tomar el valor de la word baja.

## Build & run

```bash
tools/build/build-demo.sh playground/fire-benchmark --clean
tools/run/run-demo.sh       playground/fire-benchmark
```
