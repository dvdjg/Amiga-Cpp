# Ollama local para análisis visual

## Configuración comprobada

- Servicio: `http://127.0.0.1:11434`.
- Versión comprobada: `0.32.14` (verificado por `/api/version`).
- Modelo visual predeterminado: `qwen3-vl:8b-instruct-q8_0`.
- Modelo alternativo instalado: `gemma3:12b`.
- Endpoint compatible con el pipeline: `/v1/chat/completions` (comprobado en local con imagen `data:image/...;base64,...`).
- Endpoint de salud: `/api/version`.
- Modelos cargados: `/api/ps`.
- Modelos instalados (verificado por `/api/tags`): `qwen3:8b` (texto, pre-análisis `meta`),
  `qwen2.5-coder:7b`, `qwen3-vl:8b-instruct-q8_0` (visión) y `gemma3:12b` (visión, alternativa).
  En la API nativa de Ollama, `qwen3-vl` se reporta con familia `qwen3vl`, 8.8B parámetros,
  cuantización Q8_0, ~9,9 GB de VRAM y `context_length` 8192.

La configuración `tools/vision-review/providers/ollama.local.json` ya apunta a este proveedor. El script normaliza la URL a `/v1` y envía cada imagen como `data:image/...;base64,...` en modo `multi-image`.

## Decisión de modelo

Qwen3-VL queda como modelo predeterminado porque la documentación oficial de Ollama destaca comprensión de vídeo dinámico, contexto largo y relaciones espaciales, capacidades directamente útiles para secuencias de frames y perfiles de scroll. Gemma 3 12B también es multimodal y los experimentos de `D:/scripts` le atribuyen mejor clasificación semántica en imágenes estáticas; se conserva como alternativa para una captura aislada, pero no se cambia el proveedor principal.

La mención a una versión “Qwen 3.8” parece mezclar familias o tamaños. En la instalación local solo está la familia `qwen3vl` (nada de `qwen2.5vl`); el modelo disponible y probado es `qwen3-vl:8b-instruct-q8_0`, que Ollama identifica como familia `qwen3vl`.

## Patrón heredado de `D:/scripts`

`06_vision.py` y `16_clasificar_videos.py` establecen un patrón útil para este proyecto:

- usar `/api/generate` con `images: [base64]` cuando se quiere la API nativa;
- usar `format: json`, temperatura `0` y un límite de salida holgado;
- guardar resultados por unidad y poder reanudar sin reprocesar;
- reintentar si el JSON no se puede analizar;
- para vídeo, extraer frames con `ffmpeg`, no enviar el vídeo al VLM;
- supervisar con `/api/version` y `/api/ps`;
- separar el proceso de modelo de los workers y registrar progreso.

El batch de vídeo usa tres frames espaciados por vídeo, `num_predict=1200`, timeout de 290 segundos, tres reintentos con backoff y una conexión SQLite por worker. Guarda primero el resultado en la base de datos, después renombra y mueve el vídeo, y valida los temporales de FFmpeg antes de reemplazar el original. El watchdog y el panel observan progreso, procesos, GPU y `/api/ps`; esta separación es más sólida que tratar una captura aislada como única señal de éxito.

`vision-review.ts` usa la variante OpenAI-compatible porque permite reutilizar el mismo contrato con Ollama, LM Studio u otro backend. Para el engine Amiga se prefiere este flujo acotado: primero métricas deterministas, después 4--6 frames relevantes y finalmente el VLM. El pipeline no envía un vídeo crudo al modelo: extrae frames y conserva su orden/telemetría; esto es suficiente para analizar secuencias de scroll y animación, y evita depender de una API de vídeo específica.

## Operación segura

Los scripts del escritorio y `D:/scripts/tools/reiniciar_ollama.py` enseñan una regla esencial: al reiniciar hay que detener `ollama` y `llama-server` juntos, porque el hijo puede conservar VRAM y forzar una caída a CPU. No matar procesos globalmente durante una sesión WinUAE salvo que se haya decidido parar también el pipeline visual.

Comprobaciones manuales:

```powershell
Invoke-RestMethod http://127.0.0.1:11434/api/version
Invoke-RestMethod http://127.0.0.1:11434/api/tags
Invoke-RestMethod http://127.0.0.1:11434/api/ps
```

El panel existente es `C:/Python314/python.exe D:/scripts/tools/ollama_panel.py`. No es una dependencia del engine; solo sirve como observabilidad de GPU, procesos, cola y progreso.

## Operación local (arrancar, parar, cambiar modelo)

### Arrancar

- **Automático (recomendado):** la tarea programada `ollama_serve` arranca
  `ollama.exe serve` al iniciar sesión y lo hace en **ventana visible**
  (registrada sin el wrapper `run_hidden.vbs`). Comprobar primero que la tarea
  está **enabled** (`schtasks /query /tn ollama_serve`); si aparece
  *Deshabilitado* (estado verificado en este entorno), hay que habilitarla antes
  de poder lanzarla:
  ```powershell
  schtasks /change /tn ollama_serve /enable
  schtasks /run /tn ollama_serve
  ```
- **Manual:** una consola y a correr; idéntico al arranque de la tarea, la
  ventana permanece abierta mientras viva el servidor:
  ```powershell
  & "$env:LOCALAPPDATA\Programs\Ollama\ollama.exe" serve
  ```

### Parar

- Cerrar la ventana de consola de `ollama serve`, o matarlo directamente:
  ```powershell
  Get-Process ollama,llama-server -ErrorAction SilentlyContinue | Stop-Process -Force
  ```
- Al reiniciar hay que detener **`ollama` y `llama-server` juntos**: el hijo
  puede conservar VRAM y forzar una caída a CPU. Si se prefiere el reinicio
  limpio ya escrito (mata ambos y relanza la tarea `ollama_serve`):
  ```powershell
  python D:\scripts\tools\reiniciar_ollama.py
  ```

### Cambiar el modelo

- Ver los modelos instalados (tamaño y cuantización):
  ```powershell
  Invoke-RestMethod http://127.0.0.1:11434/api/tags
  ```
- Descargar un modelo nuevo si falta (p. ej. la alternativa de la doc):
  ```powershell
  & "$env:LOCALAPPDATA\Programs\Ollama\ollama.exe" pull gemma3:12b
  ```
- Seleccionarlo en los consumidores del pipeline:
  - **Vision Review:** editar `"model"` en
    `tools/vision-review/providers/ollama.local.json` (por defecto
    `qwen3-vl:8b-instruct-q8_0`), o crear otro provider apuntando a otro modelo.
    Ya existe `tools/vision-review/providers/ollama.local.gemma3.json` listo para
    `gemma3:12b`.
  - **Perfiles (`tools/profile/README.md`):** pasar `--model <modelo>` /
    `--text-model <modelo>` al orquestador/analizador, o usar las env
    `OLLAMA_MODEL` (visión), `OLLAMA_TEXT_MODEL` (texto para `--mode meta`) y
    `OLLAMA_BASE` (los tres se leen en `ollama-analyze.mjs`).
- No hace falta reiniciar el servidor para usar un modelo nuevo: Ollama lo carga
  bajo demanda y lo mantiene en RAM/VRAM según `OLLAMA_KEEP_ALIVE` (24h).

## Uso con Vision Review

```powershell
npm run build
bash .\tools\vision-review\vision-review.sh `
  --source .\out\run\101_ehb_tile_scroll_driver\sequence `
  --profile amiga-scroll-transition `
  --provider .\tools\vision-review\providers\ollama.local.json `
  --sendMode multi-image
```

La salida debe conservar `request.json`, frames seleccionados, respuesta bruta y el informe JSON/Markdown. Si Ollama no responde, el paquete offline sigue siendo válido y permite revisar o enviar la evidencia posteriormente.

## Rendimiento y contexto

El modelo predeterminado ocupa aproximadamente 9.8 GB en Q8_0. La experiencia de `D:/scripts` recomienda no aumentar contexto y concurrencia sin medir VRAM; para este flujo la prioridad es calidad de evidencia, no clasificación masiva. Un análisis de 4--6 frames por incidencia es el tamaño inicial recomendado.
