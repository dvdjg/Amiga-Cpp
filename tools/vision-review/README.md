# Vision Review

Herramienta ligera para pedir una segunda opinion visual a un modelo con vision
sobre pocos frames seleccionados. Esta carpeta contiene el contrato operativo; el
roadmap completo está en `docs/testing/VISION_REVIEW_ROADMAP.md`.

> **Prompts y flujo híbrido**: contrato de los prompts (referencia+comparación, respuesta
> estructurada, regiones relativas, few-shot) y descripción del pipeline determinista→visión en
> [`PROMPTS.md`](PROMPTS.md).

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

**Cada demo declara lo que se espera ver de ella** (y, si aplica, oír); esa declaración es la que
se compara con lo que describe el modelo de visión. El chequeo genérico del overlay
(verde/amarillo/blanco) es solo **informativo**: muchas demos no dibujan overlay (audio, escenas
oscuras) y no debe ser un fallo. El gate duro es el **analizador propio** de la demo
(`analyze-screenshot.sh`) o su `pixel-contract`; si no hay ninguno, el veredicto visual lo da
`vision-points.json` comparado con Ollama.

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

## Frame-diff determinista (`frame-diff.mjs` / `frame-diff.py`)

Referencia directa para separar **movimiento** de **glitch**: cuenta los píxeles cambiados y su
bbox entre frames consecutivos. Hay dos implementaciones equivalentes:

```bash
node tools/vision-review/frame-diff.mjs --sequence <dir> [--thresh 40] [--json]   # JS (pngjs)
python tools/vision-review/frame-diff.py --sequence <dir> --metric both [--json]  # NumPy/OpenCV (SIMD)
```

La versión **Python** (`frame-diff.py`) es más rápida (operaciones de array con SIMD: SSE/AVX) y
añade **SSIM** por par (`--metric diff|ssim|both`): 1.0 = idéntico; `blocks_low` cuenta bloques con
SSIM bajo (cambio **estructural** real, no solo brillo). `flicker-check.mjs` prefiere la versión
Python si está disponible y cae a la de Node si no. Si una **zona estable** cambia erráticamente,
es glitch; si solo cambian las zonas que se desplazan, es movimiento. Ante discrepancia con el
modelo de visión, **prevalece el frame-diff**.

## Diff de buffers gráficos (`screendump-diff.mjs`, canal lateral)

Compara el **framebuffer real** (por planos) leído por el canal lateral de WinUAE-DBG
(`mem <addr> <len>`), sin pasar por el PNG escalado: comparación "buffer a buffer" para análisis
diferencial de pantalla.

```bash
node tools/vision-review/screendump-diff.mjs \
  --addr <hex-base> --planes 6 --row-bytes 40 --plane-bytes 10240 \
  --width 320 --height 256 [--gap-ms 500] [--side-port 2346]
```

Lee la base de bitplanes en dos momentos (A, B), decodifica cada plano y compara píxel a píxel;
informa píxeles cambiados, bbox, **por plano** y bloques calientes. Requiere una instancia viva
(canal lateral activo). Auto-test sin emulador: `selftest-screendump.mjs` (servidor TCP fake).

Con `--from-copper` deduce base y geometría de la **copperlist activa** (`COP1LC` → `BPL1PT`/`BPL1MOD`)
en lugar de recibirlas por argumento; el resto (`--planes`/`--width`/`--height`) se pasa a mano.

Validación en emulador real: `tools/debug/verify-side-channel-contract.sh` lanza una demo, espera
`side-channel READY`, cierra su cliente del canal lateral y ejecuta `screendump-diff --from-copper`,
comprobando que deduce la geometría (p. ej. `row_bytes 40`, `plane_stride 10240`, 6 planos) y produce
`screendump-diff.json`. Limitación conocida: si la copperlist de la demo no expone `BPL1PT` en su
tramo inicial legible (algunas la mueven tras `WAIT`/`COPJMP` o el volcado sale a cero), la base sale
`0x0`; en ese caso hay que pasar `--addr`/geometría explícitos.


## Detección temporal (`temporal-detect.py`, OpenCV)

Capa determinista de `flicker-check.mjs` (requiere `python` + `opencv-python` + `numpy`). Localiza
candidatos `flicker`/`tearing`/`corruption` por frame-diff + *optical flow* (Farneback) + bloques,
descartando el movimiento coherente de la escena.

```bash
python tools/vision-review/temporal-detect.py --sequence <dir> [--block 16] [--diff 30] [--flow 0.5]
```

Salida: `out/vision-review/<demoId>/temporal-detect.{json,md}`.

## Parpadeo / glitch (`flicker-check.mjs`) — enfoque **híbrido**

Los VLM fallan más en glitches **temporales** (parpadeo, flickering, objetos que aparecen/desaparecen)
que en glitches espaciales. Por eso el análisis de parpadeo tiene **dos capas**: una determinista
(barata y reproducible) que localiza la sospecha, y el modelo de visión **solo** para confirmarla o
descartarla. Así el modelo no busca a ciegas (menos alucinaciones).

```bash
node tools/vision-review/flicker-check.mjs --demo <ruta> [--frames 6] [--cells 16] [--top 4]
                                           [--no-ollama] [--no-detect]
```

1. **Capa 1 — detección temporal determinista** (`tools/vision-review/temporal-detect.py`, OpenCV):
   diferencia de frames + *optical flow* denso (Farneback) + análisis por bloques. Clasifica:
   `flicker` (cambia **sin** flujo: parpadeo/oscilación), `tearing` (cambio con flujo disperso),
   `corruption` (cambio muy alto). El **movimiento coherente** (objetos/scroll que se desplazan) se
   descarta comparándolo con el flujo de referencia de la escena. Ventana de contexto ±1 frame.
   Parámetros (por CLI): `--block` (8/16 px), `--diff` (umbral de cambio, 30 por defecto),
   `--flow` (umbral de flujo, 0.5 por defecto). Salida `temporal-detect.{json,md}`.
2. **Capa 2 — modelo de visión (Ollama)**, solo sobre la **región candidata** con frames de
   referencia+contexto. Respuesta **estructurada** (sí/no + tipo + **zona relativa**, sin píxeles +
   confianza + explicación breve). Modelo por defecto `OLLAMA_VL_MODEL` = `qwen3-vl:8b-instruct-q8_0`.

Informe: `out/vision-review/<demoId>/flicker-report.{json,md}` (candidatos + respuesta del modelo) y
`temporal-detect.{json,md}` (detalle). Integración: `tools/test-regression.sh --flicker` añade la
columna **Flicker** (`reported`/`skip`; descriptiva, no falla).

### Modelos de visión recomendados (Ollama)

| Modelo | Tamaño | Notas |
|---|---|---|
| **Qwen3-VL** (principal) | 8B / 32B | Mejor comprensión temporal/espacial; multi-imagen y vídeo; menos alucinaciones en "qué cambia entre frames". |
| Qwen2.5-VL | 7B / 32B | Alternativa sólida (vídeo largo, localización temporal). |
| Gemma 3 | 12B / 27B | Buena con varias imágenes en una consulta (comparar con referencia). |

Evitar **LLaVA clásico**: es el que más alucina coordenadas. Regla: **prohibir píxeles** en el prompt
y pedir **regiones relativas**; pasar siempre pares/tríos (referencia + actual + siguiente).

> Cautela: **ningún** VLM open-weight es 100 % fiable en glitches temporales finos (parpadeo de 1–2
> frames, tearing sutil, corrupción de copperlist). La referencia fiable es la **capa determinista**;
> la respuesta del modelo es **apoyo**, no veredicto. Ante discrepancia, prevalece el frame-diff.



