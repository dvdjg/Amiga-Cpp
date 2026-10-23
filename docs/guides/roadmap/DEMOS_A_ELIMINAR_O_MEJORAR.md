# Demos a eliminar o mejorar (auditoría de calidad)

Listado vivo de demos que **no pasan el umbral de calidad**. Regla vigente: **una demo por
debajo de 25 fps no es usable** — se marca *a mejorar o eliminar*. El objetivo es que al
ejecutar una demo se vea si **se ha empeorado** y si **merece la pena**, con señales
verificables y no con impresiones.

## Criterios de la ficha (por demo)

1. **¿Demuestra lo que pretende?** — visión (Ollama) + el criterio de captura propio
   (`analyze-screenshot.sh` de la demo); la **descripción de la secuencia** que se envía a
   Ollama (`vision-points.json`) debe corresponder con lo que se ve.
2. **¿Usa el hardware de forma adecuada?** — Blitter/Copper/DMA para el trabajo pesado;
   el raster CPU punto a punto en el bucle por frame es un antipatrón (077/084: ~1,4 fps
   por líneas CPU; el camino correcto es `FramePlan` + `BlitJobKind::Line`, patrón 077/079).
3. **¿Tiene el mayor fps posible?** — medido vs el baseline de
   `BITACORA_SCROLL_TILES.md` (la mejor muestra registrada; −10 % = **empeorado**).
4. **¿Aguanta la sesión?** — el contador de frames no puede congelarse (una demo que
   **vuelve al SO/crashea tras READY** invalida su medida y su uso).

Veredicto: < 25 fps → *mejorar*; < 10 fps o sin camino claro → *eliminar/rehacer*;
sin medida por crash tras READY → *arreglar antes de juzgar*.

## Cómo se regenera (recurrente)

```bash
# medidas frescas de la flota (≈1 h) + clasificación:
node tools/debug/audit-demos-fps.mjs --all
node tools/debug/audit-demos-quality.mjs --all --reuse          # tabla en out/fleet-quality/
node tools/debug/audit-demos-quality.mjs <demoId>               # una demo (mide si hace falta)
node tools/debug/audit-demos-quality.mjs <demoId> --analyze --vision   # + captura + Ollama
# deriva de fps vs baseline registrado (mismo criterio, mejor de N):
node tools/debug/check-fps.mjs --warn-only
```

Cuándo: **al tocar una demo** (su fila + `check-fps --demo`), y en el **barrido de flota**
(regresión + auditoría). El `detail`/los motivos por demo quedan en `out/fleet-quality/quality.json`.

## Estado (auditoría 2026-10; 133 demos del repo)

### A eliminar / rehacer (fps < 10)

| demo | fps | campos | causa conocida |
|---|---|---|---|
| `ui_amiga_300_gui_compositor` | 0,35 | 143 | compositor GUI por CPU (por diagnosticar; el hermano 301 va a 49,97) |
| `214_app_sprite` | 0,70 | 71,5 | por diagnosticar (¿reconstrucción por frame del plan/capa?) |
| `077_math3d_cube` | 1,50 | 33,4 | **líneas CPU punto a punto** (sin `FramePlan`); pendiente ruta Blitter |
| `070_mixer_three_voices` | 1,85 | 27,1 | familia de mezcladores (por diagnosticar) |
| `084_mf_rotation` | 2,04 | 24,5 | líneas CPU (el cubo ya va por Blitter; falta borrado por Blitter/EOR y el coste/job) |
| `275_c2p_seam` | 2,10 | 23,9 | por diagnosticar |
| `204_collide_game` | 2,15 | 23,3 | por diagnosticar |
| `061_c2p_chunky_4bpl` | 2,94 | 17,0 | por diagnosticar |
| `067_mixer_melody` / `069_mixer_two_voices` | 3,09 / 3,44 | 16,2 / 14,5 | familia de mezcladores |
| `078_math3d_solid` | 3,64 | 13,7 | **líneas CPU** (misma familia que 077) |
| `083_fbm_noise` / `engine_amiga_061_indexed_display` | 4,54 / 4,55 | 11,0 | por diagnosticar |
| `068_mixer_ref` / `073_sample_mixer` / `075_mixer_tones` | 4,94 / 4,94 / 4,99 | 10,1 | familia de mezcladores |
| `079_wireframe` | 6,79 | 7,4 | líneas por Blitter, pero el coste por job domina (optimizar el camino) |
| `131_app_split_screen` / `129_split_screen_bands` | 8,19 / 8,49 | 6,1 / 5,9 | por diagnosticar (¿copias por CPU por frame?) |
| `118_dna3d` | 9,98 | 5,0 | por diagnosticar |

### A mejorar (10–25 fps)

| demo | fps | causa conocida |
|---|---|---|
| `dk_port` / `dk_port_xl` | 11,33 / 12,48 | ports del original (deuda conocida) |
| `112_xlimited_robocod` | 12,08 | familia scroll (deuda conocida: 12,07) |
| `057_audio_mixer` / `058_sfx_mixer` | 12,43 | mezcla por CPU con buffers grandes |
| `203_polygon_planes` | 12,47 | por diagnosticar |
| `211_blit_state_bench` | 12,53 | bench (por diagnosticar) |
| `080_fire_rgb` | 13,63 | por diagnosticar |
| `208_risky_woods` | 15,23 | por diagnosticar |
| `111_xlimited_sidescroller` / `056_input_aggregator` / `117_bobs3d` | 16,6 | por diagnosticar |
| `ui_amiga_215_gui_widgets` | 16,62 | chrome GUI por CPU (G8 pendiente: copy del compositor por Blitter) |
| `116_flatshade_convex` | 19,37 | por diagnosticar |
| `cards_amiga_124_cards_bench` | 22,46 | bench (por diagnosticar) |
| `engine_amiga_065_mechanisms_bridge` | 23,81 | deuda conocida (interleaved/hojas) |
| `205_hw_probe` | 24,16 | por diagnosticar |
| `055_copper_rainbow` / `212_blitter_feeder` | 24,9 | 2 campos = en el suelo (24,96 nominal); vigilar que no bajen |

### Sin medida (no arranca / crash tras READY / falta runner)

- **Crash tras READY (contador congelado; verificado en 221: pc en la ROM, con el engine
  anterior a los fixes de sprites — preexistente)**: `221_sprite_palette_split`,
  `130_app_scroll_ladder`, `132_runtime_geometry`, `204_app_strip_scroll`,
  `214_floppy_raw`, `071_mixer_four_voices`. DT-017.
- **Init/arranque (familia audio conocida)**: `074_mixer_drums`, `076_mixer_sample_channels`,
  `276_music_mixer`.
- **Medida pendiente** (re-medir tras el fix del auto-mark de la fachada `App`,
  `engine/api/game.hpp`): `011_typed_memory`, `209_reactive_loop`, `212_message_loop`,
  `215_gui_widgets`, `300_gui_compositor`, `board_chess_amiga_123_chess_match`.
- Nota: `out/demos` conserva builds de demos retiradas (`RETIRADA` en la auditoría); no
  entran en el listado.

### Excepciones documentadas

Ninguna hoy: una excepción por coste intrínseco exige motivo + medida en el `VALIDATION.md`
de la demo y **aun así respeta el suelo de 25 fps**. Los «campos ≈ 2» (24,94–24,96 fps) se
consideran en el suelo nominal (medio frame por VBlank).

## Relación con el resto del flujo

- Regresión por demo: `tools/test-regression.sh --demo <ruta>` (build→run→analyze) y
  `--vision-review` para la descripción a Ollama.
- Barrido de flota: `tools/test-regression.sh` (gates) + la auditoría de este documento.
- La deuda técnica asociada se registra en `ROADMAP_DEUDA_TECNICA.md` (DT-016, DT-017).
