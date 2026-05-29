# Registro de desarrollo

Este documento deja contexto operativo para continuar el proyecto incluso si se abre
una conversacion nueva desde cero.

## Estado actual

El workspace activo es:

```text
C:\Users\David\Documents\Programa\Amiga\Amiga-C
```

El objetivo actual es construir un engine C++23 para Amiga 500, empezando por una
base verificable de toolchain, ejecucion automatizada y captura visual.

## Decisiones tomadas

- El dialecto del engine sera `gnu++23`.
- El runtime Amiga se tratara como entorno casi freestanding.
- No se dependera de STL pesada ni de cabeceras hosted como `stdint.h`.
- Las abstracciones internas deben ser C++ real: `Engine<Game, Backend>`, conceptos,
  arenas, drivers graficos y backends intercambiables.
- La logica de juego no debe depender directamente de Amiga; Amiga sera un backend.
- El perfil inicial de hardware es `A500_1MB_Slow`.
- La Slow RAM se considera memoria de capacidad, no Fast RAM real.
- El desarrollo sera close-to-metal, con el Amiga Hardware Reference Manual
  local como referencia principal.
- El uso del ROM kernel queda permitido como politica opcional del backend,
  especialmente para modo OS-friendly, reserva/restauracion y prototipado.
- Todo codigo fuente nuevo debe comentarse con estilo tutorial, especialmente las
  cabeceras compartidas.
- ACE se mantiene como candidato a backend/HAL posterior, pero la base inicial usa
  un backend minimo propio para validar toolchain y flujo.
- Los "profiles" graficos se han formalizado como drivers graficos: `EhbScene`,
  `Standard5`, `Standard4`, `FakeDualPlayfield`, `DualPlayfield`,
  `SpriteBackdrop` y `CopperHeavy`.

## Descubrimientos tecnicos

- El compilador del plugin es GCC 15.1.0 para `m68k-amiga-elf`.
- Acepta `-std=c++23` y `-std=gnu++23`.
- El entorno no tiene un `stdint.h` hosted completo en este modo; los tipos base del
  engine se definen manualmente en `amg/core/types.hpp`.
- Para lanzar una demo con WinUAE-DBG hay que continuar la ejecucion tras conectar,
  porque `debugging_trigger=:a.exe` deja el programa cargado/parado como en una
  sesion de depuracion.
- La captura interna del monitor de WinUAE funciona y guarda PNG correctamente.
- El overlay de debug de WinUAE-DBG sirve como primera salida visual verificable.

## Artefactos creados

Engine base:

- `engine/include/amg/core/types.hpp`
- `engine/include/amg/engine.hpp`
- `engine/include/amg/memory/arena.hpp`
- `engine/include/amg/graphics/driver.hpp`
- `engine/include/amg/platform/amiga_minimal.hpp`
- `engine/src/platform/amiga_minimal/amiga_minimal.cpp`

Demo inicial:

- `demos/000_toolchain_cpp23/src/main.cpp`
- `demos/000_toolchain_cpp23/README.md`
- `demos/010_chip_slow_memory/src/main.cpp`
- `demos/010_chip_slow_memory/README.md`

Tooling:

- `tools/build/build-demo.ps1`
- `tools/run/run-demo.ps1`
- `tools/run/run-demo.mjs`
- `tools/analyze/analyze-demo.ps1`
- `tools/analyze/analyze-screenshot.ps1`
- `tools/test-regression.ps1`

Documentacion:

- `docs/ROADMAP_ENGINE_CPP_AMIGA500.md`
- `docs/BUILD_AND_RUN.md`
- `docs/CODING_STYLE.md`
- `docs/MEMORY_MODEL.md`
- `docs/GRAPHICS_DRIVERS.md`
- `docs/CONTINUATION_CONTEXT.md`
- `docs/HARDWARE_AND_ROM_KERNEL_POLICY.md`

## Comandos verificados

```powershell
.\tools\build\build-demo.ps1 demos\000_toolchain_cpp23 -DebugBuild
.\tools\run\run-demo.ps1 demos\000_toolchain_cpp23
.\tools\analyze\analyze-demo.ps1 demos\000_toolchain_cpp23
```

Resultado verificado:

- se genera `out\demos\000_toolchain_cpp23\000_toolchain_cpp23.exe`;
- se genera `out\demos\000_toolchain_cpp23\000_toolchain_cpp23.elf`;
- WinUAE arranca y ejecuta la demo;
- se genera `out\run\000_toolchain_cpp23\screenshot.png`;
- el analizador visual detecta el overlay y pasa.
- la regresion completa pasa con `tools\test-regression.ps1`.
- `010_chip_slow_memory` valida el bootstrap de arenas desde `MinimalBackend`.
- En la captura de `010_chip_slow_memory`, Chip aparece en rango bajo
  `0x00015050` y Slow en zona trapdoor/bogo `0x00C0F830` en la configuracion
  emulada actual.

Ultimo informe de regresion conocido:

```text
out\regression\20260529-014748\regression-report.md
```

## Siguiente paso previsto

La regresion automatizada ya existe en `tools/test-regression.ps1`.

Siguiente bloque recomendado:

1. Separar el analisis visual por demo para no depender solo de colores genericos.
2. Crear `020_copper_basic`.
3. Preparar una copperlist minima gestionada por engine.
4. Empezar a definir el primer driver visual real `EhbScene`.
