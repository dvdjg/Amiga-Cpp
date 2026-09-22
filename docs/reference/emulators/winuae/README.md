# WinUAE-DBG — fichas de hardware

Lo **observado** en el código fuente de WinUAE (local: `../WinUAE-DBG/`) al contrastar el comportamiento de los chips con la documentación oficial. Una ficha por **tema**, con referencias a **fichero y línea** del fuente. Ver el procedimiento y «cómo añadir una ficha» en [`../README.md`](../README.md).

| Tema | Foco | Ficha |
|---|---|---|
| Colisión de sprites | `CLXCON`/`CLXDAT` (máscara y latch) | [collision.md](collision.md) |
| Sprite DMA | estructura de sprite con cabecera | [sprite-dma.md](sprite-dma.md) |
| Copper: autoridad de escritura | `CDANG` (`COPCON`) | [copper.md](copper.md) |
| Disco a nivel de device | `DSKPT`/`DSKLEN`/`DSKBYTR` (DMA, MFM) | [trackdisk.md](trackdisk.md) |
| IRQ de audio (`AUD0..3`, nivel 4) | `setirq`/`event_audxdat_func`, `AUDxLEN`/`AUDxLCH` | [audio-irq.md](audio-irq.md) |
