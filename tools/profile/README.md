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
| `tools/profile/ai-analyze.mjs <outName> [frames] [opciones]` | **Orquestador para la IA**: captura → extrae → analiza con Ollama y vuelca el informe final por stdout. Ideal para llamar desde un asistente (opencode, etc.). |
| `tools/profile/profile-analyze.sh <outName> [frames] [opciones]` | Pipeline: captura → extrae → analiza en un solo comando. |
| `tools/profile/probe-screen.sh <demo> [frames] [opciones]` | Lanza la demo (WinUAE desacoplado), espera, captura, extrae y analiza. |
| `tools/profile/hotspots.mjs [demo] [--seconds N] [--out archivo.md]` | **Sampler profiler de hotspots** (e9k-style): muestrea el PC del 68000 por el canal lateral, agrupa por símbolo (resuelve el `.map`) y emite un informe "dónde se va el tiempo del CPU". |
| `tools/debug/step-memory.mjs --bp <simbolo\|0xaddr> …` | Conecta a GDB, pone un breakpoint, continua y en cada parada lee/rendera el frame buffer. |

## `ai-analyze.mjs` — orquestador para la IA

Un solo comando que encadena captura → extracción → análisis con Ollama y, al
terminar, **imprime el informe por stdout** para que la IA (o un humano) lo lea
directamente sin abrir el fichero. Deja la evidencia en `out/profile/<outName>/`.

```bash
# Verificar que un scroll horizontal fino monta los tiles sin salto de 16px
node tools/profile/ai-analyze.mjs demo101 6 \
  --prompt "scroll horizontal fino; comprobar que los tiles se ensamblan sin salto de 16px en el cruce coarse"

# Lanzando la demo directamente (WinUAE como hijo de ai-analyze) y esperando READY
node tools/profile/ai-analyze.mjs demo050 4 \
  --demo demos/050_blitter_bobs \
  --prompt-file tools/profile/prompts/050-blitter-bobs.md
```

`--demo <demo>` lanza WinUAE **como proceso hijo del propio script** (lo que
garantiza que el canal lateral siga vivo durante la captura — el runner clásico
sale y WinUAE muere con su proceso padre), espera READY por `runstatus` y apaga
WinUAE al terminar aunque algo falle. El módulo `launch-winuae.mjs` reutiliza
la detección de la extensión Bartman, stagediza el `.exe` en
`out/run/<demo>/dh1/` y la config `runner.uae`.

> **Tiempo**: el análisis completo (`--mode all` = meta + frames + montaje) con
> modelos locales tarda ~2-4 minutos. Para un avance rápido usa `--mode meta`
> (solo técnico, sin imágenes). La captura en sí es cuestión de segundos.

Parámetros: igual que `ollama-analyze.mjs` (`--model`, `--text-model`, `--base`,
`--mode`, `--prompt`, `--prompt-file`) más los de captura (`--wait-cmd`,
`--contains`, `--wait-ms`, `--lock-owner`, `--port`) y `--demo` /
`--demo-timeout-ms`. Requiere WinUAE-DBG con el canal lateral activo
(`WINUAE_GDB_PERSIST_LISTENER=1`) y Ollama local.

## Modos de `ollama-analyze.mjs`

| Modo | Qué hace | Cuándo usarlo |
|---|---|---|
| `--mode meta` | Envía `profile-summary.json` (registros, DMA, bitplanes, ciclos) a un **modelo de texto** (`--text-model`, p. ej. `qwen3:8b`) para un **pre-análisis técnico sin imágenes**. Barato y rápido. | Pre-análisis rápido de todo el perfil; detectar registros sospechosos antes de mirar la pantalla. |
| `--mode frames` | Analiza cada frame con el modelo de visión, usando la descripción del frame anterior como contexto. Fiable en cualquier modelo. | Detalle frame a frame (más llamadas). |
| `--mode montage` | Combina los frames en **una sola imagen** (hoja de contacto) con ffmpeg y la analiza en una llamada. | Ver la secuencia/transiciones en una sola llamada. |
| `--mode all` (por defecto) | `meta` + `montage`. | Informe completo. |

## Sampler profiler de hotspots (`hotspots.mjs`)

Muestrea el PC del 68000 por el canal lateral (2346) de forma **no intrusiva**
durante N segundos, agrupa las muestras por símbolo (resuelve el `.map` de la
demo contra `baseText`) y emite un informe de "dónde se va el tiempo del CPU".

```
node tools/profile/hotspots.mjs demos/101_ehb_tile_scroll_driver --seconds 5 [--out out/hotspots.md]
```

Ejemplo (demo 101): `wait_vblank` ~29%, `rebuild_copper` ~28%, `memset` ~17% —
revela que reconstruir la copperlist cada frame es costoso. Requiere la demo
compilada (`.exe` + `.map`). El modelo de visión NO interviene; es puro conteo
de PC.

## Sobre el modelo de visión (hallazgo importante)

- **`qwen3-vl` NO analiza bien varias imágenes separadas en un mismo mensaje**:
  las funde y solo ve una. Por eso el modo secuencia usa **hoja de contacto**
  (montaje en una imagen), que sí funciona.
- `gemma3:12b` (instalada) también es multimodal y suele ser más rápido; se
  elige con `--model gemma3:12b`.
- Para el pre-análisis técnico (`meta`) se usa un modelo de texto rápido
  (`qwen3:8b` por defecto, configurable con `--text-model`).

## `probe-screen.sh` — cómo lanza la demo

`probe-screen.sh` usa el **runner** (`run-demo.sh --keep-running`) para lanzar la
demo: el runner conecta GDB, alcanza `side-channel READY` y deja la demo
renderizando. El script espera la marca `READY` en el log del runner, captura el
perfil por el canal lateral, extrae frames y analiza. Ajustes:

- `SETTLE_MS` (env, por defecto 2000): asentamiento extra tras READY antes de
  capturar. Si la captura sale con la demo en un estado transitorio (p. ej. un
  diálogo "No disk present in unit 0" que aparece en algún momento de la
  ejecución), sube/baja este valor o usa `--wait-cmd/--contains` en `capture-profile`.
- El emulador debe lanzarse con `WINUAE_GDB_PERSIST_LISTENER=1` (el script lo
  exporta) para que el perfil no cierre los listeners.

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
