# HOST-020 — Tabla de seno exacta del original (constexpr)

Fija la tabla de seno **4.12 exacta** del original (`libmisc/sintab.c`), generada en
**constexpr** en `engine/include/eng/core/sintab.hpp` y usada por `math2d::SinTableQ12`.

## Qué cubre

- **Invariantes**: `sin(0)=0`, `sin(π/2)=4096`, `sin(π)=0`, `sin(3π/2)=-4096`.
- **Simetría** (`sin(4096−i) = −sin(i)`) y **monotonía** en el primer cuadrante.
- **Checksum** de regresión (`1083047936`) → fija la tabla byte a byte.
- **`math2d` la usa** (`sin_q12`/`cos_q12` == `kSinTab`) y un valor concreto (`kSinTab[512]=2896`).

## Por qué existe

`sinetable.hpp` generaba con **Bhaskara I + redondeo**, que difiere hasta **±8** de la tabla
exacta del original (medido: 3608/4096 entradas distintas). Los efectos portados 1:1 (p. ej.
`plasma`) necesitan la exacta.

`sin` es trascendente y `constexpr` no puede llamar `std::sin`; se resuelve con una **serie de
Taylor con reducción al cuadrante** en doble precisión (`detail::sin_series`). Con 12 términos
el error queda por debajo del umbral de truncado: el generador produce la tabla del original
**4096/4096 (0 diferencias)** — verificado antes de fijarlo. `dualtab` sí pudo ser formula
entera; aquí la generación es numérica pero igualmente determinista (evaluada por el
compilador).

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/020_sintab
```
