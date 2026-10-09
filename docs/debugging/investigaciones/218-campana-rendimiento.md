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

### F3-A (H1: dependencia de fase de raster) — **CONFIRMADA** (con matiz)

Experimento: mover el ancla del bucle (`wait_raster_layer`, inmediato de `cmpi.l` en
`0xc11ff4`, valor `0x2c00` = línea 44) **52 líneas en caliente** (poke a `0x6000` = línea 96) y
comparar los contadores 7/8 (restore/dibujo de BOBs) y 10 (periodo). El poke de código se aplica
con GDB (`pause` → `writeMemory` → `continue`); el `poke` del canal lateral con conexiones
separadas no aplica de forma fiable (la cola/lock se suelta antes de ejecutar) — usar GDB para
parchear código.

Resultado: ancla 96 **no mejora** (update 221,9k vs 220,7k; BOBs 137k vs 122,6k) y la lectura
aparentemente idéntica de [7]/[8] fue un artefacto de comparar clases de frame (`m_c32`)
distintas. El barrido controlado de F3-B mostró que la fase **sí** importa (ancla 0 → −40k):
H1 queda confirmada con el matiz de que lo que importa es la **ventana de bus** en que caen los
blits (bordes vs display), no el orden del código en sí.

### F3-B (profiler por scanline) — **HECHO**: el problema es densidad de bus y fase

Medición con el mismo método en port y referencia (`winuae-profile.mjs`; el binario lleva un grid
DMA posicional 227×313: cada celda es un slot de bus en (hpos,vpos); tipos: 1 refresh, 2 CPU,
3 Copper, 5 Blitter, 6 bitplanes, 7 sprites — `include/debug.h:361-372`):

| Slots de bus por campo | Port | Referencia |
|---|---|---|
| Copper + bitplanes + sprites + refresh | 41,9k (idéntico) | 41,9k (idéntico) |
| Blitter | 11.834 (repartido por la zona visible) | 23.019 (17,5k en bordes: líneas 0-31 y 256-312) |
| CPU (bus Chip) | 9.859 | 4.701 |
| Densidad total de bus | 89,7 % | 98,1 % |

Por update: el port mueve **127,4k slots** (2 campos) y la referencia **69,7k** (1 campo). El
trabajo de Blitter por update es el mismo (~23k): la diferencia es (a) el display se paga dos veces
(42k) y (b) la CPU mueve 19,7k slots/update vs 4,7k. **Presupuesto para 50 fps**: 42k (display) +
23,7k (blitter) + ≤5k (CPU) ≈ 71k = exactamente un campo a la densidad de la referencia.

Barrido de ancla por hot-patch (poke del inmediato del `cmpi.l` del ancla en `0xc11ff4`; 6
lecturas por ancla, comparando vectores completos porque la clase de frame (`m_c32`) cambia el
camino ejecutado):

- ancla 44 (actual): update 220-240k; BOBs 100-112k; restores ~20k; periodo estable 284k.
- ancla 0: update **176-179k** (−40k); BOBs **53-55k** (−48k, corren en el borde inferior);
  restores **63-65k** (+44k, corren en mitad del display); periodo inestable (284k casi siempre,
  picos de 426k/710k cuando el update de 1,25 campos pierde la ventana de línea 0).

⇒ La fase importa y mucho: los BOBs bajan de 11,2k a ~5,9k por job cuando caen en el borde. El
siguiente objetivo son los **restores** (reordenarlos al borde superior, donde los fills ya corren
a ~2,3 ciclos/slot) y estabilizar la cadencia.

Caveat de herramienta: el `poke` del canal lateral puede tardar en aplicar (verificar con `mem`);
para parchear código es más fiable GDB (`pause` → `writeMemory` → `continue`).

Mapa de por qué importa la banda (medido; slots de bus por línea):

```
Campo PAL (313 líneas, 227 slots/línea):
  líneas 0-43    borde superior (sin DMA de bitplanes)   ~215 libres/línea
  líneas 44-267  ventana visible (bitplanes+sprites+Copper+refresh ≈ 186)  ~41 libres/línea
  líneas 268-312 borde inferior (sin DMA de bitplanes)   ~215 libres/línea
  ⇒ libre por campo ≈ 89×215 + 224×41 ≈ 28,4k slots; el Blitter corre a ~1 slot/2 ciclos
    cuando cae en los bordes y a ~1 slot/8 ciclos cuando cae en la ventana (compite con
    los fetches de la CPU por los ~41 slots libres de cada línea visible).

Reparto del Blitter por bandas de 32 líneas (slots/campo, grid DMA):

banda:        0-31  32-63 64-95 96-127 128-159 160-191 192-223 224-255 256-287 288-312
REFERENCIA:   6511  1933   667    494     474    1076    1351    1352    4118    4914
              █████ ██     █      ▏       ▏      █       █       █       ████    █████
              \____ borde sup. ____/        \______ ventana (poco) ______/  \_ borde inf. _/
PORT ancla 44: 4003  2262  1278   1383    1295   1251    1238    1307    1053     669
              ████  ██    █      █       █      █       █       █       █        █
              repartido por TODA la ventana → cada job a ~8 ciclos/slot (2× más lento)
```

### F4 (siguiente, fuente, una variable)

E1 = ancla en línea 0; E2 = reordenar restores justo tras el ancla (el original también los lleva
al final, pero con un cuerpo mucho más corto). Medir vector completo de contadores + estabilidad
de periodo; validar con hash determinista + visión.

### F4 — E1/E2 aplicados y medidos (ancla 300 + restores al principio)

- **E1 (fuente, 1 variable)**: ancla 44 → **300** (borde inferior). Update 220k → **~168k**
  (−52k); BOBs 100-112k → **40-54k** (borde); los restores pasan a 65-78k (caen a la ventana);
  periodo estable 284k (2 campos).
- **E2 (fuente, 1 variable)**: restores emitidos al **principio** del update (tras el ancla;
  contador 7 = su wall). Update → **~163,5k**; restores → **20,8k** (3,0 ciclos/slot, borde ✓);
  BOBs → ~55k; `[3]` datos ~57k (el ensamblado cae a la ventana).
- **Suma cero estructural**: con 27,5k slots de Blitter, el hueco de 89 líneas absorbe ~18k
  (a ~2,9 ciclos/slot); los ~9,4k restantes en la ventana cuestan ~11 ciclos/slot (~103k). Suelo
  estimado ≈ 150k: para 1 campo (142k) hay que **recortar ~4-5k slots** (E3: fills de 19 jobs,
  staging, tiles) o tiempo de CPU.
- **Validación (regla de oro)**: secuencia de 6 frames + Ollama (`out/tmp/e2_seq`) → VEREDICTO:
  **CORRECTO** (imagen completa, sin saltos ni tearing).
- **Modelo** (`eng/hw/bus_budget.hpp` + HOST-350): dimensión vertical (hueco 88×215 = 18.920
  slots; ventana con display+Copper ≈ 38-41 slots/línea) + costes constexpr (`blit_cost`,
  `copper_cost`, `cpu_cost`, `with_cost`) + hints `kHintBlitterInDisplay`/`kHintVBlankOverflow`.
  La 218 declara su escena y lleva un `static_assert` tripwire (el Blitter 27.488 > hueco y sin
  plan → detectado al compilar). Al cerrar la campaña, actualizar el tripwire.

### F4b — E2b + barrido fino del ancla: **meseta ~163,5k**

- **E2b** (intercambio fills/restore, 1 variable): 164,0k vs 163,5k de E2 — empate. Los restores
  suben a 37,6k (pagan la ventana al ir segundos) y los BOBs bajan a ~49k. Se conserva el orden
  de E2 (restore primero).
- **Barrido fino del ancla** (líneas 298-308 por hot-patch): 163,1-164,3k — **curva plana**; no
  hay punto mejor. El sistema está en un óptimo local con esta estructura.
- **Conclusión estructural**: con 27,5k slots de Blitter el suelo del reparto es ~163k. Para el
  campo único (142k) hace falta **recortar ~13 % de trabajo real** — candidatos medibles: 9→7
  BOBs (≈ −9k ciclos de pared), área/cadencia del restore, tiles de 3 buffers. Orden y ancla ya
  no dan más.
- **Corrección de E3**: fusionar los 19 fills **no es viable** — el original hace 19 `BlitPattern`
  con el valor POS en `BLTADAT` (`blitter.asm:35-42`, `layer.asm:87-170`), 1 slot/línea; una
  fusión exigiría fuente de patrón (A con puntero) → +4,2k slots. El port ya es fiel aquí y más
  rápido por job que el original (0,65k vs 3,43k ciclos/job).

### F4c — Perfil del plateau (ancla 300 + E2): el Blitter ya está bien colocado

- **Grid DMA** (frame pesado): Blitter 15.777 slots, **82 % en los bordes** (bandas 0-31 y
  256-312; top+bottom) + 2.793 en la ventana; CPU 8.978. Frame ligero: 6.723, todo en bordes.
- Por update: **~22,5k slots de Blitter ≈ la referencia (23k)** y mejor colocados (82 % vs 75 %).
- **Línea temporal de CPU** (muestras del perfil resueltas con el `.map`): bloques dominantes:
  fills ~23k ciclos en `blitter_fill_words_strided` (emisión de 19 jobs + esperas);
  **staging/datos: bloque de ~57k** (el mayor; ensamblado ~7k + 2 jobs + esperas); BOBs con la
  CPU detenida (nasty) mezclada. Las muestras con PC=0 o direcciones fuera del `.text` (0xc42xxx,
  ROM) son **estados de halt del nasty** durante los blits: el sampler no es fiable con la CPU
  detenida (mismo caveat que VPOSR).
- **Lectura**: el Blitter ya no es el problema (colocación y slots ≈ referencia). El ~13 %
  restante es **coste de CPU/serialización por sección**: emisión por job (camino C++ ~550
  ciclos/job vs ~300-400 del asm del original), el bloque del staging (~57k, mayor objetivo
  único) y publicación/contadores. La siguiente campaña (F5) es de **CPU/emisión**, no de fase.

### F5 — Desglose del bloque de staging (contadores 11-14, medido)

Desglose con contadores temporales en `update_layer_data` (frames 8-29):

| Sub-bloque | En ventana (orden E2) | En borde (datos movidos al ancla) |
|---|---|---|
| Ensamblado CPU (224 iteraciones) | **39.956** (~178 ciclos/iteración) | 17.290 (~77) |
| begin + job 1 | 4.966 | 1.730 |
| wall job 1 + emisión job 2 | 6.014 | 1.440 |
| end (job 2) | 4.568 | 934 |
| **Total** | **~55,5k** | **~21,4k** |

- Causa del ensamblado caro: corre en la ventana visible y cada acceso de CPU a Chip cuesta
  ~19-44 ciclos por contención (el código también vive en RAM lenta, que comparte bus).
- **Mover el bloque al ancla no mejora el total** (166-172k vs 163,5k): desplaza
  restore+fills a la ventana (82-105k). El hueco (~57 líneas ≈ 26k ciclos) no aloja a la vez
  ensamblado (17,3k) + restore (21k) + fills (12,5k): suma cero. La asignación de E2
  (restore en el hueco; fills medio-hueco; ensamblado+jobs en la ventana) es la mejor medida.
- **Consecuencia**: el staging lineal (28 jobs → 2 jobs + ensamblado CPU) ahorró slots pero
  **añadió el ensamblado (17-40k de CPU)**, que bajo el modelo de bandas cuesta más de lo que
  ahorra. Siguiente experimento (F5b): **A/B contra la estructura del original** —
  `UpdateLayerData` con los 28 blits directos de medio-tile (sin ensamblado CPU), que además es
  más fiel. Predicción: −17 a −40k (fuera el ensamblado) a cambio de +1,4k slots y +emisión
  (~14k); neto favorable si los 28 jobs caen en hueco/medio-hueco.

### F5b — A/B con la estructura del original: **empate, se queda la del original**

- Implementado `UpdateLayerData` (frames 8-29) con los **28 blits de medio-tile** del original
  (1 palabra × 16 líneas, `AMOD=2`, `DMOD=kSprColMod`, `layer.asm:350-375`); eliminado el scratch
  `m_bg_stage` y el ensamblado.
- Medición: update **~164,5k** (vs 163,5k del staging) — empate; `[3]` datos ~65,8k (vs ~57k).
  Los 28 jobs directos cuestan lo mismo que ensamblado+2 jobs: el ensamblado no era desperdicio,
  era un intercambio.
- **Se conserva la estructura del original** (más fiel y más simple) y se elimina el scratch.
- Validación (regla de oro): secuencia de 6 frames + Ollama → **CORRECTO**; periodo estable
  284k (2 campos).
- **Veredicto de la campaña**: la meseta ~163,5-164,5k es robusta frente a estructura, orden y
  ancla (todos los repartos empatan por suma cero del hueco de bus). Cerrar el ~13% hasta 142k
  exige **recortar trabajo real** (fidelidad): 9→7 BOBs (≈ −9k) o equivalente; es decisión de
  diseño, no de optimización.

Caveats de herramienta (medidos):

- Leer registros custom o `VPOSR` con la CPU parada en un watchpoint **no es fiable** (BLTSIZE/CON0
  idénticos para jobs distintos; VPOSR constante 88 en 900 muestras).
- El watchpoint de BLTSIZE **no captura los fills** de posiciones del port (19/update): sus
  escrituras de `BLTSIZE` no disparan el watchpoint (¿camino `blit_fill_word_strided`?); para
  esos, usar los contadores. Los jobs del batch se emiten desde `blitter_blob_run_one`
  (`0xc10a14`/`0xc10c42`).
- Para atribuir: ciclos del canal lateral + deltas entre hits + clasificación por PC; la forma
  del job, de la fuente.
