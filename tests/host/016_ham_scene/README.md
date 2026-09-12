# HOST-016 — `HamScene` (display planar con repetición de filas)

Valida en host, sin emulador, el driver reutilizable
`engine/include/eng/graphics/drivers/ham_scene.hpp`, extraído del porte 1:1 de
`effects/fire-rgb`.

## Qué cubre

- **Contrato**: `HamScene` cumple `eng::GraphicsDriver` / `eng::DisplayDriver`
  (`static_assert`), y `takeover()`/`install()` delegan en el backend con la misma
  copperlist.
- **Display**: se emite `BPLCON0`, DIW/DDF y los `BPLxPT` (con `reverse_plane_ptrs`
  en orden `bpl[N-1..0]`, como el original).
- **Repetición de filas (cuadruplicado)**: por cada fila lógica se emiten
  `row_repeat` líneas; `BPL1MOD/BPL2MOD = -bytes_per_row` en todas menos la última
  del grupo (que avanza con módulo 0), y `BPLCON1` alterna `0`/`bplcon1_shift`
  (dither del original). Para `rows=64, row_repeat=4`: 192 módulos negativos, 64
  nulos, 128 `BPLCON1` desplazados y 128 nulos.
- **Paleta** cargada y terminación de lista (`0xffff`).
- **Parametricidad**: un segundo config (`row_repeat=1`, 5 planos, otro `BPLCON0`)
  produce otra geometría sin tocar el driver.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/016_ham_scene
```
