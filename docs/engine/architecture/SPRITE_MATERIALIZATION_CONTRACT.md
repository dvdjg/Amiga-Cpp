# Contrato de materialización de sprites (intención → plan → emisión)

Este documento fija qué garantiza el core cuando el programador **solo declara intenciones** (contenido, posición, animación y preferencia) y qué decisiones toma el planner por él. Es el contrato que valida la batería de estrés ([stress-sprites-bobs-roadmap.md](../../guides/roadmap/stress-sprites-bobs-roadmap.md)) y el que debe respetar cualquier cambio en `SpriteAllocator`, `compose_sprites` o `SpriteScene`.

```text
  Contenido (Visual / CompositeVisual + Animation)
           │
           ▼
  Intenciones (ActorDesc / SpriteIntent)   preferred = Auto | Hw | Bob
           │
           ▼
  Planner / Allocator  ←  ocupación exacta por línea (bitfield 256 bits × 8 canales)
           │  decide: canal libre, par attached, tira 32 px, cadena vertical o BOB
           ▼
  Placements (HwSpritePlacement) + BlitJobs (solo degradados)
           │
           ▼
  Emisión (rearme por Copper o cadena DMA) + FramePlan del Blitter
```

La aplicación **no** elige canal, no escribe `SPRxPOS`/`SPRxCTL` y no decide entre rearme y cadena. Solo describe qué se ve y dónde.

## 1. Reglas de materialización

| Contenido declarado | Decisión del core |
|---|---|
| 16 px de ancho, ≤ 3 colores + transparencia (2 planos) | Sprite HW libre (un canal por franja). |
| 16 px de ancho, 4 planos (hasta 7 colores + transparencia) | Par *attached*: dos canales contiguos, mismo `vstart`; el impar lleva `ATTACH`. |
| 32 px de ancho | Tira de dos canales contiguos con POS adyacentes; si no hay corrida libre, degrada a BOB. |
| Dos o más objetos del mismo canal con franjas Y disjuntas (gap ≥ `kSpriteVerticalGapLines`) | Reuso vertical del canal, con rearme por Copper o cadena DMA según el selector de emisión. |
| Contenido con `Animation` | Se publica la DATA del frame vigente (`base + índice × frame_stride`); el arte no se regenera por frame. El par *attached* se cocina una vez por contenido y se reutiliza mientras no cambie su fuente/altura. |
| `preferred = Hw` | Se intenta HW; si no cabe, degrada a BOB (la degradación no cambia la posición). |
| `preferred = Bob` | Siempre BOB, con la política de fondo/transparencia del actor. |
| `preferred = Auto` | El planner maximiza HW bajo las reglas anteriores y manda el resto a BOB. |
| Saturación de canales en una franja | First-fit por línea; los que no caben degradan **una sola vez** y en orden de `z` (sin dibujarse dos veces). |

## 2. Ocupación por línea (la pieza que decide el reuso)

`SpriteAllocator` mantiene un bitfield de 256 líneas por canal (arrays fijos, sin heap) y lo reconstruye por frame:

1. Las intenciones llegan ordenadas por `top` (counting-sort estable con scratch del llamador; inserción como fallback).
2. Para cada intención se consulta "canal libre en `[top, bottom)`" contra el bitfield; `channel` es preferencia y `assign_rank` da pasadas de prioridad.
3. Al aceptar, se marcan las líneas ocupadas (el `bottom` es exclusivo: dos franjas contiguas comparten canal) y el canal queda disponible más abajo.
4. El resultado se publica como `HwSpritePlacement` ordenado por `vstart`, que es lo que consumen el emisor por rearme y el constructor de cadenas DMA.

## 3. Telemetría publicada

`SpriteComposeResult` incluye, además de `sprites`/`degraded`/`bobs`/`copper`/`palette`:

- `channels`: máscara de canales HW usados en el frame (`bit n` = `SPRn`).
- `max_reuse`: número máximo de placements publicados en un mismo canal.

Se calculan en una pasada sobre los placements ya emitidos (sin asignación), de modo que un test puede exigir "8 canales y 5 reusos" sin inspeccionar registros ni contadores externos.

## 4. Límites conocidos (declarados, no ocultos)

- **Admisión vs capacidad por línea**: ya no hay presupuesto de admisión que consuma canales al dar de alta; todos los actores entran y el `SpriteAllocator` decide en la composición cuántos caben y cuáles degradan. Una escena con más actores que canales debe dimensionar su capacidad y mirar el reporte.
- **Coste de recomposición**: emitir un frame desde `SpriteScene` con decenas de actores y parchear la copperlist completa cuesta cientos de miles de ciclos (medido: ~670k para 30 actores, 5.0 campos/frame). Para 50 fps con muchos objetos, la estructura debe ser estática y parchearse por frame (POS/CTL y, al cambiar de arte, PT); la demo 224 es la referencia medida de ese camino.
- **Tiras de 32 px y estabilidad de asignación** entre frames: el allocator ya modela corridas; la política de "conservar la misma asignación mientras no se rompan reusos" es determinista por el orden estable de entrada, pero no hay todavía una caché explícita de asignación por actor.

## 5. Referencias

- `engine/include/eng/scene/sprite_scene.hpp` (`SpriteScene`), `eng/scene/actor_sprite.hpp` (`compose_sprites`), `eng/graphics/sprite_allocator.hpp`.
- `docs/engine/architecture/OBJECT_SYSTEM.md` (contenido, transparencia, fondos), `SPRITE_CHANNEL_WINDOWS.md` (ventanas y ledger).
- `docs/reference/amiga/techniques/sprite-layer.md` y `sprite-multiplexer-bob-fallback.md`.
- Demos de referencia: 216 (attached), 217 (plantillas), 219 (cadena), 224 (lista estática parcheada a 50 fps).
