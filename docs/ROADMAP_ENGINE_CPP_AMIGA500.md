# Roadmap del engine C++ para Amiga 500

Este documento define una hoja de ruta incremental para construir un engine C++ moderno
para Amiga 500, empezando por escenas EHB ricas y extendiendo despues el soporte a
otros drivers graficos: 5 bitplanes, fake-DPF, Dual Playfield, fondos por sprites y
escenas copper-heavy.

La filosofia del proyecto es simple: cada fase debe producir una demo verificable,
un conjunto de pruebas automatizables y documentacion suficiente para que una IA o
una persona pueda reproducir el resultado sin conocimiento implicito.

## 1. Objetivos tecnicos

- Usar C++23 en modo `gnu++23` con disciplina freestanding.
- Soportar Amiga 500 OCS PAL con 1 MB total en configuracion `A500_1MB_Slow`.
- Tratar la Slow RAM como memoria de capacidad, no como Fast RAM.
- Mantener todos los recursos DMA en Chip RAM: bitplanes, copperlists, sprites,
  audio y buffers del blitter.
- Construir sobre ACE como backend inicial de bajo nivel, sin acoplar el engine a
  decisiones internas de ACE.
- Integrar UAF como formato de autoria y exportar un formato runtime cocinado para
  Amiga.
- Priorizar escenas tipo aventura grafica EHB, sin cerrar la puerta a juegos de
  plataformas, shooters y demos tecnicas.

## 2. Organizacion propuesta del repositorio

```text
engine/
  include/
  src/
  asm/
  c/
  cpp/
  platform/
    amiga_ace/
    amiga_registers/
  memory/
  graphics/
    drivers/
      ehb_scene/
      standard5/
      standard4/
      fake_dpf/
      dual_playfield/
      sprite_backdrop/
      copper_heavy/
    copper/
    blitter/
    sprites/
    tiles/
  scene/
  scripting/
  audio/
  debug/

demos/
  000_toolchain_cpp23/
  010_chip_slow_memory/
  020_copper_basic/
  030_ehb_palette_zones/
  040_palette_cycle_effect/
  050_blitter_bobs/
  060_hardware_sprites/
  070_tile_scroll/
  080_uaf_runtime_loader/
  100_mvp_ehb_room/
  110_mvp_fake_dpf_platformer/
  120_mvp_dual_playfield_scroll/

tools/
  build/
  run/
  capture/
  analyze/
  uaf_export/

docs/
  ROADMAP_ENGINE_CPP_AMIGA500.md
  BUILD_AND_RUN.md
  CODING_STYLE.md
  MEMORY_MODEL.md
  GRAPHICS_DRIVERS.md
  TESTING_AND_PROFILING.md
  WINUAE_SIDE_CHANNEL_DEBUG.md
```

## 3. Reglas globales de verificacion

Cada demo debe poder compilarse y ejecutarse con un comando unico, por ejemplo:

```powershell
.\tools\build\build-demo.ps1 demos\030_ehb_palette_zones
.\tools\run\run-demo.ps1 demos\030_ehb_palette_zones
.\tools\capture\capture-demo.ps1 demos\030_ehb_palette_zones
.\tools\analyze\analyze-demo.ps1 demos\030_ehb_palette_zones
```

La IA no debe dar una fase por terminada si no ha comprobado:

- build limpio del binario Amiga;
- arranque correcto en WinUAE-DBG;
- captura de pantalla o video cuando haya salida visual;
- analisis automatico de la captura cuando aplique;
- logs de profiler o contadores cuando haya requisitos de rendimiento;
- ausencia de regresiones en demos anteriores;
- README actualizado de la demo;
- notas tecnicas en codigo solo donde aclaren restricciones reales del hardware.

## 4. Fase 0: base de toolchain y debug

Objetivo: fijar una base reproducible de compilacion, ejecucion y depuracion.

Entregables:

- Demo `000_toolchain_cpp23`.
- Programa C++23 minimo con `constexpr`, `consteval`, templates y llamada a codigo C.
- Flags recomendados: `-std=gnu++23`, `-fno-exceptions`, `-fno-rtti`,
  `-fno-threadsafe-statics`, `-fno-use-cxa-atexit`.
- Script comun de build/run/capture.
- Documento `BUILD_AND_RUN.md`.

Pruebas:

- El compilador acepta `c++23` y `gnu++23`.
- El binario arranca en A500 OCS PAL.
- El depurador permite breakpoint en C/C++, pausa y stepping por instruccion.
- La IA captura una pantalla conocida y verifica un color o patron de pixeles.

Criterio de aceptacion:

- Un comando reconstruye y ejecuta la demo desde cero.
- La sesion de debug normal sigue funcionando.

## 5. Fase 1: modelo de memoria A500_1MB_Slow

Objetivo: tener control explicito de Chip RAM, Slow RAM y memoria temporal.

Entregables:

- Demo `010_chip_slow_memory`.
- `ChipArena`, `SlowArena`, `FrameScratch` y pools fijos.
- Reporte en pantalla y por log de uso de memoria.
- Documento `MEMORY_MODEL.md`.

Pruebas:

- Reservas alineadas a word/longword.
- Fallos controlados si se agota Chip RAM.
- Verificacion de que bitplanes, copperlists y sprites se ubican en Chip RAM.
- Verificacion de que datos no DMA pueden vivir en Slow RAM.

Criterio de aceptacion:

- La demo muestra memoria libre/usada y no hace asignaciones dinamicas durante el frame.

## 6. Fase 2: backend grafico minimo y frame scheduler

Objetivo: establecer el ciclo PAL y las fases del engine.

Entregables:

- Demo `020_copper_basic`.
- Frame scheduler con fases: input, update, prepare, blit, copper commit, sprite commit,
  wait VBlank y swap.
- Contadores de frame, VBlank y tiempo estimado.
- Copperlist minima con color de fondo cambiante.

Pruebas:

- Captura con bandas de color esperadas.
- Profiler confirma 50 Hz estable en escena vacia.
- El cambio de color ocurre en la linea raster esperada con tolerancia documentada.

Criterio de aceptacion:

- El engine tiene una API estable para registrar trabajos por fase.

## 7. Fase 3: driver `EHBScene`

Objetivo: primer driver real, orientado a aventura grafica con fondos ricos.

Entregables:

- Demo `030_ehb_palette_zones`.
- Demo `040_palette_cycle_effect`.
- Inicializacion de 6 bitplanes EHB.
- Fondo planar EHB precocinado.
- Zonas copper con cambios parciales de paleta.
- Color cycling barato para agua, fuego o luces.
- `CopperTimeline` minimo para medir coste por linea visible.
- `PaletteCycleEffect` reutilizable sin redibujar bitplanes.
- Documento del driver en `GRAPHICS_DRIVERS.md`.

Pruebas:

- Captura comparada contra imagen de referencia.
- Verificacion de que los 32 colores base y los 32 half-brite se representan.
- Verificacion de que un ciclo de paleta avanza varias fases antes de `READY`.
- Analisis de H-BLANK para confirmar que los cambios copper por linea caben.
- Warning automatico si se solicita un cambio de paleta completo en una linea visible.

Criterio de aceptacion:

- Una escena EHB estatica se ve igual que la referencia dentro de tolerancias RGB444.
- Los cambios de paleta no generan corrupcion visible.
- Las zonas o efectos que excedan el presupuesto conservador quedan marcados en
  `ScheduleReport`/`TimelineReport`.

## 8. Fase 4: blitter, BOBs y dirty rects

Objetivo: mover personajes y objetos sobre fondos EHB sin redibujar toda la escena.

Entregables:

- Demo `050_blitter_bobs`.
- BOB con mascara cookie-cut.
- `FramePlan` con trabajos de Blitter.
- Backend Amiga capaz de materializar un BOB planar enmascarado con el Blitter.
- Save/restore de fondo.
- Dirty rects y orden de restauracion/dibujo.
- Presupuesto por BOB en funcion de ancho, alto y bitplanes.

Pruebas:

- Captura de personaje en varias posiciones.
- Analisis de diferencia de frames para detectar basura visual.
- Profiler de blits por frame.
- Prueba de saturacion: varios BOBs hasta emitir warning.

Criterio de aceptacion:

- Un personaje animado camina sobre una escena EHB sin dejar rastros.
- El frame publica dirty rects fusionados y la prueba automatica valida al menos
  una fusion real de region anterior/nueva.
- El motor sabe rechazar o advertir composiciones que excedan el presupuesto.
- El MVP inicial debe mostrar al menos un BOB cookie-cut sobre EHB usando
  `FramePlan -> backend -> Blitter`, sin registros custom en la demo.

## 9. Fase 5: hardware sprites y `VirtualSprite`

Objetivo: abstraer sprites hardware, attached sprites y fallback a BOB.

Entregables:

- Demo `060_hardware_sprites`.
- Asignador de 8 canales de sprite.
- Attached pairs para 15 colores.
- Cursor hardware.
- `VirtualSprite` con decision hardware/BOB.

Pruebas:

- Captura que valide posiciones y prioridades.
- Verificacion de canales reservados.
- Test de fallback: si no quedan canales, el objeto pasa a BOB.
- Prueba de attached sprites con paleta correcta.

Criterio de aceptacion:

- El engine puede mantener cursor hardware y al menos un actor BOB sin conflictos.

## 10. Fase 6: loader runtime UAF-R

Objetivo: cargar escenas cocinadas desde UAF sin parsing pesado en Amiga.

Entregables:

- Demo `080_uaf_runtime_loader`.
- Especificacion inicial UAF-R.
- Chunks: header, palettes, bitplanes, copper templates, patch tables, sprites,
  BOBs, tile metadata, collision, strings.
- Conversor desde UAF authoring a UAF-R.

Pruebas:

- Roundtrip de una escena simple.
- Hash de chunks binarios.
- Carga en emulador y comparacion visual contra preview/export de UAF.
- Validacion de offsets, alineacion y ubicacion Chip/Slow RAM.

Criterio de aceptacion:

- Una escena creada/cocinada fuera del engine se carga y renderiza fielmente.

## 11. MVP 1: habitacion EHB de aventura grafica

Objetivo: primera demo jugable tipo aventura moderna.

Entregables:

- Demo `100_mvp_ehb_room`.
- Habitacion EHB con fondo rico.
- Zonas copper para cielo/interior/agua/luz.
- Personaje animado con BOB.
- Cursor hardware.
- Walkboxes, profundidad por Y y hotspots.
- Dialogo o texto basico.

Pruebas:

- Capturas golden de la habitacion en reposo.
- Capturas con personaje delante/detras de zonas de profundidad.
- Script de input automatizado: mover cursor, caminar, activar hotspot.
- Profiler: 50 Hz estable con una carga representativa.
- Analisis de Chip RAM disponible.

Criterio de aceptacion:

- La demo funciona como una micro-aventura de una pantalla y sirve como plantilla real.

## 12. Fase 7: tiles y scroll

Objetivo: preparar el motor para plataformas y escenas con camara.

Entregables:

- Demo `070_tile_scroll`.
- Tilemap 16x16.
- Scroll horizontal fino por `BPLCON1`.
- Scroll coarse por punteros.
- Recompuesto incremental de columnas/filas con Blitter.
- Collision metadata por tile.

Pruebas:

- Captura/video de scroll sin saltos.
- Verificacion de que solo se redibujan margenes necesarios.
- Profiler de tiles recompuestos por frame.
- Prueba de camara con ida/vuelta y ring buffer.

Criterio de aceptacion:

- Scroll suave en A500 sin redibujar pantalla completa por CPU.

## 13. Fase 8: drivers `Standard5`, `Standard4` y `FakeDPF`

Objetivo: soportar modos de accion con menor presion DMA que EHB.

Entregables:

- Demo `110_mvp_fake_dpf_platformer`.
- Driver `Standard5`.
- Driver `Standard4`.
- Driver `FakeDPF`: por ejemplo 4 planos de juego + 1 plano de fondo/marca/sombra.
- Comparador de presupuesto entre drivers.

Pruebas:

- Misma escena exportada en varios drivers.
- Capturas comparativas.
- Profiler de CPU libre estimada.
- Verificacion de paletas y prioridades.

Criterio de aceptacion:

- El engine permite elegir driver segun necesidades artisticas y rendimiento.

## 14. Fase 9: Dual Playfield real

Objetivo: soportar parallax real y composiciones tipo Mega Typhoon/Jim Power.

Entregables:

- Demo `120_mvp_dual_playfield_scroll`.
- Driver `DualPlayfield`.
- PF1/PF2 con scroll independiente.
- Prioridad configurable por zona.
- Tiles por playfield.

Pruebas:

- Captura/video con dos playfields desplazandose a velocidades distintas.
- Validacion de asignacion de bitplanes impares/pares.
- Validacion de mapa de paleta PF1/PF2.
- Profiler de DMA con 2+3, 3+3 y variantes.

Criterio de aceptacion:

- Parallax real estable y documentado, con presupuesto comprensible para artistas.

## 15. Fase 10: copper avanzado y fondos por sprites

Objetivo: explotar el caracter propio del Amiga sin romper el presupuesto.

Entregables:

- Driver `SpriteBackdrop`.
- Driver `CopperHeavyScene`.
- Multiplexado vertical controlado.
- Sprite strips y horizontal repeats cuando el presupuesto lo permita.
- Timeline visual/exportable desde UAF.

Pruebas:

- Analisis H-BLANK por linea.
- Deteccion de conflictos de registros copper.
- Capturas de fondos por sprites.
- Stress test que debe emitir warnings antes de fallar visualmente.

Criterio de aceptacion:

- El engine permite efectos avanzados, pero los trata como recursos presupuestados.

## 16. Fase 11: audio, scripting y herramientas de aventura

Objetivo: convertir el MVP visual en base de juego.

Entregables:

- Reproductor MOD/P61 integrado.
- Triggers, scripts simples, inventario, dialogos y estados.
- Sistema de localizacion de textos.
- Herramientas de exportacion desde UAF.

Pruebas:

- Audio estable sin romper el frame.
- Script automatizado de una mini escena completa.
- Captura de texto y UI.
- Validacion de memoria con varias rooms cargables.

Criterio de aceptacion:

- Se puede crear una aventura corta de varias pantallas.

## 17. Fase 12: regresion continua y QA visual

Objetivo: que cada avance preserve lo anterior.

Entregables:

- Bateria `test-regression`.
- Golden screenshots por demo.
- Comparador de imagenes con tolerancia.
- Parser de logs del profiler.
- Informe HTML/Markdown de cada ejecucion.

Pruebas:

- Todas las demos compilan.
- Todas arrancan.
- Todas generan captura.
- Las capturas coinciden con referencia o explican diferencias aprobadas.
- Los presupuestos de DMA/Blitter/Copper no empeoran sin justificacion.

Criterio de aceptacion:

- La IA puede modificar el engine y demostrar que no ha roto demos anteriores.

## 18. Fase futura: canal lateral de depuracion WinUAE-DBG

Objetivo: permitir que la IA ayude sobre la misma instancia viva de WinUAE que
esta usando David desde Cursor/VS Code, sin competir por el socket GDB principal.

Entregables:

- Canal lateral localhost en WinUAE-DBG o proceso companion con acceso interno.
- Protocolo de sesion con modos `observe`, `assist` y `takeover`.
- Debug lock para operaciones que pausan CPU o modifican estado.
- Lectura de registros, memoria, disasm, screenshots, input y profiler.
- Escritura de memoria con auditoria y rollback.
- Zona scratch para diagnostico y carga de codigo maquina 68k en caliente.
- Logs de sesion en `out/debug-sessions`.
- Documento tecnico `WINUAE_SIDE_CHANNEL_DEBUG.md`.

Pruebas:

- VS Code/Cursor mantiene una sesion normal de depuracion.
- La IA se conecta al canal lateral de esa misma instancia.
- La IA lee memoria/registros y captura pantalla sin romper la sesion manual.
- La IA toma debug lock, pausa, inspecciona, reanuda y libera lock.
- La IA escribe y revierte bytes en una zona segura.
- La IA carga una rutina 68k pequena en scratch, la ejecuta y restaura estado.

Criterio de aceptacion:

- Cuando David se quede atascado en una sesion manual, la IA puede entrar,
  diagnosticar con herramientas avanzadas y dejar trazabilidad completa de lo que
  observa o modifica.

## 19. Definicion de terminado

Una fase se considera terminada solo si cumple:

- demo funcional;
- README de demo;
- documentacion tecnica actualizada;
- pruebas automatizadas o semiautomatizadas;
- captura o evidencia profiler archivada;
- criterios de aceptacion superados;
- regresion de fases anteriores ejecutada.

No basta con que "se vea bien" una vez. Debe poder repetirse.

## 20. Abstracciones que no debemos olvidar

Esta seccion es la libreta de diseno del engine. La meta no es escribir demos
aisladas, sino permitir juegos con ambicion visual tipo `Jim Power`, `Lionheart`,
`OutRun Europa`, aventuras EHB modernas y, mas adelante, efectos de demoscene
reutilizables. La logica de juego debe vivir arriba; los coprocesadores deben
quedar abajo, coordinados por sistemas centrales y no por cada entidad.

### 20.1 Modelo de juego portable

- `GameWorld`: estado logico sin dependencia de Amiga.
- `EntityId` y componentes ligeros: transform, sprite/actor, collider, trigger,
  hotspot, script state.
- `SceneGraph2D` retenido: capas, camaras, nodos visibles y orden de composicion.
- `Camera2D`: seguimiento, limites, shake, parallax abstracto.
- `InteractionLayer`: hotspots, walkboxes, zonas de profundidad, dialogos e
  inventario para aventuras.
- `Physics2D` simple: colisiones por tiles, slopes, plataformas, sensores y
  triggers. Debe poder usarse tambien en futuros frontends Mega Drive/Neo Geo.

### 20.2 Backend de plataforma

- `PlatformBackend`: memoria, input, reloj, audio, display y debug.
- `RenderBackend`: adaptador de driver grafico concreto. En Amiga traduce a
  bitplanes/Copper/Blitter/sprites; en otra plataforma traducira a VDP/sprites/tilemaps.
- `MemoryPolicy`: OS-friendly, takeover parcial o takeover completo.
- `AssetRuntime`: carga recursos cocinados sin parsing pesado.
- `DebugPort`: trazas, profiler, screenshots, input automatizado y canal lateral.

### 20.3 Render retained-mode

- `RenderScene`: descripcion estable de lo visible; no dibuja directamente.
- `RenderLayer`: fondo, tilemap, parallax, actores, UI, overlays y efectos.
- `RenderResourceHandle`: fondos, tilesets, palettes, sprites, BOBs, copper
  templates y audio, siempre con ownership claro.
- `RenderCommandBuffer`: comandos de alto nivel generados por el juego.
- `RenderCompiler`: convierte comandos/layers a trabajos concretos del driver.
- `FramePlan`: resultado por frame: blits, sprites asignados, copper patches,
  cambios de paleta, DMA y warnings de presupuesto.
  Ya existe un primer `FramePlan` con parches de paleta para `040_palette_cycle_effect`.

### 20.4 Coprocesadores Amiga como servicios centrales

- `CopperScheduler`: unico dueno de la copperlist final. Ningun recurso debe
  escribir Copper por libre. Mezcla display setup, zonas de paleta, color cycling,
  splits, sprites, waits, modulo/punteros y efectos raster.
- `CopperTimeline`: slots por linea/H-BLANK con coste estimado. Debe rechazar o
  advertir cambios completos de paleta donde no caben.
- `BlitterQueue`: trabajos ordenados por prioridad: clear, copy, cookie-cut,
  mask, restore background, tile column updates, fills y line draws.
- `BlitterBudget`: coste estimado por job y por frame; si se satura, el engine
  degrada o emite warning antes de romper la imagen.
- `SpriteAllocator`: 8 canales hardware, attached pairs, multiplexado vertical,
  prioridades, cursor reservado y fallback a BOB.
- `DmaBudget`: bitplanes, sprites, audio, blitter y copper por modo grafico.

### 20.5 Drivers graficos previstos

- `StaticEhbScene`: ya iniciado. Fondo EHB estatico con zonas Copper.
- `EhbRoomDriver`: aventura EHB retenida con BOBs, cursor, hotspots y color cycling.
- `TileScrollDriver`: tilemaps 16x16, scroll fino/coarse y columnas recompuestas.
- `Standard4/Standard5`: accion con menor presion DMA.
- `FakeDPF`: 4 planos de juego + 1 plano auxiliar para fondo/sombra/marca.
- `DualPlayfield`: playfields reales con scroll independiente y parallax.
- `RoadRasterDriver`: carretera/rasters/sprites para pseudo-3D estilo arcade.
- `SpriteBackdropDriver`: fondos hechos con sprites, strips o multiplexado.
- `CopperHeavyDriver`: intros, transiciones, cielos, agua, plasma, wipes y efectos
  de demoscene presupuestados.

### 20.6 Efectos reutilizables

- `PaletteCycleEffect`: agua, fuego, luces, neones.
- `PaletteZoneEffect`: cambios completos/parciales por bandas.
- `RasterGradientEffect`: cielos, niebla, horizonte.
- `RasterDistortionEffect`: ondas, heat haze, agua.
- `ParallaxEffect`: capas reales, fake-DPF o Copper splits.
- `SpriteMultiplexEffect`: objetos/fondos por franjas verticales.
- `BlitterTransitionEffect`: wipes, masks, reveals, page effects.
- `RoadPerspectiveEffect`: tablas de escala/offset por linea.
- `CopperScript`: formato portable desde UAF para describir intenciones, no
  instrucciones raw sin arbitraje.

### 20.7 Herramientas y UAF-R

- Exportador UAF-R con validacion de presupuesto por driver.
- Preview PC que use la misma descripcion retenida.
- Reporte de memoria Chip/Slow por escena.
- Reporte de Copper por linea y Blitter por frame.
- Golden screenshots y pruebas de input automatizado.
- Conversores de imagen a planar, sprites attached, BOB masks y tiles.

### 20.8 Criterio arquitectonico

Si un efecto necesita tocar directamente Copper, Blitter o sprites desde la logica
del juego, falta una abstraccion. El juego debe pedir "agua con ciclo de paleta",
"actor en capa de profundidad", "carretera con tabla de perspectiva" o "split de
cielo"; el driver y los schedulers deciden si cabe en el frame y como materializarlo
en el hardware.
