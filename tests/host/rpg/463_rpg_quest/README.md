# HOST-463: narrativa — quests declarativas sobre hechos (`eng::rpg`)

Test host de `engine/include/eng/rpg/quest.hpp`, hito M5 de la capa RPG.

## Qué comprueba

1. **Cascada en un tick**: el tercer lobo escribe `hunt_done` y con ese **mismo hecho compartido** la quest de caza se cierra y la de comercio avanza de stage — dos quests que no se conocen entre sí.
2. **Fast-out por epoch**: cambiar solo el inventario **no** reevalúa hasta `quest_invalidate` (los cambios que no tocan hechos se declaran); `force` reevalúa sin cambios.
3. **Consumo defensivo**: si la política `Items` rechaza el consumo (inventario cambiado entre validación y acción), el stage **no** avanza y el estado queda consistente.
4. **Journal**: `quest_state`, `quest_stage`, `quest_stage_count` para el HUD; `quest_open` activa sin evaluar.

## Salida de referencia

```
RPG quest (declarativas sobre hechos):
OK: RPG quest (QuestBook/quest_tick/cascada/fast-out)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/rpg/463_rpg_quest
```
