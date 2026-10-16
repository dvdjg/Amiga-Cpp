# HOST-453 — actor_ring (actor sobre el anillo corkscrew)

Test host del **puente actor ↔ playfield X-Limited** (`engine/include/eng/field/actor_target.hpp`):
el hueco que descubrió la demo 121 (dibujar entidades de juego sobre el anillo corkscrew sin
reimplementar la fontanería de bandas/máscaras).

## Qué cubre

- **`make_actor_target`**: `BobTarget` del anillo con la geometría del corkscrew
  (`row_bytes` = bytes por fila y plano, `height` = `display_height`, layout
  interleaved, base = asignación completa).
- **Contrato del anillo**: `actor_ring_y` (visible → línea del anillo con
  `display_offset`) y `actor_fits_ring` (el rect debe caber en el bucle **sin cruzar la
  costura**; el emisor de actores aún no parte el rect — el de mundo sí).
- **Emisión completa** (`scene::actor_emit` sobre el anillo): el rect sale en líneas del
  anillo, el job cookie-cut interleaved va con la **hoja pack** ([imagen][máscara] por
  fila y **plano del destino**), el destino cae en la línea del anillo correcta
  (`(ring_y·planos + 0)·row_bytes`) y la ejecución software (modelo interleaved =
  corkscrew) pinta los **píxeles exactos** con la transparencia de la máscara.
- **Hallazgos de contrato que el test fija** (documentados también en el header):
  - el `ActorEmitContext.clip` compara contra el rect **en coordenadas del anillo**
    (con `offset.y = display_offset` el clip debe cubrir el bucle, no solo la ventana);
  - en destinos interleaved, `bob_draw` exige `BobMaskPack::InterleavedPair` (el pack
    suelto se rechaza — dispatch documentado en `bob.hpp`);
  - los píxeles que el barrel arrastraría del byte bajo dependen de la palabra siguiente
    del pack (guarda); este test cubre el byte alto exacto y ese borde lo cubren
    HOST-439/450.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/453_actor_ring
```

## Estado

El puente queda validado en host; la **migración de la demo 121** a `ActorStore` +
`Animation` sobre este puente es el paso que cierra `OBJECT_SYSTEM.md` §14.8 (demo con
gate visual del sistema de actores).
