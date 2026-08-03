# Contexto para continuar desde cero

Si una IA abre este proyecto sin historial de conversacion, debe empezar leyendo:

1. `docs/README.md` (índice maestro del árbol documental)
2. `docs/methodology/DEVELOPMENT_LOG.md`
3. `docs/architecture/ROADMAP_ENGINE_CPP_AMIGA500.md`
4. `docs/build/BUILD_AND_RUN.md`
5. `docs/architecture/CODING_STYLE.md`
6. `docs/architecture/HARDWARE_AND_ROM_KERNEL_POLICY.md`
7. `docs/emulation/MOUSE_AUTOMATION.md`
8. `docs/emulation/WINUAE_SIDE_CHANNEL_DEBUG.md`
9. `docs/demoscene/DEMOSCENE_REPO_INDEX.md`
10. `docs/demoscene/DEMOSCENE_EFFECT_REPLICATION_POLICY.md`
11. `demos/000_toolchain_cpp23/README.md`

## Objetivo inmediato

Mantener una base de engine C++23 verificable para Amiga 500. Cada cambio debe poder
probarse con:

```powershell
.\tools\test-regression.ps1
```

## Restricciones importantes

- No romper el proyecto C histórico de `legacy/`.
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
- El runner debe lanzar WinUAE con `warp=false` por defecto. El engine puede estar
  correctamente sincronizado a VBlank y aun asi parecer acelerado si el emulador
  corre en warp. Usar `-Warp` solo para diagnostico/throughput explicito.
- Para uso humano, preferir `tools\run\demo-menu.ps1`: permite elegir demo,
  compilar, lanzar a ritmo real, analizar, capturar secuencias o dejar WinUAE
  abierto para depuracion. Por defecto tampoco usa warp.
- Para ciclos internos rapidos de IA se permite `tools\test-regression.ps1 -Warp`;
  esto debe interpretarse como prueba de funcionalidad, no como validacion visual
  de suavidad.
- Durante pruebas automatizadas, WinUAE no debe capturar ni encerrar el raton de
  Windows. El runner fuerza `win32.absolute_mouse=yes` y las pruebas deben mover
  el raton emulado con `tools\input\mouse-path.ps1`.
- El runner no debe depender de esperas largas para demos con `g_amg_run_status`.
  `tools\run\run-demo.*` falla si no hay `side-channel READY` en unos segundos;
  el fallback largo solo existe con `--allow-timeout-fallback` para diagnosticos
  manuales. Con `warp=false`, el timeout lateral por defecto es 10000 ms para dar
  margen al arranque realista de AmigaDOS/WinUAE sin esconder cuelgues de demo.
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

La demo `101_ehb_tile_scroll_driver` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- alcanzar `side-channel READY`;
- mostrar la escena EHB con superficie de 480x416 y ventana visible de 320x256;
- reservar diez columnas y diez filas ocultas para predibujar tiles sueltos
  antes de que sean visibles;
- usar `EhbTileScrollScene` para programar punteros de bitplane y `BPLCON1`;
- animar una ruta visible: derecha dos tiles, izquierda, arriba dos tiles, abajo y
  despues una orbita de cuatro tiles de radio, con commit sincronizado a VBlank;
- ejecutar el prefetch solo sobre franjas lineales fuera del viewport visible,
  dejando el anillo modulo para un futuro driver de wrap fisico real;
- convertir updates progresivos de tiles offscreen en `TileBlockCopy` reales, con
  presupuesto pequeno por frame;
- superar `demos\101_ehb_tile_scroll_driver\analyze-screenshot.ps1`;
- dejar en `g_amg_run_status.detail` la marca `0x11......`, con camara X/Y y
  trabajos de tile actualizados mientras corre. El nibble bajo publica flags de
  prefetch: `0x1` columnas recicladas y `0x2` filas recicladas; el estado valido
  debe tener ambos bits.

Las animaciones y scrolls deben validarse con secuencias cuando una captura unica
no demuestre suficiente comportamiento temporal:

```powershell
.\tools\run\run-demo.ps1 demos\101_ehb_tile_scroll_driver -SequenceFrames 4 -SequenceIntervalMs 80
.\tools\analyze\analyze-frame-sequence.ps1 out\run\101_ehb_tile_scroll_driver\sequence -ExpectAnimated
```

Para demos animadas se usara `-ExpectAnimated`; la herramienta genera
`sequence-analysis.json` y `contact-sheet.png`, reduciendo el coste de revisar
frames visualmente.

La regresion ejecuta automaticamente esa comprobacion cuando una demo tiene un
`analyze-sequence.ps1`. La 101 ya lo usa, por lo que el informe debe mostrar
`Sequence = ok` ademas de Build/Run/Analyze.

Si mas adelante se quiere una comprobacion semantica de mayor nivel, LM Studio
puede actuar como segundo observador local sobre la hoja de contacto: primero se
fallara o aprobara por metricas baratas, y solo cuando haga falta se enviara una
imagen resumida al modelo de vision local.

FrameScope es el nuevo subproyecto generico para diagnostico temporal:

```powershell
.\tools\framescope\frame-scope.ps1 `
  -Source .\out\run\101_ehb_tile_scroll_driver\sequence `
  -OutDir .\out\framescope\101_latest `
  -ExpectAnimated
```

Acepta carpetas de frames o videos locales si `ffmpeg` esta en `PATH`. Genera
`framescope-report.json`, `framescope-summary.md` y `framescope-contact-sheet.png`.
El roadmap del subproyecto esta en `docs\FRAMESCOPE_ROADMAP.md`. El perfil
`amiga-scroll` compara telemetria de `run-report.json` con movimiento observado,
recorta automaticamente el viewport activo para ignorar los bordes negros de
WinUAE y puede fallar con `-RequireProfileMatch`.

La demo 101 ya usa esa prueba fuerte desde:

```powershell
.\demos\101_ehb_tile_scroll_driver\analyze-sequence.ps1 -Warp
```

La validacion actual captura 12 frames cada 120 ms, usa rejilla 64x48 y
`SearchRadius 12`.
Esto fue necesario porque la rejilla inicial 32x24 detectaba animacion pero
confundia direcciones durante la orbita. El driver EHB usa ahora coarse X
redondeado hacia arriba + `BPLCON1 = 16 - fine` para eliminar el diente de sierra
horizontal, y la demo genera 64 patrones de tile para que FrameScope tenga marcas
visuales no periodicas. FrameScope conserva direcciones candidatas casi empatadas
para que la validacion pueda explicar casos ambiguos en vez de ocultarlos.

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

Vision Review queda definido como capa ligera de inspeccion con IA visual:

```text
docs\VISION_REVIEW_ROADMAP.md
tools\vision-review\
```

Su objetivo no es analizar videos completos, sino seleccionar 4-6 frames
relevantes y preguntarle a un modelo con vision si una hipotesis concreta se ve
correcta. El primer perfil sera `amiga-scroll-transition`: revisar frames alrededor
de un cruce de 16 pixels/coarse scroll para detectar salto, tile-pop, tearing,
cambio de paleta inesperado o corrupcion planar. El proveedor previsto inicialmente
es LM Studio mediante endpoint OpenAI-compatible; faltan los parametros locales del
usuario antes de implementar la llamada real.

La Fase 1 offline ya existe:

```powershell
.\tools\vision-review\vision-review.ps1 `
  -Source .\out\run\101_ehb_tile_scroll_driver\sequence `
  -RunReport .\out\run\101_ehb_tile_scroll_driver\run-report.json `
  -FrameScopeReport .\out\framescope\101_ehb_tile_scroll_driver\framescope-report.json `
  -Profile amiga-scroll-transition `
  -OutDir .\out\vision-review\101_auto
```

Genera `request.json`, `request.md`, copias de frames y `contact-sheet.png`.
La prueba actual eligio frames `0,1,2,3` por `coarse-x-change-5-to-6`. El siguiente
paso era Fase 2.

La Fase 2 OpenAI-compatible ya esta implementada para LM Studio:

```powershell
.\tools\vision-review\vision-review.ps1 `
  -Source .\out\run\101_ehb_tile_scroll_driver\sequence `
  -RunReport .\out\run\101_ehb_tile_scroll_driver\run-report.json `
  -FrameScopeReport .\out\framescope\101_ehb_tile_scroll_driver\framescope-report.json `
  -Profile amiga-scroll-transition `
  -Provider .\tools\vision-review\providers\lmstudio.legion.json `
  -SendMode multi-image `
  -OutDir .\out\vision-review\101_lmstudio_multi
```

LM Studio responde en `http://legion:1234/` con modelo
`qwen2.5-vl-7b-instruct`. El modo `multi-image` acepta varias imagenes y detecto
un defecto sintetico de tile-pop. Tras ajustar el prompt para frames muestreados,
`out\vision-review\101_lmstudio_multi_v2` valida la demo limpia con `status=ok`, y
`out\vision-review\synthetic_tile_pop_multi_v2` detecta el defecto con
`visibleTilePop=true`. El modo `contact-sheet` responde rapido y sirve de fallback,
pero no detecto ese defecto sintetico, probablemente por perdida de detalle al
reducir la hoja.

La demo 101 puede usar Vision Review opcionalmente:

```powershell
.\demos\101_ehb_tile_scroll_driver\analyze-sequence.ps1 -Warp -RequireVisionReviewOk
```

La regresion tambien acepta `-VisionReview` y `-RequireVisionReviewOk`; sin esos
flags no llama a LM Studio. FrameScope permite ahora un mismatch heuristico en la
demo 101 porque se observo un falso negativo intermitente en el primer par de
frames; cuando se requiere vision, la validacion semantica estricta queda a cargo
del VLM.

La demo 101 ahora usa tiles simbolicos con borde, marcador de variante y glifo
hexadecimal `0..F`. Esto se hizo para que Vision Review pueda describir fallos con
mas precision. Hubo una version pixel-a-pixel demasiado lenta para debug; la
version actual compone filas mediante mascaras y vuelve a alcanzar `READY` en el
timeout normal. Evidencias recientes:

- Se detecto y corrigio un bug de sincronizacion: instalar la copperlist con
  `COPJMP1` desde `update`/`render` antes de VBlank reiniciaba el Copper a media
  pantalla, generando artefactos en la mitad inferior. El bucle del engine queda
  en `update -> wait_vblank -> render`; la demo recompila en `update` e instala en
  `render` ya dentro de VBlank.
- `tools\run\run-demo.mjs` limpia `out\run\<demo>\sequence` antes de capturar para
  no mezclar frames viejos con una ejecucion nueva.
- `tools\analyze\assert-no-inner-black.ps1` valida que los tiles simbolicos de 101
  no contienen negro interno en la ventana visible. Este detector atrapa el fallo
  de reinicio de Copper que FrameScope/Vision Review podian pasar por alto.
- `analyze-sequence.ps1 -Warp`: ok con 12 frames, FrameScope y detector de negro
  interno.
- `tools/test-regression.ps1 -Demo demos\101_ehb_tile_scroll_driver -Warp`: ok,
  informe `out\regression\20260601-011220\regression-report.md`.
- Defecto sintetico sobre tiles simbolicos:
  `out\vision-review\synthetic_symbol_tiles_multi_prompt2` falla con
  `visibleTilePop=true`.

Correccion posterior del salto de columna izquierda:

- La formula inicial de fine scroll arreglaba la mitad inferior, pero todavia
  dejaba que la columna izquierda apareciese de golpe al cruzar `fine15 -> fine0`.
- Se comparo con `demoscene-repo\effects\tiles16`, pero la pieza que faltaba en
  nuestro driver era el fetch extra: solo cambiar `BPLCON1` y puntero no bastaba.
- `EhbTileScrollScene` usa ahora `DDFSTRT=$30`, `display_modulo=18` porque se
  leen 42 bytes por linea, puntero un word antes de la parte coarse y
  `BPLCON1=fine`.
- La ruta de la demo se centro en `cameraX=80` con radio de 4 tiles para conservar
  siempre un word valido a la izquierda durante la orbita.
- `tools\run\run-demo.ps1/mjs` soporta `-SequenceCameraX`, que captura frames
  cuando la telemetria lateral alcanza valores concretos.
- `demos\101_ehb_tile_scroll_driver\analyze-fine-scroll.ps1` captura y valida:
  `cameraX=94,95,96,97` con shifts `-2,-2,-2`, y
  `cameraX=112,111,110,109` con shifts `+2,+2,+2`.
- La ultima regresion correcta es
  `out\regression\20260601-013329\regression-report.md`.
