# Demos Amiga

Demos del engine para **Amiga OCS/ECS (A500)**. Convención de nombres y
estructura: `docs/STRUCTURE.md` §4. Clasificación por concepto:

- **Fundamentos (`0xx`)**: toolchain/boilerplate, Chip RAM, Copper, paletas,
  bobs/Blitter.
  - `000_toolchain_cpp23` — herramienta/toolchain, hola mundo con READY.
  - `010_chip_slow_memory` — Chip vs Fast RAM.
  - `020_copper_basic` — copperlist básica.
  - `030_ehb_palette_zones` — paletas EHB por zonas.
  - `040_palette_cycle_effect` — ciclo de paleta.
  - `050_blitter_bobs`, `051_blitter_shifted_bobs`, `052_tile_staging_blits` — Blitter y bobs.
- **Scroll y tile fields (`1xx`)**:
  - `100_virtual_tile_scene_scroll` — escena virtual con scroll.
  - `101_ehb_tile_scroll_driver`, `102_tile_scroll_dualpf`, `103_tile_scroll_ring`,
    `104_tile_scroll_ring_dualpf`, `105_tile_scroll_xyunlimited_dualpf`,
    `106_tile_field_showcase`, `107_xlimited_corkscrew` — drivers de scroll y tiles.
- **Escenas con pipeline de assets (`2xx`)**:
  - `201_ehb_map` — mapa real EHB X-Limited con el pipeline completo.
  - `202_xlimited_dpf` — dual playfield parallax 2:1.

Cada demo documenta en su `README.md` invariantes, comandos y validación.
Build/run/analyze desde `demos/amiga/`:
```
bash ./tools/build/build-demo.sh demos/amiga/107_xlimited_corkscrew --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/107_xlimited_corkscrew
bash ./tools/analyze/analyze-demo.sh demos/amiga/107_xlimited_corkscrew
```