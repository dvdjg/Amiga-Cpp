# Subsistemas avanzados del engine

Documentación de las APIs opcionales agrupadas en [engine_extensions.h](../engine/include/engine_extensions.h). Complementan [engine.h](../engine/include/engine.h) y [engine_suite.h](../engine/include/engine_suite.h) sin sustituir al núcleo mínimo.

## Parent links

- [Arquitectura](engine-architecture.md)
- [Fases de evolución](engine-feature-phases.md)
- [Matriz batería ↔ engine](engine-test-battery-matrix.md)

## Copper avanzado (`engine_copper_list.h`)

- **Doble buffer**: `EngineCopperDouble` con dos bloques CHIP; `engine_copper_double_flip_back` alterna el buffer de escritura.
- **WAIT**: `engine_copper_wait_first_word` + segunda palabra `0xFFFE` (mismo criterio que casos C01/B01).
- **Cierre de lista**: `engine_copper_list_finish` añade el WAIT final.
- **Cola vs reinicio**:
  - `engine_copper_double_queue_front` / `engine_copper_double_install_front` solo actualizan `COP1LC` para el siguiente frame.
  - `engine_start_copper` queda separado y debe usarse solo cuando realmente toca arrancar o reiniciar el copper.
  - mezclar ambas cosas en mitad del frame produce corrupciones dependientes de fase, como se vio al depurar `DX39P5`.
- **Ideas de ampliación**: colas de “bloques” por scanline (paleta, modulos, pointers) con reciclado de memoria; COP2 para lista secundaria sincronizada con BLIT.

## Sprites hardware (`engine_sprite.h`)

- OCS: hasta **8** sprites; punteros y POS/CTL por índice.
- `engine_sprite_calc_posctl`: lores, **x par**, rango vertical inclusivo; no cubre todos los modos AGA.
- `engine_sprite_fill_column_2c`: datos 2 colores (DATA/DATB por línea).
- **Adjuntos / 15 colores**: no implementados aquí; construir a mano según HRM si hace falta.

## Joystick (`engine_joy.h`)

## Sprites CPU / retained (propuesta)

Todavia no existe un modulo oficial para sprites CPU, pero la direccion recomendada es:

- primitivas low-level parametricas por bitplanes, mascara y clipping;
- capa retained para escena, anchors mundo/pantalla, dirty rects y persistencia por frame.

Propuesta concreta: [engine-cpu-sprites-api-proposal.md](engine-cpu-sprites-api-proposal.md).

Primer paso ya implementado:

- `engine_cpu_sprite.h` expone `engine_cpu_sprite_blit_1bpl`.
- `CS01_cpu_sprite_1bpl` valida en vivo la primitive minima con clipping en 1 bitplane.
- `engine_cpu_sprite.h` expone tambien `engine_cpu_sprite_blit_4bpl_masked`.
- `CS02_cpu_sprite_4bpl_masked` valida en vivo dibujo masked 4bpl sobre fondo estable.
- `engine_cpu_sprite.h` expone una retained minima con `EngineSceneCpuSprite`, `EngineSceneLayer`, `engine_scene_begin_frame`, `engine_scene_cpu_sprite_submit` y `engine_scene_present`.
- `CS03_scene_sprite_scroll` valida en vivo world-space con scroll horizontal, posicion derivada por frame y `dirty_rect` minimo.
- `CS04_scene_overlay_fixed` valida en vivo `anchor = SCREEN`, manteniendo un overlay fijo mientras el mundo hace scroll por debajo.
- `engine_cpu_sprite.h` expone tambien `engine_cpu_sprite_blit_interleaved_masked` para surfaces interleaved arbitrarias, reutilizando la primitive masked del subsistema `engine_sprite_*`.
- `CS06_scene_overlay_fixed_32c` valida en vivo un overlay fixed 5bpl/32 colores con mascara y secuencia animada de scroll ciclico.
- Decision actual del subsistema: `mask` y `undraw` siguen desacoplados. No todos los casos que necesitan mascara necesitan tambien restauracion de fondo.

- `engine_joy_dat_raw` lee JOY0DAT/JOY1DAT.
- `engine_joy_fire_button` usa CIAA (`0xBFE001`) botones típicos de puerto.
- `engine_joy_digital_mask` aplica heurística OR-tap + fire; el hardware real puede exigir calibración por juego.

## View / viewport (`engine_view.h`)

- Metadatos de scroll y ventana; **no** es la `View` de graphics.library.
- `engine_view_finescroll_hcon1`: componente fino para BPLCON1.
- `engine_viewport_calc_mods`: caso lineal playfield más ancho que ventana (interleave simple).

## Display / playfield (`engine.h`)

- `engine_copper_setup_display_mode_ocs` encapsula el contrato base `DIW/DDF/BPLCONx/BPLxMOD` para modos OCS/ECS ya documentados.
- `engine_copper_mode_ocs` separa el "modo de vídeo" del resto del cableado cuando un caso necesita controlar ventana/fetch por otra vía.
- `engine_copper_set_planar_bitmap_view` conecta un bitmap planar a una secuencia contigua de `BPLxPT`, incluyendo offset coarse `x/y`.
- `engine_copper_set_dual_playfield_bitmap_views` conecta dos bitmaps planares a slots alternos de dual playfield (`PF1` en planos 1/3/5, `PF2` en 2/4/6).
- `engine_alloc_chip_copy` promociona a memoria CHIP un bloque que vaya a ser consumido por DMA, evitando depender de la residencia del ejecutable DOS.
- `DX39P4_layers_scroll_no_raster` ya valida el siguiente escalón reusable: scroll por frame con `BPLxPT + BPLCON1` y doble buffer de copperlist, aún sin raster dinámico.
- `DX39P5_layers_wrap_mod` valida ya el wrap vertical por cambios temporales de `BPL1MOD/BPL2MOD`, separado de los gradientes por raster.
- `DX39P6_layers_raster_gradients` añade el otro bloque reusable del efecto: bandas de color por copper (`COLOR01..06` y `COLOR09..13`) sobre la misma base de scroll y wrap ya validada.
- el aprendizaje reusable de `DX39` no se limita al efecto:
  ver [amiga-a500-dma-copper-state-rules.md](amiga-a500-dma-copper-state-rules.md) para reglas generales sobre residencia CHIP, ciclo de copper, dual playfield y scroll de playfield en A500.
- Estado actual recomendado:
  - esta familia nace de `DX39 layers` y entra como `ENGINE_PARCIAL`;
  - el contrato base de registros ya converge con el original;
  - el aprendizaje adicional es que los assets DMA importados deben garantizarse en CHIP antes de programar `BPLxPT`.
  - el pipeline de evidencia debe vigilar también la residencia de `BPLxPT` en CHIP para no reabrir el mismo fallo.
  - scroll y wrap por modulo ya tienen fases separadas y verificables; quedan gradientes por banda y composicion final antes de declararlo `STABLE_REUSABLE`.
  - falta cerrar la composición visual final antes de darla por `STABLE_REUSABLE`.

## Tilemap (`engine_tilemap.h`)

- Direccionamiento: tabla de índices **UWORD** por celda (`engine_tilemap_cell_offset`).
- `engine_tilemap_atlas_tile_ptr`: atlas lineal de tiles.
- Render y culling quedan en la app o en blitter; este módulo evita duplicar aritmética de punteros.

## Custom chip (`engine_custom_chip.h`)

- Peek/poke por **offset de byte** desde `$DFF000` (p. ej. `ENGINE_CUSTOM_COLOR(0)`).
- Alternativa ordenada a esparcir accesos `custom->` por el código de aplicación.

## Paleta y color cycling (`engine_palette_cycle.h`)

- `engine_palette_set_range` escribe una ventana de `COLORxx` por CPU.
- `engine_palette_cycle_step` encapsula la técnica de color cycling con acumulador `rate/step`, celdas y tabla de colores.
- Estado actual recomendado:
  - `engine_palette_cycle_*` es `PROVISIONAL_REUSABLE`.
  - `DX03_color_cycle_logo` es su primer consumidor real.
  - si entran más efectos de paleta o copper relacionados, esta API puede refactorizarse hacia variantes más genéricas o mejor separadas.
- `engine_copper_set_contiguous_planes` entra como helper estable de bajo nivel para bitplanes planares contiguos no interleaved.

Política general:

- ver [engine-api-maturity-and-refactor-policy.md](engine-api-maturity-and-refactor-policy.md).

## Fuente raster (`engine_font.h`)

- Dígitos y espacio, trazado **CPU** en un plano (8×8).
- Para texto completo o proportional, enlazar otro atlas o IFF FONT.

## AmigaDOS (`engine_dos_io.h`)

- `engine_dos_file_slurp`: tamaño vía `Lock`+`Examine`, lectura con `Read` (KS 1.3 compatible).
- `engine_dos_dir_scan_first_file`: primer fichero del directorio (no patrón glob complejo).
- Si `engine_has_dos()` es 0, no llamar (boot sin DOS).

## Audio (`engine_audio.h`)

- **Backends posibles** (elección del proyecto, no impuesta por el núcleo):
  - Replayer **tracker** enlazado como módulo aparte.
  - **Paula directa**: AUDx, listas de sample, sincronía con VBL/CIA.
  - **AHI** cuando el target lleva sistema y drivers adecuados.
- El núcleo expone `engine_audio_paula_mute` y registro de `EngineAudioBackendKind` para coordinar con el replayer elegido.

## Convenciones

- Todo lo que toca CHIP debe usar `engine_alloc(..., MEMF_CHIP)` o memoria equivalente.
- Tras `TakeSystem()`, silenciar Paula si un caso de prueba no usa audio evita ruido en capturas (`engine_audio_paula_mute`).
