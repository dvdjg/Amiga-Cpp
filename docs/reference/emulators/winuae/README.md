# WinUAE-DBG — fichas de hardware

Lo **observado** en el código fuente de WinUAE (local: `../WinUAE-DBG/`) al contrastar el comportamiento de los chips con la documentación oficial. Una ficha por **tema**, con referencias a **fichero y línea** del fuente. Ver el procedimiento y «cómo añadir una ficha» en [`../README.md`](../README.md).

| Tema | Foco | Ficha |
|---|---|---|
| Colisión de sprites | `CLXCON`/`CLXDAT` (máscara y latch) | [collision.md](collision.md) |
| Sprite DMA | estructura de sprite con cabecera | [sprite-dma.md](sprite-dma.md) |
| Color de sprite | prioridad de pareja (número de canal), ATTACH y reuso | [sprite-color-priority.md](sprite-color-priority.md) |
| Blitter | arranque por `BLTSIZE`, decodificación, modos, canal D | [blitter.md](blitter.md) |
| Copper: autoridad de escritura | `CDANG` (`COPCON`) | [copper.md](copper.md) |
| Disco a nivel de device | `DSKPT`/`DSKLEN`/`DSKBYTR` (DMA, MFM) | [trackdisk.md](trackdisk.md) |
| IRQ de audio (`AUD0..3`, nivel 4) | `setirq`/`event_audxdat_func`, `AUDxLEN`/`AUDxLCH` | [audio-irq.md](audio-irq.md) |
| Inyección de teclado y handshake CIA-A | `input key`/`input event` (`256+sc`, ids permutados), `keymcu_execute` | [keyboard-injection.md](keyboard-injection.md) |

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
| Copper no escribe / `CDANG` | [copper.md](copper.md) |
| Colisión de sprites (`CLXCON`/`CLXDAT`) | [collision.md](collision.md) |
| IRQ de audio / DMA de audio | [audio-irq.md](audio-irq.md) |
| Disco (`DSKPT`/`DSKLEN`) | [trackdisk.md](trackdisk.md) |
