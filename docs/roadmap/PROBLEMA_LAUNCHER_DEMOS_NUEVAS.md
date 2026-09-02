# Enunciado para IA externa — el runner no ejecuta demos NUEVAS (queda en AmigaDOS)

## Contexto del proyecto
Repositorio `C:\Users\dvdjg\Documents\programa\AI\Amiga\Amiga-Cpp`: un **engine de juegos
retro para Amiga 500** (freestanding, `gnu++23`, sin libs del sistema). Las demos se
compilan con `m68k-amiga-elf-gcc 15.1.0` (toolchain bebbo) a un ELF y se convierten a
.formato HUNK con `elf2hunk` (AROS, modificado por Bartman/Abyss). Se ejecutan en
**WinUAE-DBG** con un **runner** Node (`tools/run/run-demo.ts`, compilado a `dist/`).

## El problema (síntoma)
Una demo **nueva** (`demos/201_ehb_map`) compila perfectamente, pero al lanzarla con el
runner la pantalla queda en el **escritorio/CLI de AmigaDOS 1.3** (no se ejecuta el exe).
Demos **existentes** (`demos/000_toolchain_cpp23`, `demos/102_tile_scroll_dualpf`) SÍ
bootean con el mismo runner (el harness `tools/debug/verify-harness.mjs` pasa 3/3 en 102).

## Cómo reproducirlo
1. Compilar: `bash ./tools/build/build-demo.sh demos/201_ehb_map --debug --clean`
   (debe poner `OK out/demos/201_ehb_map/A500_debug/201_ehb_map.A500_debug.exe`).
2. Lanzar: `bash ./tools/run/run-demo.sh demos/201_ehb_map --reset-emulator
   --allow-timeout-fallback --sequence-frames 3 --sequence-interval-ms 250`
   (Windows + Git Bash + Node; `AMIGA_BIN_PATH=...\bin\win32`).
3. Captura en `out/run/201_ehb_map/A500_debug/screenshot.png` → **AmigaDOS 1.3**.
4. Control: `bash ./tools/run/run-demo.sh demos/102_tile_scroll_dualpf --config A500_release`
   → SÍ bootea el demo.

## Mecánica del runner/boot (lo que ya se ha auditado)
- `tools/run/run-demo.ts` es lo que hay que mirar.
- Publica `builtExe` → `out/run/<demo>/<config>/dh1/a.exe` (`fs.copyFileSync`, línea ~826).
- Escribe `startup-sequence` en el directorio dh0 fijo de la extensión con:
  `cd dh1:` + `:a.exe` (línea ~842). El KS1.3 bootea eso → debe correr `dh1:a.exe`.
- En el `.uae` resultante se mete `debugging_trigger=:a.exe` (líneas ~180/182): WinUAE-DBG
  **detiene la CPU al entrar en `a.exe`** para que GDB cargue símbolos; luego hay un
  `continue`. Config base: `config/mcp-amiga-c-debug.uae`; el runner escribe
  `out/run/<demo>/<config>/runner.uae`.
- Puertos: GDB 2345 (`WinUAEConnection`), canal lateral 2346 (READY). El wait de READY
  reconecta por poll.
- Logs del emulador/launcher en `%TEMP%\winuae-mcp\*.log`.

## Evidencias ya obtenidas (NO son la causa)
1. `201` con banco de 294 KB en sección `.MEMF_CHIP` (hunk `HUNKF_CHIP` verificado con
   `elf2hunk -v`): → AmigaDOS.
2. `201` con el banco en `.rodata` (sin hunk chip): → AmigaDOS.
3. `201` **mini** (banco de 256 B, exe diminuto): → AmigaDOS.
⇒ No es ni el tamaño del exe, ni LoadSeg, ni el hunk chip. El problema es que la CPU del
A500 **nunca llega a ejecutar `a.exe`** (o se queda detenida en el `debugging_trigger` sin
continuar) para binarios **nuevos**, mientras los demos ya existentes funcionan.

## Hipótesis a investigar (ordenadas)
1. **Attach de GDB a binarios nuevos**: en el `debugging_trigger` el runner/GDB debe
   "continue" tras cargar símbolos y relocalizar (qOffsets). ¿Se queda el flujo en un
   qRcmd/halt para demos y no para otras? Comparar el comportamiento de `102` vs `201`
   (mismos puertos, distinto binario/mapa).
2. **Resolución de símbolos/mapa del binario nuevo**: el runner resuelve `g_eng_run_status`
   desde `.map` + secciones runtime; para 201 podría fallar y por eso no "libera" el boot
   (el READY nunca se confirma y el run usa fallback). Ver `out/run/201_ehb_map/A500_debug/runner.uae`
   y si el GDB conectó (log `%TEMP%\winuae-mcp`).
3. **Suposiciones dependientes de lista de demos reexistente** (p. ej. algo keyed por el
   nombre/carpeta en `out/demos`, ADF pre-generado, o `dh0` estático que no se refresca).

## Qué se pretende
El runner debe **ejecutar cualquier demo nueva** (igual que las existentes): boot → correr
`a.exe` → el demo toma el control de la pantalla (copper/planos) y sube READY por el canal
lateral. Mientras tanto, el paso aislado es identificar DÓNDE se queda (¿GDB attach?
¿continue del trigger? ¿RESOLUCIÓN de símbolos del binario nuevo?) y arreglarlo.

## Solicitud concreta
Con esa información (y acceso al repo en la ruta indicada), indica:
1. Cuál es la causa más probable en `tools/run/run-demo.ts` / la pieza GDB del connector.
2. El **aire exacto** de instrumentación para confirmarlo (qué logs/breakpoints/qué nodo).
3. El **fix propuesto** (código o config) y cómo verificarlo (comando + qué debe mostrar).
Prioriza evidencia reproducida frente a hipótesis sin probar.