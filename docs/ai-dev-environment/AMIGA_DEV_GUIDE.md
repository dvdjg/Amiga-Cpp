# Guía del entorno Amiga para agentes IA

Este documento describe el **ecosistema completo** para compilar, ejecutar, depurar, capturar y perfilar programas Amiga 68k, de modo que **cualquier IA** que lo lea pueda operar sin conocer de antemano las herramientas. Es transversal a cuatro proyectos que viven en `C:\Users\dvdjg\Documents\programa\AI\Amiga\`:

```text
  C:\Users\dvdjg\Documents\programa\AI\Amiga\
  ├── Amiga-Cpp\            Engine C++23 + demos + tools (build/run/analyze) + docs canónicas
  ├── vscode-amiga-debug\   Extensión VS Code "Amiga C/C++" (gcc 15.1, gdb 17, winuae-gdb) — fork de BartmanAbyss
  ├── WinUAE-DBG\           WinUAE 6.0 + servidor GDB + canal lateral + perfilador + monitor extendido (fork)
  └── mcp-winuae-emu\       Servidor MCP (Node) con ~60 tools de depuración sobre WinUAE-GDB
```

**Regla transversal**: no inventar. La fuente de verdad de cada cosa está en un proyecto; este documento enlaza a la fuente y da los comandos listos para usar.

## 0. Cómo se conectan las piezas

```text
  código C/C++/asm
      │  m68k-amiga-elf-gcc / vasmm68k_mot        (toolchain, en vscode-amiga-debug/bin/win32)
      ▼
  .elf (debug + símbolos)  ──elf2hunk──►  .exe (HUNK Amiga)   + .map + .s
      │                                         │
      │                                         │  se copia a dh1: / se inserta como ADF
      │                                         ▼
      │                              WinUAE-DBG (winuae-gdb.exe)  ── ejecuta el .exe
      │                                   │            ▲
      │   GDB Remote Serial Protocol (TCP 2345)        │  monitor <cmd> / qRcmd
      └──────────────► m68k-amiga-elf-gdb ─────────────┘
                                   │
                                   │  canal lateral (TCP 2346, JSON por línea)
                                   ▼
                      mcp-winuae-emu / tools/debug/winuae-side-channel.sh
```

- **GDB** (puerto **2345**) da control de bajo nivel: registros, memoria, breakpoints/watchpoints, `monitor` (qRcmd).
- **Canal lateral** (puerto **2346**) da telemetría y acciones seguras sin tocar la línea de GDB: estado, registros, memoria, `runstatus` (READY), screenshot, input, profile.
- **MCP** envuelve ambos en tools para asistentes.
- Puertos **configurables por entorno**: `WINUAE_GDB_PORT`, `WINUAE_SIDE_CHANNEL_PORT`, `WINUAE_GDB_PERSIST_LISTENER`. Cada hilo/agente debe usar **su propio par de puertos** para no colisionar (ver §7.4).

---

## 1. Toolchain

- Compilador: `m68k-amiga-elf-gcc` / `g++` (GCC **15.1.0**) y ensambladores `m68k-amiga-elf-as`, `vasmm68k_mot`.
- Ubicación habitual: `vscode-amiga-debug\bin\win32\opt\bin\m68k-amiga-elf-*.exe`. También en las extensiones instaladas (`%USERPROFILE%\.vscode\extensions\bartmanabyss.amiga-debug-*\bin\win32` y `.cursor\...`).
- El build del repo resuelve el toolchain probando, por orden: `AMIGA_BIN_PATH`, extensiones de Cursor, extensiones de VS Code, y el `PATH`; **elige el GCC más moderno** (`sort -V`) e imprime `[build] toolchain: ... (gcc X.Y.Z)`.

```powershell
# Windows: invocar SIEMPRE Git Bash por ruta absoluta (no `bash` a secas, puede resolver a WSL)
$env:AMIGA_BIN_PATH = "$env:USERPROFILE/Documents/programa/AI/Amiga/vscode-amiga-debug/bin/win32"
Test-Path "$env:AMIGA_BIN_PATH/opt/bin/m68k-amiga-elf-g++.exe"   # debe dar True
```

- Defectos conocidos del GCC 15.1 (ICE de CFI a `-O0` con lambdas, libcalls `__mulsi3`/`__divsf3` en `-nostdlib`, etc.), con workarounds y re-verificación: `Amiga-Cpp/docs/reference/toolchain/m68k-gcc.md`.
- Requisitos del host: **Windows + Git Bash + Node.js**. **No usar WSL**.

---

## 2. Compilar

### 2.1 Demos/tests del engine (Amiga-Cpp)

```bash
& 'C:\Program Files\Git\bin\bash.exe' ./tools/build/build-demo.sh <demo> [--debug|--release|--o0] [--clean]
```

- `--debug` → `-O1`; `--release` → `-O2`; `--o0` → `-O0` (depuración interactiva fiable).
- Flags comunes: `-g -m68000 -nostdlib -Wextra -fomit-frame-pointer -fno-exceptions -fno-strict-aliasing -DENG_AMIGA=1`; enlaza con `-Ttext=0x400 -nostdlib` y genera `.elf`, `.exe` (HUNK vía `elf2hunk`), `.map` y `.s`.
- Salida: `out/demos/<demoId>/<CONFIG_ID>/<demoId>.<CONFIG_ID>.{elf,exe,map,s}`, con `CONFIG_ID = <MACHINE>[_<flags>]_<debug|release|o0>`.
- Barrido completo: `bash ./tools/build/build-all-demos.sh [--demo <substr>] [--strict] [--release]`.
- Overrides por demo en `build.args` (una `CLAVE=valor` por línea): `ENGINE_OPT`, `DEMO_OPT`, `C_OPT`, `FAST_STACK`. Hook `src/prebuild.sh` antes de compilar (recibe `MACHINE_ID`/`TARGET_MACHINE`).

Documentación canónica: `Amiga-Cpp/docs/build/BUILD_AND_RUN.md`.

### 2.2 Plantilla de la extensión (proyecto suelto)

`vscode-amiga-debug/template/Makefile`:

```make
program = out/a
CC = m68k-amiga-elf-gcc
CCFLAGS = -g -m68000 -Ofast -nostdlib -fomit-frame-pointer -flto -fwhole-program \
          -fno-exceptions -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--emit-relocs,--gc-sections,-Ttext=0,-Map=$(OUT).map
all: $(OUT).exe
$(OUT).exe: $(OUT).elf
	@elf2hunk $(OUT).elf $(OUT).exe
```

Flujo de la extensión: command palette → **`Amiga: Init Project`** → apuntar el Kickstart (`config:amiga.rom-paths.A500` o `"kickstart"` en `launch.json`) → **F5**.

### 2.3 Modelo de máquina (A500 / A1200 / A4000 / CD32)

Importante: en el engine, **`TARGET_MACHINE` solo etiqueta el `CONFIG_ID`** (`MACHINE_ID="${TARGET_MACHINE:-A500}"` en `tools/build/build-demo.sh`); **no añade `-m68020` ni AGA**. La CPU y el chipset se eligen con flags de compilación y con el quickstart de WinUAE.

| Destino | CPU (compilar) | AGA/chipset | Kickstart | Quickstart WinUAE |
|---|---|---|---|---|
| **A500** (defecto) | `-m68000` | OCS/ECS, `-DENG_AMIGA=1` | 1.3 | `quickstart=a500,1` |
| **A1200** | `-m68020` | `-DK_AGA=1` | 3.1 | `quickstart=a1200,0` (+ `cpu_model=68020`) |
| **A4000** | `-m68030` | `-DK_AGA=1` | 3.1 | `quickstart=a4000,0` |
| **CD32** | `-m68020` | `-DK_AGA=1` | 3.1 (ROM CD32) | `quickstart=CD32,0` |

- El backend lee AGA con `#if defined(K_AGA)` (`engine/include/eng/platform/amiga/backend.hpp`).
- En el engine, para A1200 suele bastar `TARGET_MACHINE=A1200 C_OPT=-m68020 EXTRA_DEFINES=-DK_AGA=1` (o `build.args` por demo). El `tools/run/run-demo.ts` **fuerza `cpu_model=68020`** cuando `WINUAE_QUICKSTART` es `a1200`/`a4000` (para que código `-m68020` no dé *instrucción ilegal*). La compilación por defecto del engine es **68000**.
- La extensión expone presets en `launch.json` (campo `"config"`): `A500`, `A1200`, `A1200-FAST`, `A1200-030` (necesita `cpuboard`), `A3000`, `A4000`. **No hay preset `CD32`** en la extensión; se configura a mano en WinUAE (`quickstart=CD32,0`).
- **CD32 (receta)**: compilar `-m68020 -DK_AGA=1`; ejecutar con `WINUAE_QUICKSTART=CD32,0` y Kickstart de CD32. WinUAE modela CD32 con 68020 + AGA + Akiko (`akiko.cpp`). El runner admite `--cd32` para presentar un *pad* CD32 en el puerto 2 (`joyport1mode=cd32joy`; el pad puede no detectarse sin hardware joystick real, ver `run-demo.ts`).

### 2.4 Tipos de salida

| Salida | Cómo | Notas |
|---|---|---|
| **Ejecutable suelto** | `elf2hunk a.elf a.exe` (lo hace el Makefile/build) | HUNK Amiga; el `.elf` lleva debug info y símbolos |
| **Listado/símbolos** | `m68k-amiga-elf-objdump --disassemble -S a.elf > a.s`; `.map` del link | El `.map` es la fuente de símbolos del runner y del MCP |
| **DLL / librería dinámica** | **No hay DLL estilo Windows**. El repo tiene **HUNK nativo de AmigaOS** para carga dinámica y un formato propio **`.englib`** (`engine/include/eng/res/hunk.hpp`, `tools/fs/`, `docs/engine/architecture/FILE_SYSTEM_AND_DYNAMIC_LIBRARIES.md`) | Para una `.library` clásica: enlazar con `-shared` y convertir a HUNK, o usar `.englib` |
| **Imagen de disquete (ADF)** | Extensión: `exe2adf -i in.exe -a out.adf`. Repo: `node tools/fs/make-volume.mjs [--out <dir>] [--adf <path>]` (usa `xdftool`) | Montar con `run-demo.sh ... --disk out/fs/<x>.adf` (DF0:) o tool MCP `winuae_insert_disk` |
| **Disco duro (HDF)** | **No soportado por el tooling.** Se usa **directorio montado**: `dh0:` (sistema) y `dh1:` (carpeta del programa) | Manual: crear HDF con `xdftool`/`rdbtool` y montarlo con `-s`/`.uae` en WinUAE |

Referencia de formatos de binario/disco: `Amiga-Cpp/docs/build/amiga-binary-and-disk-formats.md` (ADF 901 120 B; HDF/RDB descrito como extensión futura).

---

## 3. Ejecutar con WinUAE

### 3.1 Runner del repo (Amiga-Cpp)

```bash
& 'C:\Program Files\Git\bin\bash.exe' ./tools/run/run-demo.sh <demo> [opciones]
```

El runner copia la demo a `dh1:a.exe`, genera `runner.uae` desde `config/mcp-amiga-c-debug.uae`, resuelve el emulador (`WINUAE_GDB_DIR` → `../WinUAE-DBG/bin` → extensión Bartman), lanza `winuae-gdb.exe`, conecta GDB, sigue tras `debugging_trigger`, conecta al canal lateral `127.0.0.1:2346`, espera `READY` (por `g_eng_run_status`), guarda PNG y cierra.

Salidas: `out/run/<demoId>/<CONFIG_ID>/screenshot.png`, `run-report.json`, y `sequence/` si se piden frames.

Opciones más usadas:

| Opción | Efecto |
|---|---|
| `--debug`/`--release` | variante de build (por defecto debug) |
| `--warp` | `warp=true` (solo throughput/diagnóstico; por defecto off para juzgar suavidad) |
| `--keep-running` | no cierra WinUAE al terminar (Ctrl+C para salir) |
| `--sequence-frames N [--sequence-interval-ms M]` | N capturas cada M ms (def. 100) |
| `--sequence-step-frames N [--sequence-step-start-fine F]` | N frames consecutivos (1 frame entre capturas) |
| `--sequence-camera-x a,b,c` / `--sequence-fine-x a,b,c` | captura por valor de `cameraX` / su fine scroll |
| `--wait-port <s>` / `--reset-emulator` | esperar/liberar **solo** los PIDs propios |
| `--protect <target>,<block\|set:0xVALUE>[,size]` | reglas `monitor protect` tras READY |
| `--config <id>` | fuerza una config (p. ej. `A500_release`) |
| `--disk <adf>` | monta `floppy0=<adf>` (DF0:) |
| `--screenshot <ruta>` | ruta de la captura |
| `--cd32` | pad CD32 en el puerto 2 |
| `--immediate-blits`, `--telemetry-samples N`, `--read-debugperiph [sub]` | diagnóstico |
| ratón: `--mouse-from X,Y --mouse-to X,Y [--mouse-control X,Y] [--mouse-click]` | automatización del ratón |
| teclado/joystick: `--key-events <id>`, `--key-scan <from>-<to>`, `--joy <port>:<dir\|fire>` | entrada |
| `--no-side-channel` | desactiva la espera por canal lateral |

Máquina emulada por entorno: `WINUAE_QUICKSTART` (p. ej. `a1200,0`, `CD32,0`), `WINUAE_KICKSTART` (ROM) y, para placas con ROM extendida (CD32), `WINUAE_KICKSTART_EXT` (p. ej. `C:/amiga/CD32_EXT.rom`). El runner fuerza `cpu_model=68020` cuando el quickstart es A1200/A4000/CD32.

> **Receta CD32 validada** (build con `EXTRA_DEFINES="-DK_AGA=1"`, código 68000 —AGA es chipset, no exige 68020— y ejecución con `WINUAE_QUICKSTART=CD32,0 WINUAE_KICKSTART=C:/amiga/KICK_CD32.rom WINUAE_KICKSTART_EXT=C:/amiga/CD32_EXT.rom`): el modelo CD32 de WinUAE arranca, el programa alcanza `READY` (state 3) y la captura interna sale 756×576. Si se quiere codegen 68020, añadir `-m68020`, pero entonces el binario deja de pasar `asm-audit` (la política del engine es 68000).

### 3.2 Runner de la extensión (VS Code)

`launch.json` (type `amiga`, `request: launch`, `preLaunchTask: compile`), con `config` ∈ {`A500`,`A1200`,`A1200-FAST`,`A1200-030`,`A3000`,`A4000`}, `kickstart`, y overrides de memoria. La extensión usa `winuae-gdb.exe -portable` (o FS-UAE en Linux/macOS) con `debugging_features=gdbserver` y GDB `target remote localhost:2345`.

### 3.3 Layouts de memoria

Los overrides (case-insensitive) se pasan en `launch.json` y los interpreta WinUAE:

| Campo | Valores |
|---|---|
| `chipmem` | `256k`→0, `512k`→1, `1m`→2, `1.5m`→3, `2m`→4 |
| `fastmem` | `0`→0; `64k/128k/256k/512k`; `1m/2m/4m/8m` |
| `slowmem` (bogo) | `0`→0, `512k`→2, `1m`→4, `1.8m`→7 |
| `ntsc` | `true`/`false` (frame PAL 313 líneas por defecto) |
| `cpuboard` | ROM de tarjeta (p. ej. Blizzard 1230-IV para `A1200-030`) |

También por entorno en el runner: `WINUAE_QUICKSTART`, además de las líneas `-s clave=valor` (ver §7).

### 3.4 WinUAE por línea de comandos

```text
winuae-gdb.exe -portable                 # sin tocar la config global
winuae-gdb.exe -f <config.uae>           # cargar una config
winuae-gdb.exe -s <clave=valor> ...      # fijar una opción individual
winuae-gdb.exe -statefile=<file.uss>     # savestate
winuae-gdb.exe -cdimage=<img>            # imagen de CD (CD32)
```

La config mínima para depurar (`.uae`) incluye: `debugging_features=gdbserver`, `debugging_trigger=:a.exe`, `use_gui=no`, `quickstart=...`, `filesystem=rw,dh0:<sistema>`, `filesystem2=rw,dh1:dh1:<carpeta>,-128`, `boot_rom_uae=min` (necesario para warp/`KPrintF`/overlay).

---

## 4. Depurar con GDB

- GDB del toolchain: `vscode-amiga-debug/bin/win32/opt/bin/m68k-amiga-elf-gdb.exe`, lanzado por la extensión como `-q --interpreter=mi2`.
- Conexión: GDB Remote Serial Protocol sobre `127.0.0.1:2345` (el servidor RSP lo implementa `WinUAE-DBG/od-win32/barto_gdbserver.cpp`).
- Activación del servidor: `debugging_features=gdbserver` (bit 2 de `debugging_features`, `cfgfile.cpp`) + `debugging_trigger=<nombre proceso>`.
- Capacidades: registros (`g`/`G`, D0-D7/A0-A7/SR/PC), memoria (`m`/`M`/`X`), breakpoints `Z0`/`z0`, watchpoints `Z2`/`Z3`/`Z4` (read/write/access), `vCont`, `qOffsets` (relocalización), y `monitor` vía `qRcmd`.
- **Relocalización**: el `.exe` HUNK no está a la dirección del `.elf`; `qOffsets` localiza el proceso AmigaDOS y devuelve las bases de hunk, y el servidor reubica los breakpoints (`ELF_TEXT_BASE=0x400`). Ver `WinUAE-DBG/doc/RELOCATION-FIX.md`.
- **Símbolos**: el `.map` (símbolos) y el DWARF del `.elf` (campos de structs) los usa el MCP para `winuae_print`. La extensión además carga símbolos de Kickstart (`add-symbol-file .../symbols/kick_<sha1>.elf -s .kick <base>`).

### 4.1 Comandos `monitor` (qRcmd)

| Comando | Uso |
|---|---|
| `profile <n> <unwind> <out>` | perfil de N frames compatible con la extensión |
| `screenshot <path>` | PNG del framebuffer *interno* de WinUAE-DBG |
| `disasm <addr> [n]` | desensamblado (`sm68k`) |
| `reset` | reinicia (requiere `debugging_trigger`) |
| `input key\|event\|joy\|mouse ...` | inyecta entrada |
| `df0..df3 insert\|eject` | inserta/ejecta discos en caliente |
| `memcfg` / `membanks` | mapa de bancos de memoria |
| `status` | estado del servidor |
| `watch ...`, `protect ...`, `train ...`, `base ...`, `print ...` | watchpoints/protección/símbolos |
| `rewind ...` | *rewind* (rebobinado) |
| `trace on\|off\|status` | traza |
| `debugperiph ...` | periférico de depuración `0xB70000` (consola/checkpoints/contadores) |
| `offset [set <addr>]`, `logfile <path\|off>`, `findproc [name]`, `breakpoints`, `findcode`, `warp` | varios |

Detalle completo: `WinUAE-DBG/GDB_MONITOR_COMMANDS.md` y `WinUAE-DBG/docs/WINUAE-MONITOR-EXTENSIONS.md`; uso por agentes en `Amiga-Cpp/docs/debugging/system/debug-winuae-v2-guide.md`.

### 4.2 Periférico in-Amiga `0xB70000` (checkpoints y consola)

Escribiendo en `0xB70000` el programa Amiga puede: imprimir por consola (`+0x00`), disparar un breakpoint (`+0x04`), declarar bases de sección `.text/.data/.bss` (`+0x08/0C/10`), *checkpoints* de profiling (`+0x20`), salir del depurador (`0xDEAD` en `+0x24`), etc. Wrapper de dominio: `engine/include/eng/debug/peripheral.hpp`. Tabla completa en `debug-winuae-v2-guide.md` §4.

---

## 5. Canal lateral (side channel)

- Implementado en `WinUAE-DBG/od-win32/barto_gdbserver.cpp` (mismo TU que el servidor GDB). Escucha **solo en `127.0.0.1`**, en un hilo aparte.
- Puerto por defecto **2346**; configurable con **`WINUAE_SIDE_CHANNEL_PORT`** (paralelo a `WINUAE_GDB_PORT` para el GDB).
- Protocolo: **una orden por línea, respuesta JSON por línea**. Al conectar emite `{"ok":true,"event":"connected",...}`.
- Comandos: `hello`, `mode`, `lock status|acquire <owner> [observe|assist|takeover]|release`, `state` (gdbConnected, secciones, pc, sr, cycles…), `regs` (D0-D7/A0-A7/SR/PC), `mem <addr> <len>` (≤4096), `runstatus <addr>` (16 bytes: `magic/version/state/frame/detail`), y acciones según *lock*: `screenshot`, `input`, `profile` (assist) y `poke`, `rollback`, `pause`, `resume` (takeover). Las acciones con efectos se encolan y ejecutan en `vsync_pre` (no desde el hilo TCP).
- **`runstatus`/READY**: el runner espera a que el estado del canal sea `3` (`Ready`), leyendo `g_eng_run_status` (magic `ENGR`/`0x454e4752`).

Uso desde el repo:

```bash
bash ./tools/debug/winuae-side-channel.sh state
bash ./tools/debug/winuae-side-channel.sh regs
bash ./tools/debug/winuae-side-channel.sh mem 0xdff000 32
bash ./tools/debug/winuae-side-channel.sh screenshot out/tmp/pantalla.png
```

### 5.1 Protocolo del runtime Amiga (`0xF0FF60`)

El programa Amiga habla con WinUAE-DBG llamando al vector fijo **`0xF0FF60`** (runtime `vscode-amiga-debug/template/support/gcc8_c_support.c`). Si el vector no está instalado (no es WinUAE-DBG), las llamadas se ignoran y el programa continúa. El primer argumento selecciona el modo:

| Modo (arg0) | Nombre | Uso |
|---|---|---|
| `82` | `UaeConf` | fija opciones (warp: `cpu_speed`, `cpu_cycle_exact`, `cpu_memory_cycle_exact`, `blitter_cycle_exact`, `warp`) |
| `86` | `UaeDbgLog` | log a la consola del depurador (`KPrintF`) |
| `88` | *debug command* | comando de overlay/profiler/gfx; `arg1` = `barto_cmd` |

Comandos del modo `88` (`enum barto_cmd`): `clear`, `rect`, `filled_rect`, `text`, `register_resource`, `set_idle`, `unregister_resource`, `load`, `save`.

**Recurso gráfico** (`debug_register_*`) — `struct debug_resource` de **52 bytes** (WinUAE es 64-bit: la dirección se pasa como `u32`):

| Offset | Campo | Notas |
|---:|---|---|
| 0 | `u32 address` | puntero al dato (Chip RAM) |
| 4 | `u32 size` | bytes |
| 8 | `char name[32]` | etiqueta mostrada en el depurador |
| 40 | `u16 type` | 0=bitmap, 1=palette, 2=copperlist |
| 42 | `u16 flags` | bitmap: `interleaved=1`, `masked=2`, `ham=4` |
| 44 | `union` | bitmap `{s16 width, height, numPlanes}` / palette `{s16 numEntries}` |

API de dominio (declarada en `gcc8_c_support.h`): `debug_clear`, `debug_rect`, `debug_filled_rect`, `debug_text` (coordenadas PAL `(0,0)-(768,576)`, color `0x00RRGGBB`), `debug_start_idle`/`debug_stop_idle`, `debug_register_bitmap`/`_palette`/`_copperlist`, `debug_unregister` (`NULL` = todas), y `debug_load`/`debug_save` para transferir ficheros host↔Amiga (el CWD del emulador decide la carpeta).

---

## 6. MCP `mcp-winuae-emu`

Servidor MCP (Node/TypeScript, **stdio**) que expone WinUAE-GDB a asistentes como tools.

### 6.1 Instalar / construir

```bash
cd C:\Users\dvdjg\Documents\programa\AI\Amiga\mcp-winuae-emu
npm install
npm run build      # tsc -> dist/index.js
```

Requiere **Node ≥ 18** y una build **x86** de WinUAE-DBG (`winuae-gdb.exe`); la **x64** tiene un bug de arranque (emulación congelada), no del GDB server.

### 6.2 Configuración

`.mcp.json` de proyecto (formato Claude/VS Code); ejemplo del repo:

```json
{
  "mcpServers": {
    "winuae-emu": {
      "command": "node",
      "args": ["C:/Users/dvdjg/Documents/programa/AI/Amiga/mcp-winuae-emu/dist/index.js"],
      "env": {
        "WINUAE_PATH": "C:/Users/dvdjg/Documents/programa/AI/Amiga/WinUAE-DBG/bin",
        "WINUAE_CONFIG": "C:/Users/dvdjg/Documents/programa/AI/Amiga/Amiga-Cpp/config/mcp-amiga-c-debug.uae",
        "WINUAE_GDB_PORT": "2345"
      }
    }
  }
}
```

Para **opencode**, el equivalente usa `mcp` con `type: "local"` y `command` como **array**. Ejemplo en la raíz del workspace (`Amiga/opencode.json`), apuntando `WINUAE_PATH` a la build del fork:

```json
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "winuae-emu": {
      "type": "local",
      "command": ["node", "C:/Users/dvdjg/Documents/programa/AI/Amiga/mcp-winuae-emu/dist/index.js"],
      "environment": {
        "WINUAE_PATH": "C:/Users/dvdjg/Documents/programa/AI/Amiga/WinUAE-DBG/bin",
        "WINUAE_CONFIG": "C:/Users/dvdjg/Documents/programa/AI/Amiga/Amiga-Cpp/config/mcp-amiga-c-debug.uae",
        "WINUAE_GDB_PORT": "2345"
      }
    }
  }
}
```

`WINUAE_PATH` debe apuntar al directorio con `winuae-gdb.exe` (**recomendado**: `WinUAE-DBG\bin`, que trae canal lateral y puertos configurables). Variables relevantes: `WINUAE_PATH`, `WINUAE_CONFIG`, `WINUAE_EXE`, `WINUAE_GDB_PORT` (def. 2345), `WINUAE_HEADLESS`, `WINUAE_DEBUGGING_TRIGGER`, `AMIGA_NM_PATH` (para resolver símbolos), `WINUAE_MEMORY_WRITE_NO_PAUSE`, `OLLAMA_MODEL`/`OLLAMA_TEXT_MODEL`/`OLLAMA_BASE`.

### 6.3 Catálogo de tools (por área)

- **Sesión**: `winuae_connect`, `winuae_connect_existing`, `winuae_disconnect`, `winuae_status`, `winuae_session_config`, `winuae_memory_map`, `winuae_qoffsets`.
- **Carga/reset/disco**: `winuae_load` (Hunk o imagen de disco), `winuae_reset`, `winuae_warp`, `winuae_insert_disk`, `winuae_eject_disk`, `winuae_run_program`, `winuae_findproc`.
- **Memoria**: `winuae_memory_read`, `winuae_memory_write`, `winuae_memory_dump`, `winuae_machine_snapshot`, `winuae_memory_pattern_search`.
- **CPU**: `winuae_registers_get`, `winuae_registers_set`.
- **Breakpoints/watchpoints**: `winuae_breakpoint_set`/`_clear`, `winuae_breakpoint_conditional_wait`, `winuae_watchpoint_set`/`_clear`, `winuae_watchpoint_set_ext`/`_list`/`_last`/`_clear_ext`, `winuae_train`, `winuae_protect`, `winuae_rewind`, `winuae_trace`.
- **Ejecución**: `winuae_step`, `winuae_continue`, `winuae_pause`, `winuae_wait_stop`, `winuae_emulator_status`, `winuae_postmortem_capture`.
- **Hardware Amiga**: `winuae_custom_registers`, `winuae_bitmap_decode`, `winuae_copper_disassemble`, `winuae_disassemble_full`, `winuae_print`.
- **Entrada**: `winuae_input_key`, `winuae_input_event`, `winuae_input_joy`, `winuae_input_mouse`, `winuae_amiga_input_set`/`_state`, `winuae_amiga_enter_demo`.
- **Canal lateral / periférico**: `winuae_side_read`, `winuae_debugperiph`, `winuae_base`.
- **Captura / perfil**: `winuae_screenshot`, `winuae_profile`, `winuae_profile_ollama`, `winuae_exec_chunk`.

### 6.4 Capturas y memoria por MCP

- `winuae_screenshot`: `capture_mode` ∈ `auto|monitor|internal|host_window`. `monitor`/`internal` usan el buffer interno de WinUAE-DBG (`monitor screenshot`); `host_window` captura la ventana del host (P/Invoke) y cae a `CopyFromScreen` si sale negra.
- `winuae_memory_read`/`_dump` (hex + ASCII), `winuae_machine_snapshot` (ventanas de Chip/Fast RAM, ≤16 KiB por ventana), `winuae_memory_pattern_search` (patrón+stride, ≤256 KiB), `winuae_bitmap_decode` (decodifica un bitmap planar/interleaved a imagen con paleta).

### 6.5 Límites

Solo Windows; usar **build x86**; **sin acceso a CIA** (`$BFE001`/`$BFD000`); **una conexión GDB a la vez**; `winuae_disassemble` es básico (usar `_full`); escritura de memoria `M` puede no estar en todas las builds (el MCP pausa y prueba `X`). Detalle: `mcp-winuae-emu/README.md` y `debug-winuae-v2-guide.md`.

---

## 7. WinUAE-DBG (el fork)

Es **WinUAE 6.0** + el servidor GDB de BartmanAbyss + extensiones del fork (monitor v2.1/v2.2, canal lateral, periférico `0xB70000`, relocalización, puerto configurable).

### 7.1 Compilar

- Sistema de build: **MSBuild / Visual Studio** (no hay CMake para WinUAE). Solución: `od-win32\winuae_msvc15\winuae_msvc.sln`.
- Requisitos: VS con "Desarrollo para escritorio con C++"; **Platform Toolset v145** (o v143 en VS2022); Windows 10 SDK 10.0.17763+; WDK 16299+; `winuaeinclibs.zip` extraído a `C:\dev\include` y `C:\dev\lib`; `aros.rom.cpp.zip` extraído; **NASM** en el `PATH`.
- Salida: `bin\winuae-gdb.exe` (x86) y `bin\winuae-gdb-x64.exe` (x64). **Usar la x86.**

```powershell
# Recomendado (32-bit Release, "más estable")
./build.bat
./build.bat x64
# Si VS no está en la ruta por defecto:
$env:VS_PATH = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
build.bat
```

### 7.2 Estructura clave (para ampliar)

| Área | Fichero |
|---|---|
| Servidor GDB + canal lateral + comandos `monitor` | `od-win32\barto_gdbserver.cpp` (~5200 líneas) |
| Handlers de registros custom | `custom.cpp` (`custom_wget_1`, `custom_wput_1`, `custom_wput_copper`) |
| Cabecera/registros | `include\custom.h`; nombres en `identify.cpp` |
| Render por línea | `drawing.cpp` (`draw_denise_line`, `denise_thread`) |
| Preferencias/config | `cfgfile.cpp` (`struct uae_prefs` en `include\options.h`), `debugging_features`, `debugging_trigger` |
| Hooks de ciclo de vida | `devices.cpp`, `od-win32\win32.cpp`, `debug.cpp` |

### 7.3 Cómo ampliarlo

- **Nuevo comando `monitor foo`**: `monitor_foo_command()` en `barto_gdbserver.cpp` + rama en el dispatch de `qRcmd`; devolver texto por `qRcmd` (patrón de `status`); documentar en `GDB_MONITOR_COMMANDS.md`.
- **Nuevo comando del canal lateral**: rama en `side_channel_handle_command()`; si tiene efectos (render/input/profile) añadir el tipo a `enum class side_channel_action_type`, encolar en `side_channel_enqueue_action()` y ejecutar en `side_channel_process_actions()` (corre desde `vsync_pre`); respetar lock/auditoría.
- **Nuevo paquete RSP**: rama en `handle_packet()`.
- **Nuevo registro custom**: leer/escribir en `custom.cpp`; añadir nombre en `include\custom.h` e `identify.cpp`.
- **Nuevo fichero**: registrarlo en `winuae_msvc15\winuae_msvc.vcxproj` (y `.filters`).
- **No romper**: formato de `qOffsets`, `Z0`/`z0` ni los *ack* sin revisar el GDB fork y la extensión (ver `doc/BARTMAN-VSCODE-Y-EVOLUCION.md`).

Reglas para no romper la compatibilidad con la extensión y el MCP: `WinUAE-DBG/doc/DEBUGGING-ARCHITECTURE.md`, `doc/RELOCATION-FIX.md`, `docs/amiga-debug-profile-compatibility.md`.

### 7.4 Puertos y convivencia entre agentes

- **GDB**: `WINUAE_GDB_PORT` (def. **2345**). **Canal lateral**: `WINUAE_SIDE_CHANNEL_PORT` (def. **2346**). `WINUAE_GDB_PERSIST_LISTENER` mantiene el listener tras desconectar.
- Regla: **cada hilo/agente elige su par de puertos al empezar** y lo usa siempre; si un puerto está ocupado, **usar otro par** (no conectarse a instancias ajenas). **Nunca matar** procesos `winuae-gdb` ajenos: solo los propios (por PID). El runner tiene `--wait-port` y `--reset-emulator` (libera solo sus PIDs).

```bash
WINUAE_GDB_PORT=2355 WINUAE_SIDE_CHANNEL_PORT=2421 bash ./tools/run/run-demo.sh demos/techniques/amiga/setup/000_toolchain_cpp23
```

---

## 8. Capturas, secuencias de vídeo y volcado de memoria

### 8.1 Capturas de pantalla

- Runner del repo: guarda `out/run/<demo>/<cfg>/screenshot.png`; `--screenshot <ruta>` para elegir destino.
- Monitor WinUAE-DBG: `monitor screenshot <path>` (framebuffer interno).
- MCP: `winuae_screenshot` (modos `monitor`/`internal`/`host_window`).
- Canal lateral: `screenshot` (con lock `assist`); cliente `tools/debug/winuae-side-channel.sh screenshot <ruta>`.
- Framebuffer real por planos: `tools/vision-review/screendump-diff.mjs` (lea planos por canal lateral, `--addr`, `--planes`, `--row-bytes`, `--plane-bytes`, `--width`, `--height`).

### 8.2 Memoria (GDB y canal lateral)

- **GDB**: `winuae_memory_read`/`winuae_memory_dump` (hex), `winuae_machine_snapshot` (Chip/Fast RAM), `winuae_bitmap_decode`, `winuae_memory_pattern_search`. En crudo: paquetes `m`/`M`/`X`.
- **Canal lateral**: `mem <hex-addr> <len>` (≤4096) — usado por `tools/debug/profile.mjs` y `tools/debug/winuae-side-channel.sh mem ...`. Es la vía barata para volcar RAM y leer contadores sin parar la CPU.

### 8.3 Secuencias de frames / vídeo

- `run-demo.sh <demo> --sequence-frames N [--sequence-interval-ms M]` → `out/run/<demo>/<cfg>/sequence/frame_*.png`. Vaciado previo de `sequence/`.
- Frame-exacto: `--sequence-step-frames N` (usa el breakpoint `eng_debug_ready_probe`, 1 frame entre capturas); por valor de scroll: `--sequence-camera-x` / `--sequence-fine-x`.
- Perfil multi-frame (extensión "Profile (Multi)": 50 frames) o `winuae_profile`/`winuae_profile_ollama` (MCP): incrusta un **screenshot por frame** en el perfil y extrae `frame_%04d.png`.
- **Regla de oro (§9)**: capturar **secuencia**, no un frame suelto; el movimiento no se ve en una captura estática.

---

## 9. Análisis con Ollama

Ollama local en `127.0.0.1:11434`. Modelos por defecto: **visión** `qwen3-vl:8b-instruct-q8_0`, **texto** `gemma3:12b`/`qwen3:8b`. Variables: `OLLAMA_BASE`, `OLLAMA_VL_MODEL`, `OLLAMA_TEXT_MODEL`.

| Script (Amiga-Cpp) | Qué hace |
|---|---|
| `tools/profile/ai-analyze.mjs <outName> [frames] --demo <demo> --prompt "..."` | orquestador captura→extrae→Ollama; evidencia en `out/profile/<outName>/` |
| `tools/analyze/ollama-desc.mjs <dir> <idx...> ["prompt"]` | describe/critica frames concretos (visión) |
| `tools/analyze/ollama-frames.mjs <seqDir>` | continuidad de scroll con varias imágenes |
| `tools/analyze/ollama-compare-files.mjs <real> <esperado>` | compara imagen real vs esperada |
| `tools/analyze/ollama-profile-compare.mjs <perfil.amigaprofile> [idx...]` | compara capturas incrustadas de un perfil |
| `tools/analyze/profile-report.mjs <perfil> --ai [--model ...]` / `--vision [--frames ...]` | informe del perfil + análisis IA |
| `tools/vision-review/flicker-check.mjs --demo <ruta> [--no-ollama] [--no-detect]` | parpadeo (detector temporal OpenCV + visión) |
| MCP `winuae_profile_ollama` | captura por canal lateral + extrae frames + hoja de contacto (`ffmpeg`) + análisis |

El veredicto de visión es **filtro de sospecha, no prueba**: no pedir coordenadas en píxeles; usar la detección temporal (`tools/vision-review/temporal-detect.py`) para localizar la región y luego la visión para confirmar. Metodología: `Amiga-Cpp/docs/guides/methodology/DEMO_VISUAL_DEBUG.md`.

---

## 10. Perfilado (`.amigaprofile` de la extensión) y su análisis

### 10.1 Capturar

- **Extensión VS Code**: barra de debug → **Frame Profiler** (`amiga.startProfile`, 1 frame) o **Profile (Multi)** (`amiga.startProfileMulti`, 50). Flujo: genera `.unwind` (objdump `--dwarf=frames-interp`), ejecuta `monitor profile <n> "<unwind>" "<out>"`, resuelve símbolos y escribe un `.amigaprofile` (JSON) en el directorio temporal (`amiga-profile-AAAA.MM.DD-HH.MM.SS.amigaprofile`). También: **Size Profiler** y **Shrinkler** (`*.shrinklerstats`).
- **Sin VS Code** (repo):
  ```bash
  node tools/debug/winuae-profile.mjs <demo> [CONFIG] [frames]
  node tools/profile/capture-profile.mjs <outFile> [frames] [--port 2346] [--wait-cmd '...' --contains 'READY']
  ```
- **MCP**: `winuae_profile` (`num_frames`, `out_file`, `unwind_file`).

### 10.2 Analizar (scripts del repo)

```bash
node tools/analyze/profile-report.mjs <perfil.amigaprofile> [--top N] [--json]
node tools/analyze/profile-report.mjs <perfil.amigaprofile> --ai  [--model gemma3:12b]
node tools/analyze/profile-report.mjs <perfil.amigaprofile> --vision [--frames 0,1,2,3]
node tools/analyze/profile-samples.mjs <out/tmp/wprof-*.bin> <demo> [CONFIG] [--top N] [--json]
node tools/debug/profile.mjs <demo> [CONFIG] [segundos]     # perfil por secciones del engine (g_eng_prof)
```

- El `.amigaprofile` es **Chrome DevTools JSON** (`nodes/samples/timeDeltas`) + extensiones `$amiga` (`customRegs`, `agaColors`, `dmaRecords`, `gfxResources`, `idleCycles`, `pcTrace`…) y `$base` (`objdump`, Chip/Bogo RAM base64, símbolos, secciones), con `screenshots` incrustados; multi-frame añade `<tmp>_NN.amigaprofile`. Se abre también en <https://speedscope.app> o en la vista del perfil de la extensión (flame-graph, Assembly, Screen/Denise).
- **Regla**: cruzar **muestreo** (`.amigaprofile`) con **instrumentado** (`tools/debug/profile.mjs`, secciones `ENG_PROF`). Empezar por `--ai` y `--vision`. Guía: `Amiga-Cpp/docs/tools/PROFILING_FROM_AGENT.md`.

---

## 11. Verificación y regresión (Amiga-Cpp)

- `& 'C:\Program Files\Git\bin\bash.exe' ./tools/test-regression.sh --demo <ruta> [--warp]` → build→run→analyze + gates estáticos (type-tagging, encoding, links, generic-headers, api-facade, doc-index, asset-manifests, asm-audit, codegen-report). Opt-in: `--pixel-assert`, `--vision-review`, `--flicker`, `--build-all`, `--protect`, `--fps-gate`. Informe en `out/regression/<timestamp>/`.
- Gates de fps/codegen: `tools/debug/measure-fps.mjs`, `check-fps.mjs`, `tools/analyze/asm-audit.mjs`.
- `bash ./tools/run-host-tests.sh` → tests host + gates.

---

## 12. Solución de problemas: síntoma → herramienta

| Síntoma / objetivo | Herramienta |
|---|---|
| ¿Está corriendo el emulador? ¿en qué frame? | `winuae_emulator_status` |
| No llega el `READY` esperado | `winuae_side_read` con `runstatus <addr>`; `--require-ready`/`--allow-timeout-fallback` del runner; revisar `run-report.json` |
| El build falla con libcalls (`__mulsi3`, `__divsf3`…) | `docs/reference/toolchain/m68k-gcc.md`: usar `u16`, evitar `u32 * /` y float en `-nostdlib` |
| Instrucción ilegal tras compilar `-m68020` | el quickstart no fijó la CPU: `WINUAE_QUICKSTART=a1200,0` (o cd32) + fuerza `cpu_model=68020` |
| Puerto GDB/canal ocupado | elegir otro par (`WINUAE_GDB_PORT`/`WINUAE_SIDE_CHANNEL_PORT`); `--wait-port`/`--reset-emulator` |
| Alguien toca una dirección (CPU/Copper/Blitter) | `winuae_watchpoint_set_ext` con `src=` + `winuae_watchpoint_last` |
| El Copper escribe un registro en cierta línea | `winuae_watchpoint_set_ext { address:"0xdff180", access:"w", source:"copper" }` |
| El Blitter pisa un buffer | `winuae_watchpoint_set_ext { source:"blitter", access:"w" }` |
| Congelar o forzar un valor (cheat) | `winuae_protect { action:"block"|"set", ... }` |
| Coste por segmento del frame | *checkpoint profiler* (`winuae_debugperiph checkpoints`) |
| Qué cambió entre dos momentos | `winuae_rewind` + `winuae_side_read state` |
| Capturar pantalla como evidencia | `winuae_screenshot` o `winuae-side-channel.sh screenshot` |
| Ver movimiento, no un frame suelto | `run-demo.sh <demo> --sequence-frames N` + Ollama (§9) |
| GDB se queda inerte | usar el canal lateral (`winuae_side_read`) |
| Sin acceso a CIA (`$BFE001`/`$BFD000`) | limitación conocida: leer esos datos por memoria del periférico o `winuae_debugperiph` |

Referencia ampliada (watchpoints con origen, periférico, límites): `docs/debugging/system/debug-winuae-v2-guide.md`.

## 13. Referencias (fuente de verdad)

- Build/run del repo: `docs/build/BUILD_AND_RUN.md`, `docs/build/amiga-binary-and-disk-formats.md`.
- Depuración WinUAE/MCP/canal lateral: `docs/debugging/system/debug-winuae-v2-guide.md`.
- Toolchain/gcc: `docs/reference/toolchain/m68k-gcc.md`.
- Visual/Ollama: `docs/guides/methodology/DEMO_VISUAL_DEBUG.md`.
- Perfilado: `docs/tools/PROFILING_FROM_AGENT.md`.
- Navegación IA: `docs/ai-dev-environment/DOC-MAP-PRINCIPAL.md`.
- Extensión: `../vscode-amiga-debug/README.md`, `../vscode-amiga-debug/template/Makefile`, `../vscode-amiga-debug/template/.vscode/launch.json`.
- WinUAE-DBG: `../WinUAE-DBG/README.md`, `../WinUAE-DBG/GDB_MONITOR_COMMANDS.md`, `../WinUAE-DBG/docs/WINUAE-MONITOR-EXTENSIONS.md`.
- MCP: `../mcp-winuae-emu/README.md`.

> Nota: documento transversal a los cuatro proyectos del workspace; vive en `docs/ai-dev-environment/` del repo `Amiga-Cpp`.
