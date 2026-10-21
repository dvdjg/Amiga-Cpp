# HOST-471: pacing de render (`eng::graphics/render_pacing.hpp`)

Test host de la regla que convierte el **coste medido de la pasada completa** (ciclos de CPU) en el numero de **Fake frames** que conviene intercalar antes del siguiente Full, con los objetivos de cadencia de la fase F1 del `ROADMAP_3D_PRESENTACION` (base >= 12,5 Hz y presentacion con interpolados >= 25 Hz).

## Que comprueba

1. **Sin medida**: `affordable_fakes` devuelve 0 (camino conservador: Full por tick).
2. **Campos al techo**: `full_fields()` = `ceil(full/tick)` (medio campo -> 1; un ciclo por encima -> 2).
3. **Regla y clamps**: `K = max(0, campos - 2)`; `F <= 2` -> 0 fakes; `F = 3` -> 1 fake; `F = 4` -> 2; `F >= 5` -> 3 con el cap de la demo.
4. **Máximo de las dos últimas muestras**: un Full caro cambia el pacing inmediatamente; un Full barato aislado no lo reduce y dos Fulls baratos consecutivos sí.
5. **Techo conservador**: cualquier coste que supere un número entero de campos se asigna al siguiente campo (`3 campos + 1 ciclo` -> 4).

## Salida de referencia

```
Pacing de render (render_pacing):
OK: render pacing (máximo del último par, techo de campos y cap 0..3)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/graphics/471_render_pacing
```
