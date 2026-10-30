# WinUAE-DBG — fichas de hardware

Lo **observado** en el código fuente de WinUAE (local: `../WinUAE-DBG/`) al contrastar el comportamiento de los chips con la documentación oficial. Una ficha por **tema**, con referencias a **fichero y línea** del fuente. Ver el procedimiento y «cómo añadir una ficha» en [`../README.md`](../README.md).

> **Diagnóstico de render**: antes de concluir que hay corrupción de registros, aplica [RENDER_CHANGE_PROTOCOL.md](../../guides/methodology/RENDER_CHANGE_PROTOCOL.md). Los `BPLxPT` leídos en display activo son punteros DMA corrientes, no la base estable; usa VBlank/`COP1LC`/copperlist y canarios.

| Tema | Foco | Ficha |
|---|---|---|
| Colisión de sprites | `CLXCON`/`CLXDAT` (máscara y latch) | [collision.md](collision.md) |
| Sprite DMA | estructura de sprite con cabecera | [sprite-dma.md](sprite-dma.md) |
| Color de sprite | prioridad de pareja (número de canal), ATTACH y reuso | [sprite-color-priority.md](sprite-color-priority.md) |
| Blitter | arranque por `BLTSIZE`, decodificación, modos, canal D | [blitter.md](blitter.md) |
| Blitter ocupado | escrituras de registros/`BLTSIZE` con blit activo (contrato AHRM; restart en WinUAE) | [blitter-busy-writes.md](blitter-busy-writes.md) |
| Copper: autoridad de escritura | `CDANG` (`COPCON`) | [copper.md](copper.md) |
| Disco a nivel de device | `DSKPT`/`DSKLEN`/`DSKBYTR` (DMA, MFM) | [trackdisk.md](trackdisk.md) |
| IRQ de audio (`AUD0..3`, nivel 4) | `setirq`/`event_audxdat_func`, `AUDxLEN`/`AUDxLCH` | [audio-irq.md](audio-irq.md) |
| Inyección de teclado y handshake CIA-A | `input key`/`input event` (`256+sc`, ids permutados), `keymcu_execute` | [keyboard-injection.md](keyboard-injection.md) |
| Inyección de ratón emulado | `input mouse move/abs/button`, activación del puerto y contadores `JOY0DAT` | [mouse-injection.md](mouse-injection.md) |

## Síntoma → fuente (empezar por aquí)

Al depurar, **leer primero la fuente** de la fila correspondiente (regla dura AGENTS §0/§1.12).

| Síntoma | Empezar por |
|---|---|
| Color/píxel de sprite que no sale, sprite tapado, prioridad | `drawing.cpp` (`denise_render_sprites`, `sprite_offs`); [sprite-color-priority.md](sprite-color-priority.md) |
| Bits de color de un par *attached* / ATTACH | `drawing.cpp:2722`, `4239`; [sprite-color-priority.md](sprite-color-priority.md) |
| Multiplexado de sprite por línea (reuso) | `drawing.cpp:2656`, `4940` (`matchsprites2`); [sprite-color-priority.md](sprite-color-priority.md) |
| Estructura de sprite / cabecera / columna fantasma | `sprite-dma.md`, `custom.cpp` (`generate_sprites`) |
| Blit desplazado/parcial, alto↔ancho | `custom.cpp:3963` (`BLTSIZE_func`); [blitter.md](blitter.md) |
| Blit que no arranca | `custom.cpp` (`BLTSIZE` → `do_blitter`); [blitter.md](blitter.md) |
| Planos perdidos al encadenar blits / escribir con Blitter ocupado | [blitter-busy-writes.md](blitter-busy-writes.md) (`blitter.cpp:2036/2058/2101`; AHRM «Multitasking and the Blitter») |
| Copper no escribe / `CDANG` | [copper.md](copper.md) |
| Colisión de sprites (`CLXCON`/`CLXDAT`) | [collision.md](collision.md) |
| IRQ de audio / DMA de audio | [audio-irq.md](audio-irq.md) |
| Disco (`DSKPT`/`DSKLEN`) | [trackdisk.md](trackdisk.md) |
| Clickes/arrastres emulados no llegan a una demo | [mouse-injection.md](mouse-injection.md) (`send_input_event`/`check_enable` frente a `setmousestate`) |
