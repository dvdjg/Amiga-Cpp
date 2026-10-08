# Campaña de rendimiento de la 218 («SPR Layer») — bitácora de decisiones

Método: [`METODO_OPTIMIZACION.md`](../../guides/optimization/METODO_OPTIMIZACION.md). Objetivo:
explicar y cerrar (o declarar irreductible) el hueco de cadencia entre el port y la referencia
(`spr_layer_ref`, Jeroen Knoester). **No se re-miden los números de esta tabla**: son el baseline
común (F1) medidos con el método descrito; cualquier sesión nueva empieza replicando el último
gate verde.

```text
□ Estado de escena congelado (port y referencia en el mismo plan de trabajo; capturas deterministas)
□ Métrica de periodo común registrada (ref + port)  → F1 HECHA (abajo)
□ Cota inferior estática de bus calculada           → pendiente (F2, siguiente)
□ Volcado del profiler por scanline en ambos        → pendiente (F2, siguiente)
□ Filas de la tabla de presupuesto pobladas         → semilla abajo (atribución medida)
□ Siguiente experimento: ver «F3 propuesto»
```

## F0 — Fidelidad (verde)

Port y referencia con el mismo plan de trabajo (copperlists, estructuras de sprite, mismo
calendario de 32 frames y mismo reparto de blits por frame: 14 en frames 0-7, 28 en 8-29 — la
cuenta de jobs/loop de la referencia lo confirma). Fidelidad píxel a píxel ya validada en la
pantalla de título; el hash determinista de copperlists (update exacto, punteros enmascarados) es
el gate para cambios de emisión.

## F1 — Baseline común (medido; NO repetir)

Método: watchpoints de **BLTSIZE** (`0xDFF058`, 2 B, write) + **COP1LC** (`0xDFF080`, 4 B, write),
clasificación de hits por PC, ciclos del canal lateral (`state.cycles`, 256 unidades = 1 ciclo
CPU) en cada stop. 900 hits por tanda; 17-19 loops completos por tanda. En el port: COP1LC en
`0xc0e2c8` (engine, `install_copper_list`); en la referencia: `0xc0cfaa`.

| Métrica | Referencia | Port |
|---|---|---|
| Periodo de loop (ciclos CPU) | **141.6-142.6k = 1,00 campo** (50 loops/s) | **284,2k = 2,00 campos** (25 updates/s) |
| Jobs de Blitter por loop | **40-41** (frames 0-7) / **54-55** (frames 8-29) | ~35/update (19 fills + 2 staging + 3 tiles + 11 BOBs) |
| Wall de fills de posiciones (`BlitPattern`, 1×224) | 3.430 (mediana, n=162) | **1.842** (35k/19) — *el port gana* |
| Wall de copias/columnas (`BlitCopy`, 1 palabra × 16 líneas) | 1.252 (mediana, n=619) | staging: 20.200/job (2 jobs de 1×224) |
| Wall de restores de BOBs | 9 copias de 3×128 (dentro de `BlitCopy`) | 2 jobs de 15/12×128 ≈ 10k/job |
| Wall de dibujo de BOBs (`BlitBob`, 3×128, amod −2, dmod 38) | **4.636** (mediana, n=100) | **11.200** (100,7k/9) |
| CPU tras publicar COP1LC | 1.844 (COP1LC→siguiente BLTSIZE) | ~4k (contadores+puntero FG tras publicar) |

Formas de job **idénticas** (confirmado por fuente: `bob_bsize=(32*4)<<6|3` = 3 palabras × 128
planelíneas; `amod=$fffe`; `dmod=buffer_modulo-bob_bwidth=38`; `BlitPattern` 1×224).

## F2 — Presupuesto diferencial (semilla, atribución medida)

| Unidad | Ref (ciclos/loop) | Port (ciclos/update) | Delta | Confianza |
|---|---|---|---|---|
| Fills de posiciones (19) | 65.200 (19×3.430) | 35.000 | **−30.200 (port gana)** | alta |
| Columnas de datos | ~35.000 (28×1.252) | 40.400 | +5.400 | alta |
| Restores de BOBs | ~11.300 (9×1.252) | ~20.000 | +8.700 | media |
| Dibujo de BOBs (9) | ~41.700 (9×4.636) | 100.700 | **+59.000** | alta |
| CPU+publicación+resto | ~26.000 | ~25.000 | ≈0 | media |
| **Total** | **~142.000 (1 campo)** | **~220.600 (2 campos)** | **+78.600** | alta |

**Conclusión de F2 (atribución)**: el hueco son los **BOBs** (+59k) y sus restores (+9k): mismos
tamaños, mismo minterm, mismos módulos, pero el wall por job del port es **2,4×** el de la
referencia. El resto de secciones está a la par o mejor. La siguiente pregunta es *por qué* el
mismo blit tarda 2,4× más: hipótesis a falsar en F3 (orden/raster phase, estado de `BLTPRI`
alrededor del job, y coste de emisión del `BlobBatch` frente al código inline del original).

## Caveats de herramienta (medidos; no repetir el error)

- **Leer registros custom o `VPOSR` con la CPU parada en un watchpoint no es fiable**: devuelven
  valores internos (BLTSIZE/CON0 idénticos para jobs distintos; VPOSR constante 88 en 900
  muestras). Para atribuir: **ciclos del canal lateral + deltas entre hits + clasificación por
  PC**; para la forma del job, la fuente.
- El canal lateral (`state.cycles`) es la referencia temporal fiable (256 unidades = 1 ciclo CPU).

## F3 — Siguiente experimento (una variable)

**Hipótesis H1**: el wall 2,4× de los BOBs es de *emisión/orden* (fase de raster y estado
`BLTPRI`), no del blit en sí.

- **Experimento A (fuente, 1 variable)**: reordenar `draw_bobs` justo tras el ancla (línea 44),
  replicando el orden del original (`.updcp` → punteros FG → restore → BOBs), sin tocar nada más.
- **Métrica**: wall por job de los BOBs con `ref_probe.mjs` (PCs del port) + contadores 4/7/8 +
  periodo (10).
- **Delta esperado**: si H1 es cierta, el wall de los BOBs baja de ~11,2k a ~4,6k (−60k en el
  update) → periodo ~142k = 1 campo.
- **Falsación**: si el wall de los BOBs no baja ≥50 % del esperado o el hash de copperlists
  cambia → H1 falsa; revertir y pasar a H2 (coste de emisión del `BlobBatch` vs código inline).
