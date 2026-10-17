# Perfilar desde el agente (sin VSCode)

La extensión `bartmanabyss.amiga-debug` perfila con **F5 → "Frame Profiler"**, que internamente hace tres cosas: genera una **tabla de unwind** desde el ELF, arranca WinUAE con GDB y envía `monitor profile N "<unwind>" "<bin>"`. Todo eso es reproducible por línea de comandos, así que **el agente puede perfilar sin abrir VSCode**.

```
   agente                          WinUAE + GDB                     ficheros
┌───────────────────┐   monitor    ┌──────────────┐   escribe   ┌──────────────────────┐
│ winuae-profile.mjs│─────────────▶│  profile N   │────────────▶│ out/tmp/wprof-*.bin  │
│  · conecta GDB    │   profile N  │  ".unwind"   │             │  out/tmp/wprof-*.unwind
│  · arranca demo   │              └──────────────┘             └──────────┬───────────┘
│  · bateria UNWIND │                                                       │
└───────────────────┘                                                       ▼
                                                            ┌───────────────────────────┐
                                                            │ profile-samples.mjs       │
                                                            │  parsea bin + resuelve PC │
                                                            │  con el .map -> top rutina│
                                                            └───────────────────────────┘
```

## Comandos

```bash
# 1) capturar (ciclos ocupada/libre, DMA por tipo y, si hay unwind, muestras de CPU)
node tools/debug/winuae-profile.mjs <demo> [CONFIG] [frames]

# 2) resolver las muestras a rutinas
node tools/analyze/profile-samples.mjs out/tmp/wprof-<demo>-<CONFIG>.bin <demo> [CONFIG] [--top N] [--json]

# 3) secciones instrumentadas del engine (sin emulador de por medio)
node tools/debug/profile.mjs <demo> [CONFIG]
```

Antes de capturar, matar instancias huérfanas de WinUAE (bloquean los puertos GDB 2345/2346) y asegurarse de que el binario y su `.map`/`.elf` existen en `out/demos/<demo>/<CONFIG>/`.

## El HUD no puede contaminar la medida (regla de medición)

**Antes de medir rendimiento, comprobar el coste del HUD de depuración.** Un HUD dibujado por
frame sobre los planos de la escena (o sobre varios planos) **distorsiona la medida** y convierte
una demo rápida en una lenta artificialmente. Casos medidos en este repo:

| HUD | Coste medido | Fuente |
|---|---|---|
| Texto por frame en playfield de 4 planos (3 líneas de 40) | **~750k ciclos/frame (≈6 campos)** | DT-025 (`001_rpg_dungeon`) |
| Texto por frame en 5 planos (anillo de la 079, prototipo de paso) | **~205k ciclos/frame** | campaña 079/116 (este doc) |
| 5º plano de 1 bpp **activo todo el frame** (pintado solo al cambio) | **~0,1–0,25 campos/frame** (fetch fijo; 079 ≈ −2,6 fps, 116 ≈ −1,7 fps) | 079/116 modernizadas |
| Zona de 5º plano con WAIT `>255` | **Rota**: dispara en la raster 255 (`>=`) y no se puede cerrar; franjas de basura | DT-027 |

Receta del HUD ligero (norma del repo, §0 de `AGENTS.md`): **zona aparte** de la pantalla con
**1–2 bitplanes propios activos TODO el frame** (`BPLCON0` + puntero en la cabecera de la
lista de cobre) y **paleta espejo** (`palette[16]=tinta`, `palette[16+k]=palette[k]`) para que
el +16 del plano sea transparente fuera del texto. **No** usar «zonas» de WAIT `>255` para
encerrar el plano: el comparador del Copper usa semántica `>=` sobre `V[7:0]`
(`WinUAE-DBG/custom.cpp:4416` `coppercomp`), dispara en la raster 255 y **no permite ningún
WAIT posterior** dentro de la lista (el plano queda encendido — fallo real de 079/116,
2026-10-17). **Actualización solo al cambio**: etiqueta estática una vez; contador en campo de
ancho fijo ~1 vez/segundo (24 escrituras), escritura **por palabras**
(`Surface::draw_text_words`), nunca por píxel ni por frame. La franja va en una banda libre
del efecto (medir la geometría real del display antes de colocarla: en la 079/116 la ventana
visible son ~240 filas y el fetch va desfasado; ver `src/main.cpp` de la 079). Para medir fps
de la lógica **sin** HUD, usar el toggle de ablación de la demo (`K120_SKIP_HUD` es la
referencia) y comparar A/B.

## La tabla de unwind

El comando `monitor profile N "" bin` produce **0 muestras de CPU**: WinUAE necesita un `.unwind` que le diga cómo desenrollar la pila para atribuir la muestra a la función. `tools/debug/winuae-profile.mjs` lo construye portando la `UnwindTable` del plugin:

1. `objdump --dwarf=frames-interp <elf>` (m68k-amiga-elf-objdump del toolchain).
2. Parsear CIE/FDE y, para cada 2 bytes de `.text`, derivar `{ (cfaReg<<12)|cfaOfs, r13, ra }` (3 int16).
3. Escribir ese array al fichero `.unwind` que se pasa al comando `profile`.

Sin errores, el log imprime `[wprof] unwind: … (.text N bytes)`. Si aun con la tabla el binario sigue con 0 muestras, el problema está en la configuración de muestreo de WinUAE (opción del emulador), no en la tabla: es el item abierto registrado en `METODOLOGIA_PROFILING.md` §5.

## Por qué no se reutiliza directamente el parser del MCP

`parseProfile` del MCP lee del binario **ciclos y DMA pero descarta las muestras** (avanzaba `profileCount*4` sin leer el array). Ya está corregido en el MCP: `ProfileFrame.profileArray` conserva los PCs y `profile-ollama` los resume por sección/PC caliente. Para nombres de función hace falta además el `.map` (o el ELF), que el MCP no conoce: por eso `tools/analyze/profile-samples.mjs` resuelve los PCs con el `.map` del build y da la tabla por rutina que se le puede pasar a Ollama en modo texto.

## Perfiles de VSCode (`.amigaprofile`) — análisis por rutina e IA

Es un **CPU profile de Chrome DevTools** (JSON con árbol de llamadas + `samples`/`timeDeltas`, y
`screenshots` incrustados). Se analiza sin abrir VSCode:

```bash
# Top por rutina (tiempo propio), lectura rápida:
node tools/analyze/profile-report.mjs <perfil.amigaprofile> [--top N]

# Tabla compacta (para encadenar a un modelo local):
node tools/analyze/profile-report.mjs <perfil.amigaprofile> --json

# Análisis con IA LOCAL (Ollama): hotspots + reparto por categoría + ideas + anomalías:
node tools/analyze/profile-report.mjs <perfil.amigaprofile> --ai [--model gemma3:12b]

# Defectos de PANTALLA con visión (flicker/tearing/filas corridas/corrupción) sobre las capturas:
node tools/analyze/profile-report.mjs <perfil.amigaprofile> --vision [--frames 0,1,2,3]
```

Modelos por defecto: `gemma3:12b` (texto) y `qwen3-vl:8b-instruct-q8_0` (visión); ajustables con
`--model`/`--vl-model` o `OLLAMA_TEXT_MODEL`/`OLLAMA_VL_MODEL` (Ollama en `OLLAMA_BASE`).

**Regla.** Para diagnosticar el rendimiento de una demo, **medir siempre** (no adivinar) y cruzar las
**dos vistas complementarias**: el **muestreo** de este `.amigaprofile` (dónde está la CPU, con los
`inlined` atribuidos por separado) y el **instrumentado** de `tools/debug/profile.mjs` (secciones
`ENG_PROF` del engine, que **incluyen la espera del Blitter dentro de `blits`**). Empezar por `--ai`
(anomalías: spin-waits, trabajo no pedido) y `--vision` (defectos visuales). `speedscope.app` abre el
`.amigaprofile` directo para inspección manual. Los perfiles del depurador incluyen sus IRQs; para
medidas finas de rendimiento, complementar con el camino de línea de comandos de arriba.
