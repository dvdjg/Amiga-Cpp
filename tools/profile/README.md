# Herramientas de perfil y análisis de pantalla (IA + humano)

Conjunto de utilidades genéricas para que la IA (o el usuario) capturen un perfil de
WinUAE-DBG, extraigan los frames de pantalla y los analicen con un modelo de
visión local (Ollama). También incluye `step-memory` para avanzar por
breakpoints y leer el frame buffer "en caliente".

## Flujo típico

```
capturar perfil → extraer frames → analizar con Ollama → informe
```

| Comando | Qué hace |
|---|---|
| `tools/profile/capture-profile.mjs <out.bin> [frames] [--wait-cmd …]` | Pide un perfil por el canal lateral (2346) y guarda el binario. Puede esperar una condición antes de capturar. |
| `tools/profile/profile-extract.mjs <perfil.bin> <outDir>` | Parsea el binario y extrae `frame_0000.jpg/.png` por frame + `profile-summary.json` (registros, bitplanes, DMA, ciclos). |
| `tools/profile/ollama-analyze.mjs <outDir> [opciones]` | Analiza con Ollama local: **modo `meta`** (pre-análisis técnico de TODO el perfil, sin imágenes), **modo `frames`** (por frame), **modo `montage`** (secuencia en hoja de contacto). |
| `tools/profile/profile-analyze.sh <outName> [frames] [opciones]` | Pipeline: captura → extrae → analiza en un solo comando. |
| `tools/profile/probe-screen.sh <demo> [frames] [opciones]` | Lanza la demo (WinUAE desacoplado), espera, captura, extrae y analiza. |
| `tools/debug/step-memory.mjs --bp <simbolo\|0xaddr> …` | Conecta a GDB, pone un breakpoint, continua y en cada parada lee/rendera el frame buffer. |

## Modos de `ollama-analyze.mjs`

| Modo | Qué hace | Cuándo usarlo |
|---|---|---|
| `--mode meta` | Envía `profile-summary.json` (registros, DMA, bitplanes, ciclos) a un **modelo de texto** (`--text-model`, p. ej. `qwen3:8b`) para un **pre-análisis técnico sin imágenes**. Barato y rápido. | Pre-análisis rápido de todo el perfil; detectar registros sospechosos antes de mirar la pantalla. |
| `--mode frames` | Analiza cada frame con el modelo de visión, usando la descripción del frame anterior como contexto. Fiable en cualquier modelo. | Detalle frame a frame (más llamadas). |
| `--mode montage` | Combina los frames en **una sola imagen** (hoja de contacto) con ffmpeg y la analiza en una llamada. | Ver la secuencia/transiciones en una sola llamada. |
| `--mode all` (por defecto) | `meta` + `montage`. | Informe completo. |

## Sobre el modelo de visión (hallazgo importante)

- **`qwen3-vl` NO analiza bien varias imágenes separadas en un mismo mensaje**:
  las funde y solo ve una. Por eso el modo secuencia usa **hoja de contacto**
  (montaje en una imagen), que sí funciona.
- `gemma3:12b` (instalada) también es multimodal y suele ser más rápido; se
  elige con `--model gemma3:12b`.
- Para el pre-análisis técnico (`meta`) se usa un modelo de texto rápido
  (`qwen3:8b` por defecto, configurable con `--text-model`).

## Prompts personalizables

El prompt BASE es **genérico** (sin asumir chipset ni contenido). Para cada test
se puede personalizar:

```bash
# Fichero de prompt específico del test
node tools/profile/ollama-analyze.mjs <outDir> \
  --prompt-file tools/profile/prompts/050-blitter-bobs.md

# Texto extra en línea
node tools/profile/ollama-analyze.mjs <outDir> --prompt "Espero un fondo azul..."
```

Plantillas de ejemplo en `tools/profile/prompts/`:
- `generic.md` — descripción genérica.
- `050-blitter-bobs.md` — invariantes de la demo 050 (BOB cookie-cut, blobs,
  fondo EHB).

## Requisitos para capturar

- WinUAE-DBG corriendo con la demo/ejecutable cargado, y **lanzado con la
  variable `WINUAE_GDB_PERSIST_LISTENER=1`** (si no, al desconectar el cliente
  del canal lateral WinUAE cierra los listeners y la sesión muere).
  ```powershell
  $env:WINUAE_GDB_PERSIST_LISTENER="1"
  ```
- La carpeta destino de `outFile` debe existir (el script la crea).
- `profile` requiere lock `assist` en el canal lateral (el script lo adquiere y
  libera solo).

## Ejemplos

### Capturar 6 frames cuando la demo llegue a READY

```bash
# El comando runstatus se repite hasta que contenga "READY"
node tools/profile/capture-profile.mjs out/p.bin 6 \
  --wait-cmd 'runstatus 0x<addr_de_g_eng_run_status>' --contains 'READY'
```

### Pipeline completo con análisis local

```bash
bash tools/profile/profile-analyze.sh demo050 6 \
  --model qwen3-vl:8b-instruct-q8_0
# -> out/profile/demo050/{demo050.profile.bin, frame_*.jpg, profile-summary.json, ollama-report.md}
```

### Análisis frame a frame (individual)

```bash
node tools/profile/ollama-analyze.mjs out/profile/demo050 \
  --model qwen3-vl:8b-instruct-q8_0 --out out/profile/demo050/reporte.md
```

### Hoja de contacto (una sola llamada a Ollama, ahorra tokens)

```bash
node tools/profile/ollama-analyze.mjs out/profile/demo050 \
  --model gemma3:12b --contact-sheet
```

### Step-memory: breakpoint + leer frame buffer

```bash
# Pone un breakpoint en update(), continua, y en cada parada lee memoria y
# (si se da --render) dibuja el frame buffer como PNG.
node tools/debug/step-memory.mjs --elf out/debug-current/current.elf \
  --bp 0xc0f986 \
  --render 0x15058 320 256 6 planar --palette '000,024,048,06c,ff0,f80,0ff,f0f,246,468,68a,8ac,ace,cdf,fff,111,012,123,234,345,456,567,678,789,89a,9ab,abc,bcd,cde,def,eee,222' \
  --steps 5
```

## Modelos Ollama recomendados

- `qwen3-vl:8b-instruct-q8_0` (visión, bueno describiendo pantallas).
- `gemma3:12b` (multimodal, rápido).
- Configuración vía `--model`, `--base http://127.0.0.1:11434` o env
  `OLLAMA_MODEL` / `OLLAMA_BASE`.

## Notas técnicas

- El formato `.bin` del perfil es el que produce `monitor profile`/canal lateral;
  por frame incluye registros custom (BPLCON0/1, bitplane pointers, DIW/DDF,
  COLOR00…), colores AGA, DMA, recursos gráficos (direcciones de bitplanes) y una
  captura de pantalla jpg/png.
- `profile-extract.mjs` replica el parser de `vscode-amiga-debug/src/backend/profile.ts`.
- `step-memory.mjs` importa `GdbProtocol` y `decodePlanarBitmap` del repo hermano
  `mcp-winuae-emu/dist`; requiere que el puerto GDB (2345) esté libre (no usar a
  la vez con F5 de VSCode).
- El canal lateral NO compite por el socket GDB; `profile` por canal lateral es la
  vía recomendada para captura mientras la IA usa GDB para otras cosas.
