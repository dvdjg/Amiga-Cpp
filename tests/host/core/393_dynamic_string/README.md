# HOST-393 — DynamicString con asignador explícito

Respalda `engine/include/eng/core/util/dynamic_string.hpp`.

## Qué cubre

- Crecimiento sobre `BumpAlloc` sin depender de `HeapAlloc` ni de `std::string`.
- Construcción de `StringView` de longitud exacta; el almacenamiento no garantiza NUL.
- Auto-concatenación desde una vista propia cuando `reserve` mueve el buffer.
- Fallo de reserva sin modificar el texto ya construido.
- Formateo: `append_u32` (con relleno a la izquierda), `append_s32` (incluido `INT32_MIN`) y
  `append_hex` (relleno y mayúsculas); el decimal lo compone el núcleo de `text.hpp`.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/393_dynamic_string
```
