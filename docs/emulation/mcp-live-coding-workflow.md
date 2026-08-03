# Flujo MCP: compilar, desplegar y verificar (WinUAE-GDB)

Protocolo de referencia para la IA y para ti cuando uses el servidor MCP **winuae-emu** (repo hermano `mcp-winuae-emu` junto a este proyecto) y **Cursor-Amiga-C**.

## Alcance de compatibilidad del workflow

Este workflow debe leerse con esta prioridad:

- **primero:** Amiga 500 + Kickstart 1.3
- **despues:** A600 si no introduce diferencias de kernel relevantes
- **luego:** A1200/CD32 como perfiles ampliados

En la parte de kernel/loader, ninguna ruta más moderna debería desplazar a la base A500/Kick 1.3 sin quedar marcada como extensión posterior.

### Visión de capacidades (IA ↔ emulador)

La IA debe poder: **cargar ADF en caliente** cuando el flujo lo permita; **volcar el binario** del cross-compiler en memoria (`winuae_load`, `winuae_memory_write`, etc.); **leer** memoria, registros **custom** y **CPU**; **escribir** memoria y registros; **depurar** (breakpoints, `continue`, **paso a paso** con `winuae_step`, `winuae_wait_stop`). Objetivo adicional: **instantánea única** del estado de la máquina y **decodificación de bitmaps** desde chip RAM para extraer gráficos coloreados — parte de esto es **desarrollo pendiente** en MCP/WinUAE; el inventario hecho/pendiente y la hoja de ruta están en [amiga-test-battery-spec.md](amiga-test-battery-spec.md) §2 y §10.

## 1. Elegir máquina

| Objetivo | Compilación | WinUAE |
|----------|-------------|--------|
| Amiga 500 (real y emulador OCS) | `TARGET_MACHINE=a500` (por defecto), `-m68000` | `config/winuae/amiga500.uae` |
| Amiga 1200 AGA | `TARGET_MACHINE=a1200`, `-m68020` | `config/winuae/amiga1200.uae` |
| CD32 | `TARGET_MACHINE=cd32`, `-m68020` | `config/winuae/cd32.uae` |

```bash
source scripts/select-winuae-machine.sh a1200   # exporta WINUAE_CONFIG
bash scripts/build.sh --debug --machine=a1200
```

Edita los `.uae` y sustituye `YOURUSER`, ROMs y rutas `filesystem`/`filesystem2` antes de depurar. Detalle: [config/winuae/README.md](../config/winuae/README.md).

**MCP:** opcionalmente `winuae_connect` con argumento `config_file` apuntando al `.uae` (sobrescribe `WINUAE_CONFIG` solo para esa conexión).

## 2. Compilar

```bash
bash scripts/verify-build.sh
# o
bash scripts/build.sh --debug
```

Salida: `out/a.exe`, `out/a.map`, `out/a.elf`.

## 3. Desplegar

**Arranque desde ADF (recomendado con Workbench/DOS):**

1. `make adf` o scripts `create-adf` → `out/disk.adf`
2. MCP: `winuae_insert_disk` con ruta a `out/disk.adf` (inserción **en caliente** con sesión ya conectada según soporte del monitor; ver limitaciones en [amiga-test-battery-spec.md](amiga-test-battery-spec.md) §2.1)
3. `winuae_connect` (o `winuae_connect_existing` si ya lanzaste WinUAE con F5 o dejaste una sesión visible abierta de un turno anterior)
4. Para observar el boot/ADF sin romper actividad DOS o disco, usa attach no intrusivo: `force_break=false` e `initialize_stopped=false`.
5. Si el caso es `dos_hunk_exe`, no asumas que `qOffsets` devolverá siempre la forma clásica `Text=...;Data=...;Bss=...`; en abril de 2026 M03 sigue devolviendo respuestas no estándar en la ruta ADF, así que la resolución automática de símbolos del proceso DOS aún no es fiable.
6. No uses `winuae_load` si el programa ya se ejecuta desde el disco; sí úsalo para volcar un binario en una dirección fija bajo control.

Para **A500/Kick 1.3**, esta sigue siendo la ruta base y más conservadora.

**Captura visual fiable:**

- Cuando WinUAE está visible en escritorio, la ruta más fiel para “ver lo mismo que el usuario” es la captura de ventana del host (`capture_mode=host_window` o `WINUAE_SCREENSHOT_CAPTURE_MODE=host_window`).
- `PrintWindow` puede devolver imágenes negras aunque la app real se vea bien; el helper de captura ya prioriza `screen_copy` para evitar ese falso negro.

**Carga directa de binario (sin ADF):**

- `winuae_load` con `file` y `address` (p. ej. `$4000`)
- `winuae_run_program` escribe el fichero en una dirección y pone PC (por defecto `0x40000`) y arranca

## 4. Verificar

1. `winuae_pause` si hace falta
2. `winuae_registers_get` — PC, A7, etc.
3. `winuae_custom_registers` — estado de playfield, copper, Paula, blitter
4. `winuae_memory_read` / `winuae_memory_dump` en direcciones de [out/a.map](..) (sección `.text` suele empezar en `0x400`)
5. `winuae_copper_disassemble` con dirección de `COP1LCH/L` leída de custom regs
6. `winuae_screenshot` con `filepath` o `filename`
7. Siempre que haya captura generada, pasarla por `scripts/lmstudio-vision.mjs` o por el pipeline de `scripts/capture-battery-evidence.mjs` para obtener una descripción/confirmación visual con LM Studio (`qwen2.5-vl-7b-instruct`) y guardar los artefactos `.json` + `.md`.
8. En la batería (`scripts/run-battery-case.mjs`), una captura `live-screen.png` o `decoded-playfield.png` sin su análisis `vision-*` debe considerarse fallo del pipeline, aunque el caso ya estuviera en error por otra causa.
9. Para los casos de batería, preferir la configuración limpia [mcp-amiga-battery.uae](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/.vscode/mcp-amiga-battery.uae) frente a la configuración de debug general, para evitar `dh0/dh1`, `debugging_trigger` y otros efectos laterales ajenos al caso.
10. Si la ejecución cae, entra en requester o devuelve una parada rara, capturar `winuae_postmortem_capture` antes de desconectar para conservar CPU, stack, desensamblado alrededor de PC y snapshot auxiliar.
11. Si el caso usa el harness común y se está diagnosticando por carga `direct`, capturar además `runtime-state.json` / `runtime-state.md` desde `g_battery_runtime_state` para saber si el binario llegó a su primera etapa lógica.

## Estrategias de carga recomendadas

### 1. Loader del sistema operativo

Usa ADF, Workbench o `debugging_trigger` cuando quieras el contexto normal de proceso AmigaOS: segmentos, pila, librerías y arranque DOS/CLI.

Prioridad por compatibilidad:

- **A500/Kick 1.3:** ruta principal
- **A600:** normalmente igual de válida
- **A1200:** válida, pero ya comparte protagonismo con otras rutas de iteración

### 1.b Ruta `devfs` para binarios DOS del repo

Cuando quieras iterar un ejecutable GCC normal sin regenerar floppy cada vez, el repo ya ofrece una ruta basada en `dh0/dh1 + debugging_trigger`:

- preparar `out/devfs/a.exe` con [scripts/stage-devfs-binary.mjs](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/scripts/stage-devfs-binary.mjs)
- capturar evidencia con [scripts/capture-devfs-battery-evidence.mjs](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/scripts/capture-devfs-battery-evidence.mjs)
- usar la config [mcp-amiga-battery-devfs.uae](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/.vscode/mcp-amiga-battery-devfs.uae)

Esta vía sigue siendo DOS/OS-loader, no metal. En abril de 2026 ya se ha validado como ruta de transporte y diagnóstico mejor que el floppy puro, aunque M00/M03 siguen reproduciendo el requester `task held`.

### 2. Carga en dirección fija conocida

Usa `winuae_load` o `winuae_exec_chunk` cuando controles tú la dirección y el contexto mínimo de ejecución. Esta ruta es útil para stubs, probes y experimentos muy pequeños, pero exige una pila coherente y entender bien qué espera el código cargado.

### 3. Carga "metal" en Fast RAM

Para binarios de experimento fuera del SO, la ruta recomendada es usar **Fast RAM** del perfil de emulación, no Chip/Slow RAM. En los perfiles batería actuales se recomienda un A500 con `chipmem_size=1`, `bogomem_size=2` y `fastmem_size=4`, y las cargas directas del pipeline usan por defecto direcciones de Fast RAM (`$00200000+`) para evitar pisar regiones de display o memoria base más sensibles.
El caso [M10_smoke_metal](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/M10_smoke_metal/README.md) ya demuestra este camino con entrada por `battery_metal_entry` y parcheo simbólico en RAM mediante `--patch-symbol` / `--patch-value` en `scripts/capture-battery-evidence.mjs`.
Para iteración rápida sobre un payload metal ya conocido, existe también [scripts/send-metal-command.mjs](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/scripts/send-metal-command.mjs), que escribe `command` + `command_arg` en `g_battery_metal_control` y espera a que el payload los consuma.
`send-metal-command.mjs` también soporta `--keep-session` para intentar dejar WinUAE vivo y una segunda pasada con `--connect-existing`, pero en las sesiones lanzadas por MCP la reconexión del stub GDB sigue siendo frágil; hoy funciona mejor como utilidad de carga+comando en una sola invocación o contra una sesión externa ya abierta.
11. Tras `winuae_continue`, llamar **`winuae_wait_stop`** para que los breakpoints detengan la CPU

Nota de compatibilidad:

- esta ruta es útil en A500, pero no sustituye al loader DOS base de Kickstart 1.3
- debe considerarse una vía diagnóstica o bare-metal, no la ruta normal de un `dos_hunk_exe`

**Breakpoints en código cargado por el OS:** tras arrancar el proceso, `winuae_findproc` (p. ej. nombre `a.exe`) para ajustar la base de texto y breakpoints relocados.

## 5. Herramientas útiles

| Herramienta | Uso |
|-------------|-----|
| `winuae_exec_chunk` | Escribir bytes máquina en memoria, fijar PC (y opcionalmente A7), continuar o dejar en pausa. Código debe ser válido (p. ej. terminar en `RTS`) y la pila coherente si continúas. |
| `winuae_disassemble_full` | Desensamblado m68k por monitor WinUAE |
| `winuae_warp` | Acelerar carga / intros |

Ejemplo mínimo `winuae_exec_chunk` (NOP + RTS en calle ejecutable y pila válida es responsabilidad tuya):

```text
hex: "4e714e75", address: "$80000", pc: "$80000", continue_after: false
```

## 6. Modos “live coding”

| Modo | Cuándo | Pasos |
|------|--------|--------|
| **A — MCP lanza WinUAE** | Automatización completa | `winuae_insert_disk` → `winuae_connect` → verificar |
| **B — WinUAE ya en marcha (F5 o sesión persistente)** | Depuración manual + MCP | `WINUAE_CONNECT_EXISTING=1` o `winuae_connect_existing` sin `winuae_load` si el programa ya corre. Para reutilizar una ventana visible entre turnos, esta vía es hoy la más fiable. |
| **C — Iteración rápida** | Cambias C y recompilas | Rebuild → regenerar ADF si aplica → `winuae_reset` o reinserción de disco; el parche en caliente de memoria con OS y relocs es frágil y no es el flujo por defecto |

## 7. Integración y tests

- Script de proyecto: [scripts/run-integration-test.sh](../scripts/run-integration-test.sh) (build + ADF + prueba UI vía Node).
- Secuencia mínima documentada arriba sustituye una “única hoja de ruta” para comprobar `out/a.exe` con GDB + MCP.
- **Batería de pruebas gráficas/hardware (catálogo, evidencias 320×…, EHB, copper, blitter, dual PF, sprites, audio, AGA):** [amiga-test-battery-spec.md](amiga-test-battery-spec.md).
- **Qué falta por implementar (fases A–E, IDs, MCP):** [amiga-implementation-roadmap.md](amiga-implementation-roadmap.md).
- **Análisis visual local de capturas:** `scripts/lmstudio-vision.mjs` y `.cursor/lmstudio.json`.

## 8. Referencias cruzadas

- Depuración general (DAP, overlay, perfil): [debug-with-ai.md](debug-with-ai.md)
- Reglas del workspace y WinUAE: `.cursor/rules/amiga-verification-flow.mdc`, `amiga-debug-with-mcp.mdc`
## Actualización: memoria y cargas directas

- Antes de fijar una dirección de carga directa, capturar `winuae_memory_map` y guardar su salida en la evidencia del caso.
- En este workspace, `WinUAE-DBG` ya expone `monitor memcfg`, que `mcp-winuae-emu` publica como `winuae_memory_map`.
- Esa evidencia ya confirma que el perfil de batería A500 mapea Chip, Bogo y Fast RAM.
- La capa de transporte ya quedó estabilizada: `winuae_load` puede cargar y verificar T01 en Fast RAM alta.
- El bloqueo actual pasa a ser de runtime del programa: la ruta `direct` entra en `Guru Meditation #00000004`, así que sigue siendo una vía de investigación/debug y no debe marcar un caso como HECHO sin evidencia viva adicional.
- `scripts/run-battery-case.mjs` ya permite `--direct-diagnostic` para ejecutar una segunda pasada `direct` además de la validación ADF/OS-loader, manteniendo esta última como ruta estándar para casos `dos_hunk_exe`.

## Actualización: harness, fresh-launch y persistencia

- Cuando el objetivo es aislar un fallo de arranque ADF, usar `--fresh-launch` en `scripts/capture-battery-evidence.mjs` o en el manifiesto del caso para matar instancias previas de WinUAE y evitar que `connect_existing + reset` contamine el diagnóstico.
- [scripts/send-dev-harness-command.mjs](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/scripts/send-dev-harness-command.mjs) ya existe para escribir `command`, `command_arg` y `metal_control_address` en `g_battery_dev_harness` cuando la ruta DOS-friendly llegue a arrancar con simbolos resolubles.
- En esa misma línea, el harness DOS ya reserva `payload_path`, `payload_stack_size`, `loader_result`, `loader_ioerr` y `payload_seglist` para que la estrategia base pueda apoyarse en `LoadSeg` y una ejecución DOS conservadora con foco en A500/Kick 1.3.
- La ruta metal ya ofrece una autopsia más fuerte: `TRIGGER_ILLEGAL` en M10 deja tanto pista visual roja como estado legible en RAM (`stage_id=0xFFFF`, `detail=4`, `status_flags=0x00040000`, `last_error=4`).
- `WinUAE-DBG` incorpora soporte experimental para mantener el listener GDB tras un `disconnect` con `WINUAE_GDB_PERSIST_LISTENER=1`, pero la reconexión entre invocaciones MCP sigue siendo parcial y no debe asumirse como flujo estable todavia.
