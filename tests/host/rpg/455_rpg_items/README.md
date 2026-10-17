# HOST-455: objetos con instancia — catálogo, pool y apilado (`eng::rpg`)

Test host de `engine/include/eng/rpg/{types,items}.hpp`. Cubre el modelo de objetos del jugador ("esta espada", no "una espada"): catálogo de tipos de contenido + pool de instancias con handles de generación.

## Qué comprueba

1. **Etiquetas de capacidad** (`item_tags`, `has_tags`/`has_any_tag`): las reglas preguntan capacidades ("¿arma?", "¿alimento?") en vez de ramificar por jerarquía de clases; la máscara vacía cumple y "alguna" distingue de "todas".
2. **`ItemCatalog`**: registro de tipos con id de índice, rango válido, lectura por `Ref` y rechazo al llenarse (`no_item_type`).
3. **`ItemStore`**: pool de `MaxItems` huecos — el primer `spawn` usa el slot 0 con generación 1; al reciclar un hueco la generación sube y **los handles viejos caducan** (nunca apuntan a la instancia nueva); doble `destroy` y destroy de handles caducados devuelven `false`; `restore` (solo para carga de partida) valida slot/generación; `for_each_live` recorre en orden ascendente.
4. **Apilado**: `stack_compatible` separa pilas por durabilidad/mejora/afijos/metadatos (una espada encantada no se funde con una lisa) y `merge_stack` mueve solo lo que cabe devolviendo el resto.

## Salida de referencia

```
RPG items (catalogo, pool de instancias, apilado):
OK: RPG items (tags/catalogo/ItemStore/apilado)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/rpg/455_rpg_items
```
