# Tests HOST — rpg

Categoría `rpg` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../docs/testing/TAXONOMY.md).

Cubre el **núcleo de mundo RPG** (`engine/include/eng/rpg/`): hechos persistentes, objetos con instancia, inventario, equipo, atributos derivados, reloj de mundo y serialización por componente. Diseño y límites con `eng::sim`: [RPG_LAYER.md](../../../docs/engine/architecture/RPG_LAYER.md); fases siguientes: [ROADMAP_RPG.md](../../../docs/guides/roadmap/ROADMAP_RPG.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-454 | [rpg_kernel](454_rpg_kernel/README.md) | `eng/rpg/{types,facts,clock}.hpp`: `FactStore` con cambio efectivo (epoch + bits changed, `add` saturado, consultas fuera de rango seguras), `FactSet` de entidad y `WorldClock` (acarreo minuto→hora→día con división nativa, saturación, `set_time` con recorte). |
| HOST-455 | [rpg_items](455_rpg_items/README.md) | `eng/rpg/{types,items}.hpp`: etiquetas de capacidad (`item_tags`), `ItemCatalog`, `ItemStore` con handles de generación (reciclado, caducidad, doble destroy, `restore`) y apilado (`stack_compatible`/`merge_stack`). |
| HOST-456 | [rpg_inventory_equipment](456_rpg_inventory_equipment/README.md) | `eng/rpg/{inventory,equipment,stats}.hpp`: bolsa con apilado y consultas, ranuras de equipo con etiqueta exigida como dato, recolección de `StatMod` y `recompute_stats` (fases fijas, saturación s16, aritmética nativa). |
| HOST-457 | [rpg_save](457_rpg_save/README.md) | `eng/rpg/save.hpp`: round-trip completo (hechos, instancias con generaciones, bolsa, equipo, base de stats, reloj), determinismo byte a byte, rechazo de versión distinta/stream truncado/handles colgantes. |
| HOST-458 | [rpg_world](458_rpg_world/README.md) | `eng/rpg/world.hpp` (M1): entidades por composición (kind + verbos + componente), ids monótonos no reutilizados con hueco reciclado y objetos del suelo (`GroundItems`). |
| HOST-459 | [rpg_interaction](459_rpg_interaction/README.md) | `eng/rpg/{interaction,sim_bridge}.hpp` (M1): cerraduras por etiqueta+metadato, recoger/soltar con división de pilas y sin perder objetos, `consume` y el puente de comer/descansar con `eng::sim::Needs`. |
| HOST-460 | [rpg_craft](460_rpg_craft/README.md) | `eng/rpg/craft.hpp` (M2): recetas por capacidad/estación/habilidad con estados explícitos, consumo multi-pila y transaccionalidad (pool lleno no consume). |
| HOST-461 | [rpg_affix](461_rpg_affix/README.md) | `eng/rpg/affix.hpp` (M2): afijos por etiquetas, `gather_item_mods` con escala de mejora y truncación delatada, e integración con `recompute_stats` (ATAQUE derivado). |
| HOST-462 | [rpg_combat](462_rpg_combat/README.md) | `eng/rpg/combat.hpp` (M3): `Vitality` saturada, pipeline de daño por fases fijas con mods/resistencias por etiqueta, mínimo 1, inmunidad total y `hurt_actor` (enlace con `eng::sim::Needs`). |
| HOST-463 | [rpg_quest](463_rpg_quest/README.md) | `eng/rpg/quest.hpp` (M5): quests declarativas (condiciones/acciones como datos) con cascada en un tick, hecho compartido entre quests, fast-out por epoch con `quest_invalidate` y consumo defensivo. |
