# HOST-079 — StringView, FunctionRef y Callback

Respalda `engine/include/eng/core/util/string_view.hpp`,
`engine/include/eng/core/util/function_ref.hpp` y
`engine/include/eng/core/util/callback.hpp`: vistas y callables no propietarios.

## Qué cubre

- **`StringView`**: desde literal y desde `(puntero, tamaño)`, `size`/`empty`,
  acceso, `substr`, `remove_prefix`/`remove_suffix`, `starts_with`/`ends_with`,
  `find` (char y subvista), `operator==`/`!=`.
- **`FunctionRef<Sig>`**: función libre, lambda sin captura, lambda con captura
  (por referencia, sin copia) y functor; también con retorno `void`.
- **`Callback<Args...>`**: puntero a función + contexto; `valid()`/`operator bool`,
  invocación que pasa el `ctx`, `clear()` y ser **POD** (`is_trivially_copyable`).

## Contrato de vida

`FunctionRef` y `Callback` **no poseen** el callable/contexto: deben vivir más que
ellos; no construir desde un temporal. `FunctionRef` solo admite `operator()` const
(una lambda `mutable` no vale). `Callback` es un agregado POD: útil para cruzar una
frontera que exige POD (registro/hook/tabla plana).

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/079_string_view_function_ref
```
