# Demo 055: copper rainbow — degradado arcoíris vía CopperIntent

Demuestra el paso 3 de `ENGINE_DESIGN.md` §5 («retained scene + actor con
CopperIntent»): la escena describe QUÉ quiere (una lista de `CopperIntent` con un
cambio de `COLOR00` por franja) y el `CopperScheduler` lo materializa en
WAIT/MOVE de la copperlist. Es el patrón *retained*: el juego no escribe
registros, solo actualiza intenciones.

Qué muestra: 12 franjas horizontales (16 líneas cada una) que colorean el fondo
con un degradado arcoíris (rojo → naranja → amarillo → verde → cian → azul →
púrpura). En `update()` la fase avanza y el arcoíris se desplaza verticalmente
(reconstruyendo la copperlist por frame).

Flujo:

1. `build_intents()`: una `CopperIntent` (`PaletteLine`) por franja, con `colors`
   apuntando al color del arcoíris desplazado por la fase.
2. `build_copper()`: `emit_planes_display` + `emit_palette` +
   `emit_copper_intents` (las 12 franjas) + fin.
3. `update()`: avanza la fase y reconstruye la copperlist.

El vocabulario `CopperIntent` es el punto de unión con los efectos del engine:
un efecto/actor produce intenciones; el scheduler las compila.

## Build & run

```bash
tools/build/build-demo.sh demos/amiga/055_copper_rainbow --clean
tools/run/run-demo.sh       demos/amiga/055_copper_rainbow
```
