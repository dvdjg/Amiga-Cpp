# HOST-435 - `gen_formation_v` / `gen_formation_v_sine` (olas en V, F2.1)

Test host de los generadores de **ola en V** de `eng/scene/formation.hpp`
(`ROADMAP_SPRITES_COMPLETO.md` §3.1): la formación clásica de shmup en la que una punta
entra primero y los brazos aparecen con retraso creciente.

- `gen_formation_v(out, count, spread_x, delay_step, trajectory, open_down)`: miembros
  simétricos (`x[i] = (i − count/2)·spread_x`), `delay_ticks = dist·delay_step` (mínimo en
  la punta) y apertura en Y de `spread_x / 2` por paso (`open_down` la invierte).
- `gen_formation_v_sine(out, count, spread_x, delay_step, trajectory_left, trajectory_right)`:
  la misma geometría con **seno en espejo**: la mitad izquierda (y el centro) usan la
  trayectoria izquierda y la derecha la derecha (p. ej. la misma seno con fases 0 y 128).

Cubre: geometría y simetría, delays por distancia, `open_down`, reparto de trayectorias,
bordes (`count == 0/1`, buffer más pequeño que `count`) y la **genericidad sobre el
escalar** ejercitada con `s32` y `float` (AGENTS §1.11), además del uso retro `s16`.

Es freestanding (sin hardware, sin heap, sin STL). El `main.cpp` usa `printf` solo para
informar.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/scene/435_formation_v   # solo este
bash tools/run-host-tests.sh                                   # todos
```

## Referencias

- `docs/guides/roadmap/ROADMAP_SPRITES_COMPLETO.md` §3.1 (F2.1).
- `docs/guides/roadmap/ROADMAP_JUEGO_SPRITES_BOBS.md` §5 F2 (trayectorias y formaciones).
- Demo `demos/features/engine/amiga/064_shmup_wave` (formación seno en espejo).
