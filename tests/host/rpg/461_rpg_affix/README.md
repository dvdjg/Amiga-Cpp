# HOST-461: afijos y mejora de objetos (`eng::rpg`)

Test host de `engine/include/eng/rpg/affix.hpp` (hito M2): progreso del objeto concreto (encantamiento y forja) y su recolección de modificadores.

## Qué comprueba

1. **`apply_affix`**: el afijo se acepta por **etiquetas** del tipo de objeto, se marca como bit en la máscara de la instancia y rechaza duplicados, tipos incompatibles y bits fuera de rango; `allowed_tags = 0` vale para cualquier objeto.
2. **`gather_item_mods`**: expansión de los mods base + afijos; la mejora aplica `add_pct += nivel × paso` (recortado a ±100, aritmética entera exacta); un `out` pequeño delata la truncación por el conteo; rangos de afijo mal formados cortan sin leer fuera.
3. **Integración**: `gather_equipment_mods` con afijos y mejora + `recompute_stats` — una espada +3 con afijo de fuego +2 y dos niveles de forja (+20 % cada mod) da **ATAQUE 14** sobre base 5, sin tocar el combate.

## Salida de referencia

```
RPG affix (afijos, mejora, recoleccion de mods):
OK: RPG affix (apply_affix/gather_item_mods/reinforce)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/rpg/461_rpg_affix
```
