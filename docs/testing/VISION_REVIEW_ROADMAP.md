# Vision Review: roadmap de inspeccion visual asistida por IA

`Vision Review` es una herramienta pequena y reutilizable para pedir una segunda
opinion visual a un modelo con vision sobre pocos frames bien elegidos. No intenta
describir videos completos ni sustituir a FrameScope. Su trabajo es responder
preguntas concretas con evidencia compacta:

- "Entre estos frames, el scroll parece continuo?"
- "Aparece algun tile dentro del area visible cuando no deberia?"
- "Hay salto, tearing, cambio de paleta inesperado o artefacto?"
- "La animacion del sprite cambia como se esperaba?"
- "Las cuatro capturas son coherentes con la hipotesis de la prueba?"

La herramienta debe poder usarse en pipeline, pero tambien como inspector manual
cuando una demo falle de una forma dificil de diagnosticar solo con metricas.

## Ambito

Incluido:

- seleccionar o recibir frames concretos desde una carpeta de capturas;
- crear un paquete de revision con imagenes, hoja de contacto, prompt y metadatos;
- consultar modelos locales o remotos mediante proveedores configurables;
- generar una respuesta estructurada `vision-review-report.json`;
- guardar un resumen Markdown legible;
- integrarse con FrameScope y `run-report.json` cuando existan;
- empezar con perfiles concretos para juegos retro y demos Amiga.

Fuera del MVP:

- analizar todos los frames de un video largo;
- construir un tracker universal de entidades;
- entender audio;
- entrenar modelos;
- convertir esto en un producto interactivo con UI.

OpenCV entra **solo** en la capa determinista de analisis de parpadeo (§Analisis de parpadeo:
enfoque hibrido), como dependencia **opcional** con fallback a la rejilla de luminancia; no es
obligatoria para el resto del pipeline.

## Relacion con FrameScope

FrameScope sigue siendo el primer filtro:

1. captura o recibe secuencias;
2. mide diferencias, movimiento y segmentos;
3. detecta mismatches baratos;
4. produce una hoja de contacto y un JSON compacto.

Vision Review se activa despues, solo sobre puntos interesantes. Puede usar:

- `framescope-report.json` para escoger pares problematicos;
- `run-report.json` para localizar cambios de camara, coarse scroll o eventos;
- una lista manual de indices de frame;
- un perfil que sepa buscar condiciones concretas.

OpenCV u otras librerias solo entraran si resuelven una necesidad muy acotada:
recortar viewport, generar crops, hacer overlays o calcular diferencias simples.
La descripcion semantica queda para el modelo con vision.

## Analisis de parpadeo: enfoque hibrido (tres capas)

Los VLM fallan mas en glitches **temporales** (parpadeo, flickering, objetos que aparecen y
desaparecen) que en glitches espaciales, y ademas **alucinan** coordenadas si se les pide buscarlos
a ciegas. Por eso el analisis de parpadeo se organiza en **tres capas**, de mas a menos determinista:

```text
secuencia PNG
   |-- 1. frame-diff.mjs      px cambiados + bbox por par (referencia directa movimiento/glitch)
   |-- 2. temporal-detect.py  candidatos flicker/tearing/corruption (OpenCV: oscilacion A-B-A +
   |                          optical flow + bloques; descarta el movimiento coherente)
   \-- 3. flicker-check.mjs   VLM SOLO sobre la region candidata con frames de referencia+contexto,
                              respuesta estructurada y regiones relativas (sin pixeles)
```

1. **`frame-diff.mjs`** (determinista, sin dependencias): cuenta pixeles cambiados y su bbox entre
   frames consecutivos. Una zona **estable** que cambia erradicamente es glitch; si solo cambian las
   zonas que se desplazan, es movimiento. Es lo que **prevalece** ante una respuesta dudosa del VLM.
2. **`temporal-detect.py`** (determinista, OpenCV opcional): criterio primario **oscilacion A-B-A**
   (un pixel que en `f-1` y `f+1` vale igual pero en `f` no = parpadeo), *optical flow* (Farneback)
   como clasificador secundario de `tearing`/`corruption`, analisis por bloques con solape y un
   *matcher* de vecindad que descarta los bloques desplazados. Salida:
   `out/vision-review/<demoId>/temporal-detect.{json,md}`.
3. **`flicker-check.mjs`** (hibrido): ejecuta las dos capas deterministas y envia al modelo de vision
   **solo** la region candidata con frames de referencia+contexto, con prompt **estructurado**
   (si/no + tipo + zona relativa + confianza) y **prohibicion de pixeles**. Si no hay candidatos, usa
   la rejilla de luminancia como fallback. Salida: `flicker-report.{json,md}`.

Contrato de los prompts (referencia+comparacion, respuesta estructurada, regiones relativas,
few-shot) y diagrama del pipeline: `tools/vision-review/PROMPTS.md`. Modelo recomendado:
**Qwen3-VL**; evitar LLaVA clasico. La capa determinista es la referencia; el modelo es **apoyo**.
Descripcion de las herramientas: `tools/vision-review/README.md`.

### Gating en la regresion

`tools/test-regression.sh --flicker` anade la columna **Flicker** (`ok` sin candidatos,
`candidates` con zonas sospechosas, `skip` sin secuencia/detector). Una demo declara su nivel
aceptado con `flicker-baseline.json` (`{"max_candidates": N}`): si el numero de candidatos lo
supera, la demo **falla** aunque no se pase `--require-flicker-ok`; con `max_candidates: 0` declara
que es estable (sin glitch temporal). `--require-flicker-ok` convierte `candidates` en fallo para
todas las demos del barrido.

### Calibracion y auto-test

`tools/vision-review/selftest-temporal.mjs` genera secuencias **sinteticas** con artefactos
conocidos (`make-synth-seq.mjs`: parpadeo, corrupcion, banda movil) y comprueba que el detector
distingue glitch de movimiento (parpadeo/corrupcion detectados; banda movil ignorada). Se ejecuta en
`tools/run-host-tests.sh` y se **omite** (no falla) si no hay OpenCV/Python. Dependencia opcional:
`pip install opencv-python numpy` (ver `docs/build/BUILD_AND_RUN.md`).

## Estructura propuesta

```text
tools/vision-review/
  vision-review.ps1              # wrapper PowerShell para uso diario
  vision-review.mjs              # nucleo CLI y proveedores
  prompts/
    amiga-scroll-transition.md
    generic-frame-diff.md
    sprite-animation.md
  providers/
    lmstudio.example.json
    openai.example.json
```

Los archivos reales de credenciales o endpoints locales no se versionaran si
contienen secretos. Los ejemplos si deben quedar en Git para reconstruir el flujo.

## Entrada CLI prevista

Revision con frames elegidos automaticamente:

```powershell
.\tools\vision-review\vision-review.ps1 `
  -Source .\out\run\101_ehb_tile_scroll_driver\sequence `
  -RunReport .\out\run\101_ehb_tile_scroll_driver\run-report.json `
  -FrameScopeReport .\out\framescope\101_ehb_tile_scroll_driver\framescope-report.json `
  -Profile amiga-scroll-transition `
  -Provider .\tools\vision-review\providers\lmstudio.local.json `
  -OutDir .\out\vision-review\101_scroll
```

Revision manual de frames:

```powershell
.\tools\vision-review\vision-review.ps1 `
  -Source .\out\run\101_ehb_tile_scroll_driver\sequence `
  -Frames 3,4,5,6 `
  -Profile generic-frame-diff `
  -Provider .\tools\vision-review\providers\lmstudio.local.json
```

## Proveedores

El contrato de proveedor debe ser deliberadamente simple:

```json
{
  "name": "lmstudio-local",
  "kind": "openai-compatible",
  "baseUrl": "http://127.0.0.1:1234/v1",
  "apiKey": "lm-studio",
  "model": "qwen2.5-vl-7b-instruct",
  "timeoutMs": 60000
}
```

Para modelos en la nube se usara el mismo contrato `openai-compatible` cuando sea
posible. Si un proveedor necesita formato especial, se anadira una rama pequena
en `vision-review.mjs`, no una abstraccion enorme.

## Respuesta esperada

El modelo debe responder en JSON estricto. Cada perfil define su esquema minimo.
Para `amiga-scroll-transition`:

```json
{
  "status": "ok|suspect|fail",
  "observedMotion": "left|right|up|down|diagonal|static|unclear",
  "continuity": "continuous|small_jump|large_jump|unclear",
  "visibleTilePop": true,
  "paletteChange": false,
  "unexpectedArtifacts": [
    {
      "frame": 2,
      "description": "Tile vertical aparece dentro del area visible",
      "severity": "minor|major"
    }
  ],
  "frameByFrameNotes": [
    {"frame": 0, "note": "Estado previo al cruce coarse."}
  ],
  "confidence": 0.0
}
```

La herramienta guardara tambien la respuesta bruta del modelo para auditoria.

## Perfil inicial: `amiga-scroll-transition`

Objetivo: revisar pocos frames alrededor de un punto donde el scroll cruza un
limite de 16 pixels y el driver cambia la parte coarse de los punteros de
bitplane.

Seleccion automatica inicial:

- leer `run-report.json`;
- usar `runStatus.cameraX`;
- buscar frames capturados donde `cameraX % 16` este cerca de `14, 15, 0, 1`;
- si FrameScope reporta mismatch, priorizar ese par;
- escoger dos frames antes y dos despues del punto elegido;
- generar una hoja de contacto ordenada y enviar tambien las imagenes originales.

Pregunta al modelo:

- si el contenido se desplaza en la direccion esperada;
- si hay continuidad visual entre los frames centrales;
- si aparece contenido nuevo dentro del area visible de forma brusca;
- si hay tearing, salto, corrupcion planar, cambio de paleta o borde raro;
- si las diferencias observadas parecen propias de scroll normal.

Aceptacion:

- `status = ok`;
- `continuity = continuous`;
- `visibleTilePop = false`;
- no hay artefactos `major`;
- confianza minima configurable, por ejemplo `0.65`.

## Fases

### Fase 0: documentacion y contrato

Estado: este documento.

Aceptacion:

- roadmap escrito;
- prompts iniciales definidos;
- ejemplos de proveedor preparados;
- alcance limitado y claro.

### Fase 1: paquete offline

Objetivo: generar evidencia sin llamar todavia a ninguna IA.

Estado: implementado.

Tareas:

- aceptar `-Source`, `-Frames`, `-OutDir`, `-Profile`;
- copiar los frames elegidos al paquete;
- crear `contact-sheet.png`;
- crear `request.json` con prompt, rutas de imagen y metadatos;
- crear `request.md` para inspeccion humana.

Aceptacion:

- dado `-Frames 3,4,5,6`, se genera un paquete reproducible;
- no requiere credenciales ni red;
- puede verse y entenderse manualmente.

Comandos verificados:

```powershell
.\tools\vision-review\vision-review.ps1 `
  -Source .\out\run\101_ehb_tile_scroll_driver\sequence `
  -Frames 3,4,5,6 `
  -Profile amiga-scroll-transition `
  -OutDir .\out\vision-review\101_manual

.\tools\vision-review\vision-review.ps1 `
  -Source .\out\run\101_ehb_tile_scroll_driver\sequence `
  -RunReport .\out\run\101_ehb_tile_scroll_driver\run-report.json `
  -FrameScopeReport .\out\framescope\101_ehb_tile_scroll_driver\framescope-report.json `
  -Profile amiga-scroll-transition `
  -OutDir .\out\vision-review\101_auto
```

La salida de ejemplo queda en `out\vision-review\101_auto`: frames copiados,
`request.json`, `request.md`, `vision-review-summary.json` y `contact-sheet.png`.

### Fase 2: proveedor OpenAI-compatible

Objetivo: llamar a LM Studio local o a un proveedor cloud con API compatible.

Estado: implementado para proveedores OpenAI-compatible.

Tareas:

- leer JSON de proveedor;
- enviar prompt + imagenes;
- guardar `raw-response.json`;
- extraer JSON estructurado a `vision-review-report.json`;
- fallar si el modelo no devuelve JSON valido.

Aceptacion:

- funciona con LM Studio usando `http://legion:1234/` y modelo
  `qwen2.5-vl-7b-instruct`;
- no asume un modelo concreto;
- los timeouts son configurables;
- soporta `multi-image` y `contact-sheet`.

Resultado de pruebas iniciales:

- `contact-sheet` produjo JSON valido y reconocio continuidad en la demo 101, pero
  no detecto un defecto sintetico de tile-pop dibujado sobre un frame. Se mantiene
  como fallback para modelos que no acepten varias imagenes.
- `multi-image` produjo JSON valido, acepto cuatro imagenes simultaneas y detecto
  correctamente el defecto sintetico con `visibleTilePop=true` y artefacto major.
  Por tanto queda como modo recomendado para LM Studio/Qwen.
- El primer prompt `multi-image` fue demasiado estricto con frames muestreados y
  marco como salto una separacion normal entre capturas. Se ajusto el prompt para
  aclarar que los frames pueden estar separados por varios frames de juego y que
  solo debe fallar por discontinuidad estructural, tile-pop o corrupcion. Tras ese
  ajuste, la demo real paso con `status=ok` y el defecto sintetico siguio
  detectandose como `suspect` con `visibleTilePop=true`.

Comando verificado:

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

### Fase 3: seleccion automatica para demo 101

Objetivo: escoger frames interesantes para scroll Amiga.

Tareas:

- leer `run-report.json`;
- leer opcionalmente `framescope-report.json`;
- seleccionar frames alrededor de cruce coarse X o mismatch;
- explicar en `request.md` por que eligio esos frames.

Aceptacion:

- sobre la demo 101 elige 4-6 frames utiles;
- la seleccion queda documentada;
- puede ejecutarse desde `analyze-sequence.ps1` como paso opcional.

### Fase 4: integracion prudente en regresion

Objetivo: permitir validacion IA sin hacer fragil toda la regresion.

Estado: implementado para `101_ehb_tile_scroll_driver`.

Tareas:

- modo opcional: `-VisionReview` genera informe;
- modo estricto: `-RequireVisionReviewOk` falla si `status != ok`, hay artefactos
  major o la confianza queda por debajo del umbral;
- guardar el informe dentro de `out/vision-review/...`;
- enlazar el informe desde la documentacion de la demo.

Aceptacion:

- por defecto no bloquea el pipeline local;
- cuando se active explicitamente puede actuar como verificador externo.

Comandos:

```powershell
.\demos\techniques\amiga\playfield\101_ehb_tile_scroll_driver\analyze-sequence.ps1 `
  -Warp `
  -RequireVisionReviewOk

.\tools\test-regression.ps1 `
  -Demo demos\techniques\amiga\playfield\101_ehb_tile_scroll_driver `
  -Warp `
  -RequireVisionReviewOk
```

La regresion normal sin esos flags sigue sin llamar al modelo.

## Riesgos

- Los VLM pueden inventar o exagerar diferencias. Por eso siempre se les dara una
  pregunta cerrada, pocos frames y un esquema de respuesta.
- La salida puede variar entre modelos. Por eso el dictamen se usara como segunda
  opinion, no como unica fuente de verdad.
- Los modelos locales pueden no soportar varias imagenes en una sola peticion. El
  proveedor debe permitir fallback a hoja de contacto unica.
- Si se usa nube, las imagenes salen del equipo. Debe ser una decision explicita
  via proveedor, nunca el comportamiento por defecto.

## Reglas para assets de prueba

Las demos que dependan de Vision Review deben usar, cuando sea razonable, assets
faciles de reconocer:

- tiles con bordes claros;
- colores de alto contraste;
- letras, numeros o simbolos simples;
- marcadores de variante;
- formas grandes y no puramente texturales;
- defectos sinteticos que cubran regiones visibles y sean explicables por el
  prompt.

Esto no significa que el engine final tenga que usar arte "de test". Significa que
las demos de infraestructura deben tener señales visuales que una IA pueda
describir con precision. La demo `101_ehb_tile_scroll_driver` ya usa tiles
simbolicos con glifos `0..F` para validar scroll, tile-pop y corrupcion local.

## Siguiente paso

La base de Vision Review (paquete offline, proveedor OpenAI-compatible, seleccion automatica y
gating) esta implementada. El estado vigente y las tres capas del analisis de parpadeo estan en
§Analisis de parpadeo: enfoque hibrido. Proximos pasos: few-shot de pares bueno/malo para demos con
parpadeo conocido, y extender los `flicker-baseline.json` a las demos declaradas estables.
