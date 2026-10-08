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

## F3 — Experimentos

### F3-A (H1: dependencia de fase de raster) — **FALSADA** (hot-patch)

Experimento: mover el ancla del bucle (`wait_raster_layer`, inmediato de `cmpi.l` en
`0xc11ff4`, valor `0x2c00` = línea 44) **52 líneas en caliente** (poke a `0x6000` = línea 96) y
comparar los contadores 7/8 (restore/dibujo de BOBs) y 10 (periodo). El poke de código se aplica
con GDB (`pause` → `writeMemory` → `continue`); el `poke` del canal lateral con conexiones
separadas no aplica de forma fiable (la cola/lock se suelta antes de ejecutar) — usar GDB para
parchear código.

Resultado: **sin cambio** (19.444 / 102.860 / 284.192 antes y después; revertido y verificado).
⇒ el wall 2,4× de los BOBs **no depende de la banda de raster**; H1 muerta. Queda H2: la
*ejecución* del job bajo nuestras condiciones de bus (el profiler por scanline debe decir dónde
se van los ciclos del Blitter durante los BOBs) y H3: el coste de emisión del `BlobBatch` frente
al código inline (la comparación estática de registros dice que el nuestro escribe **menos**
registros por job que el original: 6 vs 8 — H3 pierde fuerza).

### F3-B (siguiente): profiler por scanline (F2 pendiente)

Volcado `.amigaprofile` de port y referencia en el mismo punto de escena (breakpoint determinista
+ `winuae_profile`). Comparar: actividad del Blitter por línea durante los BOBs, canales DMA
activos, ciclos de CPU. Delta esperado si H2 es cierta: el Blitter del port pierde slots durante
los BOBs (p. ej. Copper/DMA con prioridad distinta) → la columna «Slots de bus» de la tabla
diferencial pasa a ser medida, no modelada.

Caveats de herramienta (medidos):

- Leer registros custom o `VPOSR` con la CPU parada en un watchpoint **no es fiable** (BLTSIZE/CON0
  idénticos para jobs distintos; VPOSR constante 88 en 900 muestras).
- El watchpoint de BLTSIZE **no captura los fills** de posiciones del port (19/update): sus
  escrituras de `BLTSIZE` no disparan el watchpoint (¿camino `blit_fill_word_strided`?); para
  esos, usar los contadores. Los jobs del batch se emiten desde `blitter_blob_run_one`
  (`0xc10a14`/`0xc10c42`).
- Para atribuir: ciclos del canal lateral + deltas entre hits + clasificación por PC; la forma
  del job, de la fuente.
