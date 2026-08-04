# Cómo la extensión vscode-amiga-debug accede a WinUAE

Este documento resume **cómo** la extensión [vscode-amiga-debug](https://github.com/BartmanAbyss/vscode-amiga-debug) y el fork [BartmanAbyss/WinUAE](https://github.com/BartmanAbyss/WinUAE) consiguen ver la imagen del Amiga, los puertos (registros Custom), y toda la memoria. Sirve para que el agente (IA) pueda tener el mismo poder: ya sea usando lo que ya expone el DAP/MCP o proponiendo extensiones.

## Resumen ejecutivo

| Qué quiere la IA | Cómo lo tiene la extensión | Cómo puede tenerlo la IA hoy |
|------------------|----------------------------|------------------------------|
| **Imagen / pantalla** | WinUAE captura el framebuffer al hacer **Profile** y lo escribe en el archivo de perfil (base64 en `.screenshots[]` o `.frames[].capture`). El Graphics Debugger usa savestates (.uss) y reconstruye la imagen desde memoria + Custom. | (1) Usar **Profile (Multi)** desde la barra de depuración y luego `scripts/parse-amigaprofile.sh` para extraer capturas. (2) Overlay `debug_*` en código. (3) Usuario comparte captura. No hay comando "screenshot ahora" vía DAP. |
| **Registros Custom** | La extensión envía una **custom DAP request** `read-registers` / `read-register-list`; el backend usa **GDB** conectado al servidor integrado en WinUAE y probablemente un **monitor command** que devuelve el dump de Custom. | Si el MCP Debug Tools permite enviar **custom requests** al adaptador Amiga, la IA podría llamar `read-registers` / `read-register-list`. Hoy no está documentado en este repo si MCP expone eso. |
| **Toda la memoria** | **GDB**: la extensión usa `data-read-memory-bytes <dir> <len>` (y `data-write-memory-bytes`). El servidor GDB dentro de WinUAE lee/escribe la memoria del Amiga. | **DAP** estándar tiene `ReadMemory`; la extensión lo implementa. Si MCP expone lectura de memoria por referencia/offset, la IA ya puede leer memoria. Alternativa: `execute-command` con `data-read-memory-bytes 0x0 4096` si MCP permite ejecutar comandos GDB. |
| **Puertos / circuitos** | Los registros Custom *son* los puertos del chipset (Agnus, Denise, Paula, etc.). Mismo canal que "Registros Custom". En WinUAE, `debug_parser()` (modo `ipc_debug`) acepta comandos `DBG e` para volcar Custom; el GDB server puede tener un `monitor` equivalente. | Igual que registros Custom: vía custom request o monitor command si está expuesto. |

---

## 1. Arquitectura: dos canales hacia WinUAE

### 1.1 GDB server (puerto 2345)

- La extensión lanza **winuae-gdb.exe** (WinUAE con `debugging_features=gdbserver`).
- WinUAE abre un **servidor GDB** en `localhost:2345`.
- La extensión lanza **GDB** (m68k-amiga-elf-gdb) y ejecuta `target remote localhost:2345`.
- Todo el control de depuración (breakpoints, paso, stack, variables, **memoria**) va por **GDB MI**:
  - Memoria: `data-read-memory-bytes`, `data-write-memory-bytes`.
  - CPU: registros estándar vía GDB.
  - Comandos propios de WinUAE: **monitor** (ej. `monitor profile 50 "path.unwind" "path"`, `monitor reset`).

La extensión implementa en `amigaDebug.ts`:

- `customReadMemoryRequest` → `data-read-memory-bytes <addr> <len>`.
- `readMemoryRequest` (DAP estándar) → mismo comando con offset/referencia.
- `customReadRegistersRequest` / `customReadRegisterListRequest` → seguramente envían algo como `monitor custom` o leen registros especiales vía GDB; el código concreto está en la extensión (TypeScript).

### 1.2 IPC Named Pipe (solo Windows, modo debug clásico)

- En **uaeipc.cpp** (WinUAE), cuando se usa el modo **ipc_debug**, los mensajes que llegan con prefijo `DBG ` se pasan a `debug_parser(in, out, outsize)`.
- `debug_parser` es el mismo que usa la consola de debug de WinUAE: comandos como `m <addr>`, `e` (custom registers), `c` (CIA + custom), `r` (CPU), etc. La respuesta es **texto** en `out`.
- La extensión **no usa este pipe** en el flujo normal: usa solo GDB. El pipe existe para otras herramientas o para configuración (ipc_config, ipc_event).

Conclusión: para dar a la IA el mismo poder que la extensión, el camino es **GDB (y los custom requests del DAP que la extensión ya implementa)**, no el pipe IPC, salvo que se construya un cliente específico que hable por pipe con WinUAE en modo ipc_debug.

---

## 2. Imagen que muestra el Amiga

- **Durante el Frame Profiler**: WinUAE (al ejecutar `monitor profile N ...`) genera un archivo binario de perfil. En ese flujo, WinUAE también **captura el framebuffer** (la imagen que está mostrando el emulador) y la incluye en el archivo de perfil. La extensión lee ese archivo y construye el `.amigaprofile`; en formato **IAmigaProfileSplit** las capturas pueden estar en:
  - `.screenshots[]`: array de strings `data:image/jpg;base64,...` (un frame por elemento), o
  - `.frames[].capture`: mismo formato en cada frame (formato antiguo).
- **Graphics Debugger**: usa **savestates** (.uss). Carga el savestate, lee memoria y registros Custom del estado guardado y reconstruye bitplanes, copper, blitter, etc. para visualizar. No es “captura de pantalla en vivo”, sino análisis del estado guardado.

Por tanto, para que la IA “vea” la misma imagen:

1. **Con perfil**: el usuario (o un flujo que dispare Profile) genera un `.amigaprofile`; luego `scripts/parse-amigaprofile.sh <ruta> screenshots` extrae las imágenes (ya implementado). La IA puede leer esas imágenes.
2. **Sin perfil**: no hay un comando “screenshot” expuesto vía GDB/DAP. Opciones futuras: un `monitor screenshot "path"` en WinUAE que guarde un PNG, o un MCP que capture la ventana de WinUAE (por ejemplo vía captura de ventana del SO).

---

## 3. Registros Custom (puertos del chipset)

- En WinUAE, el estado de los chips (Custom) está en memoria y en estructuras internas; el comando de consola `e` (o `e x` para AGA) vuelca todos los Custom.
- La extensión muestra la vista **Custom Registers**; para rellenarla llama a `customReadRegistersRequest` / `customReadRegisterListRequest`. Esas funciones en `amigaDebug.ts` envían requests al backend (MI2/GDB). Lo más probable es que usen un **monitor command** del GDB server de WinUAE que devuelve el volcado de Custom (o que lean una región de memoria mapeada donde WinUAE escribe ese dump).

Para que la IA tenga lo mismo:

- Si el **MCP Debug Tools** (o el DAP client que use la IA) puede enviar **custom requests** al adaptador (`read-registers`, `read-register-list`), la IA podría obtener los Custom sin cambios en la extensión.
- Si no: habría que exponer en MCP una herramienta que invoque esas custom requests, o documentar el comando GDB `monitor ...` exacto que usa la extensión (requiere revisar el código de la extensión y/o del fork WinUAE para el protocolo del gdbserver).

---

## 4. Toda la memoria

- La extensión lee memoria con **GDB** `data-read-memory-bytes <addr> <length>`.
- El espacio de direcciones del Amiga (chip, fast, slow, ROM, etc.) es el que ve WinUAE; GDB sirve esa memoria.

Para la IA:

- **DAP** `ReadMemory` está implementado en la extensión; si el MCP que usa la IA expone “leer memoria” (por referencia o por dirección/offset), la IA ya puede leer cualquier rango.
- En caso contrario, si el MCP permite ejecutar un comando en el debugger (por ejemplo `execute-command` con `data-read-memory-bytes 0x0 4096`), la IA podría leer memoria ejecutando ese comando y parseando la respuesta (la extensión ya tiene `execute-command` como custom request).

---

## 5. Código fuente de referencia

- **Extensión**: [github.com/BartmanAbyss/vscode-amiga-debug](https://github.com/BartmanAbyss/vscode-amiga-debug)  
  - `src/amigaDebug.ts`: launch, custom requests (`read-memory`, `read-registers`, `execute-command`, `start-profile`), lectura de memoria vía MI2.
  - `src/backend/mi2.ts`: envío de comandos GDB MI.
  - `src/backend/profile.ts` / `profile_common.ts`: lectura del archivo de perfil binario que escribe WinUAE y generación del `.amigaprofile` (incl. screenshots).
- **WinUAE (fork)**:
  - [github.com/BartmanAbyss/WinUAE](https://github.com/BartmanAbyss/WinUAE)
  - `uaeipc.cpp`: IPC por Named Pipe; modo `ipc_debug` y `debug_parser` para comandos `DBG ...`.
  - `debug.cpp`: `debug_parser`, comandos `m`, `e`, `c`, `r`, etc., y volcado de registros Custom (`dump_custom_regs`).
  - Servidor GDB: en el fork suele estar en un módulo tipo `barto_gdbserver` o integrado en el árbol; acepta `monitor profile`, `monitor reset`, y posiblemente comandos para custom/memoria.

---

## 6. Qué falta para que la IA tenga el mismo poder

1. **Memoria**: Comprobar si MCP Debug Tools expone lectura de memoria (DAP ReadMemory o custom `read-memory`). Si no, usar `execute-command` con `data-read-memory-bytes <addr> <len>` y parsear la salida.
2. **Registros Custom**: Comprobar si MCP expone custom request `read-registers` / `read-register-list`. Si no, añadir en MCP una herramienta que envíe esa request al adaptador Amiga, o documentar el `monitor ...` correspondiente y usarlo vía `execute-command`.
3. **Imagen en vivo**: Sin nuevo trabajo en WinUAE o en captura de ventana:
   - Flujo actual: Profile (Multi) → `.amigaprofile` → `parse-amigaprofile.sh` → IA lee imágenes extraídas.
   - Posible mejora: en WinUAE, comando `monitor screenshot "path"` que guarde un PNG del framebuffer actual; luego la IA podría pedir ese archivo tras pausar.
4. **Pipe IPC**: Solo si se quiere usar comandos `DBG ...` sin pasar por GDB: implementar un cliente (script o MCP) que se conecte al Named Pipe de WinUAE, envíe `ipc_debug`, luego `DBG e` (u otros) y parsee la respuesta de texto. No es lo que usa la extensión en el flujo estándar.

---

## 7. Resumen para el agente

- **Ya puede** (con sesión de debug Amiga y MCP/DAP): breakpoints, continuar/paso, call stack, variables, expresiones; y si el cliente expone memoria, leer memoria Amiga.
- **Puede con flujo manual**: obtener imagen del Amiga generando un perfil (Profile Multi), indicando la ruta del `.amigaprofile` y ejecutando `scripts/parse-amigaprofile.sh` para extraer capturas; luego el agente puede leer esas imágenes.
- **Falta por exponer de forma explícita** (según soporte MCP): lectura de **registros Custom** (custom request `read-registers` / `read-register-list`) y, si se desea, un comando “screenshot” vía WinUAE o captura de ventana.

Si quieres que el agente tenga **exactamente** el mismo poder que el desarrollador de la extensión (ver imagen, Custom, memoria a bajo nivel), el código fuente de la extensión y del fork WinUAE es la referencia; los puntos anteriores indican por qué canal va cada cosa y qué habría que exponer o automatizar (MCP, scripts o extensiones WinUAE) para igualar esa capacidad.
