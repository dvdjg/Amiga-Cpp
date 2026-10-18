# HOST-471: pacing de render (`eng::graphics/render_pacing.hpp`)

Test host de la regla que convierte el **coste medido de la pasada completa** (ciclos de CPU) en el numero de **Fake frames** que conviene intercalar antes del siguiente Full, con los objetivos de cadencia de la fase F1 del `ROADMAP_3D_PRESENTACION` (base >= 12,5 Hz y presentacion con interpolados >= 25 Hz).

## Que comprueba

1. **Sin medida**: `affordable_fakes` devuelve 0 (camino conservador: Full por tick).
2. **Campos al techo**: `full_fields()` = `ceil(full/tick)` (medio campo -> 1; un ciclo por encima -> 2).
3. **Regla y clamps**: `K = max(0, campos - 2)`; `F <= 2` -> 0 fakes; `F = 3` -> 1 fake (ciclo 4 campos = 12,5/25 Hz); `cap` recorta la racha; `cap = 0` fuerza Full por tick.
4. **Media movil 1/4**: un pico aislado de Full no dispara la racha; una racha de Fulls caros converge arriba y vuelve a bajar cuando el Full se abarata.
5. **Caso objetivo de la 080**: 3,125 campos -> techo 4 -> 2 fakes (cap 3); 2,97 campos -> techo 3 -> 1 fake.

## Salida de referencia

```
Pacing de render (render_pacing):
OK: render pacing (campos, regla de fakes, cap y media movil)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/graphics/471_render_pacing
```
