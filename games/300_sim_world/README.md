# Juego 300: SIM WORLD (`eng::sim`)

Primer consumidor **real** del ecosistema `eng::sim` (más allá del benchmark
`demos/features/sim/amiga/001_sim_bench`): un mundo vivo que se observa y se sigue con la
cámara.

## Qué ejercita del engine

- **`SimWorld`** con criaturas (necesidades, mente, comportamiento, LOD).
- **Planificación GOAP integrada** (`SimWorld::plan_tick`): presupuesto *anytime* + **caché de
  planes** + **reutilización de sufijo**, sobre el dominio de construcción
  (`ConstructionDomain`).
- **LOD real = cámara**: la cámara es una *room*; **cada frame** el juego marca
  `realize_room(cámara, N)`, de modo que **solo lo cercano planifica**. La histéresis decide
  *cuándo*, el LOD *quién*.
- `eng::amiga::poll_input` (joystick) y el overlay `debug()` para el HUD.

Es la prueba de que el ecosistema funciona como consumidor de juego en el 68000, no solo como
test host.

## Controles

- **Izquierda / derecha**: mueve la cámara (room). Solo las criaturas de esa room
  (`realized`) se dibujan en color vivo y planifican.
- **Fuego**: añade una criatura.

## HUD

`room`, población, planes activos y expansiones de la última búsqueda.

## Ejecutar

```bash
bash tools/build/build-demo.sh games/300_sim_world --debug --clean
bash tools/run/run-demo.sh games/300_sim_world --warp
```

## Verificación

`build -> run -> analyze` con **READY OK**.
