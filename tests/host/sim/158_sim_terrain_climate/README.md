# HOST-158: terreno y clima (`eng::sim`)

Test host de `engine/include/eng/sim/terrain.hpp` y `climate.hpp`: la representación
**algorítmica del mundo** para el movimiento y el clima que la recorre, ambos agnósticos de
la proyección (2D/iso/3D).

## Qué comprueba

1. **Terreno** (`terrain.hpp`): perfiles por tipo (coste, cobertura, abrigo) y travesía por
   capacidades (`can_traverse`): el muro es infranqueable, el agua exige nadar/volar, la
   repisa exige escalar/saltar.
2. **`TerrainMap<W,H>`**: rejilla de `TerrainKind` con `walkable`/`cost`/`cover` listos
   para `eng::util::astar`; el test resuelve una ruta **rodeando un muro** por la cobertura
   y comprueba que un objetivo encerrado no tiene solución.
3. **Clima** (`climate.hpp`): `Climate` por región (formar/consultar/disipar severidad,
   `strongest`, saturación) y `effective_exposure`/`exposure_at` con el abrigo del terreno
   y estar a cubierto.
4. **Integración en el mundo**: la `exposure` se mitiga por la región (terreno + abrigo),
   cede si no hay clima en la región de la criatura, y `region_passable` filtra el paso por
   las capacidades de movimiento.

## Salida de referencia

```
Sim terrain/climate:
OK: Sim terrain/climate (terreno, astar, clima, region)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/158_sim_terrain_climate
```
