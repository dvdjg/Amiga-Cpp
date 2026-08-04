# L0 — Bare metal Amiga 500

Tests de la capa más baja: **escritura directa de registros custom**, bitplanes en formato
planar, copperlists construidas word a word, DMA y Blitter. Es el repaso sistemático de la
programación bare metal del A500 sobre el que se apoyan todas las capas superiores.

Aquí el objetivo didáctico manda: cada test muestra el registro o técnica con su
documentación, y el script de verificación comprueba el resultado por el canal lateral
(leer el framebuffer, escribir figuras, capturar) sin necesitar que el código "se vea bien".

## Registros custom más usados (base `$dff000`)

| Registro | Offset | Función |
|----------|--------|---------|
| `DMACON` | `0x096` | Control de DMA (bit 15 = set/clear). |
| `BPLCON0` | `0x100` | Configuración de bitplanes (BPU bits 14-12, COLOR bit 9, EHB bit 8). |
| `BPLCON1` | `0x102` | Scroll fino horizontal (PFCH/PFSH). |
| `BPLCON2` | `0x104` | Prioridades de playfield/sprites. |
| `BPL1MOD`/`BPL2MOD` | `0x108`/`0x10a` | Módulos de bitplane (interleaving, saltos de línea). |
| `DIWSTRT`/`DIWSTOP` | `0x08e`/`0x090` | Ventana de display (start/stop horizontal y vertical). |
| `DDFSTRT`/`DDFSTOP` | `0x092`/`0x094` | Ventana de fetch de DMA (ancho efectivo). |
| `BPLxPTH`/`BPLxPTL` | `0x0e0+` | Punteros de bitplane (high/low). |
| `COLORxx` | `0x180+` | Registros de paleta RGB444. |
| `COP1LC`/`COPJMP1` | `0x080`/`0x088` | Carga y salto de la copperlist. |

## Catálogo

| ID | Test | Estado |
|----|------|--------|
| [L0-010](010_display_320x240/README.md) | display_320x240 — 5 bitplanes, paleta 32, líneas planares, verificación y vuelta a Workbench. | implementado |
