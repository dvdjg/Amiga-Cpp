# HOST-157: objetos materiales y economía (`eng::sim`)

Test host de `engine/include/eng/sim/{inventory,object,economy}.hpp`: la capa material que
cierra el bucle objeto↔planificación y el motor de reputación entre facciones.

## Qué comprueba

1. **Inventario** (`inventory.hpp`): pilas `(tipo, cantidad)` con saturación a 255,
   `add`/`remove`/`count`/`has`, límite de pilas distintas y etiquetas de objeto
   (`item_tags`/`has_tag`).
2. **Objetos del mundo** (`object.hpp`): `ItemStore` (spawn/buscar/quitar/contar) y
   **ejecución material** de los pasos del dominio: `Forage` da comida, `Eat` la consume,
   `Gather` da material, `CraftTool` consume material y da herramienta, y `Build` consume
   herramienta + material y **deja un refugio en el mundo**; sin recursos devuelve
   `MissingResource`.
3. **Economía** (`economy.hpp`): precio por `base + demanda`, la oferta abarata, el
   decaimiento devuelve el precio a la base, y los **regalos/tributos** suben la reputación
   del receptor (`Society`) y la demanda del objeto; el tributo pesa más que un regalo.

## Salida de referencia

```
Sim objects/economy:
OK: Sim objects/economy (inventario, objetos, economia y reputacion)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/157_sim_objects_economy
```
