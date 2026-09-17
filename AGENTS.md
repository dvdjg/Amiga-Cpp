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
- **Escribir para un lector sin conocimiento histórico.** Los documentos de referencia describen el estado/objetivo **vigente**, no su evolución: no usar «antes/ahora/desaparece/hoy/ya no», no narrar el proceso ni las alternativas descartadas. Las notas de transición, decisiones descartadas, estado de fases y bitácoras van a `docs/debugging/` o `docs/guides/roadmap/`, nunca al documento de referencia. La misma regla aplica a los **comentarios del código**: describen el comportamiento actual, no la historia de sus cambios.

### 1.3 Política de documentación y referencias

- **Preservar e indexar solo la mejor información**: ante un documento, manual o fuente que duplique contenido ya cubierto, comparar la calidad de ambos y quedarse solo con la mejor versión (o sintetizar en un único documento). No mantener dos fuentes que digan lo mismo.
- Al incorporar una referencia externa (manual, repo, curso, ficha), comprobar primero si ya existe algo equivalente en `docs/` y decidir: sustituir si la nueva es superior, componer solo si aporta algo distinto sin repetir, o descartar si es inferior o duplicada. Documentar el resultado final, no el proceso.
- **No incluir metainformación de proceso en los documentos de referencia**: fechas de edición/limpieza, «OCR corregido», «actualizado en <fecha>» o decisiones de ingesta no van en el contenido de la referencia ni de su índice; van en el mensaje de commit o, si procede, en una bitácora separada (`docs/guides/roadmap/`, `docs/debugging/`).
- Los índices y README de `docs/` deben apuntar solo a lo que existe y es canónico; si se elimina un documento, actualizar todos los enlaces en la misma pasada.
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

- Por defecto, **al comenzar un turno, hacer commit de lo que quedó del turno anterior** (solo lo hecho en el hilo actual).
- **No** hacer el commit final de lo desarrollado en el propio turno: el trabajo del turno en curso se deja sin commitear salvo que el usuario lo pida.
- No incluir en ese commit cambios ajenos al hilo actual.

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
| **WinUAE: instancias múltiples y depuración avanzada (GDB/canal lateral/MCP)** | `docs/debugging/DEBUG-WINUAE-V2-GUIDE.md` |
| **Pipeline de tiles/EHB** (cuantizar antes de extraer, comparar al 100 %, etc.) | `docs/guides/roadmap/REGLAS_PIPELINE_TILES.md` |
| **Estado vigente y próximas direcciones del engine** | `docs/guides/roadmap/ROADMAP_UNIFICADO.md` |
| **Bitácora de scroll por tiles (histórico)** | `docs/guides/roadmap/BITACORA_SCROLL_TILES.md` |

> Navegación general y protocolo de ingesta: `docs/ai-dev-environment/DOC-MAP-PRINCIPAL.md`.
> Índice maestro de la documentación: `docs/README.md`.
