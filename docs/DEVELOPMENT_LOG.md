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
  demo valida el camino `FramePlan -> backend -> Blitter`.
- `050_blitter_bobs` ahora anima el BOB usando save/restore real por Blitter:
  restaura la posicion anterior, guarda el fondo de la nueva posicion y dibuja el
  BOB cookie-cut. Los blobs no-save quedan fijos como ruta estilo Mega Typhoon. El
  estado saludable actual deja `runStatus.detail = 0x05020311`.
- `FramePlan` tambien fusiona dirty rects. El 050 publica en `runStatus.detail`
  un dirty rect final y una fusion, y congela la imagen tras el frame validado para
  que las capturas automatizadas no caigan en mitad de `restore/save/draw`.
- `BlitJob` incorpora `source_shift` y el backend Amiga lo programa como shift A/B
  en `BLTCON0`/`BLTCON1`. La demo `051_blitter_shifted_bobs` dibuja un BOB
  cookie-cut en X no alineada, con fuente de tres words por fila, y el analizador
  verifica color, `runStatus.detail = 0x05190301` y posicion logica cercana a X=73.
- Se deja como decision de arquitectura que un blob/playfield pueda aportar
  intenciones Copper asociadas (paleta, splits, ondulaciones o regiones no
  rectangulares), pero siempre canalizadas por `CopperScheduler`, no escribiendo
  registros Copper desde el recurso individual.
- `FramePlan` distingue `TileBlockCopy` de `CopyRect`. El backend usa la misma
  copia C->D, pero el presupuesto cuenta `tile_jobs` para no mezclar cargas de
  tilemap con BOBs o restores.
- Se ha añadido `052_tile_staging_blits`: compone un bloque 4x4 de tiles 16x16 en
  un buffer Chip RAM no visible mediante 16 `TileBlockCopy`, y lo publica despues
  al playfield visible con un `CopyRect`. El estado saludable deja
  `runStatus.detail = 0x05210104`.
- Se ha indexado `C:\Users\David\Documents\Programa\Amiga\demoscene-repo` en
  `docs/DEMOSCENE_REPO_INDEX.md`. Hallazgos clave: `effects/tiles16` combina dirty
  flags por doble buffer, blits interleaved de tiles, coarse scroll por `BPLxPT`,
  fine scroll por `BPLCON1`, doble copperlist y commit en VBlank; `lib/lib3d`
  aporta una base de 3D fixed-point, culling, luz por cara y ordenacion Z.
- Se ha fijado `docs/DEMOSCENE_EFFECT_REPLICATION_POLICY.md`: las replicas de
  efectos de `demoscene-repo/effects` se implementaran como demos propias del
  engine, con logica limpia y registros custom encapsulados en capas bajas. La
  primera replica recomendada es `tiles16`.
- Se ha añadido `engine/include/amg/graphics/tilemap/tile_scroll.hpp` con el primer
  modelo retenido de tilemap 16x16: celdas con dirty flags por doble buffer,
  descomposicion de scroll en tile/coarse/fine y preparacion de frame sin conocer
  registros Amiga. `052_tile_staging_blits` ya lo usa para validar sus 16 tiles
  sucios antes de lanzar `TileBlockCopy`.
- Se ha añadido `engine/include/amg/scene/virtual_scene.hpp` con `Camera2D`,
  `TileLayer`, `TileScrollStrategy` y `VirtualSceneFrame`. La demo 052 ya crea una
  escena virtual retenida y pasa por ella antes de generar sus blits de staging.
- Se ha creado `docs/RETRO_ENGINE_API_BENCHMARK.md`, con aprendizajes de ACE,
  Scorpion Engine publico y UAF, y con el MVP recomendado para escenarios
  virtuales con scroll.
- Se ha añadido `100_virtual_tile_scene_scroll`: primer MVP visual de escenario
  virtual. Usa `VirtualScene`, `Camera2D`, `TileLayer`, mapa 64x16, tiles 16x16,
  scroll horizontal con fine X=9 y EHB con zonas Copper para cielo, jungla y
  subsuelo. La primera version saturaba la pila al guardar cache de tiles dentro
  del objeto local de `main`; se ha corregido moviendo el juego a almacenamiento
  estatico. El analizador comprueba atractivo visual por muestras de color y
  telemetria.
- `TileMap16` incorpora ahora vocabulario para scroll horizontal, vertical y
  bidireccional, mas `ProgressiveTileScheduler`. Este scheduler encola tiles
  offscreen con `frames_until_visible` y aplica un presupuesto por frame, siguiendo
  la idea de preparar tiles poco a poco antes de que crucen el borde visible. La
  demo 100 ya encola una columna derecha y acepta 4 updates por frame:
  `runStatus.detail = 0x10390941`.
- Se ha añadido `engine/include/amg/graphics/drivers/ehb_tile_scroll.hpp` y la demo
  `101_ehb_tile_scroll_driver`. Es el primer driver Amiga de tile scroll horizontal:
  reserva una superficie EHB mayor que la ventana visible, muestra 320x256, programa
  `BPLCON1` animado y convierte updates progresivos en `TileBlockCopy` reales
  ejecutados por Blitter con presupuesto pequeno por frame. El margen offscreen
  permite predibujar tiles sueltos durante varios VBLANKs, no una unica columna
  pintada a ultima hora. Despues se amplio a 480x416 para cubrir tambien prefetch
  vertical y rutas circulares cortas. El analizador valida la marca
  `0x11......`, camara dentro del margen y flags de prefetch.
- `EhbHorizontalRingPrefetch` añadio el primer contrato de anillo horizontal; la
  ruta actual usa `EhbBidirectionalRingPrefetch` para mapear columnas y filas de
  mundo a slots fisicos, recordar que franjas estan preparadas y reciclarlas por
  Blitter. El nibble bajo de `runStatus.detail` expone `0x1` para columnas y `0x2`
  para filas; el analizador exige ambos bits.
- El runner `tools/run/run-demo.*` ya no usa fallback largo por defecto cuando el
  canal lateral no alcanza READY. Si falla READY en pocos segundos, la prueba falla
  con diagnostico; el fallback queda solo como opcion explicita
  `--allow-timeout-fallback`.
- El runner ya no fuerza `warp=true`: las demos se lanzan a ritmo real por defecto
  para que `wait_vblank()` produzca scroll/animacion suave en WinUAE. `-Warp`
  queda como opcion explicita. El timeout lateral por defecto sube a 10000 ms
  porque el arranque realista de WinUAE/AmigaDOS ya no va acelerado.
- `tools/run/demo-menu.ps1` proporciona un menu para uso humano: elegir demo,
  compilar, lanzar, analizar, ejecutar secuencia o dejar WinUAE abierto para
  depuracion, siempre con warp desactivado salvo que se pida. `tools/test-regression.ps1`
  acepta `-Warp` para ciclos internos rapidos de IA.
- El runner puede capturar secuencias con `-SequenceFrames` y
  `-SequenceIntervalMs`. `tools/analyze/analyze-frame-sequence.ps1` resume esas
  secuencias en JSON y una hoja de contacto, con criterios `-ExpectAnimated` o
  `-ExpectStatic`, para validar movimiento sin revisar muchas imagenes a mano. La
  demo 101 ya usa `-ExpectAnimated` como prueba temporal real.
- `tools/test-regression.ps1` detecta `analyze-sequence.ps1` dentro de una demo y
  añade una columna `Sequence` al informe. `101_ehb_tile_scroll_driver` incluye
  esa prueba para que el scroll animado quede cubierto por la regresion completa.
- El roadmap incorpora una seccion de abstracciones futuras: `RenderScene`
  retenido, `FramePlan`, `CopperScheduler`, `BlitterQueue`, `SpriteAllocator`,
  `DmaBudget`, drivers `RoadRaster`, `SpriteBackdrop`, `CopperHeavy` y efectos
  demoscene reutilizables.
- `101_ehb_tile_scroll_driver` pasa de scroll horizontal a prefetch bidireccional:
  `EhbTileScrollScene` reserva ahora una superficie 480x416, el Copper desplaza
  punteros por X/Y, y la demo valida una ruta visible derecha/izquierda/arriba/
  abajo mas orbita de cuatro tiles. El prefetch de esta demo se hace en franjas
  lineales fuera del viewport visible para no mostrar tiles reciclados a mitad de
  pantalla; el anillo modulo queda como contrato para un futuro wrap fisico real.
  La prueba local deja `runStatus.detail = 0x11055823`: camara X/Y dentro del
  margen, dos tiles ejecutados en el frame y flags `0x3` de columnas+filas.
- Se crea el subproyecto `FrameScope` para analisis visual temporal generico:
  `tools/framescope/frame-scope.ps1` acepta carpetas de frames o videos locales
  mediante `ffmpeg`, genera `framescope-report.json`, `framescope-summary.md` y
  `framescope-contact-sheet.png`, estima diferencias, direccion de movimiento,
  segmentos y grids ASCII compactos. Su roadmap propio queda en
  `docs/FRAMESCOPE_ROADMAP.md`. Se ha verificado sobre la secuencia WinUAE de la
  demo 101 y sobre un MP4 generado desde esos frames.
- `tools/run/run-demo.mjs` guarda ahora `runStatus` por cada frame de secuencia
  cuando el canal lateral esta disponible. FrameScope incorpora `-Profile
  amiga-scroll`, que decodifica esa telemetria, compara deltas de camara con la
  direccion visual observada y puede fallar con `-RequireProfileMatch`.
- La demo `101_ehb_tile_scroll_driver` ya supera `FrameScope -Profile amiga-scroll
  -RequireProfileMatch` dentro de `analyze-sequence.ps1`. Se corrigio el fine
  scroll horizontal usando coarse X redondeado hacia arriba + `BPLCON1 = 16 -
  fine`, se aumento el mapa procedimental a 64 patrones para evitar aliases
  visuales y FrameScope recorta automaticamente el viewport activo de WinUAE en el
  perfil Amiga. La prueba fuerte captura 12 frames cada 120 ms con rejilla 64x48,
  `SearchRadius 12` y correlaciona cada captura con `runStatus` lateral. FrameScope
  guarda direcciones candidatas cercanas al mejor desplazamiento para explicar
  empates visuales de tilemaps repetitivos.
- Se ha definido `Vision Review` en `docs/VISION_REVIEW_ROADMAP.md` como una capa
  ligera de inspeccion con IA visual. No analizara videos completos; preparara
  paquetes pequenos de 4-6 frames relevantes y preguntara a un modelo local/remoto
  mediante prompts concretos. Ya existen prompts iniciales para transiciones de
  scroll Amiga, diferencias genericas y animacion de sprites, mas ejemplos de
  proveedor LM Studio/OpenAI-compatible en `tools/vision-review`.

Ultimo informe de regresion conocido:

```text
out\regression\20260531-165301\regression-report.md
```

## Siguiente paso previsto

La regresion automatizada ya existe en `tools/test-regression.ps1`.

Siguiente bloque recomendado:

1. Convertir el presupuesto de Blitter en warnings/criterios de aceptacion por
   frame.
2. Añadir clipping de Blitter para bordes de pantalla/camara.
3. Empezar una demo de scroll/tilemap con zona no visible real, dirty bits por
   doble buffer, scroll fino por `BPLCON1` y commit sincronizado con VBlank.
4. Añadir doble buffer de copperlist cuando un frame necesite cambiar estructura,
   no solo valores de `COLORxx`.
