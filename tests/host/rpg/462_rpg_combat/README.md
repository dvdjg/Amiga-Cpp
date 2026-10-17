# HOST-462: conflicto — vitalidad, pipeline de daño y puente con la simulación (`eng::rpg`)

Test host de `engine/include/eng/rpg/combat.hpp` (+ `hurt_actor` de `sim_bridge.hpp`), hito M3 de la capa RPG.

## Qué comprueba

1. **`Vitality`**: daño/cura saturados a `[0, hp_max]`; `hp == 0` = derrotado.
2. **`resolve_damage`** por **fases fijas**: mods del atacante → del defensor → resistencias por etiqueta (solo las filas cuyas etiquetas tocan el ataque), recorte del factor combinado a [0×, 2×], **mínimo 1** con payload positivo salvo inmunidad total (factor 0), redondeo al más cercano y saturación a `u16`; `sum_mods` recorta cada fase a ±100.
3. **`hurt_actor`**: el daño baja la vitalidad y sube herida/miedo en `eng::sim::Needs` (la simulación propone, el RPG interpreta).

## Salida de referencia

```
RPG combat (vitalidad, pipeline, puente sim):
OK: RPG combat (Vitality/resolve_damage/hurt_actor)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/rpg/462_rpg_combat
```
