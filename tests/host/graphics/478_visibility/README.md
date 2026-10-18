# HOST-478: culling de entrada 3D (`eng/core/math/frustum.hpp`)

Test host de la **selección de objeto antes de transformar** (la métrica de A500 es «objetos que nunca llaman a `transform_vertices`»; ver `3D_GAME_ARCHITECTURE.md` §Visibilidad): vista yaw-only, frustum general de 6 planos y descarte por tamaño en pantalla.

## Qué comprueba

1. **Vista yaw-only** (`YawView`): rechazos por cada frontera (detrás del near, más allá del far, fuera de la franja vertical, fuera de la cuña por ambos lados) y aceptación de bordes — cruzar el plano cercano = **visible** (conservador, cero falsos negativos).
2. **Equivalencia** con el frustum general de 6 planos construido con los mismos parámetros (la cuña va pre-escalada por `1/(1+W)` para que ambos usen el mismo test conservador): barrido de centros/radios con `float` (el frustum general necesita fracciones).
3. **Descarte por tamaño** (`screen_size_visible`): subpíxel sin división (`radio·focal >= min_px·z`).
4. **Dos escalares**: la vista yaw funciona con `coord` (q0, ratios enteros) y con `float` (incluido un yaw de 90°).

## Salida de referencia

```
OK: culling de entrada (yaw view, frustum general, tamano y dos escalares)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/graphics/478_visibility
```

## Notas

- El coste del test es de 4-6 comparaciones y pares `mul`/`add` (≲80 ciclos) frente a ≈57k de transformar 60 vértices (`lib3d` 4.12): es la capa que más frame ahorra por ciclo invertido.
- La capa de **sectores/portales** (flood con presupuesto) queda pendiente como capa superior del mismo sistema; el orden de implementación está en `3D_GAME_ARCHITECTURE.md` §Visibilidad.
