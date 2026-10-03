# HOST-413 — escalera de motores por geometría runtime (paso 4 §7)

`eng/field/scroll_ladder.hpp` (`eng::playfield::ScrollLadder`): un **registro** de motores
(`ScrollLayer<Backend>`) con su **geometría runtime** (`RuntimeScrollGeometry`); `pick(g)` devuelve el
que **coincide** con una geometría cargada en runtime. Cubre el caso de geometría **no conocida en
compilación** (editor) **sin refactorizar** el motor NTTP.

## Qué comprueba

- Registra 2 motores con geometrías distintas; `pick` con la geometría de cada uno devuelve **su**
  motor.
- Una geometría **no registrada** → `Ref` inválido.

## Uso (idea)

El juego declara los motores que conoce (p. ej. uno por nivel, NTTP) y los registra con su geometría;
el `App`/planner **elige** el que encaja con lo que cargue en runtime. Es el camino **barato** para la
geometría runtime (el refactor del motor que consume geometría runtime es el paso siguiente).
