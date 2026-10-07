# Demo 064 — shmup mínimo: formaciones, rutas y timeline (F2 + F8)

Tutorial de la capa de dinámica de juego:

- **Formación** (`scene::Formation` + `formation_update`): 5 naves entran escalonadas (delays y
  offsets) siguiendo dos **rutas seno** en espejo (`gen_sine_vertical`).
- **Pool de entidades** (`scene::EntityPool`): las naves viven con `TrajectoryFollower` +
  `CompositeState` (contenido de 1 parte) y se retiran al terminar su ruta. Se dibujan por
  **BOB** con `composite_emit_bobs` + borrado por caja.
- **Timeline** (`util::Sequence` + `SequenceRunner`): una pista de eventos recicla una
  **ráfaga de 3 balas** cada 24 ticks a una X que barre la pantalla; las balas son **Sprite
  HW** (`SpriteScene`) que suben hasta salir y vuelven por abajo (pool fijo).
- **Fondo**: gradiente vertical de `COLOR00` por bandas de Copper (8 bandas) con planos a
  cero; el borrado por caja de los enemigos repinta color 0 puro (el gradiente de esa línea).

Nota: el pool de balas se crea en `setup_content` y se recicla **por posición** porque el alta
dinámica desde `update` deja a los Sprite HW sin publicar en target (incidencia abierta con
evidencia y plan en
[`docs/debugging/investigaciones/064-sprite-hw-creado-en-update-no-publica.md`](../../../../docs/debugging/investigaciones/064-sprite-hw-creado-en-update-no-publica.md));
el camino dinámico del engine está cubierto por HOST-391/432 y el fix se atacará con GDB.

Validación: capturas a 0,8/1,8/3,0 s con visión local (5 naves rombo, balas amarillas, fondo
degradado, sin artefactos) y HOST-430 (trayectorias/splines/formaciones/pool).

```bash
bash tools/build/build-demo.sh demos/features/engine/amiga/064_shmup_wave --debug
bash tools/run/run-demo.sh demos/features/engine/amiga/064_shmup_wave --keep-running
```
