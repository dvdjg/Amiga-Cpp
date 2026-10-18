# Atribución de ciclos en demos del engine (2026-10)

Registro de **dónde se van los ciclos** medido en demos reales, con el coste por operación del
engine en 68000/OCS, los antipatrones que los producen y el método para atribuirlos. Objetivo:
que un hilo nuevo **no vuelva a diseñar un bucle caliente alrededor de una operación que cuesta
miles de ciclos**. Complementa a [`METODO_OPTIMIZACION.md`](METODO_OPTIMIZACION.md) (campaña y
presupuesto de bus) y a [`METODOLOGIA_PROFILING.md`](METODOLOGIA_PROFILING.md) (pipeline de
medida).

## Método (el que funcionó)

1. **Medir el total primero**: `node tools/debug/measure-fps.mjs <demo> A500_debug --json`
   (ciclos/frame con el contador del periférico de depuración; 1 campo PAL ≈ 141.876).
2. **Atribuir por secciones**: instrumentar el bucle con `ENG_PROF_INIT(n)` + `ENG_PROF_FRAME()` +
   `ENG_PROF_BEGIN(i)/END(i)` y leer con `node tools/debug/profile.mjs <demo>` (las secciones del
   propio engine — `prof_sort_lines`/`prof_sort_prio`/`prof_emit` — van incluidas). Si una sección
   sale inexplicablemente cara, **sub-bisecarla** con más secciones (4, 5, 6…) o **sustituir su
   cuerpo por uno vacío** (experimento de una variable) para separar coste de escritura de coste
   de llamada.
3. **Ablación**: desactivar un elemento y medir (p. ej. sin intents del BOB) para acotar su peso.
4. Registrar la tabla en el `VALIDATION.md` de la demo y, si es un caso general, aquí.

## Coste por operación (medido, no estimado)

| Operación | Coste medido | Dónde se midió |
|---|---|---|
| `Surface::fill_rect` (cualquier tamaño, sink CPU barato) | **~6,5k ciclos por LLAMADA** (+10 fills de 2×2 = +65k) | 110, 2026-10 |
| Job de Blitter encolado (encode + ejecución con estado compartido) | **~3-5k por job** | 110 (60 jobs ≈ 282k), 066 (cadena de lanzamiento ≈ 8k/job) |
| Job de relleno (`FillRect`) en `execute_frame_plan` | ~4,7k por job (espera por job en la ruta fill) | 110 (versión diferida de 60 jobs = 850k) |
| Blit inmediato del backend (`blitter_fill_rect`, `submit + wait`) | **~10k por llamada** | 110 (ruta original: ~30 rects ≈ 300k) |
| `Plan::materialize` de ~85 intenciones | **194k** (sort_lines 37k + sort_prio 33k + emit 124k ≈ 2,3k/intención) | 085 (ruta previa) |
| Escritura/lectura a Chip con display DMA activo | **~30-70 ciclos por acceso** (RMW ≈ 60-140) | 085 (disco CPU), 110 (sink CPU) |
| Copia CPU byte a byte a Chip (45×3×8 B) | ~101k/frame | 085 (ruta previa) |
| Parcheo de una palabra de copperlist | ~decenas de ciclos por palabra (rango pequeño) | 085 (post-fix: 17k con ~50-70 parches + cielo) |
| `Wait()` de exec bajo SO vivo | congela la tarea (VERTB borrado en el cambio de tarea) | 126 (DT-011) |

## Antipatrones recurrentes (checklist para hilos nuevos)

1. **Re-emitir/ordenar la copperlist cada frame** cuando solo cambian colores o un par de
   punteros: construir **estático** (una vez por buffer) y **parchear palabras de DATO** con los
   slots del `Plan` (`slot_count`/`slot_word`/`slot_reg`/`slot_line`) o índices capturados al
   emitir (066, 085). El materialize/sort se paga en el setup.
2. **Llamar primitivas de dibujo por rectángulo/objeto en bucles**: `fill_rect` ≈ 6,5k/llamada.
   Encapsular en hojas pre-renderizadas + `CopyRect` (2 jobs por objeto: borrado + copia) o
   escribir el bitmap directamente en CPU para elementos diminutos (balas, puntos).
3. **Blits inmediatos (`submit + wait`) en bucles**: ≈ 10k por rect. Los jobs encolados cuestan
   ~3-5k; si el elemento es pequeño, casi siempre gana el CPU directo.
4. **Dibujar capas vivas sin doble buffer tarde en el frame**: si el lienzo es single-buffer, el pintado debe terminar antes de que el haz alcance la zona (y aun así hay riesgo); el arreglo real es doble buffer + flip en la copperlist (110 F4). Con capas **por bandas** el orden de pintado es la palanca barata: **de arriba abajo** — la capa viva del juego primero (sus blits se ejecutan en la parte alta del campo, antes de que el haz llegue a sus líneas) y la franja del HUD después (el haz tarda ~0,81 de campo en llegar). Medido en la 302: con el HUD primero el actor parpadeaba (22 de 40 frames visibles, alternando); con juego primero, 40/40. Regla: [`HUD_SYSTEM.md` §9.3](../../engine/architecture/HUD_SYSTEM.md).
5. **`Wait()` bajo el SO vivo** (212): usar `set_vblank_sleep(false)` o `takeover_system`
   (DT-011, demo 126).
6. **Mover/borrar BOBs con el CPU byte a byte en Chip**: pre-renderizar 16 variantes de offset de
   palabra y copiar con el Blitter (085: 101k → ~20k de programación de 2 blits).

## Casos medidos

### 085 `copper_plan_scene`: 426.733 → 142.102 ciclos (16,62 → 49,92 fps)

| Foco | Antes | Después | Técnica |
|---|---|---|---|
| `materialize` + resto de `build_frame` | ~313k | ~0 (setup) | lista estática por bitmap + slots (`bind_slots`/`patch_words`) |
| Dibujo del disco (CPU byte a byte) | ~101k | ~20k | Blitter: borrado desde fila de ceros (módulo −8) + copia de variante pre-shifteada de 16 offsets |
| Parcheo por frame | — | ~17k | rango actual ∪ anterior del disco + 16 bandas de cielo |

Detalle y visión: `demos/techniques/amiga/copper/085_copper_plan_scene/VALIDATION.md` (DT-007).

### 110 `ylimited_shooter`: 570.691 → 426.733 ciclos (12,43 → 16,62 fps; rediseño FG pendiente)

Atribución (426,7k): **FG 230k** (nave+balas ~120k, torreta ~108k), `update_scroll` 44k, compose
18k, plan BG 10,7k, trig 1,3k; resto espera. Bisect del FG: **con el sink vacío el bloque sigue
costando ~97k** → el coste es **por llamada** (~6,5k), no por escritura; la versión diferida (60
jobs de Blitter) empeora a 850k por la espera por job. Arreglo aplicado: los fills van por el
sink (`RectFillSink`) y el raster del lienzo en `AccelMode::Blitter` (antes los rects < 64 px
caían al camino CPU por píxel). **Pendiente (camino medido, DT-001 F4):** hojas pre-renderizadas
de nave/torreta (16 variantes de offset de palabra) + 2 `CopyRect` por objeto + balas/cañón por
CPU directa → FG ~25k → total ~100k (~48 fps).

### 302 `hud_band`: 26 → 49,9 fps (rellenos del HUD por CPU)

Atribución del frame con el HUD por deltas y el actor por el sistema de objetos (SaveUnder): secciones medidas con `ENG_PROF` (media por frame, 21 frames): `apply` del HUD **21,5k**, `draw_world` 26,5k, `present` 34,4k; el resto (bucle, espera de VBlank, update) ~60k. El pico estaba en `apply`: cada `fill_rect` de la franja (pips 8×8, deltas de medidor, marcos) superaba el umbral de 64 px de `AccelMode::Auto` y caía al **Blitter inmediato** (`RectFillSink`, submit+wait ~10-14k por rect); el primer repintado (varios rects) costó ~208k. Con `HudStrip::bind` fijando `AccelMode::Cpu` en el lienzo de la franja (la ruta CPU de referencia del HUD), `apply` bajó a ~16k (media con el pico inicial incluido) y el frame pasó de 269k (2 campos, ~26 fps) a 142k (1 campo, **49,9 fps** medidos en pantalla con `HudFpsWindow`). Contraste con la 110: allí los fills eran grandes (silueta de nave/torreta) y ganaba el Blitter; aquí son rectángulos pequeños y gana la CPU — la regla es el **tamaño del rect frente al arranque** (antipatrón 3).

Detalle y visión: `demos/features/ui/amiga/302_hud_band/VALIDATION.md`.

## Cómo citar

- El coste por operación de arriba es de **estos** casos (A500, display activo); antes de
  extrapolar a otro contexto, medir con el método de §Método.
- Un hallazgo de hardware/emulador requiere `fichero:línea` (AGENTS §1.12); un hallazgo de coste
  requiere la **tabla de secciones** y el comando exacto.
