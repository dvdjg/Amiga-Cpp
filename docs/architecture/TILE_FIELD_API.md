# API de campos de tiles (TileField) y compositor DPF

**Estado:** implementado en `engine/include/eng/field/` y demostrado en la demo
`demos/106_tile_field_showcase`.

## 1. Objetivo

Evolucionar la demo 102 hacia una API reutilizable y flexible de **campos
(fields)** de tiles con **scroll infinito**, donde cada playfield se controla de
forma independiente y el modo DPF es solo una configuración concreta.

Requisitos:

1. **Scroll infinito** en un eje si el framebuffer es el doble del viewport en
   ese eje; si es más pequeño, el scroll llega hasta ese tamaño (tope).
2. **Dos funciones** por campo:
   - `begin(offset_inicial)`: encola el estampado inicial del framebuffer;
   - `update(delta)`: se llama cada frame con el **desplazamiento respecto al
     frame anterior** (máximo configurable por eje, a priori).
   - Ambas saben dibujar en el framebuffer (fuera del viewport) los tiles que
     hagan falta.
3. **Repartir el dibujo**: si el salto máximo es pequeño, el algoritmo puede
   repartir el dibujo de una columna/fila en varios frames.
4. Cada playfield tiene su **controlador independiente**; el compositor DPF solo
   une los registros compartidos.
5. La API sirve también sin DPF (un solo campo, p. ej. 5 bitplanes / 32 colores).
6. Índices de tile **u16** en el mapa (tilesets de miles de patrones).

## 2. Modelo mental: capas y compositor

```
        TileFieldController      BitmapFieldController
       (mapa + framebuffer)      (lienzo: naves, HUD, BOBs)
                |                          |
                +------------+-------------+
                             |
                     FieldHardwareView    <- punteros BPLxPT, scroll, módulos
                             |
                     DpfDisplayComposer    <- ÚNICA capa que toca el hardware DPF
                             |
                       registros custom
```

- El **controlador de tiles** no conoce al de bitmap ni al compositor.
- El **compositor** solo combina: punteros `BPLxPT`, nibbles de `BPLCON1`,
  módulos, prioridad `BPLCON2` y la configuración DPF (`BPLCON0` bit 10).
- Para un solo campo (no DPF) el compositor es trivial o no existe.

## 3. Archivos de la implementación

- `engine/include/eng/field/tile_field.hpp`: tipos + `TileFieldController`.
- `engine/include/eng/field/dpf_composer.hpp`: `DpfDisplayComposer`.
- `engine/include/eng/field/tile_demo.hpp`: utilidades COMUNES de las demos
  (paleta, glifos, seno Q16, cámaras, `build_tile_cache` con soporte de tiles
  anchos). Evita duplicar la generación de assets entre demos.
- `demos/106_tile_field_showcase/`: demo ÚNICA parametrizable (dual 3+3 o
  single 5 planos, tiles de 16/32/48px). Sustituye a las antiguas 106 y 107.
- `demos/102_tile_scroll_dualpf/`: ejemplo de migración a la API con parallax
  por fases.
- `tools/analyze/verify-tile-field-fill.mjs`: test unitario del ALGORITMO de
  rellenado (`begin` con offset absoluto y `update` con delta relativo).

### Unificación de demos

Para no tener que adaptar cada demo con cada feature, hay UNA demo de campos de
tiles parametrizable por macros de compilación (`-D`):

- `K_TILE_WIDTH` = 16/32/48: anchura de tile (múltiplo de 16), el tile ancho se
  copia en UNA pasada del Blitter.
- `K_DUAL` = 1/0: dual playfield 3+3 (transparencia PF1) o single playfield
  5 planos (32 colores, sin DPF).

Ambos parámetros usan la MISMA abstracción de playfield (`TileFieldController`):
un controlador por playfield, sea dual o single. El `DpfDisplayComposer` une los
registros compartidos solo en dual; en single el compositor es trivial (un
campo). Así un videojuego con un solo PF de 5 bitplanes usa exactamente el mismo
código que uno con dual playfield.

Compilación (el build acepta `EXTRA_DEFINES`):

```bash
EXTRA_DEFINES="-DK_TILE_WIDTH=32" bash ./tools/build/build-demo.sh demos/106_tile_field_showcase --debug --clean
EXTRA_DEFINES="-DK_DUAL=0"        bash ./tools/build/build-demo.sh demos/106_tile_field_showcase --debug --clean
```

## 3.1 Test unitario del algoritmo de rellenado

`tools/analyze/verify-tile-field-fill.mjs` replica la lógica EXACTA de
`begin` (offset absoluto) y `update` (delta relativo) y verifica invariantes
sobre el framebuffer de doble página:

- `begin(offset)` en distintas zonas (0,0 / 320,0 / 480,0 / 0,256 / 640,512 /
  no alineado 160,128): el conjunto de franjas cubre TODAS las celdas del
  framebuffer exactamente una vez, y cada celda física contiene el tile correcto
  del mapa virtual (world = page_origin + fb_offset).
- `update(+N)` sin cruzar página: no encola franjas nuevas (la página opuesta ya
  está preparada).
- `update(+1..5px)` repetido: no encola hasta cruzar un tile; entonces encola
  solo la columna o fila futura necesaria.
- Cruce de página: cambia el cuadrante activo; no redibuja páginas completas.
- Inversión (cambio de signo del delta): prepara el cuadrante opuesto por
  columnas/filas incrementales, sin escribir el cuadrante visible.

Uso: `node tools/analyze/verify-tile-field-fill.mjs`. El
`analyze-sequence.sh` de la showcase lo ejecuta antes de la captura.

## 4. El framebuffer de doble página y el scroll infinito

- Framebuffer de **2 x viewport** en el eje infinito (640x256 para un viewport
  320x256 con doble horizontal; 320x512 para doble vertical; 640x512 ambos).
- Se divide en **páginas** del tamaño del viewport: A=[0,320), B=[320,640).
- La cámara scrollea dentro del framebuffer hasta el límite
  (`max_scroll = framebuffer - viewport`). Cuando cruza el **límite de página**
  (x == 320), la cámara "da la vuelta" a la otra página y la página que queda
  vacante se redibuja con los tiles del siguiente tramo de mundo.

### Geometría del display (fórmula canónica ACE/HRM, probada en 101-104)

Con `DDFSTRT=$30` se fetchean **42 bytes** por fila (40 visibles + 2 de margen).
El puntero de cada playfield apunta a su coarse `(scroll_x - 1) & ~15` y programa
su fine `(16 - fine) & 15`, de modo que `display_start == scroll_x` es continuo
en todo el rango (sin salto en el cruce de tile fine 15 -> 0).

En la doble página, `FieldHardwareView` expone:

- `display_byte_offset = página_activa*40 + fetch_px/8 + y*row_bytes`, donde
  `fetch_px = (scroll-1)&~15` (clamp a 0 en scroll=0) y `row_bytes = 640/8 = 80`;
- `bpl1mod = row_bytes - 42` (80 - 42 = 38): cada fila del display avanza una
  fila de mundo completa aunque solo lea 40 bytes;
- `fine_x` = nibble de `BPLCON1`.

### El presupuesto y el delta están acoplados

Redibujar una página = `viewport_tiles` (p. ej. 20x16 = 320 tiles). Tiempo
disponible = `viewport_px / max_delta` frames. El presupuesto por frame debe ser
`>= pagina_tiles / frames_disponibles`.

**Lección del bug de "saltos de color" en la 106:** al cruzar en diagonal se
encolan a la vez la página X vacante y la Y vacante. Con scroll en ambos ejes
(framebuffer 640x512) cada página vacante son **640 tiles** (20x32 o 40x16), y el
peor caso es **1280 tiles pendientes** con ~64 frames disponibles (a ~5px/frame)
=> ~20 tiles/frame. Un presupuesto de 4-16 tiles/frame dejaba la página activa a
medio dibujar al cruzar. La 106 usa `max_tiles_per_frame = 24`.

## 5. Estructuras finales

```cpp
// Mapa de tiles del mundo: índices u16, ancho/alto, y política de wrap.
// cells es un Span (vista contigua segura, sin aritmética de punteros).
// wrap_x/wrap_y = 0 significa fin del mundo; >0 hace la coordenada periódica.
struct TileLayerMap {
    Span<const u16> cells;  // at() con trap ante violación de rango
    u16 width, height;
    u16 wrap_x = 0, wrap_y = 0;
    u16 edge_tile = 0;
    u16 tile_at(s32 tx, s32 ty) const;
};

// Configuración estática de un campo de tiles.
struct TileFieldConfig {
    TileLayerMap map;
    const u16* tileset;      // patrones en Chip RAM (índice u16)
    u16 tileset_count;
    u8 tileset_planes;       // 5 => 32 colores (single playfield)
    u16 tile_width = 16;     // MÚLTIPLO DE 16: 32/48/64px en UNA pasada del Blitter
    u16 tile_size = 16;
    u16 viewport_w = 320, viewport_h = 256;
    s16 max_delta_x = 5, max_delta_y = 5;    // tope del salto por frame
    u8 max_tiles_per_frame = 4;              // presupuesto de dibujo
    bool scroll_x = true, scroll_y = true;
};

// Estado mutable de un campo.
struct TileFieldState {
    s32 world_x, world_y;        // posición de mundo (píxeles)
    s32 page_base_x, page_base_y;// mundo en el borde del framebuffer
    u8 active_page_x, active_page_y;  // 0/1: página visible
    TilePendingStrip pending[4]; // franjas pendientes (regiones 2D + cursor)
    u8 pending_count;
    bool initialized;
};

// Vista hardware para el compositor (módulos y puntero ya calculados).
struct FieldHardwareView {
    const u8* bitplanes;
    u32 plane_stride;
    u32 display_byte_offset;
    u8 fine_x;
    u16 bpl1mod;
    u8 plane_count;
    u8 first_hardware_plane;  // en DPF: 0 (PF1) o 1 (PF2)
};

// Lienzo bitmap opcional (naves, HUD, BOBs).
struct BitmapFieldConfig {
    const u8* bitmap; u16 width, height; u8 planes; bool scrolling;
};
```

## 6. `TileFieldController`

- `begin(memory, config, offset_inicial)`: reserva un framebuffer de **3 × viewport**
  en cada eje con scroll (9 cuadrantes en XY), encola
  el estampado de todas las páginas como franjas y deja el estado listo. **El
  dibujo NO ocurre aquí**: la demo bombea `pump(plan, budget)` en un bucle en
  init hasta que `busy()` es false (no hay límite de tiempo real durante el
  arranque). El dibujo es SIEMPRE por Blitter (`TileBlockCopy` vía `FramePlan`),
  nunca por CPU.
- `pump(plan, budget)`: consume hasta `budget` tiles de las franjas pendientes
  (estampado inicial o scroll) por Blitter; devuelve si queda algo.
- `busy()`: ¿queda trabajo de dibujo pendiente?
- `update(config, delta, plan)`: exige `max_delta_x/y <= 5`, clampa el delta (y a
  0 si el eje no scrollea), y reconstruye progresivamente el tramo que queda
  suficientemente detrás. Las dos páginas activables tienen una tercera guardia
  física contigua; si la página destino o su guardia no son válidas, no se activa.
  En XY las esquinas se particionan en rectángulos disjuntos y nunca se toca la
  ventana visible.
- `hardware_view(first_hardware_plane)`: calcula `display_byte_offset` y
  `bpl1mod` con la fórmula canónica.
- Franjas como regiones 2D: `{world_tile_x, world_tile_y, fb_tile_x, fb_tile_y,
  width, height, cursor}`. `cursor` recorre la región en fila-mayor; el arranque
  usa cuadrantes completos y el scroll usa rectángulos de tramo. En diagonal la
  unión se parte alrededor de la esquina para no duplicarla.
- `TileBlockCopy` fusiona runs horizontales (`words_per_row = run * tile_width/16`)
  y verticales (`height = run * tile_size`) únicamente con `tileset_row_major` o
  `tileset_plane_major`; el layout histórico conserva un job por tile. Nunca hay
  copias de píxeles por CPU.

### Backend y arranques reales

`MinimalBackend::execute_frame_plan()` ejecuta el plan por plano: el hardware OCS
no ofrece una operación multi-plano para este contrato. Por tanto un job con
`bitplane_count = N` produce N escrituras de `BLTSIZE`, y
`MinimalBackend::blitter_starts()` expone ese número por ejecución del plan. No se
debe confundir `FramePlan::blit_job_count()` con arranques físicos del Blitter.

## 7. `DpfDisplayComposer`

- `init(memory, config)`: reserva los dos bloques del doble buffer de copper.
- `compose(pf1, pf2)`: el primer frame emite la lista completa en ambos bloques
  (DMACON, BPLCON0/1/2, BPL1MOD/BPL2MOD, DIW/DDF, 6 punteros, paleta, wait,
  fin); los siguientes **parchean** solo BPLCON1 + los punteros (13 words), que
  es lo que cambia por frame. Layout fijo documentado en el archivo.
- `install(backend)`: activa el bloque inactivo.
- Intercala los planos por playfield: PF1 = hardware 0,2,4; PF2 = 1,3,5. En
  single playfield los planos son consecutivos (stride 1).

## 8. Demo showcase (`demos/106_tile_field_showcase`)

- Dual playfield 3+3 (por defecto): PF1 primer plano (3 planos, color 0
  transparente, ~50% de tiles totalmente transparentes) y PF2 fondo (3 planos).
  Índices de tile u16, mapa 256x128 con wrap en ambos ejes (mundo infinito
  periódico).
- **Fondo**: scroll infinito diagonal constante (2px/frame X, 1px/frame Y).
- **Primer plano**: onda de Lissajous de 2 pantallas (0..640 X, 0..512 Y) con
  seno Q16 y acumulador de resto (movimiento sub-pixel suave).
- **Tiles anchos**: `K_TILE_WIDTH=32/48` copia el tile en UNA pasada del
  Blitter (words_por_fila = tile_width/16).
- **Single 5 planos**: `K_DUAL=0` usa un solo `TileFieldController` (32
  colores, sin DPF) con la misma API.
- `max_tiles_per_frame = 56` (2 campos = 112 jobs, dentro del
  `max_blit_jobs=128` del FramePlan) para que la cola del cruce diagonal se
  vacíe entre cruces.
- `analyze-sequence.sh`: test unitario + secuencia animada + sin negro interno
  + telemetría.

## 9. Evaluación de la propuesta original (resumen)

| Aspecto | Propuesta | Refinado |
|---|---|---|
| Config/State | Correcto | Igual |
| begin/update | Correcto | `begin` encola, la demo bombea `pump` |
| delta | `offset` (delta) | Clamp a `max_delta_x/y` y a `scroll_x/y` |
| budget | `TileScrollBudget` | `max_delta` + `max_tiles_per_frame` |
| scrolling | bool | `scroll_x`/`scroll_y` |
| State | `previous_x/y` | `world_x/y` + franjas pendientes + páginas |
| indices | (u8) | **u16** + `Span` |
| dibujo | CPU | **Blitter** (`TileBlockCopy` vía `FramePlan`) |
