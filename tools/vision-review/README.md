# Vision Review

Herramienta ligera para pedir una segunda opinion visual a un modelo con vision
sobre pocos frames seleccionados. Esta carpeta contiene el contrato operativo; el
roadmap completo está en `docs/testing/VISION_REVIEW_ROADMAP.md`.

## Objetivo

FrameScope y los scripts deterministas deciden donde mirar. Vision Review prepara
un paquete pequeno de evidencia y pregunta a un modelo local o remoto si lo que se
ve encaja con una hipotesis concreta.

Ejemplo de hipotesis:

- scroll horizontal continuo alrededor de un cruce de 16 pixels;
- ausencia de tile-pop dentro del area visible;
- animacion de sprite avanzando sin congelarse;
- diferencia visual esperada entre cuatro capturas.

## Flujo previsto

1. Capturar secuencia con `tools/run/run-demo.ps1`.
2. Analizarla con FrameScope.
3. Elegir 4-6 frames relevantes manual o automaticamente.
4. Enviar esos frames a un modelo con vision usando un prompt de `prompts/`.
5. Guardar `vision-review-report.json` y `vision-review-summary.md`.

## Parametros que faltan de LM Studio

Proveedor local probado:

- URL base: `http://legion:1234/`;
- modelo: `qwen2.5-vl-7b-instruct`;
- API: OpenAI-compatible, normalizada internamente a `/v1`;
- modo recomendado: `multi-image`.

Los proveedores de ejemplo estan en:

```text
tools/vision-review/providers/lmstudio.example.json
tools/vision-review/providers/lmstudio.legion.json
```

## Perfiles iniciales

- `amiga-scroll-transition`: comprueba continuidad alrededor de un cruce coarse de
  scroll Amiga.
- `generic-frame-diff`: compara diferencias visibles generales.
- `sprite-animation`: revisa poses y continuidad de animacion de un sprite.

## Salida esperada

La respuesta del modelo debe ser JSON estricto. Los prompts ya incluyen esquemas
minimos. El script debe guardar tambien la respuesta bruta para poder auditar
fallos del modelo.

## Siguiente implementacion

Primero se implementara modo offline:

```powershell
.\tools\vision-review\vision-review.ps1 `
  -Source .\out\run\101_ehb_tile_scroll_driver\sequence `
  -Frames 3,4,5,6 `
  -Profile amiga-scroll-transition `
  -OutDir .\out\vision-review\101_scroll
```

Ese modo debe crear un paquete revisable sin llamar a ningun proveedor. Despues se
anadira `-Provider` para LM Studio u otros backends OpenAI-compatible.

## Modo offline actual

El modo offline ya genera:

- `frames/`: copias de las imagenes seleccionadas;
- `contact-sheet.png`: hoja de contacto con indices y telemetria si existe;
- `request.json`: paquete machine-readable para enviar al proveedor;
- `request.md`: prompt y contexto legible para revision manual.

Seleccion manual:

```powershell
.\tools\vision-review\vision-review.ps1 `
  -Source .\out\run\101_ehb_tile_scroll_driver\sequence `
  -Frames 3,4,5,6 `
  -Profile amiga-scroll-transition `
  -OutDir .\out\vision-review\101_manual
```

Seleccion automatica para scroll Amiga:

```powershell
.\tools\vision-review\vision-review.ps1 `
  -Source .\out\run\101_ehb_tile_scroll_driver\sequence `
  -RunReport .\out\run\101_ehb_tile_scroll_driver\run-report.json `
  -FrameScopeReport .\out\framescope\101_ehb_tile_scroll_driver\framescope-report.json `
  -Profile amiga-scroll-transition `
  -OutDir .\out\vision-review\101_auto
```

Revision con LM Studio:

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

`multi-image` envia cada frame como imagen independiente. En las pruebas con
`qwen2.5-vl-7b-instruct` detecto correctamente un defecto sintetico de tile-pop.
`contact-sheet` queda como fallback para modelos que no acepten varias imagenes,
pero puede perder defectos pequenos al reducir la secuencia a una sola hoja.

## Integracion opcional en la demo 101

La prueba temporal de la demo 101 puede invocar Vision Review cuando se pida:

```powershell
.\demos\techniques\amiga\playfield\101_ehb_tile_scroll_driver\analyze-sequence.ps1 `
  -Warp `
  -RequireVisionReviewOk
```

Tambien se puede activar desde la regresion:

```powershell
.\tools\test-regression.ps1 `
  -Demo demos\techniques\amiga\playfield\101_ehb_tile_scroll_driver `
  -Warp `
  -RequireVisionReviewOk
```

Sin `-VisionReview` ni `-RequireVisionReviewOk`, la regresion normal no llama al
modelo local. Esto mantiene rapido y estable el pipeline base.

## Frames esenciales (`vision-points.json`)

Para complementar los checks deterministas, cada demo puede declarar los **frames
esenciales** (los puntos con un cambio interno importante, no necesariamente los
primeros) y qué debe verse en ellos. Si Ollama está disponible, un modelo de visión
los describe y se compara con lo declarado.

`<demo>/vision-points.json`:

```json
{
  "model": "qwen3-vl:8b-instruct-q8_0",
  "points": [
    { "name": "cruce de tile (columna entrante)",
      "index": 16,
      "expect": "escena de tiles a color, llena; sin banda vertical negra en el borde derecho" }
  ]
}
```

- `index` (0-based) es el frame de la **secuencia capturada** por
  `analyze-sequence.sh` (`out/run/<demoId>/<config>/sequence/frame_NNN.png`). También
  se admite `frames: [i, j, …]` para enviar varios (una transición como ventana).
- `expect` es la descripción que el modelo debe confirmar.

**Selectores** (en vez de un índice fijo) para localizar el frame de interés:

- `last: true` — el último frame (p. ej. fin de una ruta).
- `every: N` — muestreo periódico (cambios que conmutan cada N frames).
- `max_diff: true` — el frame con mayor cambio de píxeles respecto al anterior
  (transición: aparece/desaparece algo, cambia una figura de sitio).

Como los frames de interés **dependen de cada demo**, un análisis asistido los propone:

```bash
node tools/vision-review/essential-frames.mjs --demo <ruta> --suggest
```

Imprime el último frame, los **picos de cambio** (diff por frame, vía `pngjs`) y un posible
periodo; el autor elige y los declara en `vision-points.json`. Los cambios de **geometría**
(mode switch) se marcan con diff `999`.

Herramienta: `tools/vision-review/essential-frames.mjs --demo <ruta>` (informe en
`out/vision-review/<demoId>/essential-frames.md`). Códigos de salida: `0` = coincide,
`3` = se omite (sin Ollama/secuencia/puntos), `4` = algún MISMATCH (informativo),
`1` = MISMATCH con `--require-ok`.

Integración en la regresión: si la demo tiene `vision-points.json` y Ollama responde,
`tools/test-regression.sh` añade la columna **Vision** (ok / skip / mismatch / fail).
`--require-essential-ok` convierte un MISMATCH en fallo; `--skip-essential` lo desactiva.

## Parpadeo / glitch (`flicker-check.mjs`)

Analiza **frames consecutivos** para detectar parpadeo o glitches (bandas que destellan, tiles
que saltan, bordes que aparecen/desaparecen) y produce un informe accionable:

```bash
node tools/vision-review/flicker-check.mjs --demo <ruta> [--frames 6] [--cells 16] [--top 4]
```

1. **Determinista**: rejilla de celdas; para cada celda mide la **oscilación temporal** de
   luminancia (`media |L[f+1]-L[f]|`). Las celdas más inestables son candidatas (una zona que
   debería ser estable y cambia cada frame es sospechosa).
2. **Modelo de visión** (si Ollama está disponible): mira los frames consecutivos de la peor
   zona (ventana donde más cambia) y describe el patrón.

Informe: `out/vision-review/<demoId>/flicker-report.{json,md}` con la zona (`x,y,w,h`), su
oscilación, la ventana de frames analizada y la descripción del modelo → **dónde mirar** para
arreglar la demo (copper/blitter/punteros de planos) y, si el defecto es del engine, el engine.

Integración: `tools/test-regression.sh --flicker` añade la columna **Flicker** (`reported`/`skip`;
descriptiva, no falla). Es opt-in por el coste del modelo.


