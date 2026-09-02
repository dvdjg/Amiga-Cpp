# Problema del runner: demos nuevas quedan en AmigaDOS — RESUELTO

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
4. **Canal lateral**: `detail=0x20102` = `mark_failed` en `main.cpp:52`. La demo
   SÍ ejecuta y falla internamente. `configure_memory` retorna `true`, pero
   `m_planes` o `m_copper` resultan inválidos por overflow de la arena chip.

## Hipótesis a investigar (ordenadas)
1. **Attach de GDB a binarios nuevos**: descartado — el canal lateral confirma
   que la demo ejecuta y emite `mark_failed`.
2. **Resolución de símbolos/mapa del binario nuevo**: descartado —
   `resolveRuntimeSymbolAddress` para `g_eng_run_status` resuelve correctamente
   a `sections[2] = 0xc0e748` (demo 201).
3. **Suposiciones dependientes de lista de demos reexistente**: descartado.
4. **Root cause real**: overflow de la arena chip por padding de alineación de
   `AllocMem` (8 bytes × 2 alineaciones a 16 = 16 bytes extra).

## Qué se pretendía
El runner debe ejecutar cualquier demo nueva igual que las existentes. Se resolvió:
la causa era un bug en la demo (overflow de arena chip), no en el runner.

## Solicitud original (ya resuelta)
Con esa información (y acceso al repo en la ruta indicada), indica:
1. Cuál es la causa más probable en `tools/run/run-demo.ts` / la pieza GDB del connector.
2. El **aire exacto** de instrumentación para confirmarlo (qué logs/breakpoints/qué nodo).
3. El **fix propuesto** (código o config) y cómo verificarlo (comando + qué debe mostrar).
Prioriza evidencia reproducida frente a hipótesis sin probar.

**Resuelto**: causa era bug de alineación en la arena chip de la demo, no en el runner.

---

## Resolución (2026-09-02)

### Hallazgo clave: la demo SÍ se ejecuta, falla internamente en `init()`

Instrumentación del runner (`sideChannelFail`) confirmó que la CPU ejecuta `a.exe`,
llega a `main()` y emite `mark_failed(g_eng_run_status, 0x00020102u)` en
`demos/201_ehb_map/src/main.cpp:52`:

```
detail = 0x00020102 = m_planes.valid() || m_copper.valid()  → false
```

**El runner y el canal lateral no son la causa.** El binario se ejecuta, el READY
nunca se emite porque la demo falla en `init()` y aborta antes de llegar al game
loop. El runner ve `Failed` por canal lateral y termina; la pantalla queda en
AmigaDOS porque la demo nunca configuró copper/planos.

### Root cause: overflow por padding de alineación en la arena de chip

El `need` de chip en la demo 201 era **exactamente** `65536` bytes:

```
need = 6 × kPlaneBytes + 4096 = 6 × 10240 + 4096 = 65536
```

La arena (`LinearArena`) gestiona un único bloque contiguo de `65536` bytes.
`AllocMem` de AmigaOS 1.3 garantiza alineación a 8 bytes (no 16). Cuando
`m_base` tiene offset 8-mod-16:

```
1ª allocate(61440, 16) → padding = 8, used = 61448
2ª allocate(4096,  16) → padding = 0, next  = 61448 + 4096 = 65544
                                     65544 > 65536  → FALLA → m_copper inválido
```

El copper allocation falla porque los 8 bytes de padding de la primera
asignación empujan el total 8 bytes por encima del límite. Esto explica por qué
la demo 102 (que pide 280KB chip) no lo padece: el margen es enorme comparado
con 16 bytes de padding potencial.

### Fix aplicado

Añadir 16 bytes de headroom al pedido de chip para cubrir el peor caso de
alineación:

```diff
- const eng::u32 need = static_cast<eng::u32>(6u) * kPlaneBytes + 4096u;
+ const eng::u32 need = static_cast<eng::u32>(6u) * kPlaneBytes + 4096u + 16u;
```

Archivo: `demos/201_ehb_map/src/main.cpp`, línea ~41.

### Fix preventivo para demos futuras

Toda demo que pida memoria chip alineada a 16 dentro de un `LinearArena` debe
incluir `+16` de headroom en el `need`. La demo 102 ya tiene margen suficiente
(280KB) y no necesita fix.

### Verificación

1. `bash ./tools/build/build-demo.sh demos/201_ehb_map --debug --clean`
2. `bash ./tools/run/run-demo.sh demos/201_ehb_map --reset-emulator --allow-timeout-fallback`
3. `out/run/201_ehb_map/A500_debug/screenshot.png` debe mostrar la demo
   (colores EHB, mapa), no AmigaDOS.
4. `out/run/201_ehb_map/A500_debug/run-report.json`: `status: "ok"`,
   `sideChannel.status: "ready"`, sin `sideChannelFail`.