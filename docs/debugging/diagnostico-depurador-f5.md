# Diagnóstico: el depurador no se lanza con F5

## Causas más probables

### 1. Configuración de depuración incorrecta seleccionada

En la **barra de depuración** (arriba o junto al botón de ejecutar) hay un desplegable con la configuración que se usa al pulsar F5.

- Si está seleccionado **"C/C++ Runner: Debug Session"**, F5 intenta lanzar el depurador C/C++ nativo (GDB) contra `build/Debug/outDebug`, que **no es tu proyecto Amiga** y puede no existir o fallar.
- **Solución**: En ese desplegable elige **"AROS"**, **"Amiga 500"**, **"Amiga 1200"** o **"Amiga 4000"** (cualquiera de las configuraciones con `type: "amiga"`).

### 2. Extensión Amiga no instalada o deshabilitada

El tipo de depurador `amiga` lo aporta la extensión **Amiga C/C++ Compile, Debug & Profile** (BartmanAbyss.amiga-debug).

- Si no está instalada o está deshabilitada, Cursor no reconoce `type: "amiga"` y el lanzamiento falla.
- **Comprobar**: `Ctrl+Shift+X` → buscar "Amiga" o "BartmanAbyss.amiga-debug" → debe estar instalada y habilitada.

### 3. preLaunchTask "compile" falla

Antes de arrancar el depurador se ejecuta la tarea **compile**, que usa:

- `${command:amiga.bin-path}` → lo aporta la extensión Amiga (ruta a las herramientas GCC/Amiga).
- `${config:amiga.program}` → nombre del programa (ej. `build/main.elf`), también de la extensión.

Si la extensión Amiga no tiene configurado **Bin-path** o **Program**, la compilación falla y el depurador **no llega a lanzarse**.

**Comprobar**:

1. `Ctrl+Shift+P` → **Tasks: Run Task** → elige **compile**.
2. Si falla, mira el mensaje en la terminal (por ejemplo "AMIGA_BIN_PATH no está definido" o "amiga.program no configurado").
3. Configura la extensión Amiga: **Settings** (Ctrl+,) → buscar "Amiga" o abrir la configuración de la extensión y rellenar **Bin-path** y **Program** (y Rom-paths si usas Kickstart).

### 4. Ver qué hace Cursor al pulsar F5

- Abre **Output** (Ver → Salida).
- En el desplegable elige **"Debug Console"** o **"Extension Host"**.
- Pulsa F5 y observa si aparece algún error (tipo desconocido "amiga", fallo de tarea, ejecutable no encontrado, etc.).

## Resumen de comprobaciones

| Comprobación | Dónde |
|--------------|--------|
| Configuración por defecto al depurar | Desplegable de la barra de depuración → debe ser "AROS" o "Amiga 500" (u otra Amiga), no "C/C++ Runner" |
| Extensión Amiga instalada y habilitada | Extensiones (Ctrl+Shift+X) → BartmanAbyss.amiga-debug |
| Bin-path y Program configurados | Settings → Amiga (o configuración de la extensión Amiga) |
| Tarea "compile" funciona sola | Ctrl+Shift+P → Run Task → compile |

## Error "Failed to launch GDB: could not connect (error 10061)"

Este mensaje significa **conexión denegada** a `localhost:2345`: GDB intenta conectarse al servidor GDB de WinUAE pero **nadie está escuchando** en ese puerto.

Causas típicas:

1. **WinUAE no se ha arrancado aún** con gdbserver, o tarda más de lo que espera el cliente.
2. **Solo puede haber una conexión GDB** al puerto 2345. Si **mcp-amiga-debug** (MCP) ya está conectado, la extensión amiga-debug no podrá conectar, y al revés. Cierra la sesión que no uses.
3. **Firewall o antivirus** bloqueando localhost:2345.

Qué hacer: asegúrate de usar **solo un cliente** (F5 con la extensión Amiga **o** las herramientas MCP), y de que WinUAE esté en modo gdbserver y haya arrancado antes de conectar.

---

## Reinstalación de la extensión amiga-debug cuando falla el .vsix

Si tras desinstalar la extensión **Amiga C/C++ Compile, Debug & Profile** la instalación desde un `.vsix` (p. ej. `amiga-debug-1.7.9.vsix`) da error en Cursor, suele deberse a **estado interno corrupto** o a restos de la extensión.

### Pasos de reparación

1. **Cerrar Cursor por completo** (todas las ventanas).
2. **Borrar la carpeta de la extensión** (si existe):
   - Cursor: `%USERPROFILE%\.cursor\extensions\bartmanabyss.amiga-debug-1.7.9`
   - VSCode: `%USERPROFILE%\.vscode\extensions\bartmanabyss.amiga-debug-1.7.9`
3. **Opcional**: borrar el listado en caché de extensiones:
   - En `%USERPROFILE%\.cursor\extensions\` existe un archivo `extensions.json`; no lo borres si no estás seguro, pero si quieres “empezar de cero” con el listado, puedes hacer copia de seguridad y luego eliminar solo la línea/entrada relacionada con `bartmanabyss.amiga-debug` si la hubiera (o dejar que Cursor regenere al abrir).
4. **Abrir Cursor** de nuevo.
5. **Instalar desde .vsix**:  
   `Ctrl+Shift+P` → **Extensions: Install from VSIX...** → elegir `amiga-debug-1.7.9.vsix`.

Si sigue fallando, prueba a **instalar desde el Marketplace** (buscar "Amiga" o "BartmanAbyss.amiga-debug" en la pestaña Extensiones) en lugar del .vsix.

### Rutas según el editor

- **Cursor** instala extensiones en: `%USERPROFILE%\.cursor\extensions\`
- **VSCode** en: `%USERPROFILE%\.vscode\extensions\`

El proyecto **Cursor-Amiga-C** y **mcp-amiga-debug** buscan la extensión en **ambas** ubicaciones y en **cualquier versión** (`bartmanabyss.amiga-debug-*`), así que funciona tanto en Cursor como en VSCode y con futuras versiones (1.7.10, etc.).

---

## "Pongo breakpoints y no se para nada"

Si el depurador arranca (WinUAE, GDB) pero **nunca se detiene en los breakpoints**:

### 1. Estás compilando con optimización (-Ofast)

Por defecto la tarea **compile** usa `-Ofast`. Con optimización alta, GDB no puede mapear bien las líneas a instrucciones y los breakpoints fallan.

- **Solución**: Usa la configuración **"AROS (debug, breakpoints fiables)"** en el desplegable de Run and Debug y pulsa F5. Esa config ejecuta antes la tarea **"compile (debug)"**, que compila con `-O0`.
- O bien: **Ctrl+Shift+P** → **Tasks: Run Task** → **compile (debug)**; cuando termine, pulsa F5 (el ejecutable ya será el de debug).

### 2. No has recompilado después de cambiar a debug

Si antes compilaste con **compile** (normal), el `.exe` que carga WinUAE sigue siendo el optimizado. Aunque elijas "AROS (debug, breakpoints fiables)", asegúrate de que la tarea **compile (debug)** se ejecute y termine bien (mira la terminal).

- Para forzar un build limpio: **Run Task** → **clean**, luego **Run Task** → **compile (debug)** (o F5 con "AROS (debug, breakpoints fiables)").

### 3. Tienes elegida la config normal en lugar de la de debug

En el desplegable de la barra de depuración debe decir **"AROS (debug, breakpoints fiables)"** (o la que tenga `preLaunchTask: "compile (debug)"`), no solo "AROS". Si está en "AROS", se usa **compile** con -Ofast.

### 4. Desajuste de direcciones (AROS carga el programa en otra dirección)

Si **Pausar** funciona pero los **breakpoints no**, lo más probable es que el ELF tenga las direcciones en 0x400 pero, bajo AROS, el programa se cargue en otra zona de memoria (LoadSeg). GDB envía direcciones del ELF y el PC real no coincide.

**Probar**: usa la configuración **"Amiga 500"** (con Kickstart) en lugar de "AROS", y un ADF booteable. Así el ejecutable suele cargarse en 0x400 y los breakpoints pueden funcionar. Véase [../docs/WINUAE-MCP-DEBUG.md](../docs/WINUAE-MCP-DEBUG.md).

### 5. Profile: "Cannot execute while target is running"

Para usar **Profile**, el programa debe estar **pausado**. Pulsa **Pausar** (o Stop) y luego solicita el profile.

### 6. Profile: "dmaLen mismatch (want 58, got 121)"

La extensión **vscode-amiga-debug** espera un formato antiguo del registro DMA: **58 bytes** por `dma_rec`.  
WinUAE-DBG (fork BartmanAbyss) escribe una estructura `dma_rec` ampliada de **121 bytes** con más campos (hpos, vpos, agnus_evt, denise_evt, etc.). El parser de la extensión falla porque comprueba `dmaLen === 58`.

- **Causa**: Incompatibilidad entre versión de la extensión y el formato de profile que genera WinUAE-DBG.
- **Solución (parche local)**: Hay un fork con el fix en `../vscode-amiga-debug`. Compilar y empaquetar:
  ```bash
  cd vscode-amiga-debug
  npm install
  npm run compile
  npx @vscode/vsce package
  ```
  Luego instalar `amiga-debug-1.8.1.vsix` vía **Extensions: Install from VSIX...**.
- **Solución upstream**: Abrir un PR a [BartmanAbyss/vscode-amiga-debug](https://github.com/BartmanAbyss/vscode-amiga-debug); ver `vscode-amiga-debug/PULL_REQUEST.md` para la descripción de los cambios.
- **Alternativa**: Usar el tool MCP `winuae_profile`, que genera el archivo binario; no lo visualiza, pero el perfil se guarda.

### 7. Profile: "Cannot read properties of undefined (reading 'frames')"

Ocurre cuando el parser de profile intenta resolver el *call stack* para una dirección PC que **no está en el source map** (p. ej. código en Kickstart, rutinas externas, o memoria sin símbolos). El código accede a `sourceMap.uniqueLines[index].frames` sin comprobar si el objeto existe.

- **Causa**: La extensión asume que toda PC tiene entrada en el source map. Si faltan símbolos o el ejecutable no se compiló con debug info completa, el índice puede devolver `undefined`.
- **Solución**: El parche en `vscode-amiga-debug` incluye un guard; si `l` es undefined, se añade un frame `[Unknown]` en lugar de fallar. Mismo PR que el fix de dmaLen.

### Resumen

| Qué comprobar | Acción |
|---------------|--------|
| Config al depurar | Desplegable → **"AROS (debug, breakpoints fiables)"** |
| Build reciente en -O0 | Ejecutar **compile (debug)** o F5 con la config de debug; revisar que no haya error en la tarea |
| Build limpio (opcional) | Run Task **clean** → luego **compile (debug)** |
| Pausar ok pero breakpoints no | WinUAE-DBG ahora relocaliza direcciones; recompila WinUAE-DBG si aplica |
| Profile falla | **Pausar** primero, luego solicitar profile |
| Profile: dmaLen mismatch | Incompatibilidad extensión vs WinUAE-DBG (58 vs 121 bytes); reportar a vscode-amiga-debug |
| Profile: frames undefined | PC sin entrada en source map; compilar con -g; reportar a vscode-amiga-debug |

Más detalle: [debug-with-ai.md](debug-with-ai.md) (sección "Compilación para depuración").

---

## MCP Debug Tools (dap-proxy)

El servidor **dap-proxy** en `.cursor/mcp.json` es para que la **IA** pueda controlar el depurador (breakpoints, start/stop, etc.). No es necesario para que **tú** puedas pulsar F5 y que arranque la sesión Amiga. Si F5 no hace nada, el problema suele estar en los puntos 1–3 de arriba, no en MCP.
