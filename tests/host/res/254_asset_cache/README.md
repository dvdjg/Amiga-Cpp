# HOST-254: caché de assets (`eng::res::AssetCache`)

Test host de la caché de assets (`engine/include/eng/res/asset_cache.hpp`): bloque propietario,
presupuesto por banco efectivo, generación de vistas, leases DMA, prioridad, `refcount`, `pin` y
desalojo LRU.

## Qué comprueba

1. **Ciclo**: `declare` → `get` lanza la carga (placeholder) → `Loading` → `on_load_done` →
   `Ready`; `get` devuelve los datos y el presupuesto se contabiliza.
2. **Desalojo por prioridad**: con presupuesto justo, sale el de **menor prioridad** y se mantiene
   el prioritario.
3. **LRU**: a igualdad de prioridad, sale el **más viejo** (`last_use`).
4. **`pin`/`refcount`**: un asset fijado o referenciado **no** se desaloja; sin víctima, la carga
   falla con `Error`.
5. **Owner y generación**: `AssetSlot` conserva el `MemoryBlock` (`data`, `size`, `MemoryKind`); evict,
   reload y shutdown invalidan las vistas anteriores y el backend recibe el banco efectivo para liberar.
6. **DMA/lectura activa**: una lease DMA impide evict y shutdown; shutdown también se niega a
   liberar el destino de una lectura asíncrona aún pendiente.

## Salida de referencia

```
OK: cache de assets (owner, generation, DMA, ciclo, prioridad y LRU) validada.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/res/254_asset_cache
```
