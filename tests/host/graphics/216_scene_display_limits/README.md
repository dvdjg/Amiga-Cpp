# HOST-216: `scene/limits.hpp` (perfiles, validación y coste de bus)

Test host de los **límites de display** (`eng/graphics/composition/limits.hpp`): el perfil de
capacidades por máquina (`DisplayLimits`), sus perfiles de datos (`ocs_a500`, `ecs`,
`aga_a1200`), la validación estática/runtime (`validate`/`valid_scene`) y el **coste de bus**
informativo (`dma_cost`). Ver `docs/engine/architecture/SCENE_COMPOSITION.md` §6.1.

## Qué comprueba

1. **Perfiles**: `ocs_a500`/`ecs` (6 planos, fetch 1×) y `aga_a1200` (8 planos, HAM8 = 8,
   DPF 4+4, fetch 4×); slots usables 226 y fijos 27.
2. **`dma_cost`** a 320 px (20 palabras de fetch por plano): 4 planos OCS = 80 slots de
   bitplane y 119 de CPU; 6 planos = 120/79; 8 planos AGA a 1× = 160/39; a 4× = 40/159.
   Un `fw` por encima del perfil se acota (OCS 4× → 1×).
3. **`valid_fetch_width`**: OCS/ECS no admiten 2×/4×; AGA sí.
4. **`validate`**: códigos de rechazo (1 ancho no múltiplo de 16, 3 por encima de los 368
   visibles, 5 más de 6 planos sin AGA, 9 planos del modo) y aceptación por modo (HAM6,
   HAM8, DPF), con HAM8/DPF 4+4 solo en AGA.
5. **`geometry_for`**: la geometría derivada de 320×256 coincide con `kPal320x256`.

Todo se comprueba con `static_assert` sobre funciones `constexpr`/`consteval`: no hay
hardware ni estado mutable.

## Salida de referencia

```
OK: scene::limits — perfiles OCS/ECS/AGA, DmaCost (fetch 1x/2x/4x), validacion y geometria.
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/graphics/216_scene_display_limits
```
