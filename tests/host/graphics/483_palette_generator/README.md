# HOST-483 — palette_generator

Verifica `eng/graphics/palette_generator.hpp`: generación RGB444 determinista y `constexpr` para 3, 4 y 5 planos, reserva convencional del índice cero, inclusión de extremos, grises sin duplicados, orden de luminancia y derivación exacta de los 32 half-bright en EHB.

El recorrido de candidatos es una heurística reproducible para distribuir muestras; no constituye una prueba de optimalidad perceptual ni de calidad checkerboard para imágenes arbitrarias.

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/graphics/483_palette_generator
```

Salida esperada: `HOST-483 palette_generator: OK`.
