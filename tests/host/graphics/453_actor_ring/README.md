# HOST-453 — actor_ring (actor sobre el anillo corkscrew)

Test host del **puente actor ↔ playfield X-Limited** (`engine/include/eng/field/actor_target.hpp`):
el hueco que descubrió la demo 121 (dibujar entidades de juego sobre el anillo corkscrew sin reimplementar la fontanería de bandas/máscaras).

## Qué cubre

- **`make_actor_target`**: `BobTarget` del anillo con la geometría del corkscrew (`row_bytes` = bytes por fila y plano, `height` = `display_height`, layout interleaved, base = asignación completa).
- **Contrato del anillo**: `actor_ring_y` (visible → línea del anillo con `display_offset`) y `actor_fits_ring` (el rect debe caber sin cruzar la costura;
  el emisor de actores aún no parte el rect — el de mundo sí).
- **Los 16 subpíxeles EXACTOS sobre el anillo** con el pack interleaved **con guarda** (`pack_interleaved_pair`, `blit_job.hpp`): por fila y plano del destino, `[imagen][guarda][máscara][guarda]`; la guarda a cero neutraliza el arrastre del barrel y queda transparente. Verificado píxel a píxel en **ambas mitades del byte**.
- **SaveUnder sobre el anillo**: el actor se mueve 16 veces y cada restauración devuelve el fondo **byte a byte** (el anillo queda limpio fuera de la caja actual); es el gap que bloqueaba la migración de la 121 (guard planar del actor, levantado).
- **Hallazgos de contrato que el test fija** (también en el header): el `ActorEmitContext.clip` compara contra el rect **en coordenadas del anillo**;
  en destinos interleaved `bob_draw` exige `BobMaskPack::InterleavedPair`.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/453_actor_ring
```

## Estado

El puente y los dos gaps de motor (pack con guarda, save-under interleaved) quedan validados en host; la **migración de la demo 121** a `ActorStore` + `Animation` sobre este puente es el paso que cierra `OBJECT_SYSTEM.md` §14.8 (DT-024.3).
