# HOST-079 — StringView y FunctionRef

Respalda `engine/include/eng/core/util/string_view.hpp` y
`engine/include/eng/core/util/function_ref.hpp`: vistas no propietarias.

## Qué cubre

- **`StringView`**: desde literal y desde `(puntero, tamaño)`, `size`/`empty`,
  acceso, `substr`, `remove_prefix`/`remove_suffix`, `starts_with`/`ends_with`,
  `find` (char y subvista), `operator==`/`!=`.
- **`FunctionRef<Sig>`**: función libre, lambda sin captura, lambda con captura
  (por referencia, sin copia) y functor; también con retorno `void`.

## Contrato de vida

`FunctionRef` **no posee** el callable: debe vivir más que la referencia; no
construir desde un temporal. Solo admite `operator()` const (una lambda `mutable`
no vale).

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/079_string_view_function_ref
```
