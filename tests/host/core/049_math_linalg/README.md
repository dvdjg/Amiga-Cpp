# HOST-049 — álgebra lineal genérica (`eng::math::Vec`/`Mat`/`Affine`)

Test de F2 del roadmap (`docs/engine/architecture/MATH_LIBRARY.md`): las mismas
plantillas sirven para fixed-point y para `float`, porque toda la aritmética pasa por
`scalar_traits<S>`.

## Qué valida

- **Vec**: suma, resta y producto escalar con normalización fusionada.
- **Mat**: `identity`, `mat*mat` (R90·R90 = −I en 4.12), `mat*vec`, `transpose`,
  `determinant` 2×2.
- **Affine**: `M*p + t` con la traslación en **LONGITUD** y la parte lineal en
  **RATIO** — el caso que el tipado resuelve; `translate` exige LONGITUD + LONGITUD.
- **Genericidad**: el MISMO `Mat<3,float>::operator*` y `Mat<3,float> * Vec<3,float>`
  funcionan con `float`.
- **Tipos**: `mat*mat` conserva el tipo; `mat(ratio)*vec(longitud) -> vec(longitud)`
  (comprobado en compilación).

## Coste (verificado en el `.s` de 68000)

`transform` de 3×3 y `mat*mat` 3×3: `muls.w` nativo, **un solo `asr.l` por elemento**
(acumulación exacta + una normalización) y **cero libcalls**. El compilador mantiene el
bucle **plegado**: en 68000 (prefetch de 2 palabras) puede ser mejor que desenrollar;
la decisión se mide en F4.

## Ejecutar

```
CXX="C:\Users\dvdjg\Documents\programa\AI\Amiga\mingw64\bin\g++.exe" \
  bash tools/run-host-tests.sh tests/host/core/049_math_linalg
```
