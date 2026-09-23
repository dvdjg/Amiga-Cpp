# HOST-096 — texto

Respalda `engine/include/eng/core/util/text.hpp`.

## Qué cubre

- `trim`, `split_next` (troceo por separador), `equal_ci` (sin distinguir mayúsculas).
- `parse_u32`/`parse_s32` (con guarda de desbordamiento, sin división).
- `to_chars_u32`/`to_chars_s32`: decimal a `StaticString<N>` **sin división** (tabla de
  potencias de diez y restas, porque `v/10` en `u32` acabaría en libgcc).
- `join` de varias `StringView` con separador.

## Uso previsto

Configuración y depuración (parseo/emisión de números), HUD y nombres generados; pareja
de `StringView`/`StaticString` sin heap.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/096_text
```
