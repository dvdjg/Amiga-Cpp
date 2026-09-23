# HOST-134 — degradado por banda (efecto)

Test host de `eng/graphics/effects/raster_gradient.hpp`: valida la lógica pura del efecto
`RasterGradientEffect` (muestreo de colores clave, geometría de bandas e intenciones) sin
`copper::Plan` ni hardware. Su integración con el `copper::Plan` se respalda con la demo
085 (el cielo pasa a generarlo este efecto).

## Qué comprueba

1. **Lineal** (`cyclic = false`): primera clave en la banda 0 y última en `bands-1`, con
   la interpolación intermedia (`0x000 → 0xfff` en 4 bandas = `0x000, 0x555, 0xaaa, 0xfff`).
2. **Geometría**: `top = first_line + b·band_height`.
3. **Intención**: `PaletteLine`, `count = 1`, `first` configurado, color en `colors[first]`.
4. **Cíclico con `phase`**: con tantas claves como bandas y `phase` entero, cada banda
   toma una clave y el degradado “rueda” (`phase=1` rota la secuencia).
5. **`first = 1`**: la vista de color abarca `first+1` y el color va en el índice 1.
6. **Límites**: `cap` recorta; sin claves devuelve 0; `apply_into` llama a `plan.add`.

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/graphics/134_raster_gradient
```
