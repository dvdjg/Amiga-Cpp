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
- `demos/020_copper_basic/src/main.cpp`
- `demos/020_copper_basic/README.md`
- `demos/020_copper_basic/analyze-screenshot.ps1`
- `demos/030_ehb_palette_zones/src/main.cpp`
- `demos/030_ehb_palette_zones/README.md`
- `demos/030_ehb_palette_zones/analyze-screenshot.ps1`

Copper:

- `engine/include/amg/graphics/copper/copper.hpp`

Tooling:

- `tools/build/build-demo.ps1`
- `tools/run/run-demo.ps1`
- `tools/run/run-demo.mjs`
- `tools/analyze/analyze-demo.ps1`
- `tools/analyze/analyze-screenshot.ps1`
- `tools/input/mouse-path.mjs`
- `tools/input/mouse-path.ps1`
- `tools/test-regression.ps1`

Documentacion:

- `docs/ROADMAP_ENGINE_CPP_AMIGA500.md`
- `docs/BUILD_AND_RUN.md`
- `docs/CODING_STYLE.md`
- `docs/MEMORY_MODEL.md`
- `docs/GRAPHICS_DRIVERS.md`
- `docs/CONTINUATION_CONTEXT.md`
- `docs/HARDWARE_AND_ROM_KERNEL_POLICY.md`
- `docs/MOUSE_AUTOMATION.md`
- `docs/WINUAE_SIDE_CHANNEL_DEBUG.md`

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
- `020_copper_basic` valida una copperlist real en Chip RAM, instalada con COP1LC
  y COPJMP1, que genera bandas raster a pantalla completa.
- `030_ehb_palette_zones` valida 6 bitplanes EHB, punteros BPL1..BPL6, paleta de
  32 colores base, colores half-brite 32..63 y cambios completos de paleta por
  zonas Copper.
- Las demos de regresion ahora usan `run_frames(0xffff)` para que sea el runner
  quien cierre WinUAE y no se capture accidentalmente el prompt tras terminar.
- `analyze-demo.ps1` soporta analizadores especificos por demo mediante
  `demos\<demo>\analyze-screenshot.ps1`.
- WinUAE-DBG tiene dos conceptos distintos de raton absoluto: `win32.absolute_mouse`
  evita el camino Win32 de captura/warping del cursor del sistema, mientras que
  `absolute_mouse=mousehack` activa un modo Amiga-side que ya estaba documentado
  como inestable. Para pruebas automatizadas usamos `win32.absolute_mouse=yes`,
  `win32.active_capture_automatically=no` y `absolute_mouse=none`.
- `tools/input/mouse-path.ps1` permite mover el raton emulado del Amiga mediante
  comandos monitor `input mouse abs` y `input mouse button`, incluyendo trayectorias
  lineales, Bezier cuadraticas/cubicas, click y drag, sin depender del raton fisico
  de Windows.
- `tools/run/run-demo.ps1` tambien acepta `-MouseFrom`, `-MouseTo`,
  `-MouseControl`, `-MouseClick` y `-MouseDrag`; este camino integrado es el
  recomendado para regresiones porque inyecta entrada antes de cerrar la conexion
  GDB original.
- El tiempo de espera por defecto del runner sube a 18 s porque `020_copper_basic`
  puede arrancar correctamente pero llegar tarde a la primera captura de 12 s en
  algunas ejecuciones; a 18 s la captura ya muestra las bandas Copper.
- Queda documentado como desarrollo futuro necesario un canal lateral de depuracion
  para WinUAE-DBG. El GDB server actual sirve para automatizacion controlada, pero
  no basta para que David depure desde VS Code/Cursor y la IA entre a la misma
  instancia viva sin coordinarse. Ver `docs/WINUAE_SIDE_CHANNEL_DEBUG.md`.

Ultimo informe de regresion conocido:

```text
out\regression\20260529-103318\regression-report.md
```

## Siguiente paso previsto

La regresion automatizada ya existe en `tools/test-regression.ps1`.

Siguiente bloque recomendado:

1. Extraer de `030_ehb_palette_zones` un primer helper reutilizable para layout
   planar EHB y carga de punteros BPL.
2. Convertir la demo EHB en el primer embrion del driver `EhbScene`.
3. Empezar a medir presupuestos Copper por zonas visibles.
4. Preparar una escena EHB exportable desde UAF-R.
