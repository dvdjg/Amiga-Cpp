# HOST-020 — Tabla de seno exacta del original

Fija la tabla de seno **4.12 exacta** del original (`libmisc/sintab.c`), materializada en
`engine/include/eng/core/sintab.hpp` y usada por `math2d::SinTableQ12`.

## Qué cubre

- **Invariantes**: `sin(0)=0`, `sin(π/2)=4096`, `sin(π)=0`, `sin(3π/2)=-4096`.
- **Simetría** (`sin(4096−i) = −sin(i)`) y **monotonía** en el primer cuadrante.
- **Checksum** de regresión (`1083047936`) → fija la tabla byte a byte.
- **`math2d` la usa** (`sin_q12`/`cos_q12` == `kSinTab`) y un valor concreto (`kSinTab[512]=2896`).

## Por qué existe

`sinetable.hpp` genera la tabla con **Bhaskara I + redondeo**, que difiere hasta **±8** de la
tabla exacta del original (medido: 3608/4096 entradas distintas). Los efectos portados 1:1
(p. ej. `plasma`) necesitan la exacta. La fuente única es `tools/assets/gen-sintab.mjs`, que
reconstruye la tabla sumando los **deltas** de `libmisc/sintab.c`.

## Ejecución

```bash
node tools/assets/gen-sintab.mjs        # regenera la tabla (reproducible)
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/020_sintab
```
