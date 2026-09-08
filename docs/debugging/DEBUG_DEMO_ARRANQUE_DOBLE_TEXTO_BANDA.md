# Hilo de depuración: arranque de demo / doble texto / banda azul (engine)

Este documento es el **prompt de contexto** para abrir un hilo de IA nuevo
(fresh context) que continúe la depuración. Cópialo literalmente como primer
mensaje del hilo (junto con el contenido de este archivo y los enlaces/paths
que se indican).

---

## Rol y proyecto

Eres un ingeniero de depuración de bajo nivel para el **Amiga 500 (OCS/ECS,
Kickstart 1.3)** en el repositorio `Amiga-Cpp`. Trabajas sobre un engine de
demos en **C++23 freestanding** (sin libstdc++, sin excepciones, sin RTTI, sin
heap en gameplay) compilado con el toolchain cruzado `m68k-amiga-elf` de la
extensión Bartman (GCC 15). El flujo de validación es `build -> run -> analyze`
con WinUAE-DBG (servidor GDB :2345 + canal lateral :2346).

**NO uses WSL.** Todo comando con `node`, `g++`/toolchain, WinUAE o el runner
debe ejecutarse con binarios de **Windows nativo** (p. ej.
`C:\Program Files\nodejs\node.exe`, los `.exe` de la extensión Bartman). El
`bash` de WSL mangla rutas y rompe el lanzamiento de WinUAE/cc1plus. Tienes el
runner compilado en `dist/tools/run/run-demo.js` y las tools TS en `tools/`.

Ruta del repo: `C:\Users\dvdjg\Documents\programa\AI\Amiga\Amiga-Cpp`
Toolchain/extension usada por el runner: `.vscode\extensions\bartmanabyss.amiga-debug-1.8.1` (o AMIGA_BIN_PATH con `vscode-amiga-debug\bin\win32`).

## Primeros pasos (SIEMPRE)

1. Lee `AGENTS.md` entero (reglas obligatorias: idioma/ortografía en español,
   formato de docs con word wrap, buscar-antes-de-implementar, regla de
   evidencia, regla de rendimiento 68000, no usar WSL).
2. Lee `docs/STRUCTURE.md` (dónde va cada archivo).
3. Lee `docs/README.md` y `docs/CONTINUATION_CONTEXT.md` (historial del engine).
4. Lee `docs/guides/optimization/OPTIMIZACION_GPP_68000.md` (problemas del
   backend m68k, bitácora) y `docs/debugging/DEBUG-WINUAE-V2-GUIDE.md`
   (herramientas MCP/WinUAE).
5. Revisa `docs/reference/ahrm/amiga-hardware-manual-index.md` (AHRM 3.ª edición
   local, texto completo en `.cat.md`) — **úsalo ante cualquier duda de
   interpretación de registros/timing del chipset**. También está la 1985:
   `docs/reference/ahrm/amiga-hardware-reference-manual-1985.md`.

## Problema reportado (bien descrito, no asumir que está resuelto)

La demo `demos/amiga/060_eng_core_selfcheck` (y según el usuario también la
`201_ehb_map`) se muestra **mal visualmente** cuando se ejecuta con la
extensión/F5 o con el runner capturando secuencias:

1. **Doble texto superpuesto**: se ven **dos capas de letras** en la misma
   zona, **amarilla y blanca**, aparentemente la misma cadena redundante o
   desplazada. No está claro si es doble dibujo, sombra, o contenido del
   Workbench asomando por debajo.

2. **Banda vertical azulada (cian) intermitente**: una franja vertical de ~29px
   de ancho y casi toda la altura, color `0x0AA` (un color de la paleta de la
   demo), que aparece en **1 solo frame** de forma esporádica (~cada 6 s en
   capturas sin warp) y luego desaparece. Reproducido: en secuencias sin warp,
   el `frame_N` cambia 1 frame y el siguiente vuelve (verificado por SHA-256 de
   la secuencia). Con `--warp` NO aparece.

3. En las capturas 1-8 de un perfil `.amigaprofile` (Frame Profiler de la
   extensión) se ven ambos artefactos y además texto que **no pertenece a la
   demo** (el VLM local leyó palabras tipo "Day/operator/should/unique/
   pattern/repeat/hour"), lo que sugiere contenido del **sistema/Workbench o
   del CLI de AmigaDOS** asomando por detrás del canvas.

Conclusiones previas (verificar, no fiarse): la banda se atribuyó a artefacto
del sampling GDB/perfilador; el doble texto se atribuyó a un `.exe` viejo. El
usuario dice que **el problema persiste y aparece también en la 201**, con lo
que es un **bug de engine/boot**, probablemente porque la demo **no limpia ni
toma el estado del Workbench/AmigaDOS correctamente** al arrancar (no desactiva
interrupciones del sistema, no cierra/aísla el display del sistema, no
restablece punteros/paleta/copper de forma completa, o el backend deja DMA
activo que pisa el frame).

## Misión

Analiza **todo el proceso de arranque** de una demo hasta tener una pantalla
limpia y estable, desde el punto de vista de bajo nivel del Amiga:

- Qué hace `MinimalBackend` (startup, `_start`, configuración de memoria,
  `configure_memory`, instalación de copper, VBlank).
- Qué deja el sistema (Kickstart/AmigaDOS/Workbench) "vivo": interrupciones
  level 3/5/6, VBR, INTENA, DMACON, copperlist del sistema, sprite DMA, audio,
  bitplane DMA, blitter, paleta, planos.
- Cómo la demo toma el display: `StaticEhbScene` / `CanvasPlayfield` /
  `XlimitedDisplayComposer` / `install_copper_list` (escribe `cop1lc` + `copjmp1`
  + `dmacon`). Buscar si es necesario apagar INTENA del sistema o instalar un
  nivel de interrupción propio.
- Si el bucle del engine (`update -> wait_vblank -> render`) y la instalación de
  la copperlist son los correctos para no producir glitches (el `copjmp1` en
  medio de línea puede reiniciar el Copper a media pantalla → banda de 1 frame).
- El **doble texto** amarillo+blanco: determina si es realmente doble dibujo,
  artefacto de mostrar el CLI, o sombra. Usa técnicas deterministas (diff por
  píxeles de frames, regiones, colores) y **no te fíes del VLM para leer texto
  de fuente 8x8** (osos salen falsos positivos).

**NO des por supuesto que el código actual es correcto.** Debes buscar y
corregir **varios bugs**. Revisa no solo la demo 060 sino el engine: `superior
formance del backend`, `support/gcc8_c_support.c`, `support/gcc8_a_support.s`,
el arranque (`_start`), y que la demo/engine tome el control del video de forma
completa al iniciar y se sepa qué hace AmigaDOS en paralelo.

## Reproducibilidad (obligatorio antes de tocar nada)

Ejecuta y captura con Windows nativo:

```powershell
# Compilar (manual, toolchain Windows; build-demo.sh requiere Git Bash nativo)
& "C:\Users\dvdjg\Documents\programa\AI\Amiga\vscode-amiga-debug\bin\win32\opt\bin\m68k-amiga-elf-g++.exe" ...
# (o, si tienes Git Bash de Windows, bash tools/build/build-demo.sh demos/amiga/060_eng_core_selfcheck --clean)

# Correr + screenshot
& "C:\Program Files\nodejs\node.exe" "C:\Users\dvdjg\Documents\programa\AI\Amiga\Amiga-Cpp\dist\tools\run\run-demo.js" demos\amiga\060_eng_core_selfcheck --warp

# Secuencia sin warp (para ver la banda)
& "C:\Program Files\nodejs\node.exe" "C:\Users\dvdjg\Documents\programa\AI\Amiga\Amiga-Cpp\dist\tools\run\run-demo.js" demos\amiga\060_eng_core_selfcheck --sequence-frames 30 --sequence-interval-ms 400
```

- Screenshot: `out/run/060_eng_core_selfcheck/A500_debug/screenshot.png`
- Secuencia: `out/run/060_eng_core_selfcheck/A500_debug/sequence/frame_*.png`
- Runner usa por defecto el CONFIG `A500_debug` → el `.exe` debe estar en
  `out/demos/060_eng_core_selfcheck/A500_debug/`. Confirma que el `.exe` es el
  de la fuente actual (los `.exe` viejos de otras configs no valen).
- En las capturas 1-8 (para comparar) está el perfil `%TEMP%\amiga-profile-2026.09.08-22.06.15.amigaprofile` (JSON `IAmigaProfileSplit`,
  `screenshots[]` = JPEG base64).

## Herramientas a tu disposición

- **WinUAE-DBG** (fork Bartman): servidor GDB :2345 + **canal lateral** :2346
  (side channel `state/regs/mem`). Especificación:
  `WinUAE-DBG/docs/WINUAE-MONITOR-EXTENSIONS.md`.
- **MCP `winuae-emu`** (si estás en entorno con MCP): `winuae_custom_registers`,
  `winuae_memory_read/write`, `winuae_copper_disassemble`, `winuae_bitmap_decode`,
  watchpoints con origen (`src=cpu|copper|blitter|bpl0-7|spr0-7|audio0-3|disk|dma`),
  `winuae_rewind`, `winuae_debugperiph`.
- **Ollama local** (vision) para describir capturas: `http://127.0.0.1:11434`
  modelos `qwen3-vl:8b-instruct-q8_0` y `gemma3:12b`. Endpoint
  `/api/generate` con `images:[base64]`. **Recuerda**: el VLM no lee fuente 8x8
  con fiabilidad; úsalo para describir estructura/colores/artefactos, no para
  transcribir texto. Complementa SIEMPRE con análisis determinista de píxeles
  (scripts Node con `tools/lib/image.js`).
- **Perfilado**: `tools/profile/README.md` (`ai-analyze.mjs --demo ...`).

## Cómo validar

- Al terminar, la demo debe mostrar **una sola capa de texto limpia, el fondo
  correcto, y sin banda azul intermitente** en secuencias sin warp.
- Comprobar que **no asoman** contenidos de Workbench/CLI de AmigaDOS ni texto
  de sistema.
- Verificar que **otras demos (p. ej. 201)** también quedan limpias si el fix
  es de engine.
- Evidencia: hashes de secuencia (todos iguales salvo el glitch de 1 frame),
  diffs de píxeles (sin regiones espurias en frames "normales"), capturas
  estáticas.

## Reglas transversales (de AGENTS.md)

- No asumir: cada afirmación de "funciona" debe tener evidencia reproducible.
- Buscar antes de implementar (no duplicar utilidades ya existentes).
- Comentarios didácticos en español en el código de hardware.
- Conservar el rendimiento 68000 (fast_div, dbra, sin __divsi3 en hot path).
- Actualizar docs/roadmap cuando cambie el estado real.

## Entregable

1. Diagnóstico raíz del bug (o bugs) de arranque/display.
2. Fix(es) en el engine/backend/demo (con comentarios didácticos).
3. Evidencia: secuencias limpias + análisis determinista + evaluación del VLM.
4. Mensaje de commit razonado.