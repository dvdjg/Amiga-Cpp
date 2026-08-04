# Batería de tests del engine (por capas de abstracción)

Esta batería reactiva el desarrollo partiendo del **conocimiento bare metal del Amiga 500**
y valida cada capa de abstracción del engine por separado. Los tests no son demos de
lucimiento: son **tutoriales didácticos verificables**. El código está comentado como una
clase, cada test incluye un `README.md` con los pasos y un **script de verificación
determinista** (canal lateral WinUAE o pixel assertions) que prueba que lo que se dibuja
o escribe es correcto.

## Capas

El engine se construye de abajo hacia arriba. Un test de una capa puede apoyarse en capas
inferiores (eso es lo realista), pero ejercita principalmente la suya:

| Carpeta | Capa | Qué ejercita |
|---------|------|--------------|
| [l0_bare_metal/](l0_bare_metal/README.md) | L0 | Registros custom, bitplanes planares, copperlist a mano, DMA y Blitter. |
| `l1_backend/` | L1 | APIs de `MinimalBackend` (memoria, VBlank, copper, FramePlan). |
| `l2_copper_frameplan/` | L2 | `CopperScheduler`, `CopperTimeline`, `FramePlan` y presupuestos. |
| `l3_drivers/` | L3 | Drivers gráficos (`StaticEhbScene`, futuro `Standard5`, `DualPlayfield`...). |
| `l4_scene/` | L4 | `VirtualScene`, `Camera2D`, `TileLayer`, escenas retenidas. |

## Convenciones de cada test

Cada test vive en `tests/<capa>/<NNN_<nombre>/` y debe contener:

- `src/main.cpp` — código didáctico comentado por capas, sin STL, `gnu++23` freestanding.
- `README.md` — qué hace, qué capas ejercita, qué registros/APIs usa y cómo verificar.
- `verify-<algo>.mjs` (o `.ps1`) — script determinista por canal lateral/GDB.
- `analyze-screenshot.ps1` — comprobación de la captura de pantalla cuando aplica.

Además, cada test debe:

- exponer `g_eng_run_status` y alcanzar `Ready` (el runner depende de ello);
- terminar restaurando el sistema (volver a Workbench) o documentar por qué no aplica;
- dejar evidencia en `out/run/<test>/` (capturas, informe).

## Cómo ejecutar un test

Los scripts de `tools/` aceptan cualquier ruta de demo/test (derivan el nombre del leaf),
así que el flujo es el mismo que para las demos:

```powershell
# Compilar
powershell -ExecutionPolicy Bypass -File .\tools\build\build-demo.ps1 tests\l0_bare_metal\010_display_320x240 -DebugBuild -Clean

# Ejecutar y capturar (el runner espera READY por canal lateral)
powershell -ExecutionPolicy Bypass -File .\tools\run\run-demo.ps1 tests\l0_bare_metal\010_display_320x240

# Analizar
powershell -ExecutionPolicy Bypass -File .\tools\analyze\analyze-demo.ps1 tests\l0_bare_metal\010_display_320x240

# Verificación determinista por canal lateral (si el test tiene verify-*.mjs)
node .\tests\l0_bare_metal\010_display_320x240\verify-framebuffer.mjs --demo tests\l0_bare_metal\010_display_320x240
```

Los artefactos se generan en `out/demos/<leaf>/` (build) y `out/run/<leaf>/` (ejecución).

## Cómo añadir un test

1. Elige la capa (`l0_bare_metal`, `l1_backend`, ...) y el siguiente número.
2. Escribe `src/main.cpp` como tutorial: cada paso con su comentario y la capa que toca.
3. Añade `README.md` con los pasos y el contrato de verificación.
4. Añade el `verify-*.mjs` si el test debe leerse/verificarse por canal lateral.
5. Regístralo en el catálogo del roadmap:
   `docs/architecture/ROADMAP_ENGINE_CPP_AMIGA500.md` (sección 21).

## Catálogo

| ID | Test | Capa | Estado |
|----|------|------|--------|
| L0-010 | [display_320x240](l0_bare_metal/010_display_320x240/README.md) — modo 320x240, 5 bitplanes, paleta 32, líneas, verificación framebuffer y vuelta a Workbench. | L0/L1 | implementado |

## Herramientas de verificación usadas

- Canal lateral WinUAE-DBG (puerto 2346): `tools/debug/winuae-side-channel.ps1`.
- Verificación de símbolos desde el `.map`: patrón de `tools/debug/verify-side-channel-takeover.mjs`.
- Especificación del canal lateral: `docs/emulation/WINUAE_SIDE_CHANNEL_DEBUG.md`.
