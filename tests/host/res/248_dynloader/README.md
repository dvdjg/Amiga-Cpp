# HOST-248: loader de código relocatable (`.englib`)

Test host del `DynLoader` (`engine/include/eng/res/dynloader.hpp`): parsea una imagen `.englib`,
aplica las **relocaciones** y resuelve **símbolos**.

## Qué comprueba

1. `declare` devuelve un handle y `load` relocaliza: la celda del `code` pasa a valer la **base**.
2. `symbol(h, "foo")` devuelve la dirección del export; un símbolo inexistente, `nullptr`.
3. Imagen corta o `magic` inválido → `Error`.
4. `unload` deja la lib `Empty` y sin símbolos; `hash_name` (FNV-1a) es estable y discrimina.

No ejecuta código (el `.englib` del test es un blob en RAM con una celda relocable y un export).

## Salida de referencia

```
OK: .englib (relocacion y simbolos) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/res/248_dynloader
```
