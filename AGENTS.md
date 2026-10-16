# AGENTS

Reglas operativas para agentes IA en este repositorio. Este documento contiene las
**reglas generales** (aplican a toda tarea) y un **índice que enruta a las reglas
específicas de cada dominio**, que viven en su documento canónico para no cargar de
contexto irrelevante a quien trabaja en otra cosa.

## Cómo usar este documento (orden de lectura único)

1. Leer este documento completo (reglas generales §1 y operación §3) y el mapa «voy a hacer X → documentación»: `docs/ai-dev-environment/DOC-MAP-PRINCIPAL.md` (su §3 es el router **completo** de tareas, con las demos/APIs de referencia; §4, efecto → demo/API; §5, referencia de hardware).
2. Localizar la tarea en el **router rápido** de abajo y leer los documentos indicados **antes** de escribir código; si la tarea cae en un dominio con regla propia, leer su documento (§4).
3. Documentar en el sitio canónico: destino de un documento nuevo, `docs/README.md`; hallazgo por tema, `DOC-MAP-PRINCIPAL.md` §3; regla de dominio, §4.
4. Al cerrar, repasar el **checklist de cierre de turno** (§5).

Este es el **único orden de lectura de arranque**: cualquier otro documento que mencione «lectura inicial» remite aquí.

### Router rápido (tareas más frecuentes)

| Tarea | Leer antes |
|---|---|
| **Crear o arreglar una demo/juego** (procedimiento completo, fases F0–F5 y gates) | `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md` |
| **Validación visual / demos atractivas** (visión Ollama, secuencias, gate de render) | `docs/guides/methodology/DEMO_VISUAL_DEBUG.md` |
| **Bug visual** (render que no cuadra sin causa de hardware clara) | `DEMO_VISUAL_DEBUG.md` + §1.5 (evidencia y alcance) |
| **Hardware no cuadra** (Copper, sprites, Blitter, DMA, colisión, timers) | §1.12 y `docs/reference/emulators/winuae/README.md` — **PARAR antes de experimentar** |
| **Optimizar o escribir código nuevo** (todo código tiene impacto en rendimiento) | `docs/guides/optimization/METODO_OPTIMIZACION.md` + §1.15 |
| **Escribir/tocar API pública o lógica de demo** | `docs/engine/architecture/PUBLIC_API.md` + `CODING_STYLE.md` + §1.10/§1.11 |
| **Tests y verificación por demo** | `docs/testing/README.md` |
| **Coherencia de setup** (BPLCON0, regiones, WAITs, validadores) | `docs/engine/architecture/COHERENCIA_DE_SETUP.md` |
| **Efecto demoscene nuevo / ingesta de repo externo** | `DOC-MAP-PRINCIPAL.md` §4 y §6 + `docs/demos/effects/DEMOSCENE_EFFECT_REPLICATION_POLICY.md` |
| **Scroll/tiles/playfield, sprites/objetos, audio, mini-SO, GUI, recursos** | `DOC-MAP-PRINCIPAL.md` §3 (fila del dominio, con demos/APIs) |
| **Documentar un hallazgo o investigación** | §1.3 (indexar en README de carpeta + DOC-MAP §3) y §1.12 si es hardware |

---

## 0. Regla de oro (obligatoria)

- **Usar SIEMPRE Ollama (modelo de visión local) para verificar que el resultado se corresponde
  con lo que se pretende.** No basta con que compile, pase tests host, dé los fps esperados o
  produzca una captura estática: hay que **mirar la salida con el modelo de visión** y comprobar que
  es lo que se buscaba (imagen correcta, movimiento suave, sin artefactos/huecos). Una captura única
  **no** muestra el movimiento: para demos animadas, capturar **secuencia** y validar la dinámica.
  **Sin límite**: el modelo es local y gratis; ante la duda, otra pregunta, otra pasada o más frames.
- Se aplica a toda demo/efecto/imagen: antes de dar algo por bueno, pasarlo por Ollama. Si el
  resultado no se corresponde con la intención, está **mal** aunque el resto pase.
- Procedimiento y herramientas: [`DEMO_VISUAL_DEBUG.md`](docs/guides/methodology/DEMO_VISUAL_DEBUG.md).
- **Procedimiento completo para crear y arreglar demos/juegos**: [`PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`](docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md) (fases F0–F5, gates, presupuesto 50 fps, reglas anti-basura y registro de errores del engine ajenos al turno). Instrucciones exactas de visión: `DEMO_VISUAL_DEBUG.md` §6.4.
- **Comportamiento de chipset que no cuadra → PARAR y leer la fuente ANTES de tocar nada.** Ante
  cualquier mecanismo de hardware que no se comporte como se espera (Copper, sprites, Blitter, DMA,
  colisión, timers…), **no experimentar a ciegas ni por prueba y error**: leer primero la
  implementación del emulador (`../WinUAE-DBG/`, fichero según el síntoma) y el AHRM, y **citar
  `fichero:línea`** en el comentario del código y en el commit. Un hallazgo de hardware **sin
  `file:line` está incompleto**. Índice «síntoma → fichero» y fichas en
  [`docs/reference/emulators/winuae/`](docs/reference/emulators/winuae/README.md); detalle en §1.12.
- **Toda demo se versiona y lo muestra en pantalla** (norma desde 2026-10): versión
  `major.minor.build` (`eng/debug/version.hpp`; el build lo inyecta `build-demo.sh` con contador
  persistente `out/.build-number`). Cada demo **reserva la franja inferior** para mostrar
  **nombre + versión + información de depuración** (contador de frames, fps…). El HUD de
  depuración debe ser **lo más sencillo posible**: **un único bitplane** y texto impreso **por
  palabras** (`Surface::draw_text_words`, dos glifos de Font8 por palabra de 16 px; **nunca
  píxel a píxel**). Alternativa admitida: ventana de debug WinUAE con esos mismos datos.
  Referencia de implementación: demo 120 (`draw_hud`, HUD 1bpp); `eng::debug::VersionStrip`
  sigue siendo válido para franjas preexistentes multi-plano (065/066).

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
- **Todo documento de hallazgo o investigación queda indexado por tema en la misma pasada**, en dos niveles: (1) su README de carpeta (`docs/debugging/README.md`, `docs/reference/emulators/README.md`, …) con una fila descriptiva, y (2) si un programador lo buscaría por tema al tocar un dominio, una entrada en la tabla «tareas → documentación» de `docs/ai-dev-environment/DOC-MAP-PRINCIPAL.md` §3. Un hallazgo citado desde el código debe ser localizable por quien vaya a tocar ese código. El **nombre** del fichero es kebab-case, con prefijo `NNN_` solo si el doc es de una demo concreta; los documentos existentes con `MAYÚSCULAS_CON_GUIONES_BAJOS` conservan su nombre (son canónicos): la convención aplica a ficheros **nuevos** y no se renombra en masa. Se verifica con `node tools/check/doc-index.mjs` (áreas declaradas en `tools/check/doc-index-areas.txt`, sin tocar código; corre en `tools/run-host-tests.sh` y `tools/test-regression.sh`).
- **Los enlaces relativos de la documentación no deben quedar rotos**: `node tools/check/links.mjs` los valida y corre en `tools/test-regression.sh` y `tools/run-host-tests.sh`. La deuda histórica se acepta de forma explícita en `tools/check/links-baseline.txt`; regenerar con `--update-baseline` solo si se decide aceptar roturas nuevas.
- Protocolo detallado de ingesta de repos/referencias externas: §6 de `docs/ai-dev-environment/DOC-MAP-PRINCIPAL.md`.
- **Consultas a IA externa (Grok): preguntar SIEMPRE en inglés** (responde mejor en ese idioma). Si la consulta se redacta primero en castellano, guardar también la versión en inglés (`*-en.md`) y pasarle esa. Las consultas autocontenidas viven en `docs/debugging/investigaciones/consultas/` (`consulta-*.md`, con su README índice).

### 1.4 Encoding y finales de línea

- **Todos los archivos de texto del repo deben ser UTF-8 válido (sin mojibake).** Tildes, `ñ`, `—`, `→`, `≈`, `≤`, `∝`, `φ`, etc. deben quedar en UTF-8 correcto. Evitar añadir BOM.
- **NO editar archivos con `Get-Content`/`Set-Content` de PowerShell**: por defecto `Get-Content` lee UTF-8 y `Set-Content` escribe Windows-1252, lo que **re-encoda** el archivo y pierde los caracteres no representables (`→` → `?`, `≤` → `=`). Es la causa del mojibake (UTF-8 doblemente codificado) detectado en el repo.
  - Usar las herramientas de edición del agente (escriben UTF-8) o `sed -i` de Git Bash (preserva bytes).
  - Si hay que usar PowerShell, forzar `-Encoding utf8` en lectura **y** escritura (`Get-Content -Encoding utf8 … | Set-Content -Encoding utf8 …`) y verificar después con `node tools/check/encoding.mjs`.
- **Finales de línea LF**: el repo usa LF. `Set-Content` de PowerShell también convierte LF→CRLF (diff de archivo completo). No introducir CRLF en fuentes/docs nuevos; el único archivo CRLF admitido es el manual AHRM ingerido (`docs/reference/ahrm/`).
- **Check obligatorio en CI**: `node tools/check/encoding.mjs` falla si algún `.cpp/.hpp/.md/.mjs/.sh/…` no es UTF-8 válido o contiene mojibake. Ya está integrado en `tools/test-regression.sh` y en la pasada completa de `tools/run-host-tests.sh`. Para reparar mojibake: decodificar el run de no-ASCII como cp1252 → UTF-8 (reversible, varias pasadas), nunca reescribir el archivo entero.

### 1.5 Evidencia

- No afirmar que una funcionalidad funciona sin evidencia reproducible de esa funcionalidad concreta.
- **El alcance de la afirmación no puede exceder el de la prueba.** Enumerar los grados de libertad del sistema (planos, ejes, buffers, regiones, fases temporales, caminos de código) y decir **qué cubre la medición y qué no**; lo no cubierto queda **SIN VERIFICAR**, nunca «correcto». Ejemplo real: medir que «la franja central traslada verticalmente de forma uniforme» **no** autoriza a decir «el scroll es correcto» (no cubre sincronía entre planos, eje X, ni la zona de la ventana que el screenshot recorta). Prohibido extrapolar de un subconjunto al todo o de un caso favorable al caso general; al informar, usar «correcto **en el alcance probado** (…), sin verificar (…)».
- Distinguir siempre entre indicios, validación parcial y evidencia concluyente; una compilación, un test host o una imagen que cambia no prueban por sí solos continuidad visual ni corrección del hardware.
- Si faltan herramientas para observar el comportamiento real (por ejemplo, registros Copper efectivos, punteros BPL por frame o ciclos del Blitter), declararlo explícitamente y no presentar una hipótesis como resultado. Si una herramienta de observación da resultados no fiables (p. ej. la captura de ventana del MCP), **arreglar la herramienta** antes de seguir diagnosticando con ella.
- Probar primero el caso límite relevante y solo después documentar o afirmar que el cambio está resuelto.

### 1.6 Próximos pasos al final de cada turno

- Al cerrar cada turno, incluir una sección **«Próximos pasos recomendados»** con entre 3 y 5 propuestas concretas, ordenadas por prioridad y orientadas al objetivo activo.
- Si el trabajo pertenece a un roadmap, indicar el siguiente paso verificable de ese roadmap y su valor; no proponer tareas genéricas ni repetir pasos ya completados.
- Si el roadmap queda cerrado, decirlo explícitamente y proponer el roadmap o frente de trabajo siguiente para que el usuario pueda elegir.
- Si hay un bloqueo que impide completar el objetivo, nombrarlo y proponer la acción mínima para resolverlo.

### 1.7 Buscar antes de implementar

- **Nunca implementar una utilidad o API del engine sin comprobar antes que no existe ya.** Antes de escribir `draw_text`, una fuente, un blit, un driver o cualquier ayuda reusable, buscar en `engine/include/`, `demos/` y `tools/` (grep por nombre y por concepto: «text», «font», «blit», «surface», «scene», «palette»…) y en los índices de `docs/` (`DOC-MAP-PRINCIPAL.md`, READMEs, roadmaps).
- Para **constantes, máscaras y valores mágicos**: `node tools/analyze/duplicate-constants.mjs` lista los valores ya declarados bajo **otro nombre** en otro fichero (evita duplicar; p. ej. cazó `kBlitterMintermCopyC`/`blt_minterm_copy_c` y `kMintermAOrB` en `blob.hpp`). Si ya existe, se **reutiliza o se aliasa**; no se redeclara.
- Si ya existe (incluso en una demo): **reutilizar, generalizar o subir al engine**, nunca duplicar. Una implementación local en una demo que sirve a otras debe promoverse a `engine/` como utilidad reutilizable.
- Antes de añadir un archivo nuevo en `engine/`, listar los helpers existentes del dominio y decidir explícitamente: ¿esto ya lo cubre `X`? ¿Puedo extender `X` en vez de crear `Y`?
- Aplica también a **fuentes, tablas y glifos**: buscar si el carácter/glifo ya está antes de redibujarlo.
- Un commit que añade algo que ya existía como duplicado se considera un error de proceso.
- **Gate mecánico (no basta la buena intención)**: `tools/check/prior-art.mjs` (suite) exige que toda demo/juego que use un
  patrón de bajo nivel ya abstraído por el engine (reglas en `tools/check/prior-art-rules.txt`, p. ej. dibujo de objetos con
  `planar_band.hpp`/`blob_batch.hpp`/`add_world_bitmap*` frente a `eng/scene/actor.hpp`) **declare la búsqueda previa** en su
  README bajo `## Búsqueda previa`, citando el patrón y al menos un símbolo candidato del engine (se verifica que exista).
  El baseline solo admite demos históricas y puede encoger, no crecer. Ejecútalo antes de dar por buena una demo:
  `node tools/check/prior-art.mjs --list`.