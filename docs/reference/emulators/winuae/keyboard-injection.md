# WinUAE-DBG — inyección de teclado (monitor) y handshake de CIA-A

Lo observado en el fuente de WinUAE (local: `../WinUAE-DBG/`) sobre **cómo se inyecta una tecla por el
monitor** y **cómo modela el handshake del teclado**, al depurar la entrada por IRQ del mini-SO
(`eng::os::enable_keyboard`). Ver el procedimiento en [`../README.md`](../README.md).

## Mecanismo observado

| Elemento | Fuente | Comportamiento |
|---|---|---|
| `monitor input key <sc> <1\|0>` | `od-win32/barto_gdbserver.cpp:1844` (canal lateral) y `:3650` (monitor GDB) | Envía `send_input_event(256 + (sc & 0x7f), state, ...)`. El comentario dice «Amiga raw scancode» pero el id no es el rawkey. |
| `monitor input event <id> [state]` | `od-win32/barto_gdbserver.cpp:3629` | Envía `send_input_event(id, ...)` **con el id crudo** de la tabla de eventos. |
| `monitor input joy/mouse ...` | `od-win32/barto_gdbserver.cpp:3653+` | Usa símbolos (`INPUTEVENT_JOY1_LEFT`, …), no rawkeys. |
| Tabla de eventos | `inputevents.def` + macros `DEFEVENT*` en `include/inputdevice.h:213-221` | El id es el **índice del enum** (`INPUTEVENT_ZERO`=0 y +1 por cada `DEFEVENT`). La sección de teclado usa `DEFEVENTKB(KEY_x, …, AK_y, PC)`; `AK_y` es el **rawkey Amiga** (valores en `include/keyboard.h`). |
| Handshake del teclado | `cia.cpp:1177` (`keymcu_execute`) y `cia.cpp:2104-2143` | `handshake = (CRA & 0x40) != 0 && (SDR & 0x80) == 0`; se detecta por **transiciones** de `CR_INMODE1` (0x40 = SPMODE). La longitud mínima la controla `currprefs.cs_kbhandshake` (`cia.cpp:2117-2131`). |
| MCU de teclado | `kbmcu/keyboard_mcu_6500_1.cpp` (`keymcu_run`) | Recibe `handshake` por nivel y lo detecta por flanco. |

## Contraste con la AHRM 3.ª

La AHRM («The Keyboard», línea 7338) dice que **tras recibir un byte** hay que pulsar SP **bajo y luego
alto**, y que el pulso debe durar **≥ 85 µs** para todas las variantes de teclado. WinUAE lo modela
como transiciones de SPMODE con longitud mínima `cs_kbhandshake`.

## Discrepancia observada (esta build del runner)

El **binario** `winuae-gdb.exe` que usa el runner **no coincide con el árbol de fuentes local** en la
tabla de eventos:

- Según el fuente local, el primer evento de teclado es `KEY_F1` = id **140** (`inputevents.def:177`) y `KEY_ESC` = id **150**.
- Escaneando el binario en marcha (`run-demo.sh --key-scan 130-270`, leyendo el rawkey que llega a la demo), el id **140** produce rawkey **0x3d** (no F1) y el **150** produce **0x17** (no ESC). La asignación está **permutada** y los eventos de teclado no ocupan un bloque contiguo claro.

Consecuencia: `monitor input key <sc>` es **inservible en esta build** (mapea a `256+sc`, que cae en los
eventos `SPC_*`, acciones del emulador como expulsar disquete o cambiar interpolación). La vía fiable es
`monitor input event <id>` **con el id calibrado contra el binario**.

## Calibración empírica (`--key-scan`) y su límite

`run-demo.sh --key-scan <from>-<to>` inyecta cada `input event <id>`, resetea el bitmask
`g_key_mask` de la demo (por el canal lateral, con lock `takeover`) y lee qué rawkeys aparecen.
Resultados observados (ids 132–271):

- Cada id produce **dos** rawkeys, `R` y `R|0x40` (p. ej. id 147 → `0x05` y `0x45`). El bit 6 es un
  artefacto del emulador/ISR (mismo patrón para todos los id).
- El rango de teclado del **binario** es una **permutación** de la del fuente (id 147 → ESC, no F8;
  el árbol da `KEY_ESC`=150). No hay fórmula.
- **No es estable entre ejecuciones**: el mismo id (p. ej. 147) da rawkeys distintos
  (`0x05`/`0x45`/`0x3f`) según la ejecución, y `keys` varía (repeticiones). El MCU del teclado
  emulado acepta las teclas con **latencia/variación** (cycle-exact), así que el `id → rawkey` no se
  puede fijar de forma fiable.

Consecuencia: **no se publica** tabla `rawkey → event id` (sería incorrecta). Para validar entrada
basta `--key-events <id>` (la tecla **llega**); para la **identidad** exacta del rawkey no hay vía
fiable en esta build.

## Implicación para el engine / runner

- **Handshake (engine, `engine/src/platform/amiga/amiga_os.cpp`)**: `os_kbd_isr` pulsa
  SP bajo y alto **con el orden correcto y con ≥ 85 µs** entre ambos (busy-wait corto). Con el orden
  invertido o el pulso partido entre ISR y `tick`, el MCU emulado **reenvía** el byte (duplicados) o
  **no envía** la siguiente tecla (solo llega la primera).
- **Runner (`tools/run/run-demo.ts`)**: `--key-events <id,...>` inyecta por id crudo (fiable);
  `--key-scan <from>-<to>` calibra el mapeo `id → rawkey` contra el binario en marcha. `--keys`
  (rawkey vía `input key`) queda **marcado como no fiable** en esta build.

## Enlaces al código

- Engine: `engine/src/platform/amiga/amiga_os.cpp` (`os_kbd_isr`, `enable_keyboard`).
- Runner: `tools/run/run-demo.ts` (`--keys`, `--key-events`, `--key-scan`).
- Demo: `demos/amiga/212_message_loop` (reporta `g_key_last_raw` y `g_eng_run_status`).
