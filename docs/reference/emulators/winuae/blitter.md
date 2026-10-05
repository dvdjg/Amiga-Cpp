# WinUAE — Blitter

Lo **observado** en `../WinUAE-DBG/` sobre el Blitter y sus puntos delicados. Contrastado con el AHRM 3.ª.

## Registros y arranque

- Handlers en `custom.cpp`: puntero A `BLTAPTH/L` (`3888`, tabla de registros `7323-7324`), puntero D `BLTDPTH/L` (`3930`, `7329-7330`), módulos `BLTxMOD` (`7334-7337`), datos `BLTxDAT` (`7339-7341`), `BLTCON0` (`3859`, `3869`), `BLTCON1` (`3875`), `BLTSIZE` (`3998`).
- **El blit arranca al escribir `BLTSIZE`**, no al escribir los punteros: `custom.cpp:3963-3975` (`BLTSIZE_func` → `do_blitter`). Orden correcto: punteros → módulos → máscaras/datos → `BLTCON0/1` → **`BLTSIZE`**.

## `BLTSIZE` (alto × ancho)

- `custom.cpp:3966-3967`: `vblitsize = v >> 6` (bits 15..6; `0` → 1024), `hblitsize = v & 0x3F` (bits 5..0; `0` → 64). Es decir `BLTSIZE = (alto << 6) | ancho`.
- Trampa clásica: **intercambiar alto y ancho** da blits desplazados o parciales (el ancho se aplica en palabras de 16 bits).

## Modos

- Ejecución: `blitter.cpp:2030` (`do_blitter`) y `930` (`actually_do_blit`).
- **Modo línea** (`BLTCON1` bit 0): `blitter.cpp:855` (`blitter_line_minterm`), `712` (`blitter_line_minterm_extra`).
- **Minterms**: byte bajo de `BLTCON0`; tabla en el AHRM y uso del engine en [`blitter-line-subpixel-fill.md`](../../amiga/techniques/blitter-line-subpixel-fill.md).

## Canal D (destino)

- D es **lectura-modificación-escritura** del contenido previo combinado con los canales según el minterm (no una copia implícita): para escribir A→D hay que seleccionar el minterm correcto y, normalmente, cargar el canal D con `BLTDAT` cuando no se quiera leer memoria.

## Validación / engine

- Técnicas: [`docs/reference/amiga/techniques/`](../../amiga/techniques/README.md) (minterms, línea, subpíxel).
- Uso en el engine: `engine/include/eng/graphics/blob.hpp` (minterms nombrados).

## Referencias

- AHRM 3.ª, cap. del Blitter + [ERRATA_Y_NOTAS.md](../../ahrm/ERRATA_Y_NOTAS.md).
- Código: `../WinUAE-DBG/custom.cpp`, `../WinUAE-DBG/blitter.cpp`, `../WinUAE-DBG/blitfunc.cpp`.
