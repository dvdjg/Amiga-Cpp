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

---

# Depuración: artefactos de arranque/display en demos (060 y 201)

Diagnóstico de los dos artefactos reportados en `demos/amiga/060_eng_core_selfcheck` y `demos/amiga/201_ehb_map`: doble texto solapado amarillo+blanco y banda vertical cian (0x0AA) intermitente de 1-2 frames. Todo lo aquí escrito está verificado con evidencia reproducible salvo donde se marque como hipótesis abierta. Comando de reproducción y sondas incluidos al final.

## 1. Resumen ejecutivo

Hay **cuatro bugs** independientes. El doble texto y la banda NO comparten causa:

| Bug | Síntoma | Causa raíz | Fix (una línea) |
|---|---|---|---|
| A. Doble texto | Dos capas de texto solapadas (blanco+amarillo), cadenas repetidas/desplazadas, glifos corruptos | `draw_text` no corta en el NUL: rasteriza la `.rodata` posterior a la cadena | Cortar con `if (cp == 0u) break;` (2 sitios) |
| B. Banda cian 0x0AA | Franja vertical ~16px de ancho, rayas de periodo 2px, desde y≈126 hasta el borde inferior, color exacto palette[19]=0x0AA, 1-2 frames cada ~10-20 s, SOLO sin warp | El sprite DMA del sistema (puntero de Workbench) queda vivo; el backend no lo apaga al tomar el display | Toma de control completa en `install_copper_list`: INTENA/INTREQ/DMACON a 0x7FFF y sprites fuera |
| C. Instalación a media pantalla | Basura/contenido del sistema asomando 1 frame en el arranque (capturas 1-8 del perfil) | `install_copper_list` hace COPJMP1 desde `init()` fuera de VBlank; el Copper arranca a media línea | Instalar DENTRO del VBL la primera vez; swaps posteriores solo escribiendo COP1LC (sin COPJMP1) |
| D. Paleta/pie | El texto del pie es invisible y 0x0AA queda en índice equivocado | La paleta pone 0x0aa en índice 19 pero el código dibuja el pie con color 20 (0x000=negro) | Poner 0x0aa en palette[20] (o dibujar con 19) |

La demo 201 se beneficia de B y C sin tocar su código: su `render()` llama `scene.install(backend)` cada frame (`demos/amiga/201_ehb_map/src/main.cpp:828`) y con el doble buffer `XlimitedDisplayComposer` (`engine/include/eng/field/xlimited.hpp:1386`) eso hoy provoca un COPJMP1 por frame; tras un `draw_hud()` lento dentro de render el COPJMP1 cae a media pantalla → banda de 1 frame cada cambio de segmento (~3 s). Con el fix C (swaps = solo puntero COP1LC) ese patrón pasa a ser correcto y seguro.

## 2. Cómo reproducir (Windows nativo, sin WSL)

```
compilar  (Git Bash de Windows, NO WSL):
  & "C:\Program Files\Git\bin\bash.exe" ./tools/build/build-demo.sh demos/amiga/060_eng_core_selfcheck --debug --clean

ejecutar + captura:
  & "C:\Program Files\nodejs\node.exe" dist\tools\run\run-demo.js demos\amiga\060_eng_core_selfcheck --warp

secuencia (la banda NO aparece con warp; validar SIEMPRE sin warp):
  & "C:\Program Files\nodejs\node.exe" dist\tools\run\run-demo.js demos\amiga\060_eng_core_selfcheck --sequence-frames 30 --sequence-interval-ms 300
```

Salidas: `out/run/060_eng_core_selfcheck/A500_debug/screenshot.png` y `out/run/.../sequence/frame_*.png`. El runner usa por defecto la config `A500_debug` (`out/demos/<demo>/A500_debug/<demo>.A500_debug.exe`).

### Trampas del entorno (cada una costó un fallo de reproducción)

1. **El `startup-sequence` de dh0 queda pisado por las sesiones F5**: la extensión monta `dh0 = <ext>\bin\dh0` (directorio real) y el runner escribe `<ext>\bin\dh0\s\startup-sequence` con `stack 131072\ncd dh1:\n:a.exe\n`. Una sesión de depuración interactiva lo reescribe con `:current.exe`. Si cualquier lanzador manual no lo reescribe, AmigaDOS arranca, no ejecuta `a.exe` y la demo nunca llega a READY (PC en ROM, `sections: []` en el canal lateral). Verificar SIEMPRE antes de lanzar: `Get-Content "C:\Users\dvdjg\.vscode\extensions\bartmanabyss.amiga-debug-1.8.1\bin\dh0\s\startup-sequence"`.
2. **Zombies de WinUAE**: si un script muere sin `conn.disconnect(true)`, quedan `winuae-gdb.exe` vivos con los puertos 2345/2346. Limpiar con `Get-Process winuae* | Stop-Process -Force`.
3. **Usar el build x86** (`winuae-gdb.exe`); el x64 tiene un bug de boot preexistente.
4. `run-demo` lanza en estilo extensión (escribe `default.uae` junto al exe y `-portable`); las sondas propias deben replicar `conn.connect({ forceBreak: false, initializeStopped: true })` + `continue()` y luego esperar READY por el canal 2346 (el comando `runstatus` espera hex SIN prefijo `0x`).

### Evidencia de la banda (frames de referencia)

En la secuencia existente de 30 frames (`out/run/060_eng_core_selfcheck/A500_debug/sequence`, SHA-256 por frame): frames 000..018 y 020..029 idénticos entre sí (`B907BCA342AA66F7...`) y `frame_019` distinto (`1B5568064751F936...`). Es decir: la pantalla es estática y estable, y el glitch es de UN frame.

Análisis de píxel determinista (`out/tmp/band-diff/band-diff.mjs` sobre frame_000 vs frame_019):

```
diff píxeles=4072  bbox captura=(500,288)..(529,547)  (30 x 260 px captura)
mapeo captura->Amiga: y_amiga = (y_cap - 36)/2 ; escala 2.25 px/px (756x576)
banda Amiga: x ≈ 212..227 (~14-16 px de ancho), y ≈ 126..255 (hasta el borde inferior)
patrón horizontal: rayas de periodo 2px (1 px encendido / 1 px apagado)
color implicado: 0x00AAAA x4072 -> palette[19] (0x0AA) — y NADA más
paleta demo: 17=0x000, 18=0x000, 19=0x0AA, 20=0x000, 30=0xFF0, 31=0xFFF
```

## 3. Bug A — doble texto: desbordamiento de `draw_text` (causa raíz confirmada)

En `demos/amiga/060_eng_core_selfcheck/src/main.cpp:258-290` y en la API del engine `engine/include/eng/field/surface.hpp:109-125` (`Surface::draw_text`), el bucle de decodificación termina con:

```cpp
const eng::u8 before = *p;
const eng::u32 cp = eng::utf8::decode(p);
if (cp == 0u && before != 0u) {
    break; // inválido / fuera de LATIN-1
}
```

`eng::utf8::decode` (engine/include/eng/core/utf8.hpp:26) al leer el NUL devuelve 0 y AVANZA el puntero (`b0 < 0x80` → `++p; return b0;`). Con `before == 0` la condición del corte NO se cumple (`cp==0 && before!=0` es falso), así que el bucle sigue y rasteriza TODA la memoria posterior a la cadena como si fueran glifos, hasta toparse con el primer byte 0x80..0xC1 (inválido LATIN-1). Las cadenas de la demo viven contiguas en `.rodata`, y cada literal se dibuja TAMBIÉN en su posición correcta por su propio `draw_text` → el mismo texto aparece dos veces (una en blanco en su sitio y otra "desbordada" en la fila anterior con el color de la llamada anterior), solapadas y corridas: exactamente el "doble texto amarillo+blanco".

Nota: en la 060 el desbordamiento también sigue avanzando `x` más allá de 320 y el byte index `py*kBytesPerRow + px/8` desborda a filas inferiores (más basura). En la 201 el doble texto NO procede de aquí (su `draw_text` local, main.cpp:479, corta bien en `*t != '\0'`); si se ve texto extraño en 201, revisar primero B y C.

**Fix (2 sitios):**

```cpp
if (cp == 0u) {
    break; // fin de cadena (NUL) o byte inválido: cortar siempre
}
```

Aplica a `Surface::draw_text` (engine) y al `draw_text` local de la demo 060. La variante `draw_text5` (surface.hpp:162-190) ya lo hace bien y sirve de referencia. La regla general: `decode()` NO distingue NUL de inválido; el llamador debe cortar en cualquier `cp == 0`.

Tras el fix, el determinismo se valida así: los bitplanes de la demo deben contener exactamente las 8 llamadas a `draw_text` y nada más (comparar por renderizado con `eng::Font8` en un script Node, o diff de capturas antes/después).

## 4. Bug B — banda cian 0x0AA: sprite DMA del sistema vivo (causalidad probada A/B)

### 4.1 Estado vivo medido (canal lateral 2346, demo corriendo)

| Registro | Valor | Lectura |
|---|---|---|
| DMACONR ($dff002) | `0x03F0` | DMAEN + BPLEN + COPEN + BLTEN + **SPREN** + DSKEN — el backend solo hace SET de master\|copper y la lista añade bitplane; los canales sprite y disco que arrancó el sistema SIGUEN activos |
| INTENAR ($dff01c) | `0x602C` | INTEN(master) + EXTER + VERTB + PORTS + SOFT — la cadena de interrupciones de exec/graphics/intuition sigue viva |
| SPR0PT ($dff120) | `$00000CFC` | El puntero de sprite 0 apunta a la zona del "puntero de Workbench"; ahí hay CEROS (la imagen real está en `$CF0-$CF7`: `0F3F C67F E0FF F1FF 0000 0000...`) |
| SPR1PT | `$000004BC` | Sprites 2..5 en `$04B8` (todos armados por el sistema) |
| COP1LC ($dff080) | `$00024060` | La copperlist de la demo (correcto) |

### 4.2 Watchpoints (nobreak) 15-45 s: casi nadie escribe

Con watchpoints sobre `$CF8/$CFC/$D00` (src=all), SPR0POS/SPR0DATA/SPR0PT (src=cpu), DSKLEN/DSKPT/DMACON (src=cpu): **0 hits de CPU/DMA en la región**, y un único hit en dos corridas:

```
TRACE watch hit addr=0x00000cfa rwi=1 size=2 src=spr0 val=0x00000000 pc=0x00c0d05a
```

Es decir: el canal de sprite 0 (DMA de Agnus, no CPU) SÍ está armado y fetcha esporádicamente de esa región. Nadie reprograma registros de sprite desde CPU en la ventana observada.

### 4.3 Mecanismo según el AHRM (capítulo 4, en `docs/reference/ahrm/`)

- Línea 3249: «si el sprite DMA se apaga mientras el sprite está en pantalla (tras VSTART, antes de VSTOP), el sistema sigue mostrando la última línea de datos fetchada. Esto produce **una barra vertical en pantalla**». Nuestro caso es el análogo con datos stale: la franja es exactamente un sprite (16 px de ancho) coloreado con COLOR19 = 0x0AA.
- Líneas 3761-3775 (hardware details): en modo DMA automático el canal fetcha de memoria `[POS][CTL]` y después pares de datos por línea hasta encontrar `0000 0000`; con `VSTOP - VSTART == 0` no hay salida y el par siguiente pasa a ser el siguiente POS/CTL. Un canal apuntando a ceros es "invisible pero vivo".
- Línea 3268: los punteros son dinámicos; y línea ~3797: «los punteros deben reescribirse al principio del blanking vertical» — no hay recarga automática fiable de SPRxPT en VBL.

Coincidencia geométrica: la banda empieza en y≈126 (coherente con un vstart del puntero del ratón, que WinUAE deja centrado ≈128 en el boot) y llega hasta el borde inferior (coherente con "datos sin fin mientras el fetch siga armado"). El color 0x0AA solo puede salir de COLOR19 → sprite 0 (sprites 2/3 serían negros, 4/5 rojos, 6/7 amarillos/blanco). El ancho ≈16 px = ancho de sprite.

### 4.4 Test de causalidad A/B (sonda `out/tmp/band-diff/probe-v4.mjs`)

Misma demo viva, capturas cada 400 ms, detección de píxeles 0x00AAAA > 200:

```
Fase A (baseline, sprite DMA del sistema ACTIVO, 20 s):
  corrida 1: frame_021 = 4072 px de banda, frame_022 = 1000 px
  corrida 2: frame_021 = 4072 px, frame_022 = 4072 px
Fase B (poke DMACON = 0x0020, clear SPREN, por canal lateral con lock takeover, 45 s):
  0 frames con banda en ambas corridas (162 frames limpios)
```

Conclusión: **la banda la produce el canal de sprite DMA que el sistema dejó armado**; apagando SPREN desaparece. El disparador exacto del fetch esporádico (~1 evento cada 10-20 s, 1-2 frames de duración, no aparece con warp porque cambia el timing) no se llegó a aislar del todo (ver §8 hipótesis abierta); para el fix no es necesario.

Nota operativa: el `poke` del canal lateral devuelve `status: queued` y aplica de forma asíncrona; un readback inmediato de DMACONR puede seguir mostrando `0x03F0` — no usar el readback inmediato como veredicto. En el fix real el bit se apaga DESDE EL PROGRAMA en el arranque, que es determinista.

## 5. Bug C — instalación de la copperlist fuera de VBL / COPJMP1 por frame

`MinimalBackend::install_copper_list` (`engine/src/platform/amiga_minimal/amiga_minimal.cpp:171-182`) hoy hace:

```cpp
*cop1lc = reinterpret_cast<u32>(copper_words);          // COP1LC
custom_base[custom_copjmp1_offset] = 0x7fff;            // COPJMP1 ¡inmediato!
custom_base[custom_dmacon_offset] = dma_setclr | dma_master | dma_copper; // DMACON
```

Problemas:

1. La 060 llama `m_scene.install(backend)` desde `init()` (fuera del bucle de frames, en un punto aleatorio del raster) → COPJMP1 a media pantalla: el Copper arranca la lista a media línea, los primeros MOVEs (la copperlist empieza con `MOVE DMACON` SIN WAIT inicial, ver `Scheduler::emit_planes_display` en `engine/include/eng/graphics/copper/scheduler.hpp:90`) se ejecutan al momento y el resto del frame muestra registros a medio programar → 1 frame de basura en el arranque (visible en las capturas 1-8 del perfil).
2. El sistema sigue vivo en ese momento (ver §4.1): interrupciones de exec/graphics/intuition armadas y sprites/disco DMA activos. La lista de la demo toma el display PERO el entorno del Workbench/CLI sigue ejecutando handlers de VBL cada frame.
3. La 201 reinstala cada frame con COPJMP1 (doble buffer): tras un `draw_hud()` lento dentro de `render()`, el COPJMP1 cae a media pantalla → banda de 1 frame en cada cambio de segmento (~3 s). El 060 ya documentó esta regla en su `render()` (main.cpp:242-250).

**Dato clave**: el Copper recarga COP1LC automáticamente al inicio de cada VBL. Para swaps por frame NO hace falta COPJMP1: basta con actualizar el puntero COP1LCH/COP1LCL en cualquier momento del frame.

### Fix diseñado para `MinimalBackend::install_copper_list`

```cpp
// Estado: bool m_display_taken = false; (miembro del backend)
void MinimalBackend::install_copper_list(const u16* copper_words) {
    if (!m_display_taken) {
        // ---- TOMA DE CONTROL COMPLETA (una sola vez) ----
        // 1) Congelar el sistema: sin interrupciones ni DMA. A partir de aquí
        //    el engine no vuelve a llamar a exec (todas las demos han terminado
        //    AllocMem antes de instalar; el bucle usa espera activa por VPOSR
        //    y el canal de depuración 0xf0ff60 no necesita interrupciones).
        custom INTENA = 0x7FFF;   // apaga todas las interrupciones (exec queda dormido)
        custom INTREQ = 0x7FFF;   // higiene: limpia peticiones pendientes
        wait_blitter();           // por si quedara un blit del sistema en vuelo
        custom DMACON = 0x7FFF;   // apaga TODO el DMA: sprites, disco, audio,
                                  // blitter, bitplane y copper (pantalla a COLOR00)
        // 2) Programar nuestra lista.
        *cop1lc = copper_words;   // COP1LCH/COP1LCL
        // 3) Esperar el arranque del VBlank (línea 311 -> 0) para que el Copper
        //    arranque alineado al frame y NO a media pantalla. La espera activa
        //    es la misma de wait_vblank() (VPOSR bits 15..8 == 311<<8 y luego !=).
        wait_vblank();
        // 4) Arrancar copper+master y forzar el inicio de la lista. Estamos en
        //    la línea 0-2: los ~45 MOVEs del setup (1 MOVE ≈ 8 ciclos ≈ 0.8
        //    líneas) terminan mucho antes de DIWSTRT (línea 0x2C=44) → frame limpio.
        custom DMACON = 0x8000 | 0x0200 | 0x0080;   // SETCLR|DMAEN|COPEN
        custom COPJMP1 = 0x7FFF;                    // arranca la lista ya (línea 0)
        m_display_taken = true;
        return;
    }
    // ---- SWAP de copperlist (doble buffer, cada frame en la 201) ----
    // Solo el puntero: el Copper lo recarga solo al comienzo del próximo VBL.
    // NUNCA COPJMP1 aquí: reiniciaría el Copper a media pantalla.
    *cop1lc = copper_words;
}
```

Detalles que conviene respetar en la implementación:

- Los punteros COP1LC se escriben como LONG en `$dff080` (ya se hace así y es correcto en 68000).
- La copperlist de la demo activa ella misma BPLEN con su primer MOVE (`0x8380`), por eso el paso 4 solo necesita master+copper.
- Tras la toma de control, `wait_vblank()` sigue funcionando (polling de VPOSR, sin interrupciones). El bucle `update -> wait_vblank -> render` de `engine.hpp` queda intacto.
- No usar `-mtune` ni cambiar flags de build para esto (ver `docs/guides/optimization/OPTIMIZACION_GPP_68000.md` §1/§8).
- Opcional pero recomendable: al tomar el control, dejar también `COP2LC` apuntando a un WAIT infinito (defensa; hoy nadie ejecuta COPJMP2).

Con este backend, la 201 no necesita cambios: su `scene.install(backend)` por frame se convierte en un swap de puntero seguro. La 060 tampoco: su install único en `init()` pasa a ser la toma de control completa.

## 6. Bug D — paleta del pie (cosmético pero confunde el análisis)

En `demos/amiga/060_eng_core_selfcheck/src/main.cpp:187-192` la paleta define `0x0aa` en el índice **19** y `0x000` en el 20, pero `main.cpp:226` dibuja el pie con color **20** ("cian 20 (pie)" según el comentario) → el pie sale negro (invisible). Fix: `0x0aa` en palette[20] (o dibujar el pie con 19). NOTA IMPORTANTE para el análisis de la banda: si se cambia la paleta, el índice del color de banda cambia de sitio; el color 0x0AA debe quedarse en ALGÚN COLOR17-19 para que la sonda de banda siga siendo válida... en realidad tras el fix B no debería aparecer ninguna franja de sprite en ningún color; mantener el detector por color RGB (0,170,170) y no por índice.

Fondo: `kBgIndex = 1` (0x06a) está definido y nunca se pinta → el fondo es COLOR00 = negro. Si se quiere el fondo azul diseñado, rellenar el plano 0 con 0xFF en init (10240 bytes con `memclr`/blit; el texto con OR sigue legible: blanco 31 conserva bit0, amarillo 30 no lo pone y pisa el fondo). Decidir con el usuario; no bloquea nada.

## 7. Sobre el "texto del sistema" que leía el VLM

El VLM local (qwen3-vl/gemma3) NO es fiable para fuente 8x8: sobre las capturas de la 060 inventó palabras ("Day/operator/should/..."). El análisis de píxeles muestra que el texto extra de los frames normales es basura del desbordamiento de `draw_text` (bug A), que rasteriza `.rodata`. Contenido real del Workbench/CLI solo puede asomar en: el frame de instalación a media pantalla (bug C, arranque) y los frames de banda (bug B). Tras los fixes A+B+C no debe quedar ninguno. Regla vigente: usar el VLM solo para estructura/colores/artefactos y validar texto con análisis determinista (diff de píxeles, decodificación de glifos con `eng::Font8`).

## 8. Hipótesis abierta (no bloquea el fix)

El disparador exacto del fetch esporádico del sprite (~1 cada 10-20 s, 1-2 frames, solo sin warp) no quedó aislado: en 22-45 s de watchpoints nadie escribió SPR0POS/DATA/PT ni DMACON por CPU, y solo hubo 1 hit de lectura `src=spr0` en la región. Los candidatos razonables (sin confirmar): el canal de sprite DMA de Agnus procesando los ceros de `$CFC` según el estado máquina del AHRM (con VSTOP-VSTART=0 el par siguiente pasa a POS/CTL) y WinUAE mostrando datos stale del registro SPRxDATA durante ese frame (AHRM 3249); o alguna actividad del sistema cada N frames que no pasa por esos registros. Si se quisiera aislar: watchpoint CON break (no nobreak) sobre `$CFC` r w src=spr0 para congelar justo en el evento + `monitor rewind` para inspeccionar; o instrumentar SPR0DATA con `reg=` en watchpoints de DMA. Con el fix B (sprites fuera en la toma de control) el canal queda muerto y el evento desaparece por construcción.

## 9. Sondas y scripts disponibles (punto de partida)

- `out/tmp/band-diff/band-diff.mjs` — diff determinista de dos frames + mapa de colores/índices de paleta.
- `out/tmp/band-diff/band-detail.mjs` — mapeo captura→Amiga (escala 2.25, offset 36/18) y patrón de la banda.
- `out/tmp/band-diff/probe-sprite.mjs` — lanza la demo con la lib de `mcp-winuae-emu`, pone watchpoints nobreak y vuelca el estado vivo (usa `tools/profile/launch-winuae.mjs`).
- `out/tmp/band-diff/probe-v4.mjs` — test A/B de causalidad con poke de DMACON y detector de banda 0x0AA por frame.
- Patrón de lanzamiento manual (replica al runner): escribir el `startup-sequence` de dh0, copiar el exe a `out/run/<demo>/A500_debug/dh1/a.exe`, `WinUAEConnection.connect({ forceBreak:false, initializeStopped:true })` + `continue()`, esperar READY con `sideChannelCommand('state')` + `runstatus <hex-sin-0x>`, y SIEMPRE `conn.disconnect(true)` al acabar (si no, zombie de WinUAE).

## 10. Criterios de aceptación para el fix

1. Secuencia sin warp de ≥60 frames de la 060: todos los hashes SHA-256 idénticos entre sí (pantalla estática) y detector de color (0,170,170) en 0 píxeles en todos los frames.
2. Screenshot estático: UNA sola capa de texto sin solapes y sin glifos ajenos al código fuente de la demo; el pie visible (bug D) y el fondo según decisión (§6).
3. La 201 con el mismo backend: secuencia sin warp sin frames espurios (su install por frame pasa a ser swap de puntero) y sin contenido del sistema asomando.
4. El frame de arranque ya no muestra la pantalla del sistema a media pantalla (comprobar con captura inmediata post-READY o con las capturas 1-8 del Frame Profiler).
5. Repetir la secuencia 2-3 veces (la banda era esporádica: un solo run limpio no prueba nada).

---

## 11. RESOLUCIÓN (aplicada y verificada)

Los cuatro bugs se corrigieron en el engine y en la demo 060. Evidencia reproducible abajo.

### 11.1 Cambios de código

| Bug | Archivo | Cambio |
|---|---|---|
| A | `engine/include/eng/field/surface.hpp` (`Surface::draw_text`) | El bucle corta ahora en `cp == 0` (NUL o byte inválido), no en `cp == 0 && before != 0`. Antes rasterizaba la `.rodata` posterior a la cadena. |
| A | `demos/amiga/060_eng_core_selfcheck/src/main.cpp` (`draw_text` local) | Ídem: `if (cp == 0u) break;`. |
| B + C | `engine/src/platform/amiga_minimal/amiga_minimal.cpp` (`install_copper_list`) | Toma de control del display una sola vez: `INTENA=0x7FFF`, `INTREQ=0x7FFF`, `wait_blitter()`, `DMACON=0x7FFF`, programar `COP1LC`, espera activa de VBL (línea 311→0) y arranque `DMACON=SETCLR|DMAEN|COPEN` + `COPJMP1` alineado al inicio de línea. Instalaciones posteriores (doble buffer, la 201 reinstala por frame) hacen **solo swap de puntero `COP1LC`**, sin `COPJMP1`. |
| B + C | `engine/include/eng/platform/amiga_minimal.hpp` | Nuevo miembro `m_display_taken` para distinguir toma de control de swap. API separada en dos métodos con nombre propio: `takeover_display()` (toma de control, una sola vez en `init`) e `install_copper_list()` (swap de puntero por frame con retrocompatibilidad de toma de control en la primera llamada). |
| D | `demos/amiga/060_eng_core_selfcheck/src/main.cpp` | `0x0aa` movido de índice 19 a índice 20, para que el pie dibujado con color 20 sea visible. |

### 11.2 Evidencia de verificación (criterios de §10)

1. **Estado vivo tras la toma de control** (sonda `out/tmp/band-fix-verify/probe-state.mjs`, por canal lateral tras READY): `DMACONR=$0380` (DMAEN|BPLEN|COPEN, **sin SPREN ni DSKEN**; antes `$03F0`) e `INTENAR=$0020` (solo PORTS, **sin VERTB/EXTER**; antes `$602C`). Los sprites del puntero del Workbench y la cadena de interrupciones del sistema quedan apagados.
2. **Secuencia 060 sin warp**: tres corridas independientes (60 + 70 + 60 frames) con el **mismo** hash SHA-256 en todos los frames (`B64631E1...`) y **0 píxeles** de banda cian (0,170,170) en todos los frames. Cumple §10.1 y §10.5.
3. **Screenshot 060** (análisis de píxeles + VLM qwen3-vl local): tres bloques de texto separados (título, cuatro verificaciones OK, resumen amarillo) + pie cian visible, fondo negro limpio, **sin doble texto ni barras anómalas** (VLM: «no hay texto solapado ni duplicado»). Cumple §10.2.
4. **Demo 201**: secuencia sin warp con scroll activo (40 hashes distintos = animación correcta) y VLM confirma escena isométrica limpia con HUD propio, «no hay barras verticales ni horizontales anómalas ni consola del sistema superpuesta». Cumple §10.3.
5. **Regresión demo 020** (`install_copper_list` en init + swap por frame): compila, llega a READY y muestra las 5 bandas de color (rojo/verde/azul/amarillo/cian) — el refactor del arranque no cuelga ni rompe demos previas.

### 11.3 Sondas añadidas

- `out/tmp/band-fix-verify/verify.mjs` — conteo de color cian/amarillo/blanco por frame y por screenshot.
- `out/tmp/band-fix-verify/shape.mjs` — agrupa píxeles de un color en tiras de columnas contiguas para distinguir banda vertical (glitch) de fila de texto (pie).
- `out/tmp/band-fix-verify/probe-state.mjs` — lanza la demo, espera READY y vuelca DMACONR/INTENAR/SPR0PT/COP1LC + captura sin warp con detector de banda.

### 11.4 Aclaraciones finales
- El «texto del sistema» que leía el VLM en las capturas 1-8 era el desbordamiento de `draw_text` rasterizando `.rodata` (bug A) y el frame de instalación a media pantalla (bug C); no era el CLI de AmigaDOS. Tras A+B+C no asoma ningún texto ajeno (verificado).
- El fondo azul diseñado (`kBgIndex=1`) sigue sin pintarse (el fondo es COLOR00 negro). Es una decisión pendiente con el usuario que no bloquea este fix; si se quiere el fondo azul, rellenar el plano 0 con `0xFF` en `init()`.
- La paleta ahora deja `0x0aa` en COLOR20; el detector de banda por color RGB (0,170,170) y no por índice sigue siendo válido.
