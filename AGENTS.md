# AGENTS

## Regla de idioma (obligatoria)
- Toda documentación y texto en español debe escribirse con ortografía correcta: tildes, eñes y puntuación adecuadas.
- No introducir nuevas frases en español sin corregir (`ejecución`, `análisis`, `depuración`, `diseño`, etc.).

## Formato de la documentación (obligatoria)
- No poner saltos de línea a mitad de párrafo: cada párrafo es una línea lógica y el texto se adapta a la anchura del editor con *word wrap*.
- Se permiten saltos de línea explícitos solo para estructuras (listas, código, tablas, diagramas ASCII).
- Añadir diagramas ASCII para ilustrar conceptos (capas, flujos, geometrías de buffers, zonas del Copper, etc.) cuando aclaren el texto.

## Política de documentación y referencias (obligatoria)
- **Preservar e indexar solo la mejor información**: ante un documento, manual o fuente que duplique contenido ya cubierto, comparar la calidad de ambos y quedarse solo con la mejor versión (o sintetizar en un único documento). No mantener dos fuentes que digan lo mismo.
- Al incorporar una referencia externa (manual, repo, curso, ficha), comprobar primero si ya existe algo equivalente en `docs/` y decidir: sustituir si la nueva es superior, componer solo si aporta algo distinto sin repetir, o descartar si es inferior o duplicada. Documentar el resultado final, no el proceso.
- **No incluir metainformación de proceso en los documentos de referencia**: fechas de edición/limpieza, "OCR corregido", "actualizado en <fecha>" o decisiones de ingesta no van en el contenido de la referencia ni de su índice; van en el mensaje de commit o, si procede, en una bitácora separada (`docs/guides/roadmap/`, `docs/debugging/`).
- Los índices y README de `docs/` deben apuntar solo a lo que existe y es canónico; si se elimina un documento, actualizar todos los enlaces en la misma pasada.

## Regla de tests unitarios (obligatoria)
- **Toda API reutilizable del engine debe tener tests que la respalden.** No se promueve código a `engine/` sin una forma de verificar su corrección.
- Forma preferida: ejecutar la validación dentro de una **demo** (aunque sea paramétrica o por fases, recorriendo variantes del API con `g_eng_run_status.detail` y aserciones). Las demos ya se ejecutan en el pipeline `build -> run -> analyze`, así que ofrecen evidencia viva de hardware.
- Si la API no tiene demo que la ejercite, **crear una** o, como mínimo, **un test unitario** en `tests/` que la respalde y que corra en la regresión.
- Las APIs de matemáticas/algoritmos puros (sin hardware) deben tener además **test unitario host** (`tests/host/`, compilado con el `g++` del entorno del toolchain del proyecto) para validarlas rápido y de forma determinista, sin depender de MSVC ni de WSL.
- Regla de cierre: una API sin test (demo o unitario) no se considera terminada.

## Qué es este repositorio
- El repo mantiene un proyecto C Amiga legado en `legacy/` (`legacy/Makefile`, `legacy/out/a.exe`) y un flujo nuevo de demos del engine C++23 en `demos/` + `tools/`; no mezclarlos por error.
- Para trabajo del engine, usar los wrappers shell de `tools/` en vez de invocar el `legacy/Makefile`.

## Estructura del repositorio (obligatorio)
La organización de directorios es canónica y está especificada en
**`docs/STRUCTURE.md`**. Antes de crear cualquier archivo o directorio, definir
dónde debe vivir según esa especificación. Resumen de las áreas raíz:

| Área | Contenido |
|---|---|
| `engine/` | Código del engine: `include/eng/` (API, algoritmos, librerías, capas de abstracción) + `src/platform/` (implementaciones backend por máquina). |
| `demos/` | Demos por plataforma: `demos/<plataforma>/<NNN>_<tema>/`. Hoy todas en `demos/amiga/`. Los assets que usa una demo no viven en ella: fuente en `assets/`, generados en `out/assets/<pipeline>/`, incrustados por `incbin`/include. |
| `assets/` | Assets fuente (raw, con licencia): `assets/<plataforma>/<dominio>/` (tiles-reference/, sprites/, audio/, maps/). Solo lectura por pipelines. |
| `tools/` | Herramientas host del pipeline (TypeScript/bash, compiladas a `dist/`); `scripts/` = scripts de entorno; `support/` = ASM/C de apoyo al linkado. |
| `host-tools/` | Programas de apoyo independientes del engine (Go/C++ para PC). |
| `playground/` | Experimentos de algoritmos/calidad; salida siempre en `out/playground/<experimento>/`. |
| `games/` | Juegos generados con el engine (primeros en este repo, no en repos separados). |
| `docs/` | Documentación: `engine/` (arquitectura), `demos/` (efectos y tile-pipeline), `tools/`, `reference/<plataforma>/` (hardware/técnicas), `guides/` (roadmap, optimización, metodología), `debugging/`, `build/`, `emulation/`, `testing/`. |
| `out/` | Todo lo generado (gitignored); reglas canónicas en `out/README.md`. Prohibido crear directorios ad-hoc sueltos aquí. |
| `obj/`, `dist/` | Intermedios de compilación y TypeScript compilado (gitignored; regenerables). |
| `artifacts/` | Resultados/evidencia canónicos commiteados y congelados (no regenerables por defecto). |
| `legacy/` | Proyecto C Amiga congelado. |

Reglas críticas:
- Los assets **generados** por pipelines van a `out/assets/<pipeline>/…` (nunca a
  `assets/`), y cada tool debe aceptar `--out` con un defecto ya canónico.
- La salida de experimentos/medidas va a `out/playground/<experimento>/`.
- Los juegos en `games/` usan el mismo flujo build/run/analyze que las demos.
- Cualquier salida temporal va a `out/tmp/`; si ves un directorio suelto en
  `out/`, muévelo al área canónica correspondiente y corrige la tool que lo crea.
- **Nada de binarios/media en git**: no añadir archivos binarios ni media masiva
  (PNG/JPG/audio/vídeo/archivos compilados). `.gitignore` los excluye por
  defecto; sólo se versionan los assets fuente de `assets/`. Si un pipeline o una
  IA genera imágenes/`.bin`, la salida va a `out/` (ignorado) y el resultado
  "canónico" de texto (informes/README) a `docs/` o `artifacts/`.

## Herramientas locales requeridas
- Windows + Git Bash + Node.js son obligatorios para el flujo de ejecución automatizada (`tools/run/run-demo.sh` -> `dist/tools/run/run-demo.js`). No usar el `bash.exe` de WSL para invocar los binarios `.exe` del toolchain; PowerShell solo se usa cuando el script o la integración con Visual Studio lo exige.
- El toolchain Amiga se resuelve en este orden: `AMIGA_BIN_PATH`, extensión de Cursor y luego extensión de VS Code `bartmanabyss.amiga-debug-*` (versión más alta instalada; el fork local es 1.8.1).
- `tools/run/run-demo.ts` importa dinámicamente `../mcp-winuae-emu/dist/winuae-connection.js` desde el repositorio hermano; si falta, el runner falla antes de abrir WinUAE.

## Comandos canónicos
- Compilar una demo: `bash ./tools/build/build-demo.sh demos/amiga/000_toolchain_cpp23 --debug --clean`
- Ejecutar una demo y capturar: `bash ./tools/run/run-demo.sh demos/amiga/000_toolchain_cpp23`
- Analizar una demo: `bash ./tools/analyze/analyze-demo.sh demos/amiga/000_toolchain_cpp23`
- Regresión completa: `bash ./tools/test-regression.sh`
- Bucle de regresión de una demo: `bash ./tools/test-regression.sh --demo demos/amiga/101_ehb_tile_scroll_driver --warp`

## Orden de verificación (no saltar)
- Orden por defecto: `build -> run -> analyze`; `analyze-demo.sh` espera `.exe/.elf/.map` y valida `out/run/<demo>/screenshot.png` si existe.
- `tools/test-regression.sh` ya impone ese orden por demo y ejecuta `analyze-sequence.sh` automáticamente cuando existe.
- La regresión usa build estilo debug por defecto (`--debug` interno). Usar `--release` solo cuando se necesite comportamiento de optimización release.

## Comportamiento clave de runner/emulador
- `run-demo` configura WinUAE con `warp=false` por defecto. Usar `-Warp` solo para throughput/diagnóstico rápido, no para evaluar suavidad visual.
- Las demos modernas deben exponer `g_eng_run_status`; el runner espera `READY` por canal lateral en `127.0.0.1:2346` y falla rápido si no llega.
- El fallback por timeout es opt-in (`--allow-timeout-fallback`) y es para diagnóstico.
- Las capturas de secuencia se limpian antes de cada ejecución (`out/run/<demo>/sequence`) para no mezclar frames antiguos.

## Depuración avanzada (WinUAE-DBG v2.x, vía MCP `winuae-emu`)
Servidor GDB (puerto 2345) + **canal lateral** (2346). Especificación canónica:
`WinUAE-DBG/docs/WINUAE-MONITOR-EXTENSIONS.md`. **Usar el build x86**
(`winuae-gdb.exe`); el x64 tiene un bug preexistente de boot congelado.

Herramientas MCP disponibles (todos vía `mcp-winuae-emu`):
- `winuae_emulator_status` — telemetría (ciclos, frame, vpos/hpos, warp, baseText, contadores). Para confirmar que el emulador corre y estado global.
- `winuae_watchpoint_set_ext` / `_list` / `_last` / `_clear_ext` — watchpoints con filtro por **origen** (`src=cpu|copper|blitter|bpl0-7|spr0-7|audio0-3|disk|dma`), `value=`, `mask=`, `must_change`, `nobreak`. `watch_last` reporta addr/src/valor/PC del último hit. Para "¿quién/cuándo toca X?" (p. ej. el copper escribe un registro custom).
- `winuae_protect` — `block`/`set` para congelar memoria o forzar valores. Para cheats y tests de estados.
- `winuae_rewind` — `start`/`stop`/`status` (captura); el restore **congela la emulación** pero deja el snapshot legible por canal lateral. Sólo para inspeccionar un estado pasado.
- `winuae_trace` — trazas de eventos watch/protect/rewind en `%TEMP%\winuae-gdb.log` (activo por defecto).
- `winuae_side_read` — canal lateral (`state`/`regs`/`mem <addr> <len>`/`runstatus <addr>`), independiente de GDB. Cuando GDB esté inerte o para observar sin intrusión.
- `winuae_debugperiph` — **periférico de depuración in-Amiga** en `0xB70000` (consola, checkpoints, contador de ciclos, debug args, breakpoints auto-dirigidos). Para telemetría del propio programa y profiling por checkpoints.

**Ejemplo real de periférico**: la demo `demos/amiga/101_ehb_tile_scroll_driver` está
instrumentada (`engine/include/eng/debug/peripheral.hpp`): en cada cambio de
tile-set escribe `TILE_CHANGE` a la consola y abre checkpoints 10→11 (coste del
upload). Consulta: `winuae_debugperiph checkpoints` / `console`. Verificación
de scroll en 4 direcciones + diagonal a 50fps (con ollama):
`tools/analyze/verify-scroll-directions.mjs`.

Guía "cuándo usar cada herramienta" e instrumentación de demos:
`docs/debugging/DEBUG-WINUAE-V2-GUIDE.md`.

Estado scroll fino de la demo 101 (2026-08): RESUELTO el salto del cruce de tile.
La causa raíz era el signo de `BPLCON1` (invertido) y el puntero coarse: el driver
usaba `BPLCON1=fine` con `fetch=coarse-16`, lo que invertía el sentido del scroll
dentro de cada tile y producía un salto de ~31px en el cruce de 16px. Se sustituyó
por la fórmula canónica de ACE/HRM: `BPLCON1=(16-fine)&15` y
`fetch=(scroll_x-1)&~15` (un word antes solo en `fine==0`), que da
`display_start == scroll_x` continuo en todo el rango. Sigue pendiente el doble
buffer de la copperlist. Validar con `analyze-fine-scroll.sh --warp` y
`analyze-sequence.sh --warp`.

Scroll genérico multi-modo (2026-08): el driver de scroll por tiles vive ahora en
`engine/include/eng/graphics/drivers/tile_scroll.hpp` como `TileScrollScene<Mode>`
(template sobre el modo), con scroll por playfield (`TileScrollInput`) y override
coarse por bitplane (`plane[i]`, preparado para RoboCod). `ehb_tile_scroll.hpp` es
un shim de compatibilidad (`EhbTileScrollScene` = single 6). La demo 102
(`demos/amiga/102_tile_scroll_dualpf`) demuestra dual 2+3 con primer plano 50%
transparente y parallax. El test de descomposición de scroll para 4/5/6 single y
2+3/3+3 dual es: `node tools/analyze/verify-tile-scroll-modes.mjs`.

Rendimiento del scroll por tiles (2026-08, lecciones aprendidas):
- El scroll del chipset (BPLxPT + BPLCON1 vía Copper) es barato; la CPU solo
  calcula offsets y programa registros, como en los juegos reales (Mega Typhoon).
  El coste real del frame estaba en la capa software de prefetch, no en el display.
- `ProgressiveTileScheduler::take_budget` era O(n²): desplazaba toda la cola por
  cada job tomado. Con la cámara rápida (2px/frame) y franjas encoladas en cada
  cruce, dominaba el update y la demo caía de 50fps a ~36. Se arregló con puntero
  de cabeza (O(budget), compactación amortizada) en `tilemap/tile_scroll.hpp`.
- En dual playfield, `make_playfield_upload_jobs` emitía UN job por plano por tile
  (3x en 3+3). Cada job paga wait_blitter + programación de registros, y el
  overhead por blit es lo que domina en los cruces del ring dual. Se fusionó en un
  solo job por tile con `destination_plane_stride = 2*plane_bytes` (los planos de
  un playfield están intercalados: PF1=1,3,5 / PF2=2,4,6). La demo 104 subió de
  ~41.5 a ~47.6fps; el resto del gap es el overhead del emulador por blit en los
  frames de cruce, no el hardware (en Amiga real cabe de sobra en 20ms).
- Los checkpoints del periférico (`debugperiph checkpoints`) añaden ~10fps de
  overhead al update; medir fps con ellos puestos engaña. Quitarlos para medir.
- Fps medidos (emulador WinUAE-DBG, -O1): 101=~48, 102=~50, 103=~50, 104=~47.6.
  En hardware real los cuatro van a 50fps.

Roadmap del port de features de engine9000 (hecho/pendiente, priorizado para
hilos nuevos): `WinUAE-DBG/docs/WINUAE-MONITOR-EXTENSIONS.md` → sección
"Roadmap del port desde engine9000". Hechos: punto 1 (periférico Amiga 1:1),
punto 2 (checkpoint profiler), punto 4 (`train` + `base`), punto 5 (hotspots +
smoke test). Punto 3 (rewind timeline) bloqueado (captura atada al input-
recording del GUI). Pendiente: `print` DWARF.

## Rutas de alto valor
- **Puerta de entrada IA → documentación (leer primero ante cualquier tarea)**: `docs/ai-dev-environment/DOC-MAP-PRINCIPAL.md`. Mapea «voy a hacer X» → docs/demo/API a consultar, incluye el protocolo de ingesta de repos/referencias externas (buscar antes de crear, comparar calidad, componer/sustituir) y los enlaces a la referencia oficial de hardware (AHRM 3.ª en `docs/reference/ahrm/`).
- **Herramienta de tiles/sprites para juegos (todo-en-uno)**: `tools/amiga-tiles/README.md` — *tutorial y entrada* para `amiga-tiles.mjs` (quantizer/tilebank/EHB/dither/paletas), `run-demos.mjs` (genera `out/assets/tile-demos`), `run-vision-verify.mjs` (verificación con ollama), `extract-sprites.mjs` (extracción de sprites por componentes) y `game-assets.mjs` (pipeline único con IA). Ver también `docs/guides/roadmap/REGLAS_PIPELINE_TILES.md` y `docs/demos/tile-pipeline/PIPELINE_TILES_EHB.md`.
- **Estado actual (2026-09) y plan — ROADMAP VIGENTE**: `docs/guides/roadmap/ROADMAP_UNIFICADO.md` (reunifica los roadmaps: estado del engine/demos, decisiones tomadas y próximas direcciones; incluye la variante rápida «Sonic» F6). Históricos superados (con banner interno): `docs/guides/roadmap/XLIMITED_8WAY_EHB_201.md` (roadmap 201→202, F1-F4 cerradas y F5 202 DPF completado). Reglas de pipeline vigentes: `docs/guides/roadmap/REGLAS_PIPELINE_TILES.md`. DPF (Y por campo / split / lineal / mixto): `docs/engine/architecture/DPF_MIXTO_SPLIT_LINEAL.md`. y `docs/guides/roadmap/REGLAS_PIPELINE_TILES.md` (reglas de oro: cuantizar el original antes de extraer, comparar en el mismo espacio EHB con assert al 100%, catálogo ≤ original, PNG indexados con encoder propio + round-trip, umbrales con pérdida explícita). El bug 8-way "tile en el área visible" se depura con watchpoint `g_eng_diag_hit` y breakpoints en `add_draw`/`draw_block_job`/`scroll_down-up` (ver `demos/amiga/107_xlimited_corkscrew/src/main.cpp`). Enunciado para IA externa sobre el runner que no ejecuta demos NUEVAS (queda en AmigaDOS): `docs/guides/roadmap/PROBLEMA_LAUNCHER_DEMOS_NUEVAS.md`.
- **Checklist imprescindible del corkscrew XYLimited (201, lecciones 2026-09)**: ver §7 de `demos/amiga/201_ehb_map/src/README.md`. Tres invariantes NO obvios que rompen la imagen si se tocan sin entenderlos: (1) el ANILLO vertical `display_height` se dimensiona para el viewport TOTAL (256+2*16=288), NO `(viewport−HUD)+32`; si es 240, `mapy=16/17` colisiona con `mapy=1/2` en el módulo y aparecen arriba las filas que deben ir abajo. (2) `block_videoposy` envuelve en `display_height`, nunca en `bitmap_height` (304, incluye las filas extra del walk X) → basura por toda la pantalla. (3) `visible_tile_bias_x/y=1` es OBLIGATORIO para que `map[0][0]` sea visible (el hardware esconde los 16 px de guarda; sin bias hay un offset aparente −16,−16). El `mapx/mapy` del scroll son celdas FÍSICAS del anillo, no índices lógicos, por eso `map_tile_at` restándoles bias es correcto para fill y scroll. Constantes NTTP `ScrollConsts.display_height` deben coincidir con el anillo real.
- Pipeline de tiles/EHB (verificado): `node tools/ehb/quantize-ehb.mjs <png>` → `palette.json`; `node tools/ehb/slice-tiles.mjs <png> --palette out/assets/ehb/palette.json [--ehb-merge F]` → `tilebank_indexed.h` + **`tilebank.raw.bin`** (modo `--encode raw` por defecto; datos de índices 0..63, 256 bytes/tile, stride fijo) + `tiles.json`/PNG (assert COMPARAR=100% sin fusión). La demo 201 incrusta el `.bin` por incbin en sección `tiles.MEMF_CHIP` (hunk HUNKF_CHIP; receta documentada en el asm de `demos/amiga/201_ehb_map/src/main.cpp:30-36`). **Explicación completa del pipeline y del concepto X-Limited (qué engine usa, qué mecánicas del chipset explota: EHB, scroll HW con BPLCON1/BPLxPT, split de Copper, Blitter interleaved): `demos/amiga/201_ehb_map/src/README.md`**; ahí se detallan y verifican las invocaciones de `quantize-ehb` → `slice-tiles` → `emit-const-201` → `emit-xlimited-bank` y su correspondencia con los 1149 tiles / mapa 40×40 / banco 222,720 B de la demo.
- Self-test del harness (canal lateral/READY/fps): `node tools/debug/verify-harness.mjs [--strict-fps --warp]`. Nota: el throughput del emulador ~11fps limita el gate fps absoluto.
- Bucle de entrada del engine: `engine/include/eng/engine.hpp` (`update -> wait_vblank -> render`; `render` es el punto de commit).
- Backend Amiga: `engine/src/platform/amiga_minimal/amiga_minimal.cpp`.
- Validación temporal fuerte por demo: `demos/amiga/101_ehb_tile_scroll_driver/analyze-sequence.sh`.
- Scroll multi-modo: demo dual `demos/amiga/102_tile_scroll_dualpf/analyze-sequence.sh` y test host `node tools/analyze/verify-tile-scroll-modes.mjs`.
- Detalles operativos build/run: `docs/build/BUILD_AND_RUN.md`.
- Reinstalar el entorno en otro equipo: `docs/debugging/SETUP_NUEVO_EQUIPO.md` (repos, build de WinUAE-DBG, instalación del fork de la extensión, `.mcp.json`).
- Historial de fixes de depuración (relocalización de breakpoints, `-O0`, qOffsets): `docs/debugging/HISTORIAL-CAMBIOS.md`.
- Harness DAP sin VS Code (verifica breakpoints y fuente C++ de la capa DAP): `tools/dap-test/README.md`. Requiere el fork `vscode-amiga-debug` compilado y el stub de `vscode` (ver README). Trazas en `%TEMP%\amiga-debug-trace.log` y `%TEMP%\winuae-gdb.log`.
- Depuración interactiva: usa `tools/debug/build-current-demo.sh` (compila con `-O0` el archivo en primer plano a `out/debug-current/`) y F5 con la config "Amiga 500: depurar archivo actual".
- Captura y análisis de perfiles (frames de pantalla + análisis con Ollama local): `tools/profile/README.md`. **Para la IA**: `node tools/profile/ai-analyze.mjs <outName> [frames] --prompt "…"` captura por el canal lateral (2346), extrae frames + resumen y analiza con Ollama local, imprimiendo el informe final por stdout (sin gastar tokens de nube). `--demo <demo>` lanza WinUAE directo (como hijo del script), espera READY y lo apaga al terminar; el análisis completo tarda ~2-4 min, `--mode meta` es rápido. Componentes: `capture-profile.mjs`, `profile-extract.mjs`, `ollama-analyze.mjs`, `launch-winuae.mjs`, `profile-analyze.sh`. Requiere WinUAE con `WINUAE_GDB_PERSIST_LISTENER=1` y Ollama en `127.0.0.1:11434`. La tool MCP `winuae_profile_ollama` (en `mcp-winuae-emu`, no registrada aún en opencode) hace lo mismo por MCP.
- Avanzar por breakpoints y leer memoria/frame buffer en caliente: `tools/debug/step-memory.mjs`.

## Restricciones de código/diseño que hay que preservar
- Restricciones intencionales del engine: `gnu++23`, sin exceptions, sin RTTI, sin asignación dinámica en gameplay (`docs/engine/architecture/CODING_STYLE.md`).
- **APIs paramétricas, nunca de tamaño fijo**: no generar funciones con geometría/tamaño embebido (p. ej. `emit_ehb_320x256_display`); el engine expone métodos paramétricos (registros/planos/ancho, etc.) y el llamador decide los valores. Los "magic numbers" de un caso concreto viven en la demo/config, no como API.
- La lógica de juego debe ser agnóstica del backend; registros/DMA específicos de Amiga van en capas backend/driver, no en lógica de alto nivel.

## Regla de API del engine (obligatoria)
- **El programador no debe ver funciones de bajo nivel** (p. ej. `draw_text` que recibe puntero a bitplanes, índices de planos o `u8*`). Todo dibujo y acceso a la imagen pasa por **una abstracción de alto nivel** (tipo **contexto de dispositivo**), análoga al `RastPort` de la ROM de Amiga (graphics.library) o a un device-context de Windows: la app pide «surface/contexto» y dibuja sobre él, sin conocer la memoria subyacente.
- **Sin punteros ni mecanismos inseguros en el API**: no exponer `u8*`, offsets crudos, layouts, planos, registros custom ni direcciones DMA en la interfaz pública. Esas decisiones viven dentro de la implementación (driver/surface), nunca en la firma que consume la lógica de juego.
- **Versátil para cualquier configuración**: el API debe funcionar igual para EHB, single/double playfield, 4/5/6 planos, interleaved/separate, cualquier resolución. No diseñar funciones o clases que solo funcionen en EHB o en DPF o en un tamaño concreto; la abstracción expone un «pincel/contexto» que el propio contexto (superficie) configura internamente según sus parámetros.
- Prueba de diseño: una función de dibujo debe poder expresarse igualmente sobre un contexto EHB 6 planos, un contexto single 4 planos y un contexto DPF, con la misma llamada y solo cambiando la configuración del contexto; el llamador nunca ve qué modo es.
- Lo que sí puede ser específico de un modo (registros, cobre, DMA) queda **dentro del driver/surface** o en capas backend, nunca filtrado al llamador.

## Regla de «buscar antes de implementar» (obligatoria)
- **Nunca implementar una utilidad o API del engine sin antes comprobar que no existe ya.** Antes de escribir `draw_text`, una fuente, un blit, un driver o cualquier ayuda reusable, buscar en `engine/include/`, `demos/` y `tools/` (grep por nombre y por concepto: «text», «font», «blit», «surface», «scene», «palette»…) y en los índices de `docs/` (`DOC-MAP-PRINCIPAL.md`, READMEs, roadmaps).
- Si ya existe (incluso en una demo): **reutilizar, generalizar o subir al engine**, nunca duplicar. Una implementación local en una demo que sirve a otras debe promoverse a `engine/` como utilidad reutilizable.
- Antes de añadir un archivo nuevo en `engine/`, listar los helpers existentes del dominio y decidir explícitamente: ¿esto ya lo cubre `X`? ¿Puedo extender `X` en vez de crear `Y`?
- Esto aplica también a **fuentes, tablas y glifos**: buscar si el carácter/glifo ya está antes de redibujarlo.
- Un commit que añade algo que ya existía como duplicado se considera un error de proceso.

## Regla permanente de rendimiento
- Todo código nuevo debe minimizar el trabajo total por frame y reutilizar datos,
  trabajos, buffers y estados siempre que sea posible.
- La CPU debe limitarse a decidir cambios y programar hardware; evitar que haga
  copias, divisiones, módulos, recorridos o reconstrucciones repetidas que puedan
  resolverse incrementalmente, por lotes o mediante el Blitter/Copper.
- Antes de aceptar una solución, buscar explícitamente algoritmos O(1) o O(n)
  frente a colas O(n²), fusionar operaciones compatibles y reducir el número real
  de accesos al Blitter y de esperas síncronas.
- Medir los picos con profiling y telemetría en el caso límite, no solo validar
  que el frame nominal funcione; cualquier optimización debe conservar la
  corrección visual y el presupuesto de Chip RAM.
- **Criterio retro del chipset (68000)**: preferir algoritmos **rápidos y
  exactos** a lentos y precisos. Un algoritmo que subestima ~6 en `isqrt` pero
  cuesta 10 ciclos gana a uno exacto que paga `__divsi3`/`__mulsi3`. Para el
  hot path, lo deseable es aritmética 16-bit nativa (`muls.w`/`divs.w`), bucles
  countdown que emiten `dbra`, cero divisiones runtime, cero floats y cero STL.
  Directrices operativas y bitácora de descubrimientos en
  `docs/guides/optimization/OPTIMIZACION_GPP_68000.md` (leer antes de optimizar
  o portar código caliente).
- **Comprobar el ensamblador generado**: al portar o escribir APIs, revisar con
  `-S`/`-fverbose-asm` que el código que emite el toolchain no sea peor que el
  original (o que el asm a mano del repo de origen). Un port 100 % "fiel pero
  lento" pierde contra el original 68k optimizado; si el original usaba una
  optimización en asm (p. ej. `swap` para rotar, `lsl.l #8; add` para `<<9`,
  `divs/divu` de 16 bits), verificar que nuestra versión C++ produce algo al
  menos igual de eficiente y anotarlo en la bitácora del doc de optimización.

## Comentarios didácticos de código
- El código nuevo de hardware Amiga debe incluir comentarios breves, en español,
  con estilo de tutorial: explicar qué registro o mecanismo del chipset interviene,
  qué invariantes mantiene el algoritmo y por qué una alternativa aparentemente
  más simple consumiría más CPU, Blitter o Chip RAM.
- Cuando una decisión sea difícil de inferir, enlazar desde el comentario al MD
  técnico correspondiente y usar un pequeño esquema ASCII si aclara la geometría
  de buffers, Copper, bitplanes o zonas visibles.

## Regla de evidencia
- No afirmar que una funcionalidad funciona sin evidencia reproducible de esa
  funcionalidad concreta.
- Distinguir siempre entre indicios, validación parcial y evidencia concluyente;
  una compilación, un test host o una imagen que cambia no prueban por sí solos
  continuidad visual ni corrección del hardware.
- Si faltan herramientas para observar el comportamiento real (por ejemplo,
  registros Copper efectivos, punteros BPL por frame o ciclos del Blitter),
  declararlo explícitamente y no presentar una hipótesis como resultado.
- Probar primero el caso límite relevante y solo después documentar o afirmar
  que el cambio está resuelto.
