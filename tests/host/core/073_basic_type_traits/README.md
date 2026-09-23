# HOST-073 — rasgos de tipo y utilidades de lenguaje

Respalda `engine/include/eng/core/util/type_traits.hpp` y
`engine/include/eng/core/util/util.hpp`: el subconjunto de `<type_traits>` y
`<utility>` que el engine necesita y que el runtime freestanding no puede tomar de
la STL.

## Qué cubre

- **Identidad y cualificadores**: `is_same`, `remove_cv`/`remove_const`/`remove_volatile`,
  `remove_reference`, `remove_cvref`, `remove_pointer`.
- **Selección**: `conditional`, `enable_if`, `void_t`.
- **Categorías**: `is_integral`, `is_floating_point`, `is_arithmetic`, `is_signed`,
  `is_unsigned`, `is_pointer`, `is_enum`, `is_class`, `is_trivially_copyable`,
  `is_trivially_destructible`.
- **Conversiones**: `make_unsigned`, `underlying_type`, `to_underlying`.
- **Lenguaje**: `move`, `forward`, `swap` (valor y array), `exchange`, `as_const`,
  `min`/`max`/`clamp` genéricos por referencia.

## Notas

- GCC no expone `__is_integral`/`__is_arithmetic` como builtins, así que esas
  categorías se especializan a mano; el resto usa los builtins que sí existen.
- `eng::util::min`/`max`/`clamp` son genéricos (cualquier tipo con `operator<`,
  devuelven referencia). Los de escalares (`Fixed`/`MiniFloat16`) siguen en
  `eng::math` (`eng/core/scalar_ops.hpp`).

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/073_basic_type_traits
```
