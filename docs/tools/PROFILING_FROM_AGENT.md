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

**Antes de medir rendimiento, comprobar el coste del HUD de depuración.** Un HUD dibujado por frame sobre los planos de la escena (o sobre varios planos) **distorsiona la medida** y convierte una demo rápida en una lenta artificialmente. Casos medidos en este repo:

| HUD | Coste medido | Fuente |
|---|---|---|
| Texto por frame en playfield de 4 planos (3 líneas de 40) | **~750k ciclos/frame (≈6 campos)** | DT-025 (`001_rpg_dungeon`) |
| Texto por frame en 5 planos (anillo de la 079, prototipo de paso) | **~205k ciclos/frame** | campaña 079/116 (este doc) |
| 5º plano de 1 bpp **activo todo el frame** (pintado solo al cambio) | **~0,1–0,25 campos/frame** (fetch fijo; 079 ≈ −2,6 fps, 116 ≈ −1,7 fps) | 079/116 modernizadas |
| Zona de 5º plano con WAIT `>255` | **Rota**: dispara en la raster 255 (`>=`) y no se puede cerrar; franjas de basura | DT-027 |

Receta del HUD ligero (norma del repo, §0 de `AGENTS.md`): **zona aparte** de la pantalla con **1–2 bitplanes propios activos TODO el frame** (`BPLCON0` + puntero en la cabecera de la lista de cobre) y **paleta espejo** (`palette[16]=tinta`, `palette[16+k]=palette[k]`) para que el +16 del plano sea transparente fuera del texto. **La variante más barata es una BANDA superior/paneles con WAITs ≤255** (enmienda 2026-10-17: la 116 enciende el 5º plano en raster 44 y lo apaga en la 58 — el fetch del HUD baja de ~290 a ~14 filas por campo y gana +1,7 fps): el texto va en las filas de plano que el puntero fijado en la raster de encendido muestra donde se quiere (el fetch va ~10 filas por delante). **No** usar «zonas» de WAIT `>255` para encerrar el plano: el comparador del Copper usa semántica `>=` sobre `V[7:0]` (`WinUAE-DBG/custom.cpp:4416` `coppercomp`), dispara en la raster 255 y **no permite ningún WAIT posterior** dentro de la lista (el plano queda encendido — fallo real de 079/116, 2026-10-17). **Actualización solo al cambio**: etiqueta estática una vez; campos dinámicos en campos de ancho fijo ~1 vez/segundo, escritura **por palabras** (`Surface::draw_text_words`), nunca por píxel ni por frame. La franja va en una banda libre del efecto (medir la geometría real del display antes de colocarla: en la 079/116 la ventana visible son ~240 filas y el fetch va desfasado; ver `src/main.cpp` de la 079). Para medir fps de la lógica **sin** HUD, usar el toggle de ablación de la demo (`K120_SKIP_HUD` es la referencia) y comparar A/B.

### El HUD muestra el fps del último segundo

Los campos dinámicos del HUD son: **contador de frames totales** (campo "F …") y **fps del último segundo con un decimal** (`eng::debug::HudFpsWindow`, `engine/include/eng/debug/prof.hpp`): la ventana se cierra cuando el reloj de ciclos (0xB7E928, `prof_clock_hz` = 7.093.790 Hz) acumula ≥ 1 s y emite `fps10` (fps × 10). Es la media de la **última ventana**, así un cambio de cadencia se ve al segundo siguiente; el coste por frame es una comparación y la división se paga 1 vez por segundo (test HOST-465). El **delta del contador del HUD entre dos capturas separadas** es además una validación cruzada barata del fps mostrado (debe casar con el delta de `frame` del `run-report.json` y con `measure-fps`).

**Trampa real (2026-10):** no alimentar la ventana con `frame_index` sin comprobar su semántica. `frame_index` = **frames completados** (lo fija el bucle principal); la IRQ de VBlank publica su contador de **latidos** en `field_index` (`FrameStats`, `driver.hpp`; el pacing del fondo usa ese). Una versión de la IRQ pisaba `frame_index` con los latidos: una lectura en `render` (tras >1 campo de trabajo) veía ~50/s y el HUD mostraba 49,2 fps donde la tasa real era 32,7. Bug corregido (`engine.hpp`, `VBlankHeartbeat::run`) y blindado con HOST-466.

Para **leer los campos del HUD en una captura** (verificación propia o preguntas al modelo de visión), `node tools/debug/hud-zoom.mjs <captura.png>` amplía la banda superior del texto (recorte + escala nearest); es el ojo de aumento para confirmar dígitos que el modelo de visión puede confundir.

## Cadencia de un binario de referencia (sin símbolos)

Para comparar el port contra una **demo original** (p. ej. el ADF de `demoscene-repo-orig`) cuya cadencia se quiere medir sin símbolos:

1. Perfilar N campos con `winuae-profile` sobre la config que arranca el ADF (el perfil guarda **un screenshot por campo** capturado).
2. `node tools/debug/profile-cadence.mjs <perfil.bin>` extrae los frames y compara firmas de frames consecutivos: **parejas idénticas ⇒ la imagen avanza 1 vez cada 2 campos = la referencia corre a 25 fps** (2 campos/frame); todos distintos ⇒ 50 fps (1 campo).
3. Los contadores DMA del mismo perfil dan el reparto por campo con los canales del recorder de WinUAE (`WinUAE-DBG/include/debug.h:361-372`: 1 refresh, 2 CPU, 3 cobre, 4 audio, **5 blitter, 6 bitplano**, 7 sprites, 8 disco…): sirve para comparar el trabajo real (accesos por campo) contra el port.

Caso medido (2026-10, `flatshade-convex`): la original **no** cabe en 1 campo — parejas idénticas ⇒ 2,0 campos/frame (25 fps), CPU saturada (142102/142112 ciclos por campo) y ~62k accesos DMA por campo; su propio profiler daba 575 líneas de trabajo por frame (≈1,84 campos). Sirve de aviso: **medir la referencia antes de fijar un objetivo** («¿por qué no la igualamos?» — porque el listón real es 2 campos, no 1).

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

Modelos por defecto: `gemma3:12b` (texto) y `qwen3-vl:8b-instruct-q8_0` (visión); ajustables con `--model`/`--vl-model` o `OLLAMA_TEXT_MODEL`/`OLLAMA_VL_MODEL` (Ollama en `OLLAMA_BASE`).

**Regla.** Para diagnosticar el rendimiento de una demo, **medir siempre** (no adivinar) y cruzar las **dos vistas complementarias**: el **muestreo** de este `.amigaprofile` (dónde está la CPU, con los `inlined` atribuidos por separado) y el **instrumentado** de `tools/debug/profile.mjs` (secciones `ENG_PROF` del engine, que **incluyen la espera del Blitter dentro de `blits`**). Empezar por `--ai` (anomalías: spin-waits, trabajo no pedido) y `--vision` (defectos visuales). `speedscope.app` abre el `.amigaprofile` directo para inspección manual. Los perfiles del depurador incluyen sus IRQs; para medidas finas de rendimiento, complementar con el camino de línea de comandos de arriba.
