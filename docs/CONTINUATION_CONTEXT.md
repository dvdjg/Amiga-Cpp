# Contexto para continuar desde cero

Si una IA abre este proyecto sin historial de conversacion, debe empezar leyendo:

1. `docs/DEVELOPMENT_LOG.md`
2. `docs/ROADMAP_ENGINE_CPP_AMIGA500.md`
3. `docs/BUILD_AND_RUN.md`
4. `docs/CODING_STYLE.md`
5. `docs/HARDWARE_AND_ROM_KERNEL_POLICY.md`
6. `docs/MOUSE_AUTOMATION.md`
7. `docs/WINUAE_SIDE_CHANNEL_DEBUG.md`
8. `docs/DEMOSCENE_REPO_INDEX.md`
9. `docs/DEMOSCENE_EFFECT_REPLICATION_POLICY.md`
10. `demos/000_toolchain_cpp23/README.md`

## Objetivo inmediato

Mantener una base de engine C++23 verificable para Amiga 500. Cada cambio debe poder
probarse con:

```powershell
.\tools\test-regression.ps1
```

## Restricciones importantes

- No romper el proyecto C historico de la raiz.
- No asumir libc/STL hosted completa.
- No usar asignacion dinamica durante gameplay salvo pruebas controladas.
- No depender de Amiga en la logica de juego de alto nivel.
- Conservar evidencias de ejecucion en `out\run` y `out\regression`.
- Comentar cada unidad de codigo como tutorial, sobre todo cabeceras compartidas.
- Usar el Hardware Reference Manual local como referencia para registros y timing.
- Mantener el uso del ROM kernel como politica opcional de backend, no como detalle
  mezclado en la logica de juego.
- Usar `C:\Users\David\Documents\Programa\Amiga\demoscene-repo` como repositorio
  externo de referencia tecnica. Consultar `docs\DEMOSCENE_REPO_INDEX.md` antes de
  reanalizarlo desde cero.
- Al replicar efectos de `demoscene-repo\effects`, seguir
  `docs\DEMOSCENE_EFFECT_REPLICATION_POLICY.md`: no portar linea a linea, sino
  reconstruir el efecto con APIs limpias del engine y pruebas automatizadas.
- Las demos deben publicar cambios visibles sincronizados con VBlank cuando cambien
  punteros de bitplane, copperlists, scroll fino o buffers activos.
- Durante pruebas automatizadas, WinUAE no debe capturar ni encerrar el raton de
  Windows. El runner fuerza `win32.absolute_mouse=yes` y las pruebas deben mover
  el raton emulado con `tools\input\mouse-path.ps1`.
- Las demos deben exponer `g_amg_run_status` y llegar a `Ready` por el canal
  lateral de WinUAE-DBG antes de la captura. El canal escucha en `127.0.0.1:2346`
  y el runner lo usa sin detener el 68000.
- La colaboracion profunda persona+IA sobre la misma instancia viva de WinUAE ya
  tiene canal lateral con `observe/assist/takeover`, debug lock y acciones seguras
  encoladas para `screenshot`, `input` y `profile`. Tambien existen `poke`,
  `rollback`, `audit`, `pause` y `resume` bajo lock `takeover`. Sigue pendiente la
  zona scratch para diagnostico 68k y carga controlada de rutinas temporales. Ver
  `docs\WINUAE_SIDE_CHANNEL_DEBUG.md`.

## Estado minimo saludable

La demo `000_toolchain_cpp23` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- alcanzar `side-channel READY`;
- generar captura PNG;
- superar `analyze-demo.ps1`;
- mostrar `Memory arenas: OK` en el overlay.

La demo `010_chip_slow_memory` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- alcanzar `side-channel READY`;
- mostrar `Arena checks: OK`;
- mostrar barras para Chip, Slow y Frame;
- mostrar una base Chip en rango bajo y una base Slow en zona trapdoor/bogo cuando
  el perfil emulado expone memoria no-chip.

La demo `020_copper_basic` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- alcanzar `side-channel READY`;
- tomar el display a pantalla completa;
- mostrar bandas horizontales roja, verde, azul, amarilla y cian;
- superar su analizador especifico `demos\020_copper_basic\analyze-screenshot.ps1`.

La demo `030_ehb_palette_zones` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- alcanzar `side-channel READY`;
- mostrar una reticula EHB con tres zonas verticales de paleta;
- incluir muestras visibles de colores normales 0..31 y half-brite 32..63;
- superar su analizador especifico `demos\030_ehb_palette_zones\analyze-screenshot.ps1`.
- usar `StaticEhbScene` desde `engine\include\amg\graphics\drivers\ehb_scene.hpp`,
  de modo que la demo no programe registros BPL/DIW/DDF/COLOR directamente.
- construir su copperlist mediante `CopperScheduler` desde
  `engine\include\amg\graphics\copper\scheduler.hpp`, dejando disponible
  `ScheduleReport` para presupuestos y diagnostico.

La demo `040_palette_cycle_effect` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- alcanzar `side-channel READY`;
- mostrar bandas EHB cuya paleta rota sin redibujar bitplanes;
- mantener una zona inferior Copper fija;
- aplicar el ciclo mediante `FramePlan` y parches de paleta, no reconstruyendo toda
  la copperlist cada frame;
- superar `demos\040_palette_cycle_effect\analyze-screenshot.ps1`;
- dejar en `g_amg_run_status.detail` una marca `0x04xxxxxx` con fase distinta de
  cero para demostrar que el ciclo ya avanzo antes de la captura.

La demo `050_blitter_bobs` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- alcanzar `side-channel READY`;
- mostrar un BOB cookie-cut amarillo/blanco y dos blobs no-save naranja/magenta
  sobre fondo EHB azul;
- crear los trabajos como `BlitJob` dentro de `FramePlan`;
- materializarlo desde `MinimalBackend::execute_frame_plan()` usando Blitter;
- mover el BOB en pasos de 16 pixels usando save/restore real: restore anterior,
  save nuevo fondo y draw cookie-cut;
- superar `demos\050_blitter_bobs\analyze-screenshot.ps1`;
- dejar en `g_amg_run_status.detail` una marca `0x05nnjjrm`, donde `nn` son jobs
  no-save estaticos, `jj` jobs de Blitter del frame animado, `r` son dirty rects
  fusionados y `m` son fusiones realizadas. El estado saludable actual es
  `0x05020311`.

La demo `051_blitter_shifted_bobs` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- alcanzar `side-channel READY`;
- mostrar un BOB cookie-cut amarillo/blanco/cian sobre fondo EHB azul;
- usar `BlitJob::source_shift` para dibujar en X no alineada a 16 pixels;
- superar `demos\051_blitter_shifted_bobs\analyze-screenshot.ps1`;
- dejar en `g_amg_run_status.detail` el estado saludable actual `0x05190301`.

La demo `052_tile_staging_blits` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- alcanzar `side-channel READY`;
- componer un bloque 4x4 de tiles 16x16 en un buffer Chip RAM no visible usando
  `TileBlockCopy`;
- usar `engine\include\amg\graphics\tilemap\tile_scroll.hpp` para validar el modelo
  retenido de dirty tiles por buffer antes de lanzar los blits;
- usar `engine\include\amg\scene\virtual_scene.hpp` para atravesar una escena
  virtual retenida con `Camera2D` y `TileLayer`;
- publicar ese bloque al playfield EHB visible con `CopyRect`;
- superar `demos\052_tile_staging_blits\analyze-screenshot.ps1`;
- dejar en `g_amg_run_status.detail` el estado saludable actual `0x05210104`.

La demo `100_virtual_tile_scene_scroll` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- alcanzar `side-channel READY` sin caer en fallback largo;
- mostrar un escenario virtual EHB atractivo: cielo, montanas, jungla, ruinas y
  agua/subsuelo;
- usar `VirtualScene`, `Camera2D`, `TileLayer` y `TileMap16`;
- usar una camara con posicion X no alineada a tile para que `fine_x` sea distinto
  de cero;
- usar `ProgressiveTileScheduler` para encolar tiles offscreen y aceptar un
  presupuesto de 4 updates antes de que sean visibles;
- superar `demos\100_virtual_tile_scene_scroll\analyze-screenshot.ps1`;
- dejar en `g_amg_run_status.detail` el estado saludable actual `0x10390941`.

El contrato del canal lateral seguro debe pasar con:

```powershell
node .\tools\debug\verify-side-channel-contract.mjs --settle-ms 9000
```

Esta prueba debe producir `side-channel-shot.png` y `side-channel-profile.bin` en
`out\run\030_ehb_palette_zones` sin romper la conexion GDB del runner.

La convivencia de depuracion normal GDB con canal lateral debe pasar con:

```powershell
.\tools\debug\verify-gdb-step-side-channel.ps1 -Steps 3
```

Esta prueba pone un breakpoint GDB en `amg_debug_ready_probe`, continua, se para
en `T05swbreak`, avanza paso a paso por instrucciones y mantiene lecturas
laterales `state`/`regs` durante la ejecucion y en cada parada.

El primer takeover reversible debe pasar con:

```powershell
.\tools\debug\verify-side-channel-takeover.ps1
```

Esta prueba toma lock `takeover`, escribe temporalmente cuatro bytes en
`g_amg_run_status.detail`, verifica el cambio, consulta la auditoria y hace
rollback al valor original.

La pausa/reanudacion lateral debe pasar con:

```powershell
.\tools\debug\verify-side-channel-pause-resume.ps1
```

Esta prueba toma lock `takeover`, pausa la emulacion, lee memoria mientras esta
detenida, reanuda y deja que el runner complete su captura final.
