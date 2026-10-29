# Presupuesto prioritario por frame

Diseño de un **buffer de trabajos con prioridad y deadline de campo** para decidir, por frame, cuánto trabajo decorativo se ejecuta y cuánto se difiere. El objetivo es que el desarrollador elija el perfil (favor fps o favor efectos) sin reescribir la demo, y que el motor deje de encolar trabajo poco prioritario cuando el campo se agota.

Este documento describe el modelo, lo que ya existe en el engine y lo que falta. No describe una implementación completa todavía: la primera fase es un prototipo medible con el protocolo de [`OPTIMIZACION_TILED_ANIMADO.md`](OPTIMIZACION_TILED_ANIMADO.md).

## 1. Modelo

Cada unidad de trabajo declara:

- **clase**: `Essential`, `Gameplay`, `Decor`, `Idle`;
- **coste estimado** en ciclos y en palabras de Blitter;
- **plazo**: el campo (VBlank) en el que debe terminar para no perder cadencia;
- **edad**: frames que lleva diferida.

```text
campo N                         campo N+1
|================================|================================|
  update          render/present    VBlank
  [Essential][Gameplay][Decor...]   ^ deadline de Decor
                                    (se deja de encolar, no se aborta el blit)
```

## 2. Clases y política

| Clase | Ejemplos | Deadline por defecto |
|---|---|---|
| `Essential` | scroll de la banda de guarda, instalación de Copper, present, entrada | siempre; si no cabe, se baja de nivel de fps (25) |
| `Gameplay` | actores, colisión, IA, cámara | 90 % del campo |
| `Decor` | parche de tiles animados, coberturas, partículas, HUD | 90 % (`preserve50`) o 180 % (`target25`) |
| `Idle` | prefetch, calentamiento de caché, compactación | solo si sobra >20 % |

Perfiles:

- `preserve50`: el trabajo decorativo se corta al 90 % del campo; si aun así no cabe, se degrada por `degrade_order` (agua → flores → fuego).
- `target25`: se permite hasta 1,8 campos; el trabajo decorativo se ejecuta a tope hasta el siguiente VBlank, aceptando 25 fps.
- `off`: solo `Essential`/`Gameplay`; las capas vivas se congelan en el último frame válido.

El cambio de perfil debe tener **histéresis**: bajar de 50 a 25 tras N frames por encima del umbral y volver solo con margen sostenido, para no oscilar.

## 3. Deadline y preempción

- **Señal**: la IRQ de VBlank (el motor ya la recibe y despierta a `Wait()`). No se sondea `VPOSR` desde la lógica de juego; el servicio de frame publica el latido y el deadline.
- **Comprobación**: entre lotes pequeños (1–4 jobs), no por instrucción ni por píxel. El coste de leer el reloj o el flag debe ser despreciable frente al lote.
- **Preempción**: un job de Blitter **no se aborta** a mitad (dejaría el display a medias). Se termina el lote en curso y se deja de encolar. Para cadenas largas, el feeder asíncrono por IRQ permite parar entre jobs; el `FramePlan` síncrono debe usar lotes acotados por palabras/jobs.
- **Present**: se reserva un colchón fijo (p. ej. 10 % del campo) para componer/instalar la Copper y presentar. Si `Essential` no cabe, se declara el frame perdido y se baja de nivel; nunca se presenta roto.

## 4. Anti-starvation y aging

Un trabajo `Decor` diferido acumula **crédito**: +1 por frame. Al superar K (p. ej. 8), sube a `Gameplay` durante un frame. Así una ráfaga de agua que no cabe con el presupuesto no se queda pendiente para siempre, que es exactamente el fallo observado en 207 cuando el tope de jobs no drenaba la lista de grupos cambiados.

El cursor round-robin actual es una forma débil de aging; el crédito por trabajo lo hace explícito y medible (`groups_deferred` y la edad máxima).

## 5. Encaje en el engine

Lo que ya existe:

- `FramePlan` con presupuesto de jobs y palabras (`set_blit_budget_limits`, `BlitBudgetReport`).
- `TileAnimationBudgetPolicy` (`Off`/`Preserve50`/`TargetFps`) y `degrade_order` en el contrato del cook-plan.
- `pass_deferred` en el parche de tiles y cursor round-robin por capas.
- IRQ de VBlank + `Wait()` (`Engine::run_frames`) y feeder asíncrono de Blitter (`execute_frame_plan_async`).
- `eng::os` con puerto, tareas y timers para trabajo de CPU asíncrono.

Lo que falta:

1. Un campo `priority` (y `estimated_cost`) en `BlitJob` o una cola de trabajos tipada por clase.
2. Un `FrameDeadline` que publique el VBlank y permita `expired()`/`remaining()` entre lotes.
3. Un `PriorityBudget` que reparta el presupuesto del frame por clase, con aging y perfiles con histéresis.
4. Telemetría: trabajos ejecutados/diferidos por clase, edad máxima, deadline alcanzado.

**Estado del prototipo en 207 (2026-10-29).** La política de demo ya aplica una degradación por capas con histéresis: por encima de ~41 fps (1,25 campos) se congela el agua (capa ambiente) y se reanima tras 4 frames por encima de 45 fps; la fogata y las flores no se degradan. Con el tope fijo de 8 jobs como referencia, la fogata sube de 37,41 a 44,09 fps y la visita de 46,70 a 47,60 (mínimo 35,7 → 44,0). No usa `FrameDeadline` ni `BlitJob::priority`; es la referencia que el scheduler de engine debe igualar o superar. El anillo de vencimientos de `set_tick` (256 buckets) también está implementado.

La integración natural es una capa sobre `FramePlan`: el plan sigue siendo el destino de los jobs, y el scheduler decide **qué se encola y en qué orden**; el Blitter no cambia. Para CPU (patch_build, IA) el mismo scheduler puede alimentar tareas de `eng::os`.

## 6. Trampas conocidas

- **No medir el VBlank con polling**: la demo no debe escribir `INTENA` ni leer `VPOSR`; es fontanería del engine.
- **No confundir espera con trabajo**: `cycles/frame` incluye la espera; el scheduler debe usar `W` (inicio de update a fin de render) y un deadline de campo, no el suelo de VBlank.
- **No reordenar jobs que se solapan**: restore/draw de una celda debe conservar el orden; la prioridad no autoriza a romper la composición.
- **No dejar el trabajo decorativo sin aging**: el tope bajo produce backlog permanente (207 ya lo midió).
- **No presentar un frame a medias**: si el trabajo esencial no cabe, se pierde el frame completo y se baja de nivel.

## 7. Plan de evaluación

1. Implementar el `FrameDeadline` y la telemetría (clase, ejecutados, diferidos, edad) sin cambiar el orden actual.
2. Medir el coste de las comprobaciones entre lotes (debe ser <1 % del frame).
3. Prototipo `preserve50` sobre el parche de tiles: deadline al 90 % y aging K=8; comparar con el tope fijo de 8 jobs.
4. Añadir `target25` con histéresis y medir la ventana de la fogata.
5. Solo si el prototipo mejora `M` o estabiliza la cadencia, subir el scheduler al engine y exponerlo en la API pública (`App`/`Scene`).
