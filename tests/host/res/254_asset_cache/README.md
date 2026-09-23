# HOST-254: caché de assets (`eng::res::AssetCache`)

Test host de la caché de assets (`engine/include/eng/res/asset_cache.hpp`): presupuesto por banco,
prioridad, `refcount`, `pin` y desalojo LRU.

## Qué comprueba

1. **Ciclo**: `declare` → `get` lanza la carga (placeholder) → `Loading` → `on_load_done` →
   `Ready`; `get` devuelve los datos y el presupuesto se contabiliza.
2. **Desalojo por prioridad**: con presupuesto justo, sale el de **menor prioridad** y se mantiene
   el prioritario.
3. **LRU**: a igualdad de prioridad, sale el **más viejo** (`last_use`).
4. **`pin`/`refcount`**: un asset fijado o referenciado **no** se desaloja; sin víctima, la carga
   falla con `Error`.

## Salida de referencia

```
OK: cache de assets (ciclo, prioridad, LRU, pin/refcount) validada.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/254_asset_cache
```
