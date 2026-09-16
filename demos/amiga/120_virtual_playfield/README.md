# Demo 120 — Virtual playfield (bitmap continuo + scroll por punteros)

Primera demo de la **matriz de algoritmos de scroll** (`docs/guides/roadmap/SCROLL_DEMOS_CLEANUP.md`):
un mundo grande **400×…** ya dibujado en un único bitmap **FLAT contiguo**; el scroll **no
redibuja nada**, solo mueve `BPLxPT` y `BPLCON1` desde el Copper. Es el contraste exacto del
corkscrew/XYLimited (demo 107), donde el bitmap es un anillo pequeño y el Blitter pinta la banda
entrante.

```
  mundo 448 x 1684 (pre-dibujado una vez)          ventana 320 x 256
  ┌──────────────────────────────────────────┐
  │ ##########                                │   coste por frame = 0 blits
  │ ##########   <- solo se mueven BPLxPT     │   CPU: reprograma registros
  │      ┌──────────────────┐                  │   (BPLCON1 + 4 punteros)
  │      │   ventana visible │                 │
  │      └──────────────────┘                  │
  └──────────────────────────────────────────┘
```

## Qué demuestra

- **`eng::field::BigBufferScroll`** cableada a una superficie real: aporta la cámara saturada por
  eje (el struct modela un eje; se usan dos instancias, X e Y).
- **`CanvasPlayfield`** como superficie flat interleaved, con su `hardware_view()` reescrito para
  la geometría del **fetch ancho** (`DDFSTRT=$30`, 42 B/fila): `planeaddx = ((cam_x-1)&~15)/8`,
  `BPLCON1 = (16-fine)&15` duplicado en ambos nibbles y `BPLMOD = row_bytes*planes - 42`.
  Fórmula tomada del driver lineal verificado `engine/include/eng/graphics/drivers/tile_scroll.hpp`.
- **Cero blits por frame** (a diferencia del anillo): la CPU solo recompone la copperlist.

## Invariantes / límites

- La cámara X **mínima es 1** (con `DDFSTRT=$30` el puntero apunta una word antes en `fine==0`;
  con 0 leería antes del buffer).
- El presupuesto de Chip RAM del mundo es `row_bytes*height*planes`:
  276 KB (3 pl) · **368 KB (4 pl, esta demo)** · 460 KB (5 pl); 6 planos no caben en un A500.
- El mundo se pinta **una sola vez** en `init` (patrón procedural de celdas de 16 px); en el bucle
  no se toca memoria.

## Build / run / verify

```bash
bash ./tools/build/build-demo.sh demos/amiga/120_virtual_playfield --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/120_virtual_playfield
bash ./demos/amiga/120_virtual_playfield/analyze-sequence.sh [--warp]
```

`analyze-sequence.sh` comprueba que la secuencia está **animada** y lee la telemetría `detail`
(marcador `0x12`, cámara X en bits 19..12, cámara Y en bits 11..0).

## Estado

- Al cablear esta demo, `BigBufferScroll` deja de ser «solo test host» y queda **verificada por
  demo** (regla de cierre de `AGENTS.md`).
- Pendiente: art desde el tileset compartido de 32 colores (`out/assets/beginning-fields/32c`) en
  vez del patrón procedural, y promoción del mapper a engine (`ScrollView`/`AmigaDisplayMapper`,
  Fase 3 de `REFACTOR_PLAYFIELD_SCROLL.md`).
