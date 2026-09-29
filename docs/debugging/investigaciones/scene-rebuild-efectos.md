# Investigación: `Scene` con reconstrucción por frame + efectos

Bitácora de la migración de la demo `207_sprite_layer` al **modelo de efectos**
(`docs/engine/architecture/EFFECT_MODEL.md`): registrar la capa por `Scene::add_effect` y declarar
su coste con `SpriteLayer::words_estimate()`, en vez de llamar `emit_into(sched)` a mano.

## Estado

- El port **compila** y la demo **llega a READY**.
- El **visual no coincide** con la versión validada de `master`: la banda se desplaza (posición X
  y colores distintos) y se arman **~2 de los 4 canales Copper**, en vez de 4. Es el síntoma que el
  README de 207 describe para la *primera* versión rota («solo algunos canales llegaban a
  armarse»).
- Por eso el port se **revirtió** (no se rompe una demo validada). El código del intento no se
  conserva en el árbol.

## Qué cambia respecto a la versión que funciona

| | 207 (master, funciona) | Port (no funciona) |
|---|---|---|
| Lista de Copper | `SchedulerT<false> sched { m_copper_block }` + `install_copper_list` | `Scene` + `copper::Plan` (doble buffer) + `present` |
| Ciclo | `build_copper()` cada frame | `begin_build()` → `tick()` (efecto) → `materialize()` → `end_build()` → `present()` |
| Emisión | estática + `m_layer.emit_into(sched)` en una pasada | lo mismo, dentro del efecto |
| Coste | — | `reserve_band` + `note_effect_cost({8, words_estimate()})` |

## Descartado

- **Capacidad de copperlist**: `SpriteLayer::words_estimate()` para 8 canales (4 DMA), 40 líneas ≈
  **1369 palabras**; `SceneResources.copper_bytes` por defecto = 4096 B = **2048 palabras**. Cabe.
- **La emisión de la capa**: `emit_into` es idéntica en ambos (un `WAIT` por línea + ráfaga).

## Hipótesis (a comprobar)

1. **Doble buffer + `present`**: tras `compose()` la lista inicial queda en un bloque; el efecto
   reconstruye el otro. Si el `flip`/`commit` (COP1LC) no va sincronizado con VBlank, la pantalla
   puede mostrar un bloque a medio construir.
2. **Seguimiento de línea del `Scheduler`**: `emit_planes_display` + la emisión del efecto dejan el
   contador de línea en un valor distinto al de la pasada única de 207, y `wait_line_safe` coloca
   los `WAIT` de forma distinta → la ráfaga de canales no llega a tiempo.
3. **`materialize()`**: el plan ordena/emite *intenciones*; la capa emite con `move`/`wait_line`
   directos. Comprobar que no hay interacción (p. ej. reordenación) que desplace la ráfaga.
4. **Variante del `Scheduler`** (`SchedulerT<true>` con presupuesto vs `<false>`): comparar el
   comportamiento de `wait_line_safe`/`move`.

## Cómo atacarlo

1. **Test host comparativo**: construir la lista con `Scene`+efecto y con un `Scheduler` crudo, y
   **comparar las palabras** emitidas (deberían ser idénticas salvo el encabezado de la escena).
   Eso aísla si el problema es del `Scene` o del port.
2. Si son iguales, es **timing**: mirar el orden de `WAIT`s y el momento de `commit` (VBlank).
3. Si difieren, es **contenido**: revisar qué etapa añade/quita palabras.

## Referencias

- `demos/techniques/amiga/sprites/207_sprite_layer/` (versión validada).
- `docs/engine/architecture/EFFECT_MODEL.md` (§3-§6), `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`.
- `engine/include/eng/graphics/composition/compose.hpp` (`Scene::tick`, `add_effect`), `copper/plan.hpp`.
