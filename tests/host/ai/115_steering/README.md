# HOST-115: steering behaviors

Test host de `engine/include/eng/ai/steering/steering.hpp`: velocidades de movimiento
continuo genéricas sobre el escalar (`seek`/`flee`/`arrive`, `separation`/`cohesion`/
`alignment`/`flock` y `pursue`/`evade`/`wander`/`avoid_circles`), sobre el vocabulario
geométrico de `eng::math`.

## Qué comprueba

1. **seek/flee/arrive**: dirección y magnitud correctas; `arrive` frena por dentro de
   `slow_radius` y da cero en el objetivo.
2. **Flocking**: `separation` aleja, `cohesion` acerca al centroide, `alignment` imita la
   velocidad media, y `flock` combina los tres con pesos.
3. **Genérico sobre el escalar**: `double` y `q12` (con `fixed_math`). Documenta el
   límite de `length_sq` en q12 (componentes ≤ ~2).
4. **Persecución/evasión/deambular/obstáculos** (G4.4): `pursue`/`evade` apuntan a la
   posición prevista del objetivo móvil (con `max_speed = 0` devuelven cero); `wander`
   orbita un círculo por delante según `heading`/`jitter`; `avoid_circles` empuja al lado
   libre e ignora lo que queda detrás.

## Salida de referencia

```
Steering:
OK: Steering (seek/flee/arrive, flocking, pursue/evade/wander/avoid)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/ai/115_steering
```
