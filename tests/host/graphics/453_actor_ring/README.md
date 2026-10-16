# HOST-453 — actor_ring (actor sobre el anillo corkscrew)

Test host del **puente actor ↔ playfield X-Limited** (`engine/include/eng/field/actor_target.hpp`):
el hueco que descubrió la demo 121 (dibujar entidades de juego sobre el anillo corkscrew sin reimplementar la fontanería de bandas/máscaras).

## Qué cubre

- **`make_actor_target`**: `BobTarget` del anillo con la geometría del corkscrew (`row_bytes` = bytes por fila y plano, `height` = `display_height`, layout interleaved, base = asignación completa).
- **Contrato del anillo**: `actor_ring_y` (visible → línea del anillo con `display_offset`) y `actor_fits_ring` (¿cabe sin cruzar la costura?; el emisor **ya reparte** los rects que la cruzan con `ActorEmitContext::ring_height`).
- **Los 16 subpíxeles EXACTOS sobre el anillo** con el pack interleaved **con guarda** (`pack_interleaved_pair`, `blit_job.hpp`): por fila y plano del destino, `[imagen][guarda][máscara][guarda]`; la guarda a cero neutraliza el arrastre del barrel y queda transparente. Verificado píxel a píxel en **ambas mitades del byte**.
- **SaveUnder sobre el anillo**: el actor se mueve 16 veces y cada restauración devuelve el fondo **byte a byte** (el anillo queda limpio fuera de la caja actual); es el gap que bloqueaba la migración de la 121 (guard planar del actor, levantado).
- **Costura del anillo** (`graphics/bob_ring.hpp`): un actor que cruza el final del bucle (`ctx.ring_height`) se dibuja como dos trabajos (filas del arte 0..4 en las líneas 12..15, filas 4..8 envueltas a las líneas 0..3); el save-under guarda con la MISMA partición (filas del buffer espejadas) y el restore posterior deja el anillo **byte a byte**. Además, las variantes **Or flat interleaved** y **Or planar apilada** cubren los tres convenios de avance de fuente del rango de filas, y sin cruce el wrapper emite **un solo trabajo** (vía lineal exacta).
- **Hallazgos de contrato que el test fija** (también en los headers): el `ActorEmitContext.clip` compara contra el rect **en coordenadas del anillo** (con `ring_height` solo recorta X y exige el rect dentro del bucle); en destinos interleaved `bob_draw` exige `BobMaskPack::InterleavedPair`.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/453_actor_ring
```

## Estado

El puente y los dos gaps de motor (pack con guarda, save-under interleaved) quedan validados en host; la **migración de la demo 121** a `ActorStore` + `Animation` sobre este puente es el paso que cierra `OBJECT_SYSTEM.md` §14.8 (DT-024.3).
