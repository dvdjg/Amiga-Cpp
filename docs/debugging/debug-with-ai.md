# Depuración autónoma con IA (Cursor + Amiga)

Este documento describe cómo dejar que la IA depure y analice el proyecto Amiga de forma autónoma: puntos de interrupción, inspección de memoria y registros, call stack, variables, y uso del overlay/pantalla.

**Flujo MCP (WinUAE-GDB, compilar → desplegar → verificar, multi-máquina, `winuae_exec_chunk`, modos live):** ver [mcp-live-coding-workflow.md](mcp-live-coding-workflow.md).

## Arquitectura

1. **vscode-amiga-debug** ([GitHub](https://github.com/BartmanAbyss/vscode-amiga-debug))
   - Compila con GCC (Makefile), lanza WinUAE/FS-UAE y GDB.
   - Implementa el **Debug Adapter Protocol (DAP)** (`type: "amiga"`).
   - Soporta: breakpoints en C/C++, memoria, registros (CPU + Custom), call stack, watches, desensamblado, overlay de debug en WinUAE, frame profiler, size profiler.

2. **MCP Debug Tools** (extensión + CLI)
   - Conecta **MCP** (Model Context Protocol) con **DAP** en VS Code/Cursor.
   - La IA puede usar herramientas MCP que se traducen a acciones DAP sobre la sesión de depuración activa (incluida la sesión "Amiga").
   - No hace falta un MCP específico para Amiga: el mismo bridge DAP sirve para cualquier depurador, incluido el de Amiga.

## Qué necesita la IA para ser autónoma

| Capacidad              | Cómo se consigue |
|------------------------|-------------------|
| Poner quitar breakpoints | MCP: `add-breakpoint`, `remove-breakpoint`, `clear-breakpoints`, `list-breakpoints` |
| Arrancar/parar depuración | MCP: `start-debug`, `stop-debug` (con config "Amiga 500" u otra de `launch.json`) |
| Continuar / Step Over-In-Out | MCP: `continue`, `step-over`, `step-into`, `step-out`, `pause` |
| Inspeccionar variables/expresiones | MCP: `get-variables-scope`, `inspect-variable`, `evaluate-expression` |
| Call stack y estado     | MCP: `get-call-stack`, `get-debug-state` |
| Memoria y registros     | DAP (vía MCP): la extensión Amiga expone vistas "CPU Registers" y "Custom Registers"; las variables y expresiones pueden incluir punteros; para memoria cruda está "Amiga: View Memory" (palette, etc.) |
| Ver la pantalla        | (1) **Perfil con capturas**: si se genera un `.amigaprofile` (Profile/Profile Multi), las capturas van en `.screenshots[]` (base64). El agente puede ejecutar `scripts/parse-amigaprofile.sh <ruta> screenshots` y luego leer las imágenes extraídas. (2) Overlay `debug_*` en código. (3) Usuario pega captura. No hay comando "screenshot ahora" vía DAP. |
| Profiling              | Durante la sesión: botón "Profile" / "Profile (Multi)" de la extensión Amiga (frame profiler). La IA **no puede** invocar el generador; tú pulsas el botón. Sí puede **analizar** el `.amigaprofile` generado y **extraer y leer las capturas** (ver sección Perfil y capturas). |
| Registros Custom / puertos | La extensión expone "Custom Registers" vía custom DAP requests (`read-registers`, `read-register-list`). Si el MCP permite enviar custom requests al adaptador Amiga, la IA podría leerlos; si no, queda pendiente exponerlo. |

## Depurar desde código C (no solo desensamblado)

Sí se puede depurar desde el **código fuente C**: el proyecto se compila con `-g`, así que el ejecutable lleva información de depuración y el depurador puede mostrar el fuente, la línea actual y las variables en contexto C.

- **Si al parar ves solo desensamblado:** la extensión Amiga tiene la opción **"Force Disassembly"**. Si está activa, prioriza la vista de ensamblador. Para trabajar en C:
  - Abre la **Paleta de comandos** (`Ctrl+Shift+P` / `Cmd+Shift+P`).
  - Busca **"Amiga: Set Force Disassembly"** y ejecútala para **desactivarla** (toggle).
  - Vuelve a parar o a un breakpoint: deberías ver el **editor con el .c** y la línea actual, y en **Variables** / **Watch** los nombres y valores en C.
- **Call stack:** en el panel de pila de llamadas, elige un frame que tenga archivo `.c` (p. ej. `main.c`, `system.c`); el editor saltará a esa función en C y las variables serán las de ese frame.
- Los breakpoints que pongas en `main.c` o en `engine/src/*.c` hacen que la parada sea en C; con "Force Disassembly" desactivado, la vista principal será el código C y podrás inspeccionar variables cómodamente.

## Configuración paso a paso

### 1. Entorno de compilación y depuración Amiga (ya en uso)

- Extensión **Amiga C/C++ Compile, Debug & Profile** (BartmanAbyss) instalada.
- `.vscode/launch.json` con configuraciones `type: "amiga"` (p. ej. "Amiga 500", "AROS").
- `.vscode/tasks.json` con tarea de compilación (p. ej. "compile") y `preLaunchTask` en `launch.json`.
- Kickstart ROM configurado (en `launch.json` o en configuración de la extensión).

**Rutas locales de ejemplo (ajustar a tu máquina):**
- **Kickstart ROM (A500)**: `c:/Amiga/KICK13.rom` — configurar en Cursor/VS Code: *Settings → Extensions → Amiga* → `Rom-paths: A500`, o en `launch.json` en la clave `"kickstart"` de la configuración "Amiga 500".
- **Plan B (Coppenheimer)** — ADF de ejemplo para cargar en DF0: `c:/Users/dvdjg/Documents/programa/AI/Amiga-C++/demoscene-repo/effects/stencil3d/stencil3d.adf` (u otro `.adf` que quieras usar en el emulador web).

### 2. MCP Debug Tools (para que la IA controle el depurador)

1. **Instalar la extensión en Cursor**
   - En Cursor: `Ctrl+Shift+X` → buscar **"MCP Debug Tools"** (publicador UHD) → Instalar.

2. **Configurar el servidor MCP en Cursor**
   - Añadir el servidor **dap-proxy** en la configuración MCP de Cursor.
   - En este proyecto ya existe `.cursor/mcp.json` con algo como:

   ```json
   {
     "mcpServers": {
       "dap-proxy": {
         "command": "npx",
         "args": ["-y", "@uhd_kr/mcp-debug-tools@latest"],
         "env": {}
       }
     }
   }
   ```

   - Si Cursor usa la configuración MCP global en lugar de la del proyecto, añade el mismo bloque `dap-proxy` en **Cursor Settings → MCP** (o en el `mcp.json` que indique la documentación de Cursor).
   - **Windows**: Si el `mcp.json` del proyecto (`.cursor/mcp.json`) no carga las herramientas, añade el mismo servidor desde **Cursor → Settings → Tools & Integrations → MCP Servers** (configuración global: `%USERPROFILE%\.cursor\config\mcp.json`).

3. **Comprobar conexión**
   - Abre el proyecto en Cursor y, si hace falta, reinicia Cursor para que cargue el MCP.
   - La extensión MCP Debug Tools se conecta al mismo Cursor; el CLI (npx) habla con la extensión por HTTP (puerto 8890 por defecto).
   - La IA puede usar `list-vscode-instances` y `list-debug-configs` para ver instancias y configuraciones (debería aparecer "Amiga 500", etc.).

### 3. Flujo típico que puede seguir la IA

1. **Antes de depurar**
   - `list-debug-configs` → ver nombres exactos (p. ej. "Amiga 500").
   - `add-breakpoint` (o `add-breakpoints`) en `main.c` u otros archivos (rutas relativas al workspace, líneas 1-based).
   - Opcional: `list-breakpoints` para comprobar.

2. **Arrancar sesión**
   - `start-debug` con el nombre de la configuración (p. ej. `"Amiga 500"`).
   - La extensión Amiga ejecutará la `preLaunchTask` (compile) y luego lanzará WinUAE + GDB.

3. **Cuando se para en un breakpoint**
   - `get-debug-state`, `get-call-stack`, `get-variables-scope`.
   - `inspect-variable` o `evaluate-expression` para variables concretas o expresiones.
   - Para “ver” estado en pantalla: el código puede usar `debug_*` (overlay WinUAE); la IA no ve la ventana de WinUAE salvo que el usuario comparta captura.

4. **Avanzar**
   - `step-over`, `step-into`, `step-out` o `continue` hasta el siguiente breakpoint.

5. **Al terminar**
   - `stop-debug` para cerrar la sesión y WinUAE.

## Recursos MCP útiles (solo lectura)

- `debug://breakpoints` – breakpoints actuales.
- `debug://active-session` – sesión activa.
- `debug://console` – salida de la consola de debug (GDB, etc.).
- `debug://call-stack` – call stack.
- `debug://variables-scope` – variables en el scope actual.

## Perfil (.amigaprofile) y capturas

Los archivos `.amigaprofile` se guardan en `%TEMP%\amiga-profile-YYYY.MM.DD-HH.MM.SS.amigaprofile` (formato en `doc/amigaprofile-format.md`).

### Cómo puedo acceder al perfil

- **Ruta concreta**: si me dices la ruta del `.amigaprofile`, ejecuto el script del repo: `scripts/parse-amigaprofile.sh <ruta> [directorio_salida_capturas]`.
- **Último perfil**: `scripts/parse-latest-amigaprofile.sh [directorio_salida]` busca el `.amigaprofile` más reciente en `$TEMP` / `%TEMP%` y muestra formato, numFrames y top funciones por hitCount. Si pides “analiza el último perfil”, ejecuto ese script.
- **Requisitos**: `jq`, `base64`. Para guardar capturas en PNG (sin pérdida): ImageMagick (`magick` o `convert`); si no está, se guarda JPEG cuando el origen es JPEG.

### Invocar el generador de perfiles (no automático)

El profiler se lanza **solo** desde la barra de depuración (botón "Profile" o "Profile (Multi)") durante una sesión Amiga. **Flujo**: tú inicias depuración (F5), pulsas Profile/Profile (Multi), y cuando se genere el archivo pides “analiza el último perfil” o indicas la ruta; yo ejecuto los scripts bash anteriores.

### Error "Unable to start profiling: TypeError: Cannot read properties of undefined (reading 'frames')"

La extensión intenta leer `.frames` de algo que es `undefined`. Prueba en este orden:

1. **Dejar que la sesión tenga call stack:** F5 → deja que el programa corra unos segundos (o pon un breakpoint y para ahí) → después pulsa Profile. No pulses Profile al instante de arrancar.
2. **Actualizar la extensión:** Extensions → "Amiga C/C++ Compile, Debug & Profile" → Update.
3. **Sesión limpia:** Stop, cierra depuración, vuelve a F5 (solo extensión Amiga). Cuando WinUAE y el programa estén en marcha, pulsa Profile.
4. **Sesiones múltiples:** Si tienes varias ventanas de Cursor o F5 y MCP compitiendo por el mismo puerto/emulador, el estado puede corromperse (incl. este error). Cierra todas las instancias de Cursor y abre solo esta; vuelve a F5 y Profile. Ver también la sección **F5, MCP winuae-emu, puerto y conectar al emulador** más abajo.
5. **Reportar:** Si sigue igual, abre un issue en [vscode-amiga-debug/issues](https://github.com/BartmanAbyss/vscode-amiga-debug/issues) con el mensaje completo y versión de extensión y Cursor.

### Profiler a veces falla: "negative nr_color_changes: -2. FIXME!"

El **Profile** puede funcionar (de hecho a veces lo hace sin problema), pero en ciertas sesiones falla a mitad con *"negative nr_color_changes: -2. FIXME!"* y *"GDBSERVER: close()"*. Suele coincidir con haber mezclado tipos de depuración (F5 + MCP, o haber usado antes las configs winuae-gdb) o con estado raro de WinUAE/Cursor.

**Para intentar que vuelva a funcionar:**

1. **Reinicio limpio:** Cierra **todas** las ventanas de Cursor y **cualquier WinUAE** que esté abierto. Abre solo esta ventana del proyecto.
2. **Solo F5 con Bartman:** Inicia depuración con F5 usando una config de la extensión Amiga (p. ej. "AROS (debug, breakpoints fiables)" o "Amiga 500"). No uses el MCP winuae-emu ni otras configs en esa sesión.
3. **Deja correr un poco:** Cuando el programa esté en marcha, espera unos segundos (o para en un breakpoint) y **después** pulsa Profile o Profile (Multi).
4. Si sigue fallando, reporta en [vscode-amiga-debug/issues](https://github.com/BartmanAbyss/vscode-amiga-debug/issues) con el mensaje exacto y qué habías hecho justo antes (primera vez tras abrir Cursor, o después de haber usado MCP, etc.).

### Si el perfil lleva capturas

En perfiles multi-frame con capturas (`IAmigaProfileSplit` con `frames[].capture` en base64), el script guarda las capturas como **PNG** (sin pérdida) en el directorio que indiques; si el origen es JPEG, se convierte a PNG con ImageMagick. Yo puedo leer esas imágenes y correlacionarlas con el código del frame.

---

## F5, MCP winuae-emu, puerto y conectar al emulador

En este proyecto conviven **dos formas** de depurar con WinUAE: (1) **F5** con la extensión Amiga (BartmanAbyss), que lanza WinUAE y conecta su GDB al emulador; (2) **MCP winuae-emu**, que puede lanzar WinUAE y conectar su propio cliente GDB, o conectarse a un WinUAE ya en marcha (`winuae_connect_existing`). Ambas comparten una limitación de WinUAE: **solo acepta una conexión GDB a la vez**. Además, si se usan ejecutables o puertos distintos, aparecen interferencias.

### Usar el mismo winuae-gdb.exe (recomendado)

Para que no haya conflictos de versión o de comportamiento:

- **MCP winuae-emu** ya está configurado en `.cursor/mcp.json` con `WINUAE_PATH` apuntando al **winuae-gdb.exe** de la extensión **prb28 Amiga Assembly**:  
  `c:/Users/dvdjg/.cursor/extensions/prb28.amiga-assembly-1.8.14-universal/dist/bin/winuae`  
  Ahí está `winuae-gdb.exe` (WinUAE con GDB RSP integrado).
- **F5 (extensión Amiga C/C++)** puede usar su propio WinUAE embebido (BartmanAbyss) o, si la extensión lo permite, configurar la ruta del emulador. Para que todo sea coherente, conviene que **F5 use el mismo ejecutable** que el MCP. Si en tu instalación la extensión Bartman no permite cambiar la ruta del emulador, entonces cuando quieras que la IA se conecte al mismo emulador que tú, lanza WinUAE **manual** con el script del proyecto (que ya usa la ruta prb28):  
  `scripts/start-winuae-for-mcp-debug.ps1`  
  y después en Cursor pide a la IA que llame **`winuae_connect_existing`** (no `winuae_load` si ya arrancaste desde ADF).

Así evitas diferencias entre “el depurador que yo lanzo” y “el que usa la IA”.

Las entradas de depuración en este proyecto son solo las de la extensión **Bartman** (Amiga 500, AROS, etc.). No se usan configs "winuae-gdb, compatible IA" en el menú porque en este setup no arrancan bien el programa ni la UI. Para que la IA depure: **`scripts/start-winuae-for-mcp-debug.ps1`** y luego **`winuae_connect_existing`**.

### Por qué se interfieren F5 y el MCP

- Si **tú** inicias la depuración con **F5**, la extensión Amiga arranca WinUAE y **se queda con la única conexión GDB** (puerto 2345). El MCP **no** puede conectarse después al mismo WinUAE: `winuae_connect_existing` fallaría o quedaría sin efecto porque el puerto ya está ocupado por la extensión.
- Si **la IA** usa `winuae_connect` o `winuae_connect_existing` y se conecta al mismo puerto 2345, entonces la extensión (F5) ya no puede conectar su GDB a ese WinUAE.
- **Varias ventanas de Cursor** o **varios proyectos** usando el mismo puerto 2345 y el mismo WinUAE (o varias instancias compitiendo) pueden dejar estado compartido corrupto; por ejemplo el error *"Unable to start profiling: TypeError: Cannot read properties of undefined (reading 'frames')"*. En esos casos suele ayudar **cerrar todas las instancias de Cursor** y abrir solo la del proyecto en el que estés trabajando.

### Conectar la IA al emulador que tú has lanzado

- **Si arrancas con F5:** la extensión ya tiene la conexión GDB. La IA **no** puede “engancharse” a ese mismo WinUAE con `winuae_connect_existing`; no hay forma de compartir esa única conexión.
- **Si arrancas WinUAE a mano** (por ejemplo con `scripts/start-winuae-for-mcp-debug.ps1`, que usa el mismo `winuae-gdb.exe` de prb28), **sin** pulsar F5:
  - Cuando el Amiga haya arrancado, en Cursor pide a la IA que ejecute **`winuae_connect_existing`**.
  - La IA se conectará al GDB en el puerto por defecto (2345). **No** debe llamar a `winuae_load` si el programa ya está en memoria (p. ej. arrancaste desde ADF).
- Requisito: que el WinUAE que tú lanzas esté usando **el mismo** `winuae-gdb.exe` (p. ej. el del script, que toma la ruta de la extensión prb28) y escuchando en el **mismo puerto** que el MCP (por defecto 2345).

### Puerto configurable (varios proyectos o sesiones)

El MCP **winuae-emu** lee del `env` en `.cursor/mcp.json`:

- **`WINUAE_PATH`**: carpeta donde está `winuae-gdb.exe`. **Preferido**: `c:/Users/dvdjg/.cursor/extensions/bartmanabyss.amiga-debug-1.7.9/bin/win64` (64 bits) o `.../bin/win32` (32 bits). Este ejecutable incluye las extensiones para el MCP (p. ej. paquete M de escritura de memoria). Se genera desde el custom build en `C:\Users\dvdjg\Documents\programa\AI\WinUAE-DBG` → `bin/winuae-gdb.exe` (64-bit) o `bin/winuae-gdb-x86.exe` (32-bit); normalmente se copia a la extensión bartmanabyss.
- **`WINUAE_CONFIG`**: ruta al `.uae` que debe cargar. **En este proyecto** se usa **`.vscode/mcp-amiga-debug.uae`** (ruta absoluta en `mcp.json`). Ese config debe cargar siempre **`kickstart_rom_file=c:/Amiga/KICK13.rom`**; el agente debe asegurarse de que esa línea esté presente al lanzar WinUAE.
- **`WINUAE_GDB_PORT`**: puerto del servidor GDB (por defecto `2345`).
- **`WINUAE_BOOT_ADF`**: ruta absoluta al ADF de arranque (p. ej. `out/disk.adf` del proyecto). Si está definida y el archivo existe, el MCP la usa como **DF0:** al lanzar WinUAE, para que el Amiga bootee y **no se quede la pantalla en negro**. Si llamas a `winuae_insert_disk` antes de `winuae_connect`, ese disco tiene prioridad.
- **No pongas `WINUAE_USE_GUI_NO": "0"`** en el env: con ese valor el MCP no añade `use_gui=no` y WinUAE abre la interfaz en lugar de lanzar el emulador. Si no está definido (o es `1`), el MCP arranca sin GUI.

**Si no ves la ROM ni el programa:** (1) Comprueba que el `.uae` usado tenga **`kickstart_rom_file=c:/Amiga/KICK13.rom`** (o ruta válida a tu ROM). (2) Comprueba que `WINUAE_BOOT_ADF` o el disco insertado con `winuae_insert_disk` apunte a un ADF existente.

Ejemplo de `env` en `.cursor/mcp.json`:

```json
"env": {
  "WINUAE_PATH": "c:/Users/dvdjg/.cursor/extensions/bartmanabyss.amiga-debug-1.7.9/bin/win64",
  "WINUAE_CONFIG": "c:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/.vscode/mcp-amiga-debug.uae",
  "WINUAE_GDB_PORT": "2345",
  "WINUAE_BOOT_ADF": "c:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/out/disk.adf"
}
```

- **Mismo proyecto, una sola sesión:** deja `2345` (o no pongas `WINUAE_GDB_PORT`).
- **Otro proyecto en el mismo ordenador:** en ese otro proyecto puedes poner en su `mcp.json` por ejemplo `"WINUAE_GDB_PORT": "2346"`. Así el MCP de ese proyecto se conectará al puerto 2346. Para que funcione, el WinUAE de ese proyecto tendría que estar escuchando en 2346; el fork actual de WinUAE (Bartman/prb28) por defecto escucha en 2345 y no siempre expone un parámetro para cambiar el puerto del gdbserver. Si en el futuro el fork acepta algo como `-s debugging_port=2346`, se podría documentar aquí y usar en el script / MCP.

Mientras tanto, para evitar interferencias entre proyectos lo más práctico es **no tener dos sesiones de depuración activas a la vez** (un proyecto con F5 o MCP conectado, otro sin depuración), o usar **una sola ventana de Cursor** por proyecto.

---

## Plan B: Coppenheimer (emulador web + inspección)

Si el depurador WinUAE/GDB da problemas (timeouts, inestabilidad, otro SO sin extensión), se puede usar **[Coppenheimer](https://github.com/losso3000/coppenheimer)** como alternativa para ejecutar e inspeccionar el programa.

- **Qué es**: UI alternativa de [vAmigaWeb](https://vamigaweb.github.io/doc/about.html) (emulador Amiga en WebAssembly) con **inspección de memoria en tiempo real**, monitores de **DMA** y **memoria**, y controles de play/pause más visibles.
- **Demo**: [https://coppenheimer.heckmeck.de](https://coppenheimer.heckmeck.de) — puedes cargar tu ejecutable (o un ADF) y usar las herramientas de inspección a nivel humano.

### Cómo accede la IA en Plan B

En este proyecto Plan B usa **Playwright MCP** (y opcionalmente el **browser nativo de Cursor** si lo activas):

- **Playwright MCP** (ya en `.cursor/mcp.json`): la IA puede abrir Coppenheimer en el navegador (p. ej. `browser_navigate` a la demo o a tu instancia), tomar snapshots de accesibilidad (`browser_snapshot`), capturas (`browser_take_screenshot`), leer consola y red. Así puede navegar a las vistas de memoria/DMA, hacer capturas y analizar lo que se dibuja o los datos mostrados. Requiere `npx playwright install` una vez.
- **Cursor Browser** (si lo activas en Cursor): controles de navegador integrados en el Agent; la IA puede usarlos igualmente para abrir Coppenheimer y depurar/testing sin configurar otro MCP.
- **Manual**: tú usas Coppenheimer; compartes captura o pegas datos y yo los interpreto.

Cuando quieras usar Plan B, di “usa Coppenheimer” o “inspección con Coppenheimer” y usaré Playwright (o Cursor browser si está activo) para automatizar, o te pido capturas si prefieres manual.

**Documentación detallada de la UI**: [coppenheimer-ui.md](coppenheimer-ui.md) describe todos los elementos de la interfaz (controles de ejecución, ROM/DF0/DF1, DMA usage, monitor de memoria, Guess!, etc.), la **vAmiga Retro Shell** (comandos `help`, `cpu`, `agnus`, `df0`, etc., con ejemplos de salida) y cómo la IA puede usar la UI vía Cursor Browser. **Procedimiento**: en el diálogo Kickstart ROM usar **Install AROS m68k ROMS** (por defecto). Kickstart 1.3 (KICK13.rom) solo para casos muy específicos; entonces el usuario sube la ROM manualmente. La subida de ADF (DF0/DF1) sigue siendo manual (Drop file or click).

---

## Limitaciones y notas

- **Breakpoints a veces no se pillan**: El proyecto se compila con **-Ofast**. Con optimización alta, GDB mapea líneas de código a direcciones según la info DWARF; el código generado puede estar reordenado, fusionado o "repartido" entre varias líneas, así que un breakpoint en una línea puede quedar en una dirección que no se ejecuta como esperas, o en una de varias posibles. Por eso "a veces sí, a veces no": según la línea, la correspondencia línea→instrucción es fiable o no. Para depuración fiable, compila en modo debug (ver sección "Compilación para depuración" más abajo).
- **Pantalla**: No hay MCP que capture la ventana de WinUAE. Para depuración “visible” hay que apoyarse en el overlay (`debug_*`) o en descripción/captura del usuario.
- **Una sesión**: Solo una sesión de debug activa por instancia de Cursor.
- **Rutas**: Breakpoints con rutas relativas al workspace (p. ej. `main.c`, no rutas absolutas).
- **Líneas**: 1-based en las herramientas MCP.
- **Reglas del agente**: Ver [MCP_DEBUG_TOOLS_RULES.md](https://github.com/hwanyong/mcp-debug-tools/blob/HEAD/MCP_DEBUG_TOOLS_RULES.md) para orden de uso, manejo de errores y buenas prácticas.

### Compilación para depuración

Para que los breakpoints coincidan bien con las líneas de código, compila con optimización pensada para depuración. En el Makefile usamos la variable `CFLAGS_OPT`:

- **Release (por defecto)**: `make program=out/a` → usa `-Ofast`.
- **Debug (recomendado)**: `make program=out/a CFLAGS_OPT=-Og` → **-Og** es el nivel "optimize for debugging" de GCC: algo de optimización pero sin romper el mapeo línea→código; breakpoints fiables y algo más rápido que -O0.
- **Debug máximo (sin optimizar)**: `make program=out/a CFLAGS_OPT=-O0` → si -Og aún te falla en alguna línea, usa -O0.

La configuración **"AROS (debug, breakpoints fiables)"** en `launch.json` usa la tarea **"compile (debug)"**, que compila con `CFLAGS_OPT=-Og`.

---

## Depuración avanzada: memoria, registros y WinUAE

Una consideración estratégica para desarrollar juegos en Amiga es tener **acceso completo** al depurador: lectura/escritura libre de memoria, registros (CPU y Custom), y lo que permita la personalización de WinUAE. Hay dos formas de conseguirlo y **no son compatibles a la vez** en cuanto a quién tiene la conexión GDB.

### Opción A: F5 + extensión Amiga + MCP Debug Tools (dap-proxy)

- **Qué es**: Pulsas F5 en Cursor; la extensión vscode-amiga-debug lanza GDB y se conecta al gdbserver de WinUAE (puerto 2345). El **dap-proxy** (MCP Debug Tools) se engancha a esa sesión DAP y permite a la IA enviar breakpoints, continue, step, variables, call stack.
- **Ventaja**: UI completa en Cursor (breakpoints en el gutter, Variables, Call Stack). La IA puede controlar la misma sesión.
- **Limitación**: WinUAE **solo acepta una conexión GDB**. Esa conexión la tiene la extensión. El dap-proxy no abre una segunda GDB; solo reenvía peticiones DAP. Para que la IA tenga **lectura/escritura de memoria** y **registros Custom** hace falta que el dap-proxy exponga las **custom requests** que ya implementa la extensión (p. ej. `ReadMemory`, `read-registers`). Hoy el dap-proxy estándar puede no exponer todas; sería una extensión del MCP Debug Tools para reenviar esas peticiones cuando la sesión activa es Amiga.

### Opción B: mcp-amiga-debug (sin F5, depuración solo vía MCP)

- **Qué es**: El servidor MCP **mcp-amiga-debug** puede compilar, lanzar WinUAE con gdbserver y conectar **su propio** GDB al puerto 2345. Expone herramientas: `launch-debug`, `read-memory`, `execute-command`, etc. La IA depura solo usando esas herramientas; **no** usas F5 ni el panel de depuración de Cursor para esa sesión.
- **Ventaja**: Un solo cliente GDB (el de mcp-amiga-debug), con acceso directo a comandos GDB MI: memoria, `monitor` de WinUAE si el stub lo soporta, etc. Se puede añadir `write-memory`, `read-registers` (CPU/Custom vía monitor o MI).
- **Limitación**: No hay UI de breakpoints en el editor para esa sesión; todo va por herramientas MCP. Y **no puedes** tener a la vez F5 (extensión) y mcp-amiga-debug conectados al mismo WinUAE: uno de los dos se quedará sin conexión.

### ¿Son incompatibles el MCP dap-proxy y mcp-amiga-debug?

- **No** como programas: puedes tener ambos MCPs en `mcp.json`.
- **Sí** en uso simultáneo de la conexión GDB a WinUAE:
  - Si **inicias con F5** (extensión), la extensión ocupa la única conexión GDB. Las herramientas de mcp-amiga-debug que necesiten GDB (`read-memory`, `execute-command`) **fallarán** ("GDB no disponible" / "solo acepta 1 conexión").
  - Si **inicias con mcp-amiga-debug** (`launch-debug`), su GDB se conecta; entonces F5 no puede conectar otra vez al mismo WinUAE. La depuración la haces solo vía MCP (o cierras WinUAE y vuelves a F5).

**Recomendación**: Para desarrollo día a día con breakpoints en el editor, usa **F5 + "AROS (debug, breakpoints fiables)"** y dap-proxy. Para depuración avanzada (memoria, registros, comandos monitor) sin UI, usa **mcp-amiga-debug** con `launch-debug` y no F5. A medio plazo, la vía más rica sería **extender el dap-proxy** para que, cuando la sesión sea Amiga, exponga herramientas que reenvíen las custom requests de la extensión (ReadMemory, read-registers), así la IA tendría memoria y registros **sin** renunciar a F5.

Detalle técnico de la extensión y memoria/Custom: [winuae-extensión-internals.md](winuae-extensión-internals.md).

### Simular "entrar en demo" y "salir con ambos botones" (MCP winuae-emu, sin ratón)

El MCP **winuae-emu** no puede inyectar eventos de ratón en WinUAE; sí puede **escribir memoria**. El programa expone:

1. **`g_automation_enter_demo`** (entero, 4 bytes): si la IA escribe **1** aquí, en la siguiente iteración del bucle principal (fallback sin Intuition) el programa entra en demo sin pasar por el menú (equivalente a "pulsar Demo").
2. **`s_mouse_left`** y **`s_mouse_right`** (bytes en `engine_input_devices.c`): si la IA escribe **1** en ambos, el programa interpreta "ambos botones del ratón pulsados" y sale de la demo al menú.

Las direcciones salen de **`out/a.map`** (tras `make`); si cambias el código y recompilas, vuelve a leer el .map. Ejemplo de direcciones (pueden variar):

| Símbolo                  | Dirección (hex) | Tamaño | Nota |
|--------------------------|-----------------|--------|------|
| `s_mouse_left`           | (ver .map)      | 1 byte | Buscar `.bss.s_mouse_left` en `out/a.map` |
| `s_mouse_right`          | (ver .map)      | 1 byte | |
| `g_automation_enter_demo`| (ver .map)      | 4 bytes (LE) | |
| `g_automation_input`     | (ver .map)      | 20 bytes | Buffer entrada programática; offsets en engine_automation_input.h |

**Flujo para la IA (entrar en demo y salir con “ambos botones”):**

1. **Conectar**: `winuae_insert_disk` con `out/disk.adf`, luego `winuae_connect`.
2. **Entrar en demo**: `winuae_memory_write` en la dirección de **`g_automation_enter_demo`** (ver .map) con **`01000000`** (1 en 32 bits little-endian). Luego `winuae_continue`. En la siguiente iteración el programa llama a `effect_create()` y pasa a estado demo.
3. **Salir de la demo (opción A):** Escribir 1 en las direcciones de `s_mouse_left` y `s_mouse_right`. **Opción B (entrada programática):** Escribir en **`g_automation_input`**: byte 0 = 1 (enabled), byte 1 = 1 (mouse_left), byte 2 = 1 (mouse_right). Luego `winuae_continue`. El bucle detecta ambos botones y vuelve al menú.

Para **comprobar** que ha salido: poner un breakpoint en la instrucción que sigue al `app_state_set(APP_STATE_MENU)` (o en `menu_display_begin`) y hacer `winuae_continue` + `winuae_wait_stop`; o hacer `winuae_pause` y leer `current_state` (p. ej. en 0x15eac, valor 0 = MENU).

**Interfaz de entrada programática:** El programa tiene un buffer **`g_automation_input`** (20 bytes, ver `engine/include/engine_automation_input.h`). Si `g_automation_input[0] != 0`, el engine usa ese buffer como ratón/tecla/joy en lugar del input real; la IA puede escribir ahí con `winuae_memory_write` (base en `out/a.map`, offsets 0=enabled, 1=mouse_left, 2=mouse_right, 4-7=mouse_x/y big-endian, 8+=joy/key). En `mcp-winuae-emu` existe además una capa de más alto nivel: `winuae_amiga_input_state`, `winuae_amiga_input_set` y `winuae_amiga_enter_demo`, pensadas para mover el ratón por coordenadas Amiga, pulsar botones y activar la demo sin manipular bytes a mano.

**Ciclo completo solo vía MCP:** Con el WinUAE de prb28/vscode-amiga-assembly el stub GDB **no implementa el paquete M** (write memory), por lo que `winuae_memory_write` hace timeout y la IA no puede completar el ciclo (entrar demo → salir con ambos botones) solo con el MCP. Para hacer el ciclo completo: usa **F5 + extensión Amiga**, pon un breakpoint en el bucle de `main.c`, y en Variables/Evaluate asigna `g_automation_enter_demo = 1` (entrar en demo) y luego `g_automation_input[0]=1`, `g_automation_input[1]=1`, `g_automation_input[2]=1` (ambos botones para salir). Tras recompilar **mcp-winuae-emu** (`npm run build` en ese repo), recarga Cursor o reinicia el servidor MCP para que use el nuevo `dist/`.

**Nota:** El camino Intuition también comprueba ahora la vía de automatización: `g_automation_enter_demo` puede abrir la demo y `g_automation_input` puede disparar clicks por coordenadas sobre la calculadora/`Demo` sin depender de IDCMP real.

---

## WinUAE-DBG v2.1 — monitor extensions (status / watch / protect / rewind)

El fork `WinUAE-DBG` (build **x86**; el build x64 tiene un problema preexistente
de handshake GDB) incorpora comandos `monitor` estilo engine9000, expuestos por
`mcp-winuae-emu` como herramientas MCP:

| Tool MCP | Para qué sirve |
|---|---|
| `winuae_emulator_status` | Telemetría: ciclos, frame, vpos/hpos, warp, `baseText`, nº de breakpoints/watchpoints/protects, estado rewind. |
| `winuae_watchpoint_set_ext` | Watchpoint con predicados y filtro de **origen**: `source=cpu\|cpudw\|copper\|blitter\|bpl0-7\|spr0-7\|audio0-3\|disk\|dma`, `value`, `mask`, `must_change`, `reg`, `pc`, `nobreak`. |
| `winuae_watchpoint_list` / `winuae_watchpoint_last` / `winuae_watchpoint_clear_ext` | Gestionar watchpoints; `last` da el detalle del último hit (addr, r/w, size, **src**, valor, PC). |
| `winuae_protect` | Cheat/protect: `block` impide escrituras a una dirección, `set=…` fuerza un valor. Actúa sobre accesos del programa emulado mientras corre. |
| `winuae_rewind` | `start`/`stop`/`status` gestionan la captura de estados; `rewind` (sin comando) rebobina. **Restore arreglado** (dejó de crashear); la sesión GDB puede quedar inerte tras el restore — reconectar o usar el canal lateral. |
| `winuae_trace` | Controla el sistema de trazas (`on`/`off`/`status`, activo por defecto). Registra hits de watch/protect/rewind en `%TEMP%\winuae-gdb.log`. |
| `winuae_side_read` | Lee el canal lateral (puerto 2346, independiente de GDB): `state` / `regs` / `mem <addr> <len>` / `runstatus <addr>`. Útil tras un `winuae_rewind` (el GDB queda inerte pero el canal lateral sigue leyendo el snapshot restaurado). |

### Uso clave para depuración autónoma

1. **Confirmar que el emulador corre**: `winuae_emulator_status` (frame
   avanza, warp, contadores).
2. **Watchpoint por origen** (el más útil para demos): por ejemplo, ver qué
   escribe el Copper en un registro custom:
   `winuae_watchpoint_set_ext { address: "0xdff180", access: "w", size: 16, source: "copper" }`,
   luego `winuae_continue` + `winuae_wait_stop`, y `winuae_watchpoint_last`
   devuelve `addr=0x00dff180 rwi=w size=2 src=copper val=…`. Igual con
   `source: "blitter"` para DMA del blitter.
3. **Protect/cheat**: `winuae_protect { action: "block", address, size }` para
   congelar un buffer, o `action: "set", value` para forzar un valor
   (ej. mantener un estado de vidas/inmunidad). Nota: intercepta accesos del
   programa emulado **mientras corre** (no las escrituras del propio depurador
   con el emulador pausado).
4. **Interceptar escrituras del CPU con `src=cpudw`**: si una rutina pisa
   memoria que no debe, pon un watch `w size=16 src=cpudw` y mira `watch_last`.

Detalles completos y notas del motor (descomposición de escrituras de 32 bits
del 68000, requisito de input recording para rewind, issue x64):
[WinUAE-DBG/docs/WINUAE-MONITOR-EXTENSIONS.md](../../../WinUAE-DBG/docs/WINUAE-MONITOR-EXTENSIONS.md)
y `mcp-winuae-emu/scripts/verify-monitor-extensions.mjs`.

---

## Resumen: ¿necesitas algún MCP más?

- **No** hace falta un MCP específico para “Amiga”. El depurador es el de la extensión vscode-amiga-debug (DAP).
- **Sí** hace falta el bridge **MCP ↔ DAP** (MCP Debug Tools) para que la IA pueda, desde Cursor, lanzar la depuración, poner breakpoints, inspeccionar memoria/registros (vía variables y estado del debugger), ver call stack y controlar la ejecución de forma autónoma.
- Para “ver” la pantalla del emulator de forma automática haría falta en el futuro un MCP o herramienta que capture la ventana de WinUAE (por ahora no está cubierto aquí). Detalle de cómo la extensión accede a imagen, memoria y Custom: [winuae-extensión-internals.md](winuae-extensión-internals.md).
