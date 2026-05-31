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

Cuando se conecte el proveedor local necesitaremos:

- URL base, por ejemplo `http://127.0.0.1:1234/v1`;
- nombre exacto del modelo cargado en LM Studio;
- si el modelo acepta varias imagenes por peticion o prefiere una hoja de
  contacto unica;
- timeout razonable.

El ejemplo esta en:

```text
tools/vision-review/providers/lmstudio.example.json
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
