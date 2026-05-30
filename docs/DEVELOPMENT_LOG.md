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
- `tools/debug/winuae-side-channel.mjs`
- `tools/debug/winuae-side-channel.ps1`
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
- WinUAE-DBG incluye ahora un MVP de canal lateral TCP local en `127.0.0.1:2346`,
  implementado en `WinUAE-DBG/od-win32/barto_gdbserver.cpp`. Expone `hello`,
  `state`, `regs`, `mem` y `runstatus` como JSON por linea, separado del socket
  GDB principal.
- `tools/run/run-demo.mjs` usa ese canal para esperar `g_amg_run_status` mientras
  el 68000 sigue corriendo. El timeout largo queda como compatibilidad si se usa
  un WinUAE antiguo, pero la regresion actual llega a `side-channel READY`.
- Al integrar el canal se descubrio que `030_ehb_palette_zones` pasaba demasiado
  tiempo en `InitStarted`: la demo generaba el patron EHB pixel a pixel tras
  volver a velocidad real. Se optimizo a escritura por bytes de bitplane, que es
  mas representativo del futuro flujo UAF-R y permite READY en pocos segundos.
- El canal lateral no sustituye a la captura ni al profiler: sirve como senal de
  vida/logica; la imagen y el analizador siguen validando el resultado grafico.
- La colaboracion profunda persona+IA sobre una sesion manual aun requiere los
  siguientes incrementos del canal: `observe/assist/takeover`, debug lock,
  auditoria de escrituras y comandos seguros para profiler/screenshot/input.
  Ver `docs/WINUAE_SIDE_CHANNEL_DEBUG.md`.
- El canal lateral ya implementa `observe/assist/takeover`, `lock acquire/release`,
  acciones encoladas para `screenshot`, `input` y `profile`, `action status` y
  `profile-status`. `input` y `profile` requieren lock `assist`/`takeover`.
- Se ha añadido `tools/debug/verify-side-channel-contract.mjs`, que lanza
  `030_ehb_palette_zones`, entra por el canal lateral mientras GDB sigue vivo,
  comprueba el debug lock, inyecta raton emulado, captura PNG y genera un perfil
  de 1 frame. Verificacion local correcta el 2026-05-30.
- Durante esa validacion se corrigio en WinUAE-DBG el tokenizer del canal lateral:
  las rutas Windows entrecomilladas conservan `\` y ya no se convierten en rutas
  mutiladas como `C:Users...`.
- Se ha añadido `tools/debug/verify-gdb-step-side-channel.mjs` con wrapper
  PowerShell. Esta prueba valida la depuracion normal: breakpoint GDB en
  `amg_debug_ready_probe`, `continue`, parada `T05swbreak`, tres pasos
  instruccion-a-instruccion y lecturas por canal lateral simultaneas durante la
  ejecucion y tras cada parada. Verificacion local correcta el 2026-05-30.
- Se ha añadido el primer `takeover` reversible: comandos laterales `poke`,
  `rollback` y `audit`. `poke` exige lock `takeover`, guarda bytes previos, escribe
  hasta 256 bytes, verifica lectura posterior y deja `writeId`. `rollback` restaura
  los bytes originales y marca la auditoria como revertida.
- Se ha añadido `tools/debug/verify-side-channel-takeover.mjs` con wrapper
  PowerShell. La prueba escribe temporalmente `12345678` en
  `g_amg_run_status.detail`, verifica por `mem`, consulta auditoria y revierte al
  valor original. Verificacion local correcta el 2026-05-30.
- Se ha añadido `pause`/`resume` lateral bajo lock `takeover`, con
  `tools/debug/verify-side-channel-pause-resume.mjs`. `pause` se encola en
  `vsync_pre()`; `resume` es inmediato para poder salir de una emulacion pausada.
  La prueba verifica pausa, lectura de memoria mientras esta detenido, reanudacion
  y cierre limpio del runner.
- Se ha retomado el roadmap del engine con el primer nucleo reutilizable del driver
  EHB: `engine/include/amg/graphics/drivers/ehb_scene.hpp`. `StaticEhbScene`
  reserva bitplanes/copperlist en Chip RAM, activa 6 bitplanes EHB, habilita DMA de
  bitplanes y compila paletas/zones de alto nivel a una copperlist real.
- `030_ehb_palette_zones` ya usa `StaticEhbScene`: la demo solo declara paletas y
  genera el patron planar de prueba. Los registros BPL/DIW/DDF/COLOR y el setup DMA
  quedan encapsulados en el driver.
- Se ha añadido `engine/include/amg/graphics/copper/scheduler.hpp`. El
  `CopperScheduler` minimo centraliza el setup EHB y las zonas de paleta, y produce
  `ScheduleReport` con palabras usadas, waits, movimientos de paleta y avisos de
  zonas pesadas visibles. Es el primer paso hacia `CopperTimeline`.
- Se ha añadido `engine/include/amg/graphics/copper/timeline.hpp`. `CopperTimeline`
  cuenta waits/moves por linea raster y marca lineas visibles que superan un
  presupuesto conservador de H-BLANK. De momento informa, no prohibe: las escenas
  Copper-heavy siguen siendo posibles, pero quedan trazadas.
- Se ha añadido `engine/include/amg/graphics/effects/palette_cycle.hpp` y la demo
  `040_palette_cycle_effect`. `PaletteCycleEffect` rota un tramo de paleta fisica
  sin tocar bitplanes; la demo espera varias fases antes de `READY` y el analizador
  comprueba captura y `runStatus.detail`.
- Se ha añadido `engine/include/amg/graphics/frame_plan.hpp`. La demo 040 ya no
  recompila toda la copperlist cada frame: genera un `FramePlan` con un parche de
  paleta y `StaticEhbScene` actualiza solo las words de valor de `COLOR01..07`.
  `StaticEhbScene` guarda bindings de paleta base y zonas Copper al construir la
  lista para poder parchearlas despues.
- `FramePlan` ahora soporta operaciones de Blitter: `CopyRect`, `RestoreRect`,
  `MaskedBobCookieCut` y `MaskedBlobNoSave`, con presupuesto acumulado por jobs y
  words. El backend Amiga ejecuta esos jobs en
  `MinimalBackend::execute_frame_plan()`.
- Se ha añadido `demos/050_blitter_bobs`: dibuja un BOB de 32x32 y dos blobs
  no-save no solapados, todos X alineados a 16 pixels, sobre una escena EHB. La
  demo valida el camino `FramePlan -> backend -> Blitter` y deja
  `runStatus.detail = 0x05000203`.
- El roadmap incorpora una seccion de abstracciones futuras: `RenderScene`
  retenido, `FramePlan`, `CopperScheduler`, `BlitterQueue`, `SpriteAllocator`,
  `DmaBudget`, drivers `RoadRaster`, `SpriteBackdrop`, `CopperHeavy` y efectos
  demoscene reutilizables.

Ultimo informe de regresion conocido:

```text
out\regression\20260531-002019\regression-report.md
```

## Siguiente paso previsto

La regresion automatizada ya existe en `tools/test-regression.ps1`.

Siguiente bloque recomendado:

1. Ampliar `050_blitter_bobs` con animacion real, save/restore de fondo y dirty
   rects.
2. Convertir el presupuesto de Blitter en warnings/criterios de aceptacion por
   frame.
3. Añadir clipping y shifts para X no alineada a 16 pixels.
4. Añadir doble buffer de copperlist cuando un frame necesite cambiar estructura,
   no solo valores de `COLORxx`.
