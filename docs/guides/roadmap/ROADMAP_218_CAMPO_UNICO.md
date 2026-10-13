# Roadmap: 218 «SPR Layer» a 1 campo (50 updates/s)

Objetivo: cerrar el hueco de cadencia del port de la 218 respecto al original (REF: **1,00 campo
por iteración, 141,6-142,6k ciclos**; port: **~163,5-164,5k**, 2 campos = 25 updates/s) manteniendo
la máxima fidelidad al efecto (9 BOBs, 19 columnas, calendario de 32 frames).

Estado y evidencia: [`218-campana-rendimiento.md`](../../debugging/investigaciones/218-campana-rendimiento.md)
(registro completo de aciertos/fracasos, tabla diferencial F2, perfil del plateau F4c, desglose del
staging F5). Método obligatorio: [`METODO_OPTIMIZACION.md`](../optimization/METODO_OPTIMIZACION.md).

```text
Port: 220,7k ──(ancla 300)──▶ ~168k ──(restore al inicio)──▶ ~163,5k ──(F5b 28 blits)──▶ ~164,5k
REF:  142k (1 campo)
Hueco: ~21,5k (~13 %) · Colocación del Blitter: YA ≈ referencia (82 % de slots en bordes)
Residual: emisión CPU por job (camino C++) + volumen total de trabajo
```

## Lo que está agotado (no repetir)

- Reordenar secciones o mover el ancla: meseta plana medida (suma cero del hueco de bus; barrido
  298-308 = 163,1-164,3k; E2/E2b empatan).
- Fusionar los 19 fills: no viable (el original usa 19 `BlitPattern` con `BLTADAT`; una fusión
  exigiría fuente de patrón → +4,2k slots).
- Staging lineal de columnas: empatado y descartado (F5b); eliminado del código.
- Tiras de BOBs (barrel shifter entre celdas): descartado con medición (§registro de la bitácora).
- Cola de blits por IRQ: sin ganancia aquí (no hay CPU que solapar).

## Plan (orden estricto; una variable por experimento; gate con visión)

### Paso 1 — A/B 7 BOBs (reducción de trabajo real) · HECHO (medido)

- Resultado: **cuerpo 163,5k → ~153,5-155k (−10k real**, pico 152,7k); unión del restore
  derivada (15/12 → 12/9 palabras). Insuficiente solo (faltan ~11-13k) y **cadencia inestable**
  (frames ligeros ya a 1 campo: periodo 142.098 medido; pesados a 2) → **revertido a 9 BOBs**
  (fidelidad + cadencia estable) con el lever parametrizado (`kBobCount` + uniones derivadas).
- Lección: no volver a reordenar (agotado); el residual es emisión + volumen.

### Paso 2 — Medir emisión PURA por job · HECHO (contador + desensamblado)

- `blitter_blob_run_one` (CookieCut, fuera de la espera nasty): **~38 instrucciones** + emisión
  **medida con contador temporal: 290-356 ciclos/job** (desde el fin del wait hasta `BLTSIZE`).
- Original (`BlitBob`): ~30-35 instrucciones y 8 `move.l` a custom, con CON0+CON1 precalculados
  en tabla (`bl_bob`) → sobrecoste real **~100-150 ciclos/job** (no 150-250), ≈ **6-9k** en los
  ~59 jobs/update. Hallazgo extra: cada job arranca con nasty OFF (lo activa el `wait()` del
  siguiente), penalizando más a los jobs diminutos.

### Paso 3 — Aplanar el hot-path · ABIERTO (cuenta revisada: no cierra solo)

Ordenado por impacto/esfuerzo:

1. **Tabla `bl_bob`-style de CON0+CON1 por shift** (16 entradas) — el ahorro principal.
2. **Especialización por operación** (`template<BlobOp Op>`) para eliminar los checks runtime.
3. **Tabla de destinos/shifts precalculada** por frame en el llamador.
4. **Comprobar el `wait()` sin servicio** (sondeo `BBUSY`+nasty puro).
5. **Asm del `one`** solo si el ahorro medido ≥ 100 cc/job (medido ~100-150: al filo).
- **Cuenta revisada**: 7 BOBs (−10k medido) + emisión plana (−6-9k) ≈ **146-148k — aún corto**
  del umbral de 142k. Para cruzarlo haría falta además otro recorte de contenido
  (tiles 3→2 buffers, área de restore) — decisión de fidelidad del usuario.
- Gate de éxito: cuerpo ≤ 142k **con periodo uniforme de 1 campo** (la mezcla 50/25 Hz medida en
  el A/B de 7 BOBs es inaceptable para la demo).

### Paso 4 — Otros recortes reales (si el paso 1 no basta y se quiere seguir)

- Área/cadencia del restore (la unión de celdas ya es exacta por fila; explorar solo si el
  movimiento de BOBs permite menos palabras).
- Tiles FG (3 buffers → 2) si el tercero solo alimenta el restore limpio.
- Candidatos a medir con la misma checklist (1 cambio + contadores + falsación).

### Cierre — declarar suelo si aplica

Si con 9 BOBs y el calendario exacto el cuerpo no baja de ~150k, **declarar el gap irreducible
(~15 %)** en `VALIDATION.md` y en el README, con la atribución medida (emisión C++ + volumen), y
dejar el 25 updates/s documentado como excepción de presupuesto (AGENTS §3).

### Cierre — **50 fps alcanzado con recorte de contenido autorizado** (F8)

- Con 9 BOBs el cuerpo (~163,5k) es estructuralmente > 1 campo (suelo ~150-158k con palancas
  fieles), y la cadencia de 25 Hz producía parpadeo visible (alternancia de `SPRxCTL` a 12,5 Hz).
- **Iteración medida**: 7 BOBs = 153,5-155k (insuficiente + mezcla 50/25 Hz); 5 = 145,5-152,3k
  (peor clase aún en 2 campos); **3 BOBs = 130,3-137,1k con periodo 142,1k uniforme = 50 fps**
  (12 lecturas, todas las clases de frame, 5-12k de margen). Visión: CORRECTO.
- El tripwire de compilación saltó con el recorte (el Blitter ya cabe en el hueco) y se actualizó
  al estado viable; de paso se corrigió la fórmula de las uniones del restore
  (`(n−1)·6 + 3`; ver bitácora §F8).
- Fidelidad: 3 BOBs es la única concesión (autorizada); estructura, calendario, 28 blits, nasty y
  4 listas permanecen fieles al original.

## Trabajo de engine derivado (generalizable, no solo 218)

- **API de Copper** (plantilla + slots de parche + 2/4 buffers + publicación de `COP1LC`): las 4
  copperlists del original son optimización de diseño (reparten DATA en 32 frames y evitan tocar
  la lista visible); el patrón es reusable por scrollers, HUD de sprites y bandas. El port ya lo
  modela; formalizarlo en el engine cuando haya un segundo consumidor.
- **Hot-path de `BlobBatch`** (paso 3): beneficia a cualquier juego con muchos BOBs de forma fija.
- **Modelo de bus vertical**: ya en `eng/hw/bus_budget.hpp` + `BUS_BUDGET.md`; usar en el diseño
  de cada efecto nuevo (prevención > campaña).
