# HOST-460: crafteo por recetas (`eng::rpg`)

Test host de `engine/include/eng/rpg/craft.hpp` (hito M2): recetas declarativas sobre el inventario, estaciones por etiquetas y transaccionalidad.

## Qué comprueba

1. **Rechazos con estado explícito**: índice fuera del libro y rangos mal formados (`BadRecipe`), salida sin catálogo (`UnknownOutput`, antes de mirar ingredientes), estación que no cuadra (`MissingStation`), habilidad insuficiente (`MissingSkill`) y faltas (`MissingInputs`) — **sin consumir nada**.
2. **Éxito**: ingredientes por **capacidad** (etiquetas) o tipo exacto, consumo determinista en orden de entrada; la salida nace con durabilidad completa, sale del store y el llamador la coloca (apilado).
3. **Multi-pila**: una receta de 2 desde dos pilas de 1 agota y retira ambas instancias.
4. **Transaccionalidad**: el pool lleno (`StoreFull`) rechaza **antes** de consumir; el material queda intacto.

## Salida de referencia

```
RPG craft (recetas, estaciones, transaccionalidad):
OK: RPG craft (RecipeBook/craft)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/rpg/460_rpg_craft
```
