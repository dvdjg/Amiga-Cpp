# HOST-058 — puente `MiniFloat16` ↔ coma fija retro (`eng/retro/minifloat_fixed.hpp`)

Fija la compatibilidad entre `MiniFloat16` y el fixed retro (`fix` = 4.12, `fix88` = 8.8):
conversiones explícitas (con saturación), producto mixto y **transformación de
coordenadas fijas con una matriz MF**.

## Qué cubre

- **Conversiones** `MiniFloat16` ↔ `Fixed<s16,Frac>` y `fix`/`fix88` en todo el rango,
  con valores exactos (`mf_to_fix(1.5)=6144`, `mf_to_fix88(1.5)=384`) y **saturación**
  (`mf_to_fix(100)=32767`, `mf_to_fix(-100)=-32768`, `mf_to_fix88(200)=32767`).
- **Producto mixto** `ratio · valor fijo` (`mul_fixed`/`mul_fix`/`mul_fix88`): error rel
  ≈ `1.5e-3` en 4.12 y `3.9e-3` en 8.8 (acotado por los 8 bits de fracción).
- **Transformación** `Mat<N, MiniFloat16> · Vec<N, Coord>` (y afín `+ t`), tipada y
  cruda, para 4.12 y 8.8. Contra `float`: identidad **exacta**, rotación 90° exacta,
  `transform` 3x3 ≈ `1.1e-4` abs (muy por debajo de la resolución `1/4096`), y saturación
  fuera de rango.
- **4x4 homogéneo / proyección**: `transform_point(m4,p3)` da `M·(p,1)` con `w` (vale 1
  para una matriz afín); `project(m4,p3)` divide por `w` y devuelve MF (cabe pantalla).
- **Atajo 2D de `lib2d`**: el mismo `transform` acepta `Vec2 = Vec<2,q0>` (píxeles
  enteros) con una matriz 2x2 de MF.
- La vía **tipada** (`Coord12`/`Coord88`, tag = exponente de `Fixed`) y la cruda dan el
  mismo resultado; 4.12 y 8.8 no se pueden mezclar en el tipo.

## Cómo está implementado

La matriz MF se convierte **una vez** a `Fixed<s16,Frac>` y cada fila acumula
`ratio·coordenada` en 32 bits con `muls.w`, normalizando con **un único**
desplazamiento (producto escalar fusionado). La coordenada conserva sus 12 bits de
fracción y no pasa por la mantisa de 10 bits del MF. Verificado en el `.o` de m68k:
`muls.w`, sin `divs`/`divu` ni libcalls de coma flotante.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/058_minifloat_fixed
```
