# 105_tile_scroll_xyunlimited_dualpf

Standalone tutorial demo for a four-direction, page-backed virtual tilemap.
It uses its own procedural tile generator and does not include or reuse another
demo source.

## Virtual map and editor encoding

`PrefilledMap` uses `u16` cells and `u16` coordinates. The example is 256x128
cells, but the representation is deliberately independent of the display surface.
The shared `TileFieldMap` API is a non-owning view, so an editor can provide a
different map to every field. An asset pipeline may keep an editor-friendly u8
source and expand it to u16 while cooking the asset; runtime indices are never
truncated.

## Independent fields

`eng/graphics/field_controller.hpp` separates the logical field from the shared
DPF hardware scene:

- `TileFieldMap` identifies one map; the two fields in this demo have different
  prefilled maps.
- `TileFieldConfig` identifies that field's tileset, playfield, scroll policy and
  upload budget.
- `TileFieldState` owns that field's camera, pending page loads, physical page
  assignments and upload count.
- `TileFieldController::begin` and `update` are called once per field. They append
  only that field's `TileBlockCopy` jobs to the common `FramePlan`.

The DPF compositor still combines the two hardware inputs into one Copper list, as
the Amiga requires. That does not make the maps, offsets, page state or budgets
shared. A future field can instead use `TileFieldSource::Bitmap` or `Canvas`, fill
the non-owning `TileFieldBitmap` view, leave `map == nullptr`, and set `scroll ==
false`; the bitmap/canvas driver then owns its pixel data while the field
controller contributes no tilemap work.

## Four-page framebuffer

`TileScrollScene<TileScrollMode::dual(3, 3), 24, 20>` uses a 704x576 surface
(44x36 physical tiles). The logical world page is 20x16 tiles (320x256 pixels),
but each physical slot is 22x18 tiles (352x288 pixels). The extra two columns and
rows overlap the right and bottom edges so the chipset's extra fetched word cannot
expose an unfilled neighbor page. The physical pages are laid out as:

```text
page 0 (0,0)       page 1 (22,0)
page 2 (0,18)       page 3 (22,18)
```

Each playfield selects its logical page independently through `TileScrollInput::page`.
The controller loads 22x18 tiles from the logical page base and maps the source
coordinates from that 20x16 page. Its camera offset is the world position modulo
320x256, while the driver adds the physical origin to the Copper bitplane pointers
and page-aware `make_playfield_upload_jobs` destination. No scratch buffer,
`CopyRect`, or other demo source is involved.

## Progressive paging

The camera stores its world tile origin and moves in all four directions. Each
playfield has a fixed queue of four pending page loads, each retaining its page
coordinates and tile cursor. The scheduler prefetches the next horizontal and
vertical pages, their diagonal, and then a farther page in the travel direction.
It uploads four tiles per playfield per frame (eight `TileBlockCopy` jobs total),
and never writes the physical slot currently displayed by that playfield. The
camera boundary wait is therefore only a safety fallback, not the normal paging
path. Every runtime upload is a `TileBlockCopy`.

The runtime status marker contains the current physical page in its high detail
bits and the cumulative upload count in its low bits. The frame marker remains
available through `g_eng_run_status` for runner and analyzer tools.

## Verification

```bash
AMIGA_BIN_PATH=C:/Users/dvdjg/Documents/programa/AI/Amiga/vscode-amiga-debug/bin/win32 \
  bash ./tools/build/build-demo.sh demos/105_tile_scroll_xyunlimited_dualpf --debug --clean
bash ./tools/run/run-demo.sh demos/105_tile_scroll_xyunlimited_dualpf --sequence-frames 720
bash ./tools/analyze/analyze-demo.sh demos/105_tile_scroll_xyunlimited_dualpf
bash ./demos/105_tile_scroll_xyunlimited_dualpf/analyze-sequence.sh
```

`analyze-sequence.sh` ejecuta una prueba larga que cruza paginas y usa
`analyze_105_tile_visibility.mjs`. El analizador compensa el desplazamiento de
cada par de capturas y examina el interior de cada tile de 32x32 en la captura
(16x16 en el Amiga). Un bloque grande cambiado despues de compensar el
movimiento indica que se ha dibujado un tile dentro del viewport, en vez de en
una pagina oculta.
