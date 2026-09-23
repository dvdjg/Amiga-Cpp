# HOST-078 — Optional y Expected

Respalda `engine/include/eng/core/util/optional.hpp` y
`engine/include/eng/core/util/expected.hpp`: resultados sin excepciones.

## Qué cubre

- **`Optional<T>`**: vacío y con valor, `has_value`/`operator bool`, `value`/`*`,
  `value_or`, `emplace`, `reset`, `swap`; uso real con una búsqueda que puede no
  encontrar hueco.
- **`Expected<T, E>`**: éxito/error con `unexpected(e)`, `value`, `error`,
  `value_or`, `emplace`; y la especialización `Expected<void, E>`.

## Modelo de coste

Igual que `Optional`, `Expected` mantiene presente el almacenamiento de `T` y de
`E` (evita `new` de colocación y mantiene `constexpr`); `T` y `E` deben ser
construibles por defecto. `value()` sobre un error, o `error()` sobre un éxito,
detienen la CPU (parada `illegal` en m68k).

## Relación con `eng::Result`

`eng::Result` (`eng/core/types.hpp`) es el enum de causas sin valor;
`Expected<T, E>` transporta además el valor en caso de éxito.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/078_optional_expected
```
