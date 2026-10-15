# Reinstalación del entorno Amiga en un equipo nuevo

Guía mínima para volver a montar el entorno de depuración Amiga (F5 en VS Code)
desde cero. Orden de instalación recomendado.

## 1. Repositorios necesarios

| Repo | Ruta típica | Para qué |
|------|-------------|----------|
| `dvdjg/Amiga-Cpp` | `...\Amiga\Amiga-Cpp` | Engine + demos + tooling (`tools/`, `scripts/`) |
| `dvdjg/WinUAE-DBG` | `...\Amiga\WinUAE-DBG` | Emulador con gdbserver + canal lateral (fork de WinUAE) |
| `dvdjg/vscode-amiga-debug` | `...\Amiga\vscode-amiga-debug` | Extensión de depuración (fork con relocalización + addr2line) |
| `dvdjg/mcp-winuae-emu` | `...\Amiga\mcp-winuae-emu` | Conector IA↔WinUAE (RSP). Lo usa `tools/run/run-demo.ts` |

La extensión de VS Code debe quedar como **`bartmanabyss.amiga-debug-1.8.2`**
(la instalada hoy). El tooling de Amiga-Cpp resuelve `bartmanabyss.amiga-debug-*`
(versión más alta), así que no depende del número exacto.

## 2. Compilar WinUAE-DBG y desplegarlo

```bat
cd WinUAE-DBG
build.bat
```

`build.bat` compila `bin\winuae-gdb.exe` (Win32) y el target
`CopyToAmigaDebug` lo copia a las extensiones Cursor/VS Code encontradas.
También se puede usar:

```bash
bash scripts/install-winuae-dbg-to-extension.sh   # desde Amiga-Cpp
```

Comprobar que `winuae-gdb.exe` existe en
`%USERPROFILE%\.vscode\extensions\bartmanabyss.amiga-debug-1.8.2\bin\win32\`.

## 3. Compilar e instalar la extensión (fork)

```bash
cd vscode-amiga-debug
npm install
npm run vscode:prepublish          # genera dist/
npx @vscode/vsce package           # genera amiga-debug-1.8.2.vsix
```

Instalar el `.vsix` en VS Code: Extensions → Install from VSIX.
La extensión usa **modo embebido** (`EMBED_DEBUG_ADAPTER=true`); no depende de
`debugAdapter.js` standalone.

## 4. Prerrequisitos de VS Code / proyecto

- **Kickstart**: copiar `kick13.rom` a `C:\Amiga\KICK13.rom` (o ajustar
  `kickstart` en `.vscode/launch.json`).
- **Herramientas**: Windows + Git Bash + Node.js.
- **`.mcp.json`**: ajustar `WINUAE_PATH` a la ruta de la extensión instalada.
- **Variable de entorno** (opcional): `AMIGA_BIN_PATH` apuntando a
  `%USERPROFILE%\.vscode\extensions\bartmanabyss.amiga-debug-1.8.2\bin\win32`.
  Si está desactualizada (p. ej. `1.8.2` inexistente), el build hace fallback
  a la extensión, pero conviene corregirla.

## 5. Compilar una demo y depurar

```bash
# Compila la demo del archivo en primer plano y deja el ejecutable en
# out/debug-current/current (.elf/.exe). Usa -O0 (variables fiables).
bash ./tools/debug/build-current-demo.sh "demos/techniques/amiga/blitter/050_blitter_bobs/src/main.cpp"
```

En VS Code: abre `demos/techniques/amiga/blitter/050_blitter_bobs/src/main.cpp`, pon breakpoints, F5.
Configuración en `.vscode/launch.json` (`type: "amiga"`, `breakpointRelocation: true`).

### `.vscode/` es local (gitignored)

La carpeta `.vscode/` NO se versiona (`.*/` en `.gitignore`): cada máquina
tiene su propia config de editor. Al montar un equipo nuevo hay que recrearla:

**`.vscode/launch.json`** (solo configs `amiga`: **debug** y **optimizada**, las dos
compilan el `src` del archivo ACTIVO (`${file}`); NO incluir la "C/C++ Runner"
generada por cpptools, que referencia `outDebug` y rutas absolutas):

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "type": "amiga",
      "request": "launch",
      "name": "Amiga 500: depurar archivo actual (debug)",
      "preLaunchTask": "Amiga: compilar archivo actual",
      "config": "A500",
      "program": "${workspaceFolder}/out/debug-current/current",
      "stopOnEntry": false,
      "breakpointRelocation": true,
      "kickstart": "C:/Amiga/KICK13.rom",
      "stack": "65536",
      "emuargs": ["-norawinput_mouse"],
      "internalConsoleOptions": "openOnSessionStart"
    },
    {
      "type": "amiga",
      "request": "launch",
      "name": "Amiga 500: ejecutar archivo actual (optimizado)",
      "preLaunchTask": "Amiga: compilar archivo actual (optimizado)",
      "config": "A500",
      "program": "${workspaceFolder}/out/debug-current/current_opt",
      "stopOnEntry": false,
      "breakpointRelocation": false,
      "kickstart": "C:/Amiga/KICK13.rom",
      "stack": "65536",
      "emuargs": ["-norawinput_mouse"],
      "internalConsoleOptions": "openOnSessionStart"
    }
  ]
}
```

**`.vscode/tasks.json`**:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "Amiga: compilar archivo actual",
      "type": "process",
      "command": "C:\\\\Program Files\\\\Git\\\\bin\\\\bash.exe",
      "args": ["-lc", "./tools/debug/build-current-demo.sh \"$(cygpath -u '${file}')\""],
      "options": { "cwd": "${workspaceFolder}" },
      "problemMatcher": []
    },
    {
      "label": "Amiga: compilar archivo actual (optimizado)",
      "type": "process",
      "command": "C:\\\\Program Files\\\\Git\\\\bin\\\\bash.exe",
      "args": ["-lc", "./tools/debug/build-current-demo-opt.sh \"$(cygpath -u '${file}')\""],
      "options": { "cwd": "${workspaceFolder}" },
      "problemMatcher": []
    }
  ]
}
```

La config **(debug)** compila con `--debug` (-O1, depuracion fiable); la **(optimizado)** con `--release` (-O2). La unica diferencia es el nivel de optimizacion, y las dos compilan el `src` del archivo ACTIVO.

**`.vscode/c_cpp_properties.json`** (IntelliSense → compilador Amiga m68k, no `gcc`/MSVC):

```json
{
  "configurations": [
    {
      "name": "Amiga-m68k",
      "includePath": [
        "${workspaceFolder}",
        "${workspaceFolder}/engine/include",
        "${env:USERPROFILE}/.vscode/extensions/bartmanabyss.amiga-debug-1.8.2/bin/win32/opt/m68k-amiga-elf/sys-include"
      ],
      "compilerPath": "${env:USERPROFILE}/.vscode/extensions/bartmanabyss.amiga-debug-1.8.2/bin/win32/opt/bin/m68k-amiga-elf-gcc.exe",
      "cStandard": "gnu11",
      "cppStandard": "gnu++23",
      "intelliSenseMode": "linux-gcc-x64",
      "compilerArgs": ["-m68000"]
    }
  ],
  "version": 4
}
```

**`.vscode/settings.json`** (mínimo; sin bloque `C_Cpp_Runner.*`):

```json
{
  "amiga.program": "out/debug-current/current",
  "amiga.rom-paths.A500": "C:/Amiga/KICK13.rom",
  "terminal.integrated.defaultProfile.windows": "Git Bash"
}
```

## 6. Ejecución automática y canal lateral

```bash
bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/050_blitter_bobs        # ejecuta + captura
bash ./tools/test-regression.sh --demo demos/techniques/amiga/playfield/101_ehb_tile_scroll_driver --warp
```

El **canal lateral** de WinUAE-DBG escucha en `127.0.0.1:2346`:

```bash
bash ./tools/debug/amiga-session.sh state|regs|pause|resume|mem|screenshot|...
```

## 7. Prueba rápida de depuración (harness DAP)

Ver `tools/dap-test/README.md`: lanza el adaptador standalone y verifica que un
breakpoint se detiene y resuelve fuente C++.

## 8. Compilador nativo `g++` para los tests host

Los tests host (`tests/host/`, `tools/run-host-tests.sh`) compilan las APIs puras
del engine (matemáticas, ordenación, `SpriteAllocator`…) con un **g++ nativo** de
Windows (mingw), no con el cruce m68k. No viene con la extensión de VS Code.

Instalar WinLibs (GCC mingw-w64, ucrt, posix-seh) y descomprimir en
`C:\Users\dvdjg\Documents\programa\AI\Amiga\mingw64` (u otra ruta), y luego correr:

```bash
CXX="C:/Users/dvdjg/Documents/programa/AI/Amiga/mingw64/bin/g++.exe" \
  bash tools/run-host-tests.sh
```

Sin `CXX`, el script busca `g++` en el `PATH`. Requiere gcc ≥ 15 para C++23 (el
toolchain m68k del proyecto es 15.1).

## Estado comprobado de la máquina de referencia

Referencia de una instalación operativa (verificado 2026-10; al montar otra máquina, ajustar versiones y rutas):

- **Ollama** `0.32.14` responde en `http://127.0.0.1:11434`; modelo multimodal predeterminado `qwen3-vl:8b-instruct-q8_0` (visión); `gemma3:12b` como alternativa para capturas estáticas (no para secuencias). El endpoint `/v1/chat/completions` es compatible con `tools/vision-review/vision-review.ts`.
- **`mcp-winuae-emu`** expone conexión, carga Hunk, ejecución, breakpoints, stepping, memoria, registros, custom registers, Copper, capturas, perfiles, entrada, snapshots y postmortems.
- **`Amiga-Cpp`** tiene runner `build -> run -> analyze`, FrameScope, aserciones de píxel y canal lateral con `poke`, `rollback`, auditoría, `pause` y `resume`.
- La extensión Bartman está recomendada en `legacy/.vscode/extensions.json`; sus perfiles A500, en `legacy/.vscode/launch.json` y `settings.json`.
- **Binario activo de WinUAE-DBG**: `WinUAE-DBG\build.bat` compila `bin\winuae-gdb.exe` y el target `CopyToAmigaDebug` (script `deploy-winuae-to-amiga-debug.ps1`) lo copia a las instalaciones Bartman de `.cursor\extensions` y `.vscode\extensions`. En esta máquina las tres copias están sincronizadas con hash común `36864B4F6815AF27BD33B64D23764FBE91044249F89C0E11225342A43BF35A38` (2026-10-08): extensión VS Code `bartmanabyss.amiga-debug-1.8.2`, extensión Cursor `bartmanabyss.amiga-debug-1.7.9` y `WinUAE-DBG\bin`. Recompilar y ejecutar el target de despliegue es la forma de re-sincronizarlas; extensión y ejecutable se actualizan como pareja cuando cambie la relocalización o el protocolo.
- **ROM objetivo A500/Kickstart 1.3**: `C:\Amiga\KICK13.rom`, 262144 bytes, SHA-256 `EE05862D8102A08436AC4056DA7D549DB31625C7D47B24DFB7B3C9A5C113CA53`. La carpeta puede contener ADFs/capturas de referencia; no se copian ROMs ni material de terceros al repositorio.

## Notas sobre versiones

- La extensión instalada en VS Code es el **fork 1.8.2** (con los fixes de
  relocalización/addr2line); en `.cursor/extensions` queda una copia antigua 1.7.9.
- El fork NO debe fusionar a ciegas el upstream de Bartman: sus fixes
  personalizados no están en upstream y el merge rompería el flujo (ver
  `docs/debugging/system/historial-cambios.md`).
- `-O0` es obligatorio para depurar con variables; `-O1` optimiza y GDB las
  pierde (`context` synthetic pointer, `saved_background <optimized out>`).
