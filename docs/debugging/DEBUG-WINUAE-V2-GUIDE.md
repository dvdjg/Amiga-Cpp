# Guía de depuración con WinUAE-DBG v2.x (para la IA)

Referencia práctica de las herramientas de depuración del fork `WinUAE-DBG`
expuestas por `mcp-winuae-emu`. Léela junto a
`WinUAE-DBG/docs/WINUAE-MONITOR-EXTENSIONS.md` (spec canónica) y
`WinUAE-DBG/GDB_MONITOR_COMMANDS.md` (comandos monitor base).

> **Regla de oro**: usa el build **x86** (`winuae-gdb.exe`). El build x64 tiene
> un bug preexistente (boot congelado) que impide el handshake GDB.

---

## 1. Inventario de herramientas

### 1.1 Tools MCP (`winuae-emu`)

| Tool | Qué hace | Cuándo usarla |
|---|---|---|
| `winuae_connect` / `winuae_connect_existing` | Lanzar o adjuntar WinUAE por GDB | Inicio de sesión |
| `winuae_emulator_status` | Telemetría: ciclos, frame, vpos/hpos, warp, `baseText`, nº de bp/wp/protects, rewind | Confirmar que corre; estado global antes de decidir el siguiente paso |
| `winuae_memory_read` / `_write` / `_dump` | Leer/escribir memoria | Inspección directa de buffers, bitplanes, registros custom |
| `winuae_registers_get` / `_set` | Registros CPU | Leer/escribir D0-A7/SR/PC |
| `winuae_watchpoint_set_ext` / `_list` / `_last` / `_clear_ext` | Watchpoints con **origen** y predicados | "¿quién/cuándo toca X?" — ver §3 |
| `winuae_protect` | `block`/`set` de memoria | Congelar/forzar valores (cheat, tests de estado) |
| `winuae_breakpoint_set` / `_clear` | Breakpoints | Parar en un punto de código |
| `winuae_step` / `_continue` / `_pause` / `_wait_stop` | Control de ejecución | Avanzar/parar |
| `winuae_rewind` | `start`/`stop`/`status`; restore | **Sólo** inspeccionar un estado pasado (congela la emulación) |
| `winuae_trace` | Trazas host de watch/protect/rewind → `%TEMP%\winuae-gdb.log` | Tener un registro de "qué pasó" durante una temporada |
| `winuae_side_read` | Canal lateral `state`/`regs`/`mem`/`runstatus` | GDB inerte (tras rewind) u observación no intrusiva |
| `winuae_debugperiph` | Periférico in-Amiga en `0xB70000` | Telemetría del propio programa + checkpoints — ver §4 |
| `winuae_run_program` / `winuae_exec_chunk` / `winuae_load` | Cargar/ejecutar binarios | Cargar código y datos |

### 1.2 Comandos `monitor` (via `qRcmd`)

`status`, `watch`, `protect`, `rewind`, `trace`, `debugperiph`, `memcfg`,
`disasm`, `screenshot`, `input ...`, `profile`, `offset`, `logfile`.
Cualquier tool MCP que acabe en `monitor X` es un wrapper de `monitor X`.

### 1.3 Canal lateral (puerto 2346)

`state`, `regs`, `mem <addr> <len>`, `runstatus <addr>`, `screenshot`, `input`.
Independiente de GDB; útil cuando GDB no está disponible o quedó inerte.

---

## 2. Guía de decisión: síntoma → herramienta

| Síntoma / objetivo | Herramienta recomendada |
|---|---|
| El emulador ¿corre? ¿en qué frame? | `winuae_emulator_status` |
| Alguien toca una dirección (CPU/copper/blitter) | `winuae_watchpoint_set_ext` con `src=` + `winuae_watchpoint_last` |
| El copper escribe un registro custom en cierta línea | `winuae_watchpoint_set_ext { address: "0xdff180", access: "w", size: 16, source: "copper" }` |
| El blitter pisa un buffer | `winuae_watchpoint_set_ext { source: "blitter", access: "w" }` |
| Una variable cambia a un valor concreto | `watch ... value=0x...` o `must_change=true` |
| Congelar un buffer / forzar un valor (cheat) | `winuae_protect { action: "block"\|"set", ... }` |
| Programa hace algo raro al cambiar de tile-set/paleta | Instrumentar el programa con el **periférico** (consola + checkpoints) — §4 |
| Ver coste por segmento del frame (presupuesto de ciclos) | **Checkpoint profiler** (`debugperiph checkpoints`) — §4 |
| Saber qué cambió entre dos momentos | `winuae_rewind` (inspección del snapshot) + `winuae_side_read` |
| Guardar un registro de eventos | `winuae_trace` + leer `%TEMP%\winuae-gdb.log` |
| Capturar pantalla como evidencia | `winuae_screenshot` (o `winuae_side_read` `screenshot`) |
| A/B de una variante de la demo | `winuae_debugperiph arg <n> <valor>` antes de que el programa la lea |
| Aislar un crash sin adivinar | Consola del periférico (assertions) + breakpoint auto-dirigido |

---

## 3. Watchpoints con origen (el "quién/cuándo toca X")

Valores de `src=`: `cpu`, `cpud`, `cpudw`, `cpudr`, `copper`, `blitter`,
`dma`, `bpl`, `spr`, `audio`, `disk`, `bpl0-7`, `spr0-7`, `audio0-3`, `all`.

Ejemplo — ver qué escribe el Copper en COLOR00:
1. `winuae_watchpoint_set_ext { address: "0xdff180", access: "w", size: 16, source: "copper" }`
2. `winuae_continue` + `winuae_wait_stop`
3. `winuae_watchpoint_last` → `addr=0x00dff180 rwi=w size=2 src=copper val=0x00000f0f pc=0x...`

Nota: un `long` del 68000 cycle-exact se escribe como **dos words**; el hit
puede reportar el sub-word (addr=0x50001 en un watch de 0x50000). Para
predictibilidad, usa `size=16` para word-writes y `size=32` para longs.

---

## 4. Periférico de depuración in-Amiga (`0xB70000`) + checkpoint profiler

El **programa emulado** se auto-instrumenta escribiendo/leyendo estas
direcciones. Es el equivalente de "trazas dentro del Amiga".

| Dirección | Acceso | Uso |
|---|---|---|
| `0xB70000` | write byte | carácter a consola (flushea con `0`/`\n`/`\r` → `DBGPERIPH: ...` en GDB O + log) |
| `0xB70004` | write long | solicita breakpoint en esa dirección |
| `0xB70008`/`0C`/`10` | write long | bases de sección `.text/.data/.bss` (para resolución de símbolos) |
| `0xB70020` | write long | **checkpoint** slot 0-63 (registra ciclos+frame) |
| `0xB7E900..E924` | read long | debug args 0-9 (`winuae_debugperiph arg <n> <valor>`) |
| `0xB7E928` | read long | contador de ciclos de CPU |

Subcomandos: `winuae_debugperiph` (status), `arg <n> <valor>`, `console`,
`checkpoints`, `flush`.

### 4.1 Patrón de instrumentación (engine C++ / asm)

La demo `demos/101_ehb_tile_scroll_driver` ya lo usa como ejemplo real
(`engine/include/eng/debug/peripheral.hpp` + su `update`):

```cpp
// engine/include/eng/debug/peripheral.hpp — API homogénea del periférico
eng::debug::DebugPeripheral::checkpoint(0);               // inicio de frame
if (camera_tile_changed) {
    eng::debug::DebugPeripheral::console_line("TILE_CHANGE");
    eng::debug::DebugPeripheral::checkpoint(10);          // antes del upload
}
upload_prefetch_tiles(...);
eng::debug::DebugPeripheral::checkpoint(11);              // después del upload
```

Tras correr la demo, la IA consulta:
- `winuae_debugperiph checkpoints` → `[0] cycles=… frame=… count=N`,
  `[10] cycles=…`, `[11] cycles=…`. El **delta `[10]→[11]` mide el coste de
  materializar el cambio de tile-set** (upload de prefetch) por frame.
- `winuae_debugperiph console` / el log → las líneas `DBGPERIPH: TILE_CHANGE`
  correlacionan el artefacto con el frame/cámara exactos.

Verificación del scroll de la demo (4 direcciones + diagonal a 50fps, con
ollama): `tools/analyze/verify-scroll-directions.mjs` (usa la secuencia de
`run-demo.sh`, calcula el vector de scroll por par y valida visualmente).
La demo 101 verifica su scroll fino horizontal (`cameraX` 94-97 / 112-109) en
`demos/101_ehb_tile_scroll_driver/analyze-sequence.sh`.

### 4.2 Ejemplo manual bare-metal

```cpp
volatile char *const DP_CONSOLE = (volatile char *)0xB70000;
volatile unsigned int *const DP_CP = (volatile unsigned int *)0xB70020;
volatile unsigned int *const DP_CYCLES = (volatile unsigned int *)0xB7E928;

void tile_set_changed(int camx) {
    *DP_CONSOLE = 'T'; *DP_CONSOLE = 'S'; *DP_CONSOLE = ' '; *DP_CONSOLE = 0;
    *DP_CP = 10;                       // checkpoint antes del blit de transición
    // ... blit de transición ...
    *DP_CP = 11;                       // checkpoint después
    unsigned int cost = *DP_CYCLES;    // si quieres medir en el propio engine
}
```

### 4.2 Breakpoint auto-dirigido

```cpp
if (tile_row_ptr >= bank_end) {
    volatile unsigned int *const DP_BRK = (volatile unsigned int *)0xB70004;
    *DP_BRK = (unsigned int)tile_row_ptr;   // rompe; la IA inspecciona
}
```

### 4.3 Cuándo usar checkpoints vs watchpoints vs periférico

- **Periférico (checkpoints/consola)**: cuando el programa conoce el momento
  (cambios de modo, transiciones, fin de rutina). Bajo coste, sin parar.
- **Watchpoint `src=`**: cuando no sabes quién toca algo (caza del culpable).
- **Breakpoint**: cuando sabes el punto exacto y quieres inspeccionar registros.

---

## 5. Verificación / pruebas

- Batería RSP: `mcp-winuae-emu/scripts/verify-monitor-extensions.mjs` (19 tests).
- Periférico: `mcp-winuae-emu/scripts/verify-debug-peripheral.mjs` (7 tests).
- Scroll 4 direcciones + diagonal (pixel-diff + ollama):
  `Amiga-Cpp/tools/analyze/verify-scroll-directions.mjs` (usa la secuencia de
  `run-demo.sh`; opcional `--regen`).
- MCP end-to-end: `mcp-winuae-emu/scripts/verify-monitor-mcp-tools.mjs`.
- Visual + ollama local: `mcp-winuae-emu/scripts/verify-visual-copper.mjs`
  (configura un cobre magenta y pide a `qwen3-vl`/`gemma3` que describa la
  captura; confirma que la renderización sigue funcionando).
- Rewind + canal lateral: `mcp-winuae-emu/scripts/verify-side-channel-after-rewind.mjs`.

Requieren una config A500 con Kickstart (p. ej. `C:/Amiga/A500-Dev.uae` o una
con `headless=yes`), y la demo compilada.

---

## 6. Limitaciones y avisos

- **x64**: no usar (boot congelado; ver spec).
- **Rewind**: el restore congela la emulación; no "continuar" desde ahí. Sólo
  inspeccionar el snapshot por canal lateral. La captura es intermitente
  (depende de `hsync_counter % statecapturerate`); si `rewind` devuelve
  `E01 no rewind state available`, esperar y reintentar.
- **Protect/watch interceptan accesos del programa mientras corre**, no las
  escrituras del propio depurador con el emulador pausado.
- **Escrituras de 32 bits**: el 68000 cycle-exact descompone un `long` en dos
  `word`; los watchpoints/protects pueden reportar sub-words.
- Los periféricos de `0xB70000` sobrescriben el banco que había ahí (zona
  reservada de expansión en A500); es intencional y no afecta a la CIA
  (0xBF) ni a la renderización.
