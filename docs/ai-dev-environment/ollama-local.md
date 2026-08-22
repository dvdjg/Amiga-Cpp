# Ollama local para análisis visual

## Configuración comprobada

- Servicio: `http://127.0.0.1:11434`.
- Versión comprobada: `0.32.14`.
- Modelo visual predeterminado: `qwen3-vl:8b-instruct-q8_0`.
- Modelo alternativo instalado: `gemma3:12b`.
- Endpoint compatible con el pipeline: `/v1/chat/completions`.
- Endpoint de salud: `/api/version`.
- Modelos cargados: `/api/ps`.

La configuración `tools/vision-review/providers/ollama.local.json` ya apunta a este proveedor. El script normaliza la URL a `/v1` y envía cada imagen como `data:image/...;base64,...` en modo `multi-image`.

## Decisión de modelo

Qwen3-VL queda como modelo predeterminado porque la documentación oficial de Ollama destaca comprensión de vídeo dinámico, contexto largo y relaciones espaciales, capacidades directamente útiles para secuencias de frames y perfiles de scroll. Gemma 3 12B también es multimodal y los experimentos de `D:/scripts` le atribuyen mejor clasificación semántica en imágenes estáticas; se conserva como alternativa para una captura aislada, pero no se cambia el proveedor principal.

La mención a una versión “Qwen 3.8” parece mezclar familias o tamaños. Las familias relevantes aquí son `qwen2.5vl` y `qwen3-vl`; el modelo local disponible y probado es `qwen3-vl:8b-instruct-q8_0`.

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
