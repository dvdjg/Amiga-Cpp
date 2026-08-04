# WinUAE y generación de ADF

## 1. Cargar binarios en WinUAE

Tienes WinUAE en `C:\Program Files\WinUAE\winuae64.exe`. Tres formas de cargar el ejecutable:

### A) ADF bootable (recomendado para demos)

1. Genera el ADF: `make adf` o `bash scripts/create-adf.sh`
2. En WinUAE: F12 → Floppy drives → DF0 → selecciona `out/disk.adf`
3. Arranca el emulador: bootea desde DF0 y el demo se ejecuta solo

### B) Montar directorio como disco duro

1. Crea una carpeta con el .exe (ej. `out/a.exe`)
2. En WinUAE: F12 → Hard drives → Add Directory → selecciona la carpeta (ej. `out`)
3. Asigna como DH0: o similar
4. Arranca con Kickstart + Workbench (o sistema base)
5. En Shell: `cd DH0:` y `a` (o el nombre del ejecutable)

### C) ADF con Workbench

1. Monta un ADF de Workbench en DF0
2. Genera un ADF con tu ejecutable (sin boot block) y monta en DF1, o copia el .exe a un ADF existente
3. Arranca WB, abre el disco con el .exe y ejecútalo

---

## 2. Si el ADF se queda en negro

El proyecto enlaza con `-Ttext=0x400` para no pisar la tabla de vectores del 68000 (0-0x3FF). Si aún así falla:

1. **Probar sin boot**: arranca Workbench desde otro disco (DF0), monta el ADF en DF1, y ejecuta `DF1:a`. Si funciona, el ejecutable está bien y el problema es el boot block.
2. **Compatibilidad exe2adf**: algunos ejecutables (por tamaño, formato hunk o dependencias) no funcionan bien con el boot block de exe2adf.
3. **Alternativa**: crear un ADF con Workbench y copiar el .exe; arrancar desde ese disco.

---

## 3. Generar ADF desde los binarios

### Opción: exe2adf (la más simple)

[exe2adf](https://www.exe2adf.com/) crea ADF bootables desde ejecutables en segundos.

1. Descarga exe2adf para Windows: https://www.exe2adf.com/downloads/exe2adf-v03e-windows.zip
2. Extrae en `tools/exe2adf/` (o en el PATH)
3. Uso básico:
   ```
   exe2adf -i out/a.exe -a out/disk.adf
   ```
4. O modo rápido (crea disk.adf con el mismo nombre base):
   ```
   exe2adf out/a.exe
   ```

El script `scripts/create-adf.sh` automatiza esto si exe2adf está instalado.

### Opción: adftools (como vscode-amiga-assembly)

La extensión [vscode-amiga-assembly](https://github.com/prb28/vscode-amiga-assembly) usa **adftools** (adfcreate, adfinst, adfcopy) y **ADFLib**. Requiere:

- ADFTools: https://github.com/bos4711/adftools
- ADFLib: https://github.com/laurentc1/adflib

Flujo típico:
1. `adfcreate` – crear ADF vacío
2. `adfinst` – instalar boot block (opcional, para boot directo)
3. `adfcopy` – copiar el .exe al ADF

### Boot block (boot directo)

Para que el ADF arranque solo sin Workbench, el disco necesita un boot block que cargue y ejecute el .exe. exe2adf lo genera automáticamente. Con adftools hay que crear un boot block en ensamblador y pasarlo con `bootBlockSourceFile`.

---

## 4. WinUAE en otra ruta (C:\Program Files\WinUAE\)

Para usar la copia en `C:\Program Files\WinUAE\winuae64.exe`:

1. **Ejecutar WinUAE** y cargar ADF manualmente:
   - Abre `"C:\Program Files\WinUAE\winuae64.exe"`
   - F12 → Floppy drives → DF0 → busca `out/disk.adf` (ruta completa: `C:\Users\...\Cursor-Amiga-C\out\disk.adf`)

2. **Crear un .uae** para arrancar directo:
   - Crea `out/boot.uae` con:
   ```ini
   ; boot.uae - boot desde disk.adf
   floppy0=./disk.adf
   ; kickstart_rom_file=path\to\kick31.rom  ; descomenta si necesitas ROM
   ```
   - Ejecuta: `"C:\Program Files\WinUAE\winuae64.exe" -f out\boot.uae`

3. **Montar directorio** (sin ADF): F12 → Hard drives → Add Directory → selecciona `out`

---

## 5. Depuración con MCP winuae-emu

En este proyecto la depuración se hace con el MCP **winuae-emu** (no con dap-proxy). El MCP lanza WinUAE (fork BartmanAbyss con GDB RSP) y permite cargar el binario, poner breakpoints, inspeccionar memoria y registros ($DFFxxx, copper, etc.).

**Requisito:** El MCP necesita el ejecutable **winuae-gdb.exe** en una ruta concreta. Si al llamar `winuae_connect` aparece un error del tipo *"winuae-gdb.exe not found at ..."*:

1. Descarga el binario desde: https://github.com/BartmanAbyss/vscode-amiga-debug/tree/master/bin/win32  
2. Coloca `winuae-gdb.exe` en el **directorio que contiene** el ejecutable (p. ej. `.../dist/bin/winuae/`). En `.cursor/mcp.json`, `WINUAE_PATH` debe ser esa carpeta, no una subcarpeta `winuae` dentro.

**Crash de WinUAE al usar el MCP:** Si al usar `winuae_connect` / `winuae_load` la ventana de WinUAE se cierra y se generan `.dmp` en la carpeta de `winuae-gdb.exe`, es un fallo del emulador. **Solución recomendada:** usar el flujo "Depuración independiente" (5.1) para que el agente pueda depurar sin depender de F5.

### 5.1 Depuración independiente (evitar crash; el agente puede depurar solo)

Para que la IA pueda depurar **sin** que WinUAE caiga y **sin** que tengas que lanzar F5 tú:

1. **Arrancar WinUAE con GDB y ADF** (el programa arranca desde el disco, no se carga por GDB):
   ```powershell
   $env:WINUAE_PATH = "c:/Users/dvdjg/.cursor/extensions/prb28.amiga-assembly-1.8.14-universal/dist/bin/winuae"
   $env:WINUAE_CONFIG = "C:/Amiga/A500-Dev.uae"
   .\scripts\start-winuae-for-mcp-debug.ps1
   ```
   El script compila, genera el ADF y lanza `winuae-gdb.exe` con servidor GDB y DF0:=`out/disk.adf`. No uses `winuae_connect` (que lanzaría otro WinUAE).

2. **Cuando la Amiga haya arrancado** (menú o demo en pantalla), en Cursor el agente debe llamar **`winuae_connect_existing`** (conectar al GDB ya abierto). **No** debe llamar a `winuae_load` (el programa ya está en memoria desde el boot).

3. A partir de ahí: breakpoints (`winuae_breakpoint_set`), `winuae_continue`, `winuae_pause`, `winuae_step`, `winuae_registers_get`, `winuae_memory_read`, `winuae_custom_registers`, etc.

Si prefieres que el MCP lance WinUAE y cargue el binario: se ha añadido escritura de memoria por fragmentos (4 KB) en el MCP para reducir el riesgo de crash en `winuae_load`; aun así, si sigue cayendo, usa el flujo 5.1.

**Flujo desatendido (recomendado):** El agente lanza WinUAE y conecta sin intervención: 1) `make adf`. 2) `winuae_insert_disk` con ruta a `out/disk.adf`. 3) `winuae_connect` (arranca con `use_gui=no` y espera al GDB). 4) Breakpoints e inspección. No usar `winuae_load` si se arrancó desde ADF. Opcionalmente en el env del MCP: `WINUAE_GDB_INITIAL_DELAY_MS=3000`, `WINUAE_GDB_MAX_ATTEMPTS=60` si la conexión tarda. Ver regla `.cursor/rules/amiga-verification-flow.mdc` y `doc/agent-runbook.md`.

---

## Resumen rápido

| Método              | Comando / pasos                                             |
|---------------------|-------------------------------------------------------------|
| ADF bootable        | `scripts/create-adf.sh` → WinUAE DF0 = out/disk.adf         |
| Directorio          | WinUAE Hard drives → Add Directory = carpeta `out`          |
| exe2adf manual      | `exe2adf -i out/a.exe -a out/disk.adf`                      |
