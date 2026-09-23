# HOST-130: unión etiquetada sin heap (Variant)

Test host de `engine/include/eng/core/util/variant.hpp`: `Variant<Ts...>`, una unión
etiquetada de alternativas **trivialmente copiables** (sin heap ni placement new), con
`index`/`holds`/`get`/`emplace`/`visit`. El consumidor del test es una **cola de comandos**
heterogéneos de juego.

## Qué comprueba

1. Construcción desde cada alternativa (`Move`/`Attack`/`Wait`) e `index`/`holds`.
2. `get<T>()` directo y `visit(f)` que despacha según el tipo activo (`if constexpr`).
3. `emplace` cambia el alternativo en el mismo objeto.
4. El constructor por defecto deja la primera alternativa.

## Salida de referencia

```
Variant:
OK: Variant (comandos heterogeneos: index/holds/get/visit/emplace)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/core/130_variant
```
