# AGENTS

Reglas operativas para agentes IA en este repositorio. Este documento contiene las
**reglas generales** (aplican a toda tarea) y un **índice que enruta a las reglas
específicas de cada dominio**, que viven en su documento canónico para no cargar de
contexto irrelevante a quien trabaja en otra cosa.

## Cómo usar este documento

1. Antes de empezar cualquier tarea, leer `docs/ai-dev-environment/DOC-MAP-PRINCIPAL.md` (mapa «voy a hacer X → documentación»).
2. Aplicar siempre las reglas generales de §1 y §3.
3. Si la tarea cae en un dominio con regla propia, **leer su documento** (§4) antes de escribir código.
4. Documentar en el sitio canónico y enlazar desde el índice correspondiente.

---

## 1. Reglas generales (aplican a toda tarea)

### 1.1 Idioma

- Toda documentación y texto en español debe escribirse con ortografía correcta: tildes, eñes y puntuación adecuadas.
- No introducir nuevas frases en español sin corregir (`ejecución`, `análisis`, `depuración`, `diseño`, etc.).

### 1.2 Formato de la documentación

- No poner saltos de línea a mitad de párrafo: cada párrafo es una línea lógica y el texto se adapta a la anchura del editor con *word wrap*.
- Se permiten saltos de línea explícitos solo para estructuras (listas, código, tablas, diagramas ASCII).
- Añadir diagramas ASCII para ilustrar conceptos (capas, flujos, geometrías de buffers, zonas del Copper, etc.) cuando aclaren el texto.
- **Escribir para un lector sin conocimiento histórico.** Los documentos de referencia describen el estado/objetivo **vigente**, no su evolución: no usar «antes/ahora/desaparece/hoy/ya no», no narrar el proceso ni las alternativas descartadas. Las notas de transición, decisiones descartadas, estado de fases y bitácoras van a `docs/debugging/` o `docs/guides/roadmap/`, nunca al documento de referencia.
- **Los comentarios del código sí recogen hallazgos, rarezas y restricciones**, pero para **justificar y blindar** ese código, no como bitácora. Su función es que una refactorización futura no rompa un comportamiento descubierto: explican **por qué** el mecanismo es así, **qué fallo evita** o **qué invariante respeta** (p. ej. un registro que hay que escribir en cierto orden, un `volatile` obligado porque una ISR comparte el estado, un bit que el emulador trata distinto que la doc). Por eso **no** narran el proceso (sin «antes/ahora», sin cronología) y **no duplican** la investigación: **citan la fuente** (doc canónico de `docs/`, o `fichero:línea` del emulador) y, si hay investigación abierta, el doc de `docs/debugging/`. Los intentos, descartes y dudas van a `docs/debugging/`, no al código.

### 1.3 Política de documentación y referencias

- **Preservar e indexar solo la mejor información**: ante un documento, manual o fuente que duplique contenido ya cubierto, comparar la calidad de ambos y quedarse solo con la mejor versión (o sintetizar en un único documento). No mantener dos fuentes que digan lo mismo.
- Al incorporar una referencia externa (manual, repo, curso, ficha), comprobar primero si ya existe algo equivalente en `docs/` y decidir: sustituir si la nueva es superior, componer solo si aporta algo distinto sin repetir, o descartar si es inferior o duplicada. Documentar el resultado final, no el proceso.
- **No incluir metainformación de proceso en los documentos de referencia**: fechas de edición/limpieza, «OCR corregido», «actualizado en <fecha>» o decisiones de ingesta no van en el contenido de la referencia ni de su índice; van en el mensaje de commit o, si procede, en una bitácora separada (`docs/guides/roadmap/`, `docs/debugging/`).
- Los índices y README de `docs/` deben apuntar solo a lo que existe y es canónico; si se elimina un documento, actualizar todos los enlaces en la misma pasada.
- **Todo documento de hallazgo o investigación queda indexado por tema en la misma pasada**, en dos niveles: (1) su README de carpeta (`docs/debugging/README.md`, `docs/reference/emulators/README.md`, …) con una fila descriptiva, y (2) si un programador lo buscaría por tema al tocar un dominio, una entrada en la tabla «tareas → documentación» de `docs/ai-dev-environment/DOC-MAP-PRINCIPAL.md` §3. Un hallazgo citado desde el código debe ser localizable por quien vaya a tocar ese código. El **nombre** del fichero es kebab-case, con prefijo `NNN_` solo si el doc es de una demo concreta. Se verifica con `node tools/check/doc-index.mjs` (áreas declaradas en `tools/check/doc-index-areas.txt`, sin tocar código; corre en `tools/run-host-tests.sh` y `tools/test-regression.sh`).
- **Los enlaces relativos de la documentación no deben quedar rotos**: `node tools/check/links.mjs` los valida y corre en `tools/test-regression.sh` y `tools/run-host-tests.sh`. La deuda histórica se acepta de forma explícita en `tools/check/links-baseline.txt`; regenerar con `--update-baseline` solo si se decide aceptar roturas nuevas.
- Protocolo detallado de ingesta de repos/referencias externas: §6 de `docs/ai-dev-environment/DOC-MAP-PRINCIPAL.md`.

### 1.4 Encoding y finales de línea

- **Todos los archivos de texto del repo deben ser UTF-8 válido (sin mojibake).** Tildes, `ñ`, `—`, `→`, `≈`, `≤`, `∝`, `φ`, etc. deben quedar en UTF-8 correcto. Evitar añadir BOM.
- **NO editar archivos con `Get-Content`/`Set-Content` de PowerShell**: por defecto `Get-Content` lee UTF-8 y `Set-Content` escribe Windows-1252, lo que **re-encoda** el archivo y pierde los caracteres no representables (`→` → `?`, `≤` → `=`). Es la causa del mojibake (UTF-8 doblemente codificado) detectado en el repo.
  - Usar las herramientas de edición del agente (escriben UTF-8) o `sed -i` de Git Bash (preserva bytes).
  - Si hay que usar PowerShell, forzar `-Encoding utf8` en lectura **y** escritura (`Get-Content -Encoding utf8 … | Set-Content -Encoding utf8 …`) y verificar después con `node tools/check/encoding.mjs`.
- **Finales de línea LF**: el repo usa LF. `Set-Content` de PowerShell también convierte LF→CRLF (diff de archivo completo). No introducir CRLF en fuentes/docs nuevos; el único archivo CRLF admitido es el manual AHRM ingerido (`docs/reference/ahrm/`).
- **Check obligatorio en CI**: `node tools/check/encoding.mjs` falla si algún `.cpp/.hpp/.md/.mjs/.sh/…` no es UTF-8 válido o contiene mojibake. Ya está integrado en `tools/test-regression.sh` y en la pasada completa de `tools/run-host-tests.sh`. Para reparar mojibake: decodificar el run de no-ASCII como cp1252 → UTF-8 (reversible, varias pasadas), nunca reescribir el archivo entero.

### 1.5 Evidencia

- No afirmar que una funcionalidad funciona sin evidencia reproducible de esa funcionalidad concreta.
- Distinguir siempre entre indicios, validación parcial y evidencia concluyente; una compilación, un test host o una imagen que cambia no prueban por sí solos continuidad visual ni corrección del hardware.
- Si faltan herramientas para observar el comportamiento real (por ejemplo, registros Copper efectivos, punteros BPL por frame o ciclos del Blitter), declararlo explícitamente y no presentar una hipótesis como resultado.
- Probar primero el caso límite relevante y solo después documentar o afirmar que el cambio está resuelto.

### 1.6 Buscar antes de implementar

- **Nunca implementar una utilidad o API del engine sin comprobar antes que no existe ya.** Antes de escribir `draw_text`, una fuente, un blit, un driver o cualquier ayuda reusable, buscar en `engine/include/`, `demos/` y `tools/` (grep por nombre y por concepto: «text», «font», «blit», «surface», «scene», «palette»…) y en los índices de `docs/` (`DOC-MAP-PRINCIPAL.md`, READMEs, roadmaps).
- Si ya existe (incluso en una demo): **reutilizar, generalizar o subir al engine**, nunca duplicar. Una implementación local en una demo que sirve a otras debe promoverse a `engine/` como utilidad reutilizable.
- Antes de añadir un archivo nuevo en `engine/`, listar los helpers existentes del dominio y decidir explícitamente: ¿esto ya lo cubre `X`? ¿Puedo extender `X` en vez de crear `Y`?
- Aplica también a **fuentes, tablas y glifos**: buscar si el carácter/glifo ya está antes de redibujarlo.
- Un commit que añade algo que ya existía como duplicado se considera un error de proceso.

### 1.7 Contexto técnico

- **Antes de implementar cualquier mecanismo técnico** (registro o comportamiento de hardware, protocolo, formato, peculiaridad del toolchain o del chipset), **localizar y leer la documentación de referencia relevante**. No inventar ni descubrir por prueba y error.
- Fuentes preferentes: `docs/reference/ahrm/` (AHRM 3.ª), el repo hermano `../amiga-bootcamp/` (p. ej. `01_hardware/common/cia_chips.md`, `video_timing.md`, `dma_architecture.md`), los headers del SDK (`…/opt/m68k-amiga-elf/sys-include/hardware/*.h`) y los datasheets.
- Si no existe doc en el repo, **traerla o crearla antes de programar** (regla de ingesta de referencias: buscar, comparar calidad, componer/sustituir).
- **Citar la referencia** (ruta del doc, datasheet, sección) en el comentario del código y en el commit.
- Ejemplo (2026-09): el timer de CIA no recargaba por poner `CRA bit3 RUNMODE=1` (one-shot); leer `cia_chips.md` lo documenta como «0 = continuo» y fue la corrección directa.

### 1.8 Commits por turno

El objetivo es que el usuario pueda **revisar** el trabajo antes de que se consolide en git.

- **Al comenzar un turno**, hacer commit de lo que quedó **sin commitear** del turno anterior (solo lo hecho en el hilo actual), ya revisado.
- El trabajo producido en el **turno en curso** no se commitea en ese mismo turno, aunque complete una tarea pendiente o un arreglo: se deja sin commitear para que el usuario lo repase, y se commitea al inicio del turno siguiente salvo que el usuario pida lo contrario.
- No incluir en ese commit cambios ajenos al hilo actual.

### 1.9 API pública y backends

- La lógica de demo/juego incluye la **fachada** `eng/api/api.hpp` (un solo include con la API estable) y, si es una demo Amiga, su backend. No acumular includes sueltos de `eng/core`, `eng/engine.hpp`, `eng/graphics/composition`… salvo lo que no cubra la fachada (3D, efectos, utilidades concretas).
- La lógica de demo **no nombra tipos del backend** (`MinimalBackend::C2p4State`, `…::OrBobEntry`, `…::LineEorParams`): usa el **tipo de dominio** (`eng::graphics::C2p4`/`OrBob`/`LineEor`) o la API del seam (`FramePlan`, `Rasterizer`, `DrawTarget`). El backend se instancia en `main()` y se pasa al `Engine`.
- El backend no expone registros ni punteros a la app; si una demo necesita un valor preparado por hardware, se declara en la capa de dominio y el backend lo **aliasa** (ver `docs/engine/architecture/ENGINE_STRUCTURE_REVIEW.md`).
- **API público final**: debe ser **lo más intuitivo y simple posible** y **no restringir funcionalidad** (ver §1.1 de `docs/engine/architecture/PUBLIC_API.md`). Las interfaces **intermedias** del engine pueden ser técnicas; lo que consume el juego, no. Mientras falten módulos, se escribe el API de lo que ya existe y se adapta después. Al tocar un módulo, preguntar «¿cómo lo pediría un juego?».
- **Fast RAM**: los juegos detectan en **runtime** si hay Fast RAM (Agnus no la ve, CPU a plena velocidad) y la usan para tareas intensivas de CPU. **No** usar Slow RAM para eso (comparte el bus DMA pero Agnus no la ve: lo peor de ambos).

### 1.10 Genericidad de las cabeceras

- Una cabecera del engine debe ser **genérica sobre lo que varía** (escalar, dimensión, capacidad, política) siempre que el algoritmo no dependa de un tipo concreto. **No** fijar `s16`/`float`/`u32` en la firma si el algoritmo vale para cualquier tipo con las operaciones requeridas.
- Usar el **patrón del repo**: `template <class S>` con `eng::math` (`Vec<2,S>`, `scalar_traits`, `scalar_sqrt`, `div_norm`/`mul_norm`) para el escalar; parámetros `constexpr` de plantilla para capacidades; `Span`/vistas para buffers. Ejemplos de referencia: `eng/ai/steering/steering.hpp`, `eng/core/math/linalg.hpp`, `eng/core/math/geometry.hpp`.
- Si una parte **no** puede ser genérica (p. ej. una fase amplia atada a `s16`), **decoplarla como política de plantilla** o recibirla como parámetro, en vez de hardcodear el tipo en el algoritmo. Ejemplo: `eng/ai/steering/crowd.hpp` (`Crowd<S, Broadphase>`).
- El test host de una cabecera genérica debe ejercitarla con **al menos dos escalares** cuando aplique (p. ej. `s32` y `float`), además de los casos límite.
- Al tocar una cabecera existente, preguntar «¿esto vale solo para este tipo?»; si la respuesta es sí y no hay motivo, generalizarla en la misma pasada.
- **Una cabecera de plantilla no impone el escalar ni arrastra su implementación.** El escalar (o cualquier representación concreta: `Fixed`, `MiniFloat16`, `float`…) es **parámetro de plantilla**; la cabecera usa solo el **vocabulario genérico** (`scalar_traits`, `scalar_const`, `mul_norm`/`div_norm`, `fbm2`, `worley2`…). Por tanto **no** debe `#include` la cabecera de un escalar concreto (`eng/core/math/fixed.hpp`, `eng/core/math/minifloat.hpp`) ni su soporte matemático (`fixed_math.hpp`, `minifloat_math.hpp`), ni fijar un alias interno del estilo `using MF = MiniFloat16;`. Un backend con `float` nativo debe poder instanciarla con `float` sin traerse `Fixed`/`MiniFloat16`, y viceversa. **Prevalece el algoritmo; el escalar lo elige el consumidor** (el host/test puede incluir el escalar que quiera y pasarlo como `Fx`).
- **Test negativo obligatorio**: instanciar la cabecera con **dos escalares distintos** (p. ej. `float` y `Fixed<s16,12>`) demuestra que no impone uno. El gate `tools/check/generic-headers.mjs` cierra el caso estático (tipo concreto o include de escalar en cabecera genérica → falla).
- **Las optimizaciones propias no deben OCULTAR defectos.** Sustituir una *libcall* (`__mulsi3`, `__divsf3`…) por una rutina propia quita la señal barata que delataba coste/incorrección. Regla: **nunca** hacerlo sin (a) un **test de equivalencia contra una referencia independiente** (operación nativa o algoritmo clásico; idealmente byte a byte) y (b) comprobar que el binario **sigue en 68000** con `node tools/analyze/asm-audit.mjs <elf>` (falla si hay **68020+/FPU**: `muls.l`, `fmove`, `extb.l`, `bf*`, `cas.l`…). El silencio de las libcalls no es prueba de corrección. Ver `docs/reference/toolchain/m68k-gcc.md` §3.

### 1.11 Si el hardware no funciona: fuente del emulador

- Cuando un mecanismo del chipset **no se comporta como se espera** y la documentación de referencia (`docs/reference/ahrm/`, `../amiga-bootcamp/`, datasheets) no lo explica, la **implementación del emulador es la referencia de facto**: leer su **código fuente**.
- **Fuente local**: `../WinUAE-DBG/`. Ficheros clave: `custom.cpp` (registros custom: handlers de escritura/lectura, p. ej. `CLXCON`/`CLXDAT`), `drawing.cpp` (render por píxel/línea: colisión, sprites, playfield), `include/custom.h` (mapa de registros), `cfgfile.cpp` (preferencias como `collision_level`).
- **Procedimiento**: (1) localizar con `grep -rnE '<REG>|<término>'`; (2) leer el handler en `custom.cpp` y la lógica por píxel/línea en `drawing.cpp`; (3) comprobar **preferencias** que puedan desactivar la función (p. ej. `currprefs.collision_level`); (4) contrastar con el AHRM y **anotar la discrepancia**; (5) validar en emulador con una demo (caso positivo **y** negativo).
- Documentar el hallazgo en `docs/reference/emulators/<emulador>/<tema>.md` (índice en `docs/reference/emulators/README.md`), citando **fichero y línea**. Ficha de referencia por **tema**: mecanismo observado (tabla `registro/handler/fuente`), contraste con el AHRM, implicación para el engine y enlaces al código que la usa. Ejemplo: [`winuae/audio-irq.md`](docs/reference/emulators/winuae/audio-irq.md) (IRQ de audio: `setirq`/`event_audxdat_func`, `AUDxLEN`/`AUDxLCH`, contraste AHRM `:4378`, y por qué el servicio de nivel 4 es el sitio del *swap*).
- **Completar la referencia**: si el emulador aclara o corrige la doc del manual, añadir la aclaración a la copia local (`docs/reference/ahrm/ERRATA_Y_NOTAS.md` o la ficha de técnica), indicando **de dónde se obtuvo** (emulador + `fichero:línea`).

---

## 2. El repositorio

- El repo mantiene un proyecto C Amiga legado en `legacy/` (`legacy/Makefile`, `legacy/out/a.exe`) y un flujo nuevo de demos del engine C++23 en `demos/` + `tools/`; no mezclarlos por error.
- Para trabajo del engine, usar los wrappers shell de `tools/` en vez de invocar el `legacy/Makefile`.

### 2.1 Estructura (obligatoria)

La organización de directorios es canónica y está especificada en **`docs/STRUCTURE.md`**. Antes de crear cualquier archivo o directorio, definir dónde debe vivir según esa especificación. Resumen de las áreas raíz:

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

- Los assets **generados** por pipelines van a `out/assets/<pipeline>/…` (nunca a `assets/`), y cada tool debe aceptar `--out` con un defecto ya canónico.
- La salida de experimentos/medidas va a `out/playground/<experimento>/`.
- Los juegos en `games/` usan el mismo flujo build/run/analyze que las demos.
- Cualquier salida temporal va a `out/tmp/`; si ves un directorio suelto en `out/`, muévelo al área canónica correspondiente y corrige la tool que lo crea.
- **Nada de binarios/media en git**: no añadir archivos binarios ni media masiva (PNG/JPG/audio/vídeo/archivos compilados). `.gitignore` los excluye por defecto; solo se versionan los assets fuente de `assets/`. Si un pipeline o una IA genera imágenes/`.bin`, la salida va a `out/` (ignorado) y el resultado «canónico» de texto (informes/README) a `docs/` o `artifacts/`.

---

## 3. Operación (build / run / analyze)

### 3.1 Herramientas locales

Windows nativo + Git Bash + Node.js. **No usar WSL** para invocar binarios `.exe` del toolchain. El toolchain Amiga se resuelve por `AMIGA_BIN_PATH` y luego por las extensiones Bartman. Detalle completo: `docs/build/BUILD_AND_RUN.md` §Herramientas locales requeridas.

### 3.2 Comandos canónicos

- Compilar una demo: `bash ./tools/build/build-demo.sh demos/amiga/000_toolchain_cpp23 --debug --clean`
- Ejecutar una demo y capturar: `bash ./tools/run/run-demo.sh demos/amiga/000_toolchain_cpp23`
- Analizar una demo: `bash ./tools/analyze/analyze-demo.sh demos/amiga/000_toolchain_cpp23`
- Regresión completa: `bash ./tools/test-regression.sh`
- Bucle de regresión de una demo: `bash ./tools/test-regression.sh --demo demos/amiga/101_ehb_tile_scroll_driver --warp`

### 3.3 Orden de verificación (no saltar)

- Orden por defecto: `build -> run -> analyze`. `tools/test-regression.sh` impone ese orden por demo.
- La regresión usa build estilo debug por defecto (`--debug` interno). Usar `--release` solo cuando se necesite comportamiento de optimización release.
- Detalle operativo y reglas del runner/emulador: `docs/build/BUILD_AND_RUN.md`.

### 3.4 WinUAE concurrente: puertos y convivencia entre hilos

- En este build, **el puerto GDB de WinUAE-DBG es fijo (2345)**: `WINUAE_GDB_PORT` solo cambia a dónde conecta el cliente, no el puerto del emulador; ponerlo a otro valor rompe el enlace. El **canal lateral** sí es configurable con `WINUAE_SIDE_CHANNEL_PORT` (verificado). Consecuencia: **solo una instancia de WinUAE-DBG puede usar GDB a la vez**.
- Antes de lanzar, el runner comprueba con `netstat` si 2345 o el canal lateral están ocupados. Si lo están, **falla con un mensaje claro** en vez de conectarse a una instancia ajena (evita capturas cruzadas). Para serializar el GDB entre hilos: `--wait-port <segundos>` espera a que se libere; `--reset-emulator` libera **solo** los PIDs que escuchan esos puertos.
- **Nunca matar** procesos `winuae-gdb`/`winuae64` ajenos: solo cerrar los propios (por PID) al terminar. Nunca `taskkill /IM winuae-gdb.exe`, que mata a todas las instancias.
- Cada hilo puede usar un **canal lateral propio** (`WINUAE_SIDE_CHANNEL_PORT`) para reducir colisiones, pero al compartir el GDB 2345 debe coordinarse con otros hilos. Detalle: `docs/debugging/system/debug-winuae-v2-guide.md` §1.3–1.4.

Ejemplo: `WINUAE_SIDE_CHANNEL_PORT=2418 bash ./tools/run/run-demo.sh demos/amiga/000_toolchain_cpp23`.

---

## 4. Reglas específicas (leer el documento del dominio antes de trabajar en él)

Estas reglas son obligatorias, pero solo son relevantes cuando se toca su dominio. Cada una vive en su documento canónico (fuente única).

| Regla / dominio | Documento obligatorio |
|---|---|
| **Tests y verificación por demo** (toda API con test; NO VERIFICADA si no hay demo) | `docs/testing/README.md` |
| **Demos atractivas, validación visual (Ollama) y gate de optimizaciones de render** | `docs/guides/methodology/DEMO_VISUAL_DEBUG.md` |
| **API del engine** (sin hardware, sin punteros, versátil, prueba de diseño) | `docs/engine/architecture/PUBLIC_API.md` |
| **Estilo y restricciones de diseño** (gnu++23, sin excepciones/RTTI/heap, APIs paramétricas, agnosticismo del backend, comentarios didácticos) | `docs/engine/architecture/CODING_STYLE.md` |
| **Rendimiento, comentario de optimizaciones y port de rutinas calientes a asm** | §12 de `docs/guides/optimization/OPTIMIZACION_GPP_68000.md` |
| **Copper y buffers de display** (`copper::Plan`, `MultiBuffered`, doble buffer de copperlist) | §6 de `docs/engine/architecture/DISPLAY_COMPOSITION.md` |
| **Objetos: BOB ≠ polígono, transparencia, fondo, copper por objeto** | `docs/engine/architecture/OBJECT_SYSTEM.md` |
| **Blitter / minterms / líneas y polígonos** | `docs/reference/amiga/techniques/README.md` y `blitter-line-subpixel-fill.md` |
| **Runner/emulador** (warp, READY, canvas, secuencias) | `docs/build/BUILD_AND_RUN.md` |
| **WinUAE: instancias múltiples y depuración avanzada (GDB/canal lateral/MCP)** | `docs/debugging/system/debug-winuae-v2-guide.md` |
| **Pipeline de tiles/EHB** (cuantizar antes de extraer, comparar al 100 %, etc.) | `docs/guides/roadmap/REGLAS_PIPELINE_TILES.md` |
| **Motores de tablero (ajedrez/Go), footprint 20 kB–1 MB y conocimiento en disquete** | `docs/engine/architecture/BOARD_GAME_AI.md` + `docs/guides/roadmap/ROADMAP_BOARD_GAMES.md` |
| **Concurrencia y portabilidad multinúcleo (hilos/mutex/atómicos)** | `docs/engine/architecture/PARALLEL_AND_THREADS.md` |
| **Mini-SO de mensajes y UI reactiva** (`eng::os`/`eng::ui`: puerto, prioridad, VBlank latched, entrada por registros, timers, E/S asíncrona, tareas de fondo) | `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` (+ `MINI_OS_INPUT.md`, `MINI_OS_TIME.md`, `MINI_OS_IO.md`, `MINI_OS_TASKS.md`, `GUI_LIBRARY.md`) |
| **Audio** (`eng::audio`: modos/canales, mixer, música P61/pt/OctaMED, streaming desde disquete y codecs) | `docs/engine/architecture/GAME_AUDIO.md` (+ `AUDIO_MIXER.md`, `MUSIC_PLAYER.md`, `AUDIO_STREAMING.md`, `ROADMAP_AUDIO.md`) |
| **Caché de assets y código dinámico** (`eng::res`: LRU/prioridad/refcount, presupuestos Chip/Fast, DynLoader `.englib`) | `docs/engine/architecture/RESOURCE_SYSTEM.md` (+ `ROADMAP_RESOURCES.md`, `MINI_OS_IO.md`) |
| **Inventario de hardware** (`eng::hw`: CPU/FPU, chipset, RAM por tipo, display, puertos) | `docs/engine/architecture/HARDWARE_INVENTORY.md` |
| **Estado vigente y próximas direcciones del engine** | `docs/guides/roadmap/ROADMAP_UNIFICADO.md` |
| **Numeración de demos/tests entre ramas** (anti-solape: bloques reservados) | `docs/ai-dev-environment/NUMBERING.md` |
| **Bitácora de scroll por tiles (histórico)** | `docs/guides/roadmap/BITACORA_SCROLL_TILES.md` |

> Navegación general y protocolo de ingesta: `docs/ai-dev-environment/DOC-MAP-PRINCIPAL.md`.
> Índice maestro de la documentación: `docs/README.md`.
