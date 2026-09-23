# HOST-052 — la librería es agnóstica del escalar

Prueba de que `Vec<N,S>` / `Mat<N,S>` / `dot` / `Affine` **no saben nada** del tipo de
dato: el mismo código se instancia con cuatro escalares distintos.

## Qué valida

| Escalar | Definición | Uso |
|---|---|---|
| 4.12 | `Fixed<s16, 12>` | el de lib3d |
| **8.8** | `Fixed<s16, 8>` | el "fix88" del usuario |
| `float` | nativo (FPU del A4000 o emulación) | el mismo `Mat<2,float>` |
| **complejo** | `struct Cpx { s16 re, im; }` — definido **en el test** | mismo `Mat` y `dot` |

- El **mismo** `Mat<2, S>::identity()` y `operator*` con los cuatro.
- `dot(Vec<N,S>, Vec<N,S>)` usa el **producto interno que define el escalar**: para el
  complejo, el conjugado (`i·conj(i) = 1`), no el producto a secas.

## El punto de extensión

Un escalar nuevo sólo necesita `+`, `-`, `*` y un cero/uno construibles; la plantilla
**primaria** de `scalar_traits` da el resto. Se especializa sólo si cambia algo:

```cpp
template <> struct scalar_traits<Cpx> {
    static constexpr Cpx inner(Cpx a, Cpx b) { return a * Cpx{b.re, -b.im}; } // conjugado
    static constexpr Cpx one() { return Cpx{4096, 0}; }
    template <typename P> static constexpr Cpx norm_from(P p) { return p; }
};
```

`Fixed` tiene su especialización porque su producto **cambia de exponente** y hay que
normalizar; `float` porque no hay nada que normalizar. Nada de eso está cableado en
`Mat`/`Vec`.

## Ejecutar

```
CXX="C:\Users\dvdjg\Documents\programa\AI\Amiga\mingw64\bin\g++.exe" \
  bash tools/run-host-tests.sh tests/host/052_math_generic_scalar
```
