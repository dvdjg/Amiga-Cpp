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

La extensión de VS Code debe quedar como **`bartmanabyss.amiga-debug-1.8.1`**
(el fork). El tooling de Amiga-Cpp resuelve `bartmanabyss.amiga-debug-*`
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
`%USERPROFILE%\.vscode\extensions\bartmanabyss.amiga-debug-1.8.1\bin\win32\`.

## 3. Compilar e instalar la extensión (fork)

```bash
cd vscode-amiga-debug
npm install
npm run vscode:prepublish          # genera dist/
npx @vscode/vsce package           # genera amiga-debug-1.8.1.vsix
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
  `%USERPROFILE%\.vscode\extensions\bartmanabyss.amiga-debug-1.8.1\bin\win32`.
  Si está desactualizada (p. ej. `1.8.2` inexistente), el build hace fallback
  a la extensión, pero conviene corregirla.

## 5. Compilar una demo y depurar

```bash
# Compila la demo del archivo en primer plano y deja el ejecutable en
# out/debug-current/current (.elf/.exe). Usa -O0 (variables fiables).
bash ./tools/debug/build-current-demo.sh "demos/050_blitter_bobs/src/main.cpp"
```

En VS Code: abre `demos/050_blitter_bobs/src/main.cpp`, pon breakpoints, F5.
Configuración en `.vscode/launch.json` (`type: "amiga"`, `breakpointRelocation: true`).

### `.vscode/` es local (gitignored)

La carpeta `.vscode/` NO se versiona (`.*/` en `.gitignore`): cada máquina
tiene su propia config de editor. Al montar un equipo nuevo hay que recrearla:

**`.vscode/launch.json`** (solo la config `amiga`; NO incluir la "C/C++ Runner"
generada por cpptools, que referencia `outDebug` y rutas absolutas):

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "type": "amiga",
      "request": "launch",
      "name": "Amiga 500: depurar archivo actual",
      "preLaunchTask": "Amiga: compilar archivo actual",
      "config": "A500",
      "program": "${workspaceFolder}/out/debug-current/current",
      "stopOnEntry": false,
      "breakpointRelocation": true,
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
    }
  ]
}
```

**`.vscode/c_cpp_properties.json`** (IntelliSense → compilador Amiga m68k, no `gcc`/MSVC):

```json
{
  "configurations": [
    {
      "name": "Amiga-m68k",
      "includePath": [
        "${workspaceFolder}",
        "${workspaceFolder}/engine/include",
        "${env:USERPROFILE}/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/win32/opt/m68k-amiga-elf/sys-include"
      ],
      "compilerPath": "${env:USERPROFILE}/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/win32/opt/bin/m68k-amiga-elf-gcc.exe",
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
bash ./tools/run/run-demo.sh demos/050_blitter_bobs        # ejecuta + captura
bash ./tools/test-regression.sh --demo demos/101_ehb_tile_scroll_driver --warp
```

El **canal lateral** de WinUAE-DBG escucha en `127.0.0.1:2346`:

```bash
bash ./tools/debug/amiga-session.sh state|regs|pause|resume|mem|screenshot|...
```

## 7. Prueba rápida de depuración (harness DAP)

Ver `tools/dap-test/README.md`: lanza el adaptador standalone y verifica que un
breakpoint se detiene y resuelve fuente C++.

## Notas sobre versiones

- La extensión instalada en este equipo es el **fork 1.8.1** (con los fixes de
  relocalización/addr2line). La 1.7.9 sobra.
- El fork NO debe fusionar a ciegas el upstream de Bartman: sus fixes
  personalizados no están en upstream y el merge rompería el flujo (ver
  `docs/debugging/HISTORIAL-CAMBIOS.md`).
- `-O0` es obligatorio para depurar con variables; `-O1` optimiza y GDB las
  pierde (`context` synthetic pointer, `saved_background <optimized out>`).
