# HOST-096 — texto

Respalda `engine/include/eng/core/util/text.hpp`.

## Qué cubre

- `trim`, `split_next` (troceo por separador), `equal_ci` (sin distinguir mayúsculas).
- `parse_u32`/`parse_s32` (núcleo con guarda de desbordamiento, sin división) y `join`.
- Decimal **sin `*` ni `/`** (tabla de potencias de diez y restas): `to_chars_u32`/`to_chars_s32`,
  `to_chars` genérico para cualquier entero de 8/16/32 bits, `to_chars_scaled` (valor ×10^k con
  el punto insertado) y `parse<T>` con el rango de `T`.
- Escalares fraccionarios por el punto de extensión `eng::math::scalar_decimal<S>`
  (`to_chars(out, x, k)` / `parse(text, x, k)`), con **varios escalares** del mismo template
  (`Fixed<s16,12>`, `Fixed<s16,8>` y `Fixed<s16,0>`): casos límite (min/max, signo, redondeo
  al más cercano), rechazos (fuera de rango, decimales de más, formato) y **round-trip exacto**
  a 4 decimales sobre todo el rango de `s16`.

## Uso previsto

Configuración y depuración (parseo/emisión de números), HUD y nombres generados; pareja
de `StringView`/`StaticString` sin heap.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/096_text
```
