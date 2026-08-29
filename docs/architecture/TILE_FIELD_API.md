# API de campos de tiles (TileField) y compositor DPF

**Estado:** diseño aprobado para implementar. Base: demos 102/104/105 y el driver
`TileScrollScene` de `engine/.../tile_scroll.hpp`.

## 1. Objetivo

Evolucionar la demo 102 hacia una API reutilizable y flexible de **campos
(fields)** de tiles con **scroll infinito**, donde cada playfield se controla de
forma independiente y el modo DPF es solo una configuracion concreta.

Requisitos:

1. **Scroll infinito** en un eje si el framebuffer es el doble del viewport en
   ese eje; si es mas pequeno, el scroll llega hasta ese tamaño (tope).
2. **Dos funciones** por campo:
   - `begin(offset_inicial)`: dibuja/configura el framebuffer desde un offset
     (x,y) de la esquina superior izquierda.
   - `update(delta)`: se llama cada frame con el **desplazamiento respecto al
     frame anterior** (maximo configurable por eje, p. ej. 15px, y a priori).
   - Ambas saben dibujar en el framebuffer (fuera del viewport) los tiles que
     hagan falta.
3. **Repartir el dibujo**: si el salto maximo es pequeno (p. ej. 2px), el
   algoritmo puede repartir el dibujo de una columna/fila en varios frames,
   sabiendo que tiene tiempo antes de que esa zona entre al viewport.
4. Cada playfield tiene su **controlador independiente**; el compositor DPF solo
   une los registros compartidos.
5. La API sirve tambien sin DPF (un solo campo, p. ej. 5 bitplanes / 32 colores).
6. Indices de tile **u16** en el mapa (tilesets de miles de patrones).

## 2. Modelo mental: capas y compositor

```
        TileFieldController      BitmapFieldController
       (mapa + framebuffer)      (lienzo: naves, HUD, BOBs)
                |                          |
                +------------+-------------+
                             |
                     FieldHardwareView    <- punteros BPLxPT, scroll, planos
                             |
                     DpfDisplayComposer    <- UNICA capa que toca el hardware DPF
                             |
                       registros custom
```

- El **controlador de tiles** no conoce al de bitmap ni al compositor.
- El **compositor** solo combina: punteros `BPLxPT`, nibbles de `BPLCON1`,
  modulos, prioridad `BPLCON2` y la configuracion DPF (`BPLCON0` bit 10).
- Para un solo campo (no DPF) el compositor es trivial o no existe: el
  controlador escribe sus propios registros.

## 3. El framebuffer de doble pagina y el scroll infinito

La clave del scroll infinito con framebuffer pequeno:

- Framebuffer de **2 x viewport** en el eje infinito (640x288 para un viewport
  320x256 con doble horizontal; 320x544 para doble vertical; 640x544 ambos).
- Se divide en **paginas** del tamaño del viewport: A=[0,320), B=[320,640).
- La camara scrollea dentro del framebuffer hasta el limite
  (`max_scroll = framebuffer - viewport`). Cuando cruza el **limite de pagina**
  (x == 320), la camara "da la vuelta" a la otra pagina y la pagina que queda
  vacante se redibuja con los tiles del siguiente tramo de mundo.

Ejemplo horizontal infinito (delta maximo 2px):

- `begin((0,0))`: estampa pagina A (tiles visibles) y pagina B (la que se
  revelara) con los tiles del mundo.
- `update(+2)`: avanza la camara; mientras x < 320, la pagina B (offscreen) se
  va rellenando con los tiles que la camara revelara (repartidos segun presupuesto).
- x llega a 320: se muestra la pagina B; la camara "salta" a x=0 (el pointer
  vuelve al inicio) y la pagina A se marca para redibujar con el siguiente
  tramo. El salto de 320px es invisible porque la pagina B ya tenia el contenido
  correcto.

**Por que el presupuesto y el delta estan acoplados:** redibujar una pagina =
`viewport_tiles` (p. ej. 20x16 = 320 tiles). Tiempo disponible = `viewport_px /
max_delta` frames (320/2 = 160 frames con delta 2; 320/15 ≈ 21 con delta 15).
El presupuesto por frame debe ser `>= pagina_tiles / frames_disponibles`. Por
eso `max_delta` configurable permite repartir el dibujo.

## 4. Estructuras (evaluacion de la propuesta)

La propuesta original:

```cpp
struct TileFieldConfig { TileLayerMap map; TileSet tileset; TileScrollBudget budget; bool scrolling; };
struct TileFieldState  { s32 previous_x; s32 previous_y; TileWindow window; PageState pages; bool initialized; };
bool tile_field_begin(State&, const Config&, TileScrollOffset initial);
bool tile_field_update(State&, const Config&, TileScrollOffset offset, FramePlan&);
```

**Lo que esta bien:**
- Separar `Config` (estatica) de `State` (mutable): correcto para no-heap.
- `begin` (offset absoluto) + `update` (delta): encaja con el scroll hw
  (fine scroll + cruce de tile/pagina).
- `update` recibe `FramePlan` (los blits van a traves del planificador).

**Mejoras:**
1. **`scrolling` -> `scroll_x`/`scroll_y`**: un campo puede scrollar solo en X.
2. **`budget` -> delta + presupuesto explicitos**: `max_delta_x/y` (el limite
   del salto por frame, que determina cuantos frames hay para redibujar) y
   `max_tiles_per_frame`. Son dos palancas distintas y hay que fijarlas a priori.
3. **`State` necesita la posicion de mundo absoluta** (no solo `previous_x/y`)
   y las **franjas pendientes** (para repartir el dibujo entre frames).
4. **`begin` debe recibir tambien `FramePlan&`** para poder encolar los blits
   iniciales (o estampar por CPU en init, que es lo que hacen las demos).
5. **`PageState pages`**: explicitarlo: que pagina esta visible, que region de
   mundo tiene cada pagina y si esta "sucia" (pendiente de redibujar).

### 4.1 Estructuras refinadas

```cpp
// Mapa de tiles del mundo: indices u16, ancho/alto, y politica de wrap.
// wrap_x/wrap_y = 0 significa fin del mundo (no se puede scrollar mas alla);
// >0 hace que la coordenada de mundo repita cada wrap tiles.
struct TileLayerMap {
    const u16* cells;   // indices de tile (u16)
    u16 width;          // tiles
    u16 height;         // tiles
    u16 wrap_x = 0;     // 0 = borde, >0 = mundo periodico cada N tiles
    u16 wrap_y = 0;
    u16 tile_at(s32 tx, s32 ty) const;
};

// Configuracion estatica de un campo de tiles.
struct TileFieldConfig {
    TileLayerMap map;
    const u16* tileset;      // patrones en Chip RAM (ya generados)
    u16 tileset_count;
    u8 tileset_planes;
    // Contrato de rendimiento (a priori, por eje):
    s16 max_delta_x = 15;    // tope del salto por frame (X)
    s16 max_delta_y = 15;
    u8 max_tiles_per_frame = 4;  // presupuesto de dibujo por frame
    bool scroll_x = true;
    bool scroll_y = true;
};

// Estado mutable de un campo (lo mantiene el controlador).
struct TileFieldState {
    s32 world_x = 0;        // posicion de mundo (pixeles), origen = TL
    s32 world_y = 0;
    TileWindow window;      // ventana visible en tiles de mundo
    TilePageState pages;    // paginas del framebuffer (doble) y su contenido
    TilePendingStrip pending[4]; // franjas pendientes de dibujar (repartidas)
    bool initialized = false;
};

struct TileScrollOffset { s16 x = 0; s16 y = 0; };

bool tile_field_begin(TileFieldState&, const TileFieldConfig&, TileScrollOffset initial, FramePlan&);
bool tile_field_update(TileFieldState&, const TileFieldConfig&, TileScrollOffset delta, FramePlan&);
```

### 4.2 El campo bitmap (opcional)

```cpp
// Lienzo para naves, HUD o BOBs: no conoce tiles.
struct BitmapFieldConfig {
    const u8* bitmap;   // framebuffer en Chip RAM
    u16 width, height;
    u8 planes;
    bool scrolling = false;
};
```

### 4.3 Vista hardware y compositor

```cpp
// Lo unico que el compositor necesita de cada campo.
struct FieldHardwareView {
    const u8* bitplanes;
    u32 plane_stride;
    s16 scroll_x;       // posicion de scroll para BPLxPT + BPLCON1
    s16 scroll_y;
    u8 plane_count;
    u8 first_hardware_plane; // en DPF: 0 (PF1) o 1 (PF2)
};

struct DpfDisplayComposer {
    // Emite BPLxPT, BPLCON1, BPL1MOD/BPL2MOD, BPLCON2 y BPLCON0(DPF) a la
    // copperlist. Solo une; no decide como scrollea cada campo.
    bool compose(const FieldHardwareView& pf1, const FieldHardwareView& pf2, copper::Scheduler& out) const;
};
```

## 5. Contratos

### `tile_field_begin`
- Dibuja el framebuffer completo (pagina visible + pagina(s) de margen) para el
  offset inicial.
- Puede estampar por CPU (init) o encolar blits en `FramePlan`.
- Deja el estado `initialized`.

### `tile_field_update`
1. **Clampa el delta** a `max_delta_x/y` (y a 0 si `scroll_x/y` es false; si no
   hay framebuffer suficiente en ese eje, tope al borde).
2. **Aplica el delta** a `world_x/y` y detecta cruces de tile y de **pagina**.
3. **Encola** las columnas/filas de tiles que la camara revelara (en la pagina
   offscreen), marcadas con su "cuenta atras" de frames hasta ser visibles.
4. **Consume el presupuesto**: dibuja hasta `max_tiles_per_frame` de las
   franjas pendientes; el resto queda para el siguiente frame (repaso).
5. Devuelve la vista hardware actualizada (punteros + fine scroll).

## 6. Indices de tile u16

El mapa pasa a `u16` por celda (2 bytes). El tileset puede tener miles de
patrones; el mapa sigue ocupando `width*height*2` bytes. El tile en Chip RAM se
indexa con `u16`.

## 7. Refactor de la 105 en tres capas

1. **`TileFieldController`**: logica de una capa con tilemap (mapa u16,
   framebuffer, paginas, presupuesto). Vive en el engine
   (`engine/include/eng/field/tile_field_controller.hpp`).
2. **`BitmapFieldController`**: capa bitmap opcional (naves, HUD, BOBs) sin
   tiles.
3. **`DpfDisplayComposer`**: union final especifica del hardware DPF (registros
   compartidos).

La demo 105 puede usar dos `TileFieldController`, pero eso es una configuracion
concreta, no una imposicion de la API.

## 8. Evaluacion de la propuesta original (resumen)

| Aspecto | Propuesta | Refinado |
|---|---|---|
| Config/State | Correcto | Igual |
| begin/update | Correcto | `begin` recibe `FramePlan` |
| delta | `offset` (delta) | Clamp a `max_delta_x/y` y a `scroll_x/y` |
| budget | `TileScrollBudget` | `max_delta` + `max_tiles_per_frame` |
| scrolling | bool | `scroll_x`/`scroll_y` |
| State | `previous_x/y` | `world_x/y` + franjas pendientes + paginas |
| indices | (u8) | **u16** |

## 9. Plan de implementacion

1. `engine/include/eng/field/tile_field.hpp`: tipos (map u16, config, state,
   offset, hardware view, compositor).
2. `TileFieldController`: framebuffer de doble pagina, `begin`/`update`, reparto
   de franjas, wrap de pagina.
3. `BitmapFieldController` y `DpfDisplayComposer`.
4. Refactor de la 105 a las tres capas (con dos `TileFieldController`).
5. Migrar 102 a la nueva API (un `TileFieldController` por playfield).
6. Indices u16 en el mapa y en las caches de tiles.
