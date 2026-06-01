# Vision Review

Herramienta ligera para pedir una segunda opinion visual a un modelo con vision
sobre pocos frames seleccionados. Esta carpeta contiene el contrato operativo; el
roadmap completo esta en `docs/VISION_REVIEW_ROADMAP.md`.

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
