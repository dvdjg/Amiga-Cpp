# HOST-016 — `scene::compose` (display planar con repetición de filas)

Valida en host, sin emulador, el modelo de escena `scene::compose`
(`engine/include/eng/graphics/scene/compose.hpp`) con la geometría de un HAM
cuadruplicado: `display` + `palette` + `row_repeat`.

## Qué cubre

- **Display**: se emite `BPLCON0`, DIW/DDF y los `BPLxPT` de los planos.
- **Repetición de filas (cuadruplicado)**: por cada fila lógica se emiten `repeat`
  líneas; `BPL1MOD/BPL2MOD = -bytes_per_row` en todas menos la última del grupo (que
  avanza con módulo 0), y `BPLCON1` alterna `0`/`bplcon1_shift`. Para `rows=64,
  repeat=4`: 192 módulos negativos, 64 nulos, 128 `BPLCON1` desplazados y 128 nulos,
  con 257 WAITs (256 líneas + el par de overflow al cruzar la 255).
- **Paleta** cargada y terminación de lista (`0xffff`).
- **Huella estática**: `row_repeat_words(rows, repeat, first_line)` es una función
  `constexpr`: dos `static_assert` fijan su valor (2050 palabras para 64×4 desde `0x2c`)
  y que cabe en `copper_word_budget(res)`. Además se compara con la emisión real
  (`scheduler().words_used()` antes/después de la etapa): coinciden.
- **Parametricidad**: un segundo config (sin repetición, 5 planos, otro `BPLCON0`)
  produce otra geometría sin tocar `Scene`.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/016_ham_scene
```
