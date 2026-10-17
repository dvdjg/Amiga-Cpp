# HOST-456: inventario, equipo y atributos derivados (`eng::rpg`)

Test host de `engine/include/eng/rpg/{inventory,equipment,stats}.hpp`. Cierra el bucle "objeto → ranura → modificador → atributo derivado" del progreso por equipo.

## Qué comprueba

1. **`ItemBag` + `bag_*`**: entradas de capacidad fija; `bag_add` apila sobre pilas compatibles, crea pila nueva con el resto y, con la bolsa llena, devuelve las unidades que no entraron sin destruir nada; consultas por tipo y por etiqueta (`bag_find_type`/`bag_find_tag`), unidades totales (`bag_count`) y peso (`bag_total_weight`).
2. **`Equipment`**: la ranura es un índice y su etiqueta exigida es **dato del juego** (`required_tags`); `equip_item` valida la etiqueta y devuelve la pieza sustituida; rechaza handles caducados sin tocar la ranura; `unequip_item` saca la pieza; `gather_equipment_mods` expande los rangos `mod_first/mod_count` de las piezas vivas.
3. **`recompute_stats`**: pipeline de fases fijas — suma plana → porcentaje acumulado (recortado a ±100) → recorte final a `s16`; mods con `stat` fuera de rango se ignoran; aritmética con `mul_wide`/`div32` (nativa del 68000, sin `float` ni libcalls).

## Salida de referencia

```
RPG inventario/equipo/stats:
OK: RPG inventario/equipo/atributos derivados
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/rpg/456_rpg_inventory_equipment
```
