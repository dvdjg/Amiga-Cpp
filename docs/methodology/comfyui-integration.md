# Integración ComfyUI (text-to-image)

MVP para usar ComfyUI (p. ej. flux2-dev-fp8) desde el proyecto. La **conexión es configurable** de forma similar al LLM (archivo de config o variables de entorno).

## Flux.2 (y modelos con loaders separados)

Si usas **Flux.2** (UNETLoader, CLIPLoader, VAELoader), el workflow por defecto del script **no vale**: usa `CheckpointLoaderSimple`, que es para SD/Flux.1. En ese caso:

1. En ComfyUI, monta tu workflow de Flux.2 (text-to-image).
2. **File → Export (API)** y guarda el JSON.
3. En `.cursor/comfyui.json` añade la ruta a ese archivo: `"workflowPath": "ruta/al/workflow_api.json"` (o usa la variable de entorno `COMFYUI_WORKFLOW_PATH`).

El script inyectará tu prompt en el primer nodo `CLIPTextEncode` del workflow y usará el nodo `SaveImage` que tenga.

## Configuración (como el LLM)

### Archivo de proyecto: `.cursor/comfyui.json`

```json
{
  "baseURL": "http://127.0.0.1:8000/",
  "checkpoint": "flux1-dev.safetensors",
  "workflowPath": "scripts/comfyui_flux2_api.json"
}
```

- **baseURL**: URL base del servidor ComfyUI (por defecto `http://127.0.0.1:8000/`).
- **checkpoint**: Para el workflow por defecto (SD/Flux.1), nombre del archivo en `ComfyUI/models/checkpoints/`. Si usas `workflowPath`, no hace falta.
- **workflowPath**: (opcional) Ruta a un JSON exportado con **File → Export (API)** en ComfyUI. Obligatorio para Flux.2 y otros workflows con loaders separados.

### Variables de entorno (override)

- **COMFYUI_BASE_URL**: Sustituye `baseURL` (ej. `http://192.168.1.10:8000/` si ComfyUI está en otro equipo).
- **COMFYUI_CHECKPOINT**: Sustituye el checkpoint.
- **COMFYUI_OUTPUT_DIR**: Carpeta donde guardar imágenes generadas (por defecto `out`).

Así puedes tener ComfyUI en otro equipo y apuntar con `COMFYUI_BASE_URL` sin tocar el repo.

## Comprobar acceso

```bash
node scripts/comfyui-client.mjs status
```

Si responde `"ok": true`, el servidor está accesible. Si no, comprueba que ComfyUI esté en marcha en la URL configurada.

## Generar una imagen

```bash
node scripts/comfyui-client.mjs gen "a red dragon on a mountain"
```

La imagen se guarda en `out/` (o en `COMFYUI_OUTPUT_DIR`) con el nombre que devuelve ComfyUI. La salida del script es JSON con `file` y `prompt_id`.

## Notas

- Sin `workflowPath`, el script usa un workflow mínimo para SD/Flux.1 (CheckpointLoaderSimple → CLIP → KSampler → VAE → SaveImage). Para Flux.2 usa `workflowPath` con un JSON exportado (File → Export API).
- Si el error es "Prompt outputs failed validation", suele ser que el checkpoint no existe en `checkpoints/` o que tu ComfyUI usa Flux.2 (loaders separados): en ese caso configura `workflowPath`.
