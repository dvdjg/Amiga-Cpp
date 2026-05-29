# Build y ejecucion de demos

El engine usa un sistema de demos independientes para evitar mezclar el codigo nuevo
con el ejemplo C historico del workspace.

## Requisitos

- Plugin `BartmanAbyss/vscode-amiga-debug` instalado en Cursor o VS Code.
- Toolchain `m68k-amiga-elf` incluido en el plugin.
- Kickstart A500 configurada en el workspace para ejecucion manual desde el debugger.

El script busca el toolchain en este orden:

1. Variable de entorno `AMIGA_BIN_PATH`.
2. Extension de Cursor `bartmanabyss.amiga-debug-1.8.2`.
3. Extension de VS Code `bartmanabyss.amiga-debug-1.8.2`.

## Compilar una demo

```powershell
.\tools\build\build-demo.ps1 demos\000_toolchain_cpp23 -Clean
```

La salida queda en:

```text
out\demos\<demo>\<demo>.elf
out\demos\<demo>\<demo>.exe
out\demos\<demo>\<demo>.map
out\demos\<demo>\<demo>.s
```

## Analizar una demo

```powershell
.\tools\analyze\analyze-demo.ps1 demos\000_toolchain_cpp23
```

Esta primera version comprueba que existen los artefactos y que el mapa contiene
simbolos basicos de arranque. Si ya existe una captura en `out\run\<demo>`, tambien
ejecuta un analisis visual automatico de la imagen.

## Ejecutar una demo

```powershell
.\tools\run\run-demo.ps1 demos\000_toolchain_cpp23
```

El runner:

- copia la demo a un directorio temporal como `dh1:a.exe`;
- genera una configuracion WinUAE temporal basada en `config\mcp-amiga-c-debug.uae`;
- fuerza opciones anti-captura del raton para que WinUAE no atrape el puntero
  del sistema durante pruebas automatizadas;
- escribe un `startup-sequence` temporal;
- lanza `winuae-gdb.exe`;
- conecta al servidor GDB de WinUAE-DBG;
- continua la ejecucion tras el `debugging_trigger`;
- intenta conectar al canal lateral de WinUAE-DBG en `127.0.0.1:2346`;
- si la demo expone `g_amg_run_status`, espera `READY` leyendo memoria por el
  canal lateral sin detener el 68000;
- si el canal no esta disponible, deja constancia en `run-report.json` y usa el
  timeout de compatibilidad;
- guarda una captura PNG con el monitor de WinUAE;
- cierra el emulador y restaura el `startup-sequence` anterior.

La captura queda en:

```text
out\run\<demo>\screenshot.png
out\run\<demo>\run-report.json
```

## Telemetria de ejecucion

Las demos modernas definen un simbolo global `g_amg_run_status` con magia
`AMGR`. El runner resuelve su direccion desde el `.map`, consulta las secciones
runtime que expone WinUAE-DBG por el canal lateral y espera estados cortos:

- `InitStarted`: la demo entro en `init`.
- `Ready`: la demo instalo sus recursos y ya esta renderizando frames.
- `Failed`: la demo fallo de forma controlada y deja un codigo `detail`.

Esto evita usar capturas como unica senal de vida. Las capturas siguen siendo la
evidencia visual final, pero el runner ya sabe si el programa llego a READY antes
de pedir la imagen.

Opciones utiles:

```powershell
.\tools\run\run-demo.ps1 demos\030_ehb_palette_zones `
  -SideChannelTimeoutMs 6000 `
  -SideChannelPort 2346
```

Para consultar una instancia viva manualmente:

```powershell
.\tools\debug\winuae-side-channel.ps1 state
.\tools\debug\winuae-side-channel.ps1 regs
.\tools\debug\winuae-side-channel.ps1 mem 0xdff000 32
```

## Automatizar el raton emulado

Para mover el raton del Amiga sin usar ni capturar el raton fisico de Windows:

```powershell
.\tools\run\run-demo.ps1 demos\000_toolchain_cpp23 `
  -WaitMs 3000 `
  -MouseFrom 32,40 `
  -MouseTo 280,170 `
  -MouseControl 160,10 `
  -MouseClick
```

La herramienta envia comandos `input mouse abs` e `input mouse button` por el
monitor de WinUAE-DBG. Soporta trayectorias lineales, Bezier cuadraticas y Bezier
cubicas. El camino integrado en el runner es el recomendado para regresiones; el
script `tools\input\mouse-path.ps1` queda disponible para sesiones donde ya haya
un servidor GDB aceptando conexiones. Ver `docs\MOUSE_AUTOMATION.md`.

## Analizar una captura

```powershell
.\tools\analyze\analyze-screenshot.ps1 out\run\000_toolchain_cpp23\screenshot.png
```

El analizador actual verifica dimensiones y presencia de los colores de overlay
esperados por la demo 000. Las siguientes demos tendran analizadores mas especificos
para paletas, sprites, scroll y profiler.

Si una demo define:

```text
demos\<demo>\analyze-screenshot.ps1
```

`analyze-demo.ps1` usa ese analizador especifico en lugar del generico. Esto permite
que demos close-to-the-metal, como una prueba de Copper a pantalla completa, validen
la imagen por criterios propios.

## Regresion completa

```powershell
.\tools\test-regression.ps1
```

La regresion descubre las carpetas dentro de `demos`, y para cada una ejecuta:

1. build;
2. run/captura;
3. analisis.

El informe queda en:

```text
out\regression\<timestamp>\regression-report.md
out\regression\<timestamp>\regression-report.json
```

Opciones utiles:

```powershell
.\tools\test-regression.ps1 -SkipRun
.\tools\test-regression.ps1 -Demo demos\000_toolchain_cpp23
.\tools\test-regression.ps1 -ReleaseBuild
.\tools\test-regression.ps1 -KeepGoing
```
