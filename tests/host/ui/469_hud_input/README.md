# HOST-469 - `hud_input` (entrada del HUD, H2)

Test host de la fase **H2** del HUD (`docs/guides/roadmap/ROADMAP_HUD.md`): `engine/include/eng/hud/input.hpp` (`HudInput` + `hud_hit_test`).

## Qué cubre

- Hit-test por capa y por fórmula de rejilla, incluido un *pitch* que no es múltiplo de 16.
- *Click* como `down`+`up` sobre el mismo objetivo; selección y foco independientes.
- Navegación por joystick/acción y consumo de entrada según `capture_input` (`HUD_SYSTEM.md` §7).
- Prioridad de regiones con `hud_hit_test_region` (base de las dos regiones simultáneas de la demo 306).

Freestanding: sin hardware, sin heap; solo `printf` del host para el informe.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/ui/469_hud_input
```
