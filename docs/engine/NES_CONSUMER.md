# Consumidor NES → Amiga 500 (integración)

Guía para el **emulador de NES** que corre sobre este engine. El emulador es un **consumidor
externo**: define sus propias interfaces y las implementa sobre el engine. Regla:
**la app pide, el engine dispone**; el engine **no** se diseña para la NES. Ver
[ROADMAP_API_COHERENCE.md](architecture/ROADMAP_API_COHERENCE.md) §7.

## 1. Índice de la implementación de referencia

La implementación de referencia (externa a este repo) es el proyecto **`RetroReverse`**
(`repo/conversion/nes-to-amiga500/`):

- `runtime/host.hpp` — nivel **genérico** (`Host`: framebuffer de índices, audio, entrada, VBlank).
- `runtime/platform/amiga/README.md` — backend Amiga (mapeo del contrato `Host` a hardware).
- `runtime/platform/amiga/SERVICES.md` — catálogo de servicios deseados (tabla necesidad NES → servicio).
- `runtime/platform/amiga/services.hpp` — interfaces `I*` propuestas (`IChipMem`, `IBlitter`,
  `ISurface`, `IPatternCache`, `IScrollingLayer`, `IPalette`, `ICopper`, `ISpriteEngine`,
  `IAudio`, `IInput`, `IDisplay`, `AmigaServices`).

Referencia **engine-side** (lo que implementa el contrato):

- Puerta única: `engine/include/eng/api/api.hpp`.
- Adaptador de decisión: `tests/host/core/341_adapter` (HOST-341) — implementa `I*` solo con
  `api.hpp` + helpers.
- Helpers generales F7 (ver §5): `decode_2bpp_planar`, `tilemap::TileEditor`, `eng::Copper`,
  `eng::BlockPool`, `AudioMixer`.

## 2. Dos niveles (como propone `SERVICES.md`)

| Nivel | Interfaz externa | En el engine |
|---|---|---|
| **Genérico** (fallback) | `Host` (`present_indices`) | `c2p` + `Scene`/`present` + VBlank del mini-SO/`App` |
| **Aceleración (HLE)** | `AmigaServices` (`I*`) | `api.hpp` + helpers F7 |

El emulador pregunta `services.has(...)`; si falta, cae al `Host`. Ese patrón encaja sin cambios.

## 3. Mapa `I*` → engine

| `I*` | Con qué se implementa (engine) | Estado |
|---|---|---|
| `IChipMem` | `res::Budget` (cuota) + recursos como objetos; `eng::BlockPool` si hace falta `free` | ✅ (BlockPool) |
| `IBlitter` | `Device` (`blitter_*`/`execute_frame_plan`/`wait_blitter`) + `FramePlan` | ✅ |
| `ISurface` | `gfx::Bitmap` / `PlaneBytes` | ✅ |
| `IPatternCache` | **`decode_2bpp_planar`** + tile bank | ✅ (F7.1) |
| `IScrollingLayer` | `Layer` (tilemap) + `tilemap::TileEditor` + driver por `ScrollKind` | parcial (F7.3) |
| `IPalette` | `Palette` + `FramePlan` (parches) | ✅ |
| `ICopper` | **`eng::Copper`** + `Device::commit_copper` | ✅ (F7.4) |
| `ISpriteEngine` | `ActorStore`+`SpriteAllocator`+`compose_sprites` + BOB fallback | parcial (F7.7) |
| `IAudio` | `AudioMixer::play(SampleEvent{period,…})` + `paula::period_for_hz` | ✅ (existente) |
| `IInput` | `App::input()` / mini-SO | ✅ |
| `IDisplay` | `c2p` + `present` / VBlank mini-SO | ✅ |

## 4. Cómo implementar las `I*` (decisión)

**Las `I*` viven FUERA del engine.** El emulador define sus interfaces y un **adaptador fino**
sobre `eng/api/api.hpp` + helpers (demostrado por **HOST-341**). No meter `I*` en `engine/`:
acopla el engine a una app, multiplica la superficie pública y arrastra virtuals/ABI. Si se
quiere, se añade un **adaptador de referencia** fuera del core (`host-tools/`/`examples/`).

## 5. Estado de los helpers F7

✅ F7.1 `decode_2bpp_planar` · ✅ F7.2 `tilemap::TileEditor` · ✅ F7.4 `eng::Copper` · ✅ F7.5 Paula
(existente) · ✅ F7.6 `eng::BlockPool` · ⏳ **F7.3 drivers de scroll por `ScrollKind`** ·
⏳ **F7.7 `SpriteEngine` de alto nivel** (NES 8/línea + overflow).

## 6. Decisión: scroll del BG NES — **XYUnlimited vs XYLimited**

El BG de la NES es un **nametable que hace scroll libre en los dos ejes** (0..255 x, 0..239 y,
con wrap). El engine tiene dos familias (ver
[XYLIMITED_ALGORITMO_GENERICO.md](architecture/XYLIMITED_ALGORITMO_GENERICO.md) y
[PLAYFIELD_SCROLL_ARCHITECTURE.md](architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md)):

| | **XYUnlimited** (genérico) | **XYLimited** (acotado) |
|---|---|---|
| Modelo | ring completo + **split por línea** (Copper) | ventana **acotada** con bandas de guarda + `BPLxPT`/módulo |
| Chip RAM | **alta** (ring + márgenes por los dos ejes) | **estimable al reservar** (ventana + guardas) |
| Redibujado | **contenido por duplicado** (ring/wrap) | solo lo que entra por la guarda cada frame |
| Desplazamiento | **ilimitado** (cualquier velocidad/posición) | **máximo acotado** (hay que fijarlo al reservar) |
| Velocidad de scroll | libre | acota el **tamaño de guarda**: a más px/frame, banda mayor |
| Copper | split por línea (caro; **una por banda**) | `BPLxPT`/módulo (barato salvo split) |
| Coste CPU/Blitter | alto (duplicar/recomponer) | bajo (columnas/filas sucias) |

**Implicación.** `XYUnlimited` es el mapa “natural” de la NES (scroll libre 8-way) pero **consume
mucha Chip RAM y obliga a dibujar por duplicado**; `XYLimited` es barato y predecible pero
**limita el desplazamiento máximo y la velocidad** (bandas de guarda para esconder la
actualización).

### 6.1 Parámetros a acordar (estimación al reservar)

Para elegir/ajustar `XYLimited` (o dimensionar `XYUnlimited`) hacen falta **estos datos** del
emulador, que el planner usará con `region_cost` y `WorldRegion`:

1. **Rango de scroll** (`max_scroll_x`, `max_scroll_y`) por nivel/juego → tamaño de la **ventana**.
2. **Velocidad máxima** de scroll (px/frame) en cada eje → **ancho de las bandas de guarda**
   (a más velocidad, guarda mayor para que la columna/fila nueva se blitte antes de ser visible).
3. **Chip RAM disponible** para el BG (`res::Budget::remaining_chip`) → nº de buffers y guardas.
4. **Planos** del BG (NES 2bpp → ¿2/3 planos Amiga?) y si comparte DPF con la status bar.
5. **Frecuencia de actualización** del nametable (por scroll vs por cambio de tiles).

Formulación práctica (XYLimited): `ventana = visible + 2·guarda`; `guarda >= ceil(velocidad_max)`
+ margen de seguridad; `memoria = ventana_x · ventana_y · bytes_fila · planos`.

### 6.2 Decisión acordada: **adaptativo**

Se exponen **ambos** algoritmos vía `ScrollKind` (`CopperSplit` = xyunlimited, `CopperRing` =
xlimited) y el **planner elige/degrada** (`CopperSplit → CopperRing → Fine`) según el presupuesto
de Copper/Chip (`region_cost`). El emulador **pide** por capa y el engine **dispone**.

**Supuestos iniciales** (a afinar con el emulador): NES estándar **256×240**, scroll
`0..255`/`0..239`, velocidad típica **≤ 4 px/frame**. Con ellos se dimensionan ventana y guardas:

- `XYLimited`: `ventana = visible + 2·guarda`, `guarda = ceil(velocidad_max) + margen` (p. ej.
  `4 + 4 = 8` tiles); `memoria = ventana_x · ventana_y · bytes_fila · planos`.
- `XYUnlimited`: ring por los dos ejes con márgenes; `memoria ≈ (visible_x + margen_x)·(visible_y
  + margen_y)·bytes_fila·planos`, más el coste de Copper del split.

**Acuerdo propuesto**: el emulador declara por capa `{scroll_kind_preferido, max_scroll_x/y,
max_speed_px}`; el engine responde `Ok` (cabe), `Degradado` (otro algoritmo) o `Rechazado`
(`ConfigError`). Así el engine mantiene el control de recursos y el emulador no decide registros.

## 7. Qué falta para cerrar el consumo

- **F7.3** driver `XYUnlimited`/`CopperSplit` (y `BlitterColumns`) para el BG.
- **F7.7** `SpriteEngine` de alto nivel (NES 8/línea + overflow sobre `ActorStore`).
- **Attribute table** (paleta por bloques 16×16) como tabla paralela al `TileEditor`.
- **Adaptador de referencia** (fuera del core) con las `I*` reales + emulador como gate.

## 8. Requisitos de gráficos (PPU NES → engine)

Lo que el consumidor NES **necesita** para dibujar el frame, expresado como **peticiones de nivel A**
(vocabulario de juego, sin planos/Copper/Blitter/registros) y lo que el engine **decide**. El
consumidor **describe**; el engine **materializa**; si una capacidad no está, hay **degradación**
explícita (nunca fallo silencioso). Ver [GAME_API_TWO_LEVELS.md](architecture/GAME_API_TWO_LEVELS.md)
y [ENGINE_2D_ABSTRACCIONES.md](architecture/ENGINE_2D_ABSTRACCIONES.md).

### 8.1 Contrato de frame (VBlank) — **el más crítico**

- **Necesidad NES**: la lógica de frame corre en el **VBlank** (el "NMI"); todo el dibujo del frame
  se hace dentro de ese evento y **antes** de que el frame se muestre; el CPU puede hacer trabajo
  fuera del VBlank sin romper la imagen.
- **Petición (nivel A)**: un **bucle de juego del engine** con un callback por frame que se invoca
  **sincronizado con VBlank** (`app.run(frame_cb)` / `screen.on_vblank(cb)`), y que el engine
  garantice el **swap tras VBlank** (presentación). Debe poder pedirse **también** el caso
  "produzco yo el frame y solo quiero que se muestre" (`present_*`).
- **Decisión del engine**: doble buffer de display + swap `COP1LC` (ya en
  [DISPLAY_COMPOSITION.md](architecture/DISPLAY_COMPOSITION.md)); VBlank por Copper/IRQ del mini-SO.
- **Degradación**: si no hay bucle VBlank del engine, el consumidor usa un `present_indices`
  (c2p) con su propia sincronía (el `Host` actual).

### 8.2 Fondo (nametable + atributos + scroll)

- **Necesidad NES**: `2×(32×30)` nametable con **scroll libre 8-way** (x `0..255`, y `0..239`, wrap)
  y **escritura de tiles y atributos en runtime** al entrar columnas/filas por los bordes;
  **atributo por bloque `16×16`** (subpaleta del BG). Dos planos lógicos (nametable 0/1) que el
  juegos usa según scroll.
- **Petición (nivel A)**: una **`Layer`** de tiles con `tileset`, `palette`, y operaciones de
  intención:
  - `scroll_to(x, y)` (o `Camera` ligada a la capa) — posición por frame.
  - `set_tile(cx, cy, TileId)` y `set_palette_index(cx, cy, sub)` (o `set_attr`) — actualización de
    celdas por intención; el engine difiere/materializa (dirty) sin nombrar VRAM.
  - Declaración de **presupuesto/forma**: `{ max_scroll_x, max_scroll_y, max_speed_px, planes }` y
    `ScrollKind` **preferido** (`CopperSplit`/`CopperRing`/`Fine`).
  - Respuesta del engine: **`Ok`** (cabe), **`Degradado`** (otro algoritmo) o **`Rechazado`**
    (`ConfigError`) — el engine mantiene el control de recursos (ya acordado en §6).
- **Decisión del engine**: `XYUnlimited`/`CopperSplit` ↔ `XYLimited`/`CopperRing` ↔ `Fine`, según
  `region_cost`/Chip; ventana+guardas dimensionadas con los parámetros declarados.
- **Degradación**: si no hay driver de scroll, **redibujar** el BG por software (c2p) o `Fine`
  (blits por cambio). El juego sigue mostrándose "lento pero correcto".

### 8.3 Sprites (OAM)

- **Necesidad NES**: hasta **64 sprites** (`8×8` o `8×16`), **flip H/V**, **prioridad**
  (detrás/delante del BG) y **paleta por sprite**; **8 por línea** + **overflow** (el juego puede
  aprovecharlo o sufrir el *flicker*); y **OAM DMA** (`$4014`): subir 256 B cada frame.
- **Petición (nivel A)**: **`Sprite`** con `(id, x, y, tile/frame, flip_h, flip_v, priority,
  palette)` y colocación por intención; un **`SpriteEngine`** que resuelva **HW sprites + BOBs**
  (los que no caben en los 8/línea se dibujan con Blitter) respetando **prioridad BG/sprite**; y
  `sprites.upload(span<OamEntry>)` para reflejar el DMA de la NES.
- **Decisión del engine**: reparto HW/BOB, `ActorStore`+`SpriteAllocator`+`compose_sprites`.
- **Degradación**: sin HW sprites libres, **BOB cookie-cut** (más Blitter); sin Blitter, software.

### 8.4 Paleta y énfasis

- **Necesidad NES**: **32 bytes** de paleta (4 sub-paletas de BG + 4 de sprites) sobre 64 colores,
  con **cambios a mitad de frame** (splits) y bits de **énfasis** (que el consumidor puede
  **ignorar** si el juego no los usa).
- **Petición (nivel A)**: `palette.set(index, Color)` y, para cambios por zona, un **efecto/banda de
  nivel A** (`palette_shift`/`Band`) que emita en las scanlines dadas; el engine decide Copper.
- **Decisión del engine**: `Palette` + `FramePlan` (parches) / `eng::Copper`.
- **Degradación**: paleta global única por frame (sin splits); el énfasis se ignora.

### 8.5 Split de "status bar" (cambio de scroll a mitad de frame)

- **Necesidad NES**: el HUD/marcador arriba **fijo** y el juego scrolleando debajo: la NES
  reprograma el scroll en una scanline concreta.
- **Petición (nivel A)**: `viewport`/`Band` (bandas de la capa con su propio scroll/altura) — el
  engine lo materializa con Copper (split).
- **Degradación**: sin split, HUD como **sprites/BOBs** encima del BG (más coste) o HUD integrado.

### 8.6 Recursos y presentación

- **Chip RAM**: el consumidor declara la forma del BG (ventana+guardas) y el engine responde
  cabida vía **`res::Budget`** (`remaining_chip`) — `Ok`/`Degradado`/`Rechazado`.
- **Doble buffer**: lo posee la escena (`SceneResources.buffers`); el consumidor **no** reserva
  bitmaps.

## 9. Requisitos de sonido (APU NES → engine)

La APU de la NES **sintetiza** (dos pulsos, triángulo, ruido, DMC). Compromiso: **convertir en
offline** (pipeline de assets) los sonidos a **muestras** y la música a **módulo/stream**, y que el
engine **reproduzca** por su capa de audio ([GAME_AUDIO.md](architecture/GAME_AUDIO.md),
[AUDIO_MIXER.md](architecture/AUDIO_MIXER.md), [MUSIC_PLAYER.md](architecture/MUSIC_PLAYER.md)).

### 9.1 Efectos (SFX)

- **Petición (nivel A, ya existente)**: `audio.bank().add(id, {sample, prio, max_inst, cooldown,
  duck})` + `audio.play(id)`; hasta **~4 simultáneos** con pitch/volumen.
- **Decisión del engine**: `SfxMixer` (4 voces software en `AUD0`) + reparto por `AudioMode`.
- **Degradación**: menos voces / mono.

### 9.2 Música

- **Necesidad NES**: melodía por canales con **periodo/volumen/retrigger**.
- **Petición (nivel A)** — dos vías a acordar:
  - **(a) módulo**: `audio.play_music(module)` (PtPlayer/P61) tras **convertir** la música NES a
    módulo en el pipeline. Es el camino "engine-native".
  - **(b) replayer de tonos**: un **`ITonePlayer`** de bajo nivel (o de nivel A) que acepte eventos
    por canal `(canal, period, volume, retrigger)` para portar el **APU replayer** de la NES con
    exactitud (pitch/timbre NES). Requiere reservar canales de Paula y exponer pitch/volumen.
- **Decisión del engine**: qué canales se reservan para música vs SFX (`AudioMode`).

### 9.3 Reparto de voces y presupuesto

- **Petición**: conocer/declarar el **número de voces** (SFX + música) y recibir **`Result`** si no
  cabe (nunca "sonó raro" en silencio).
- **Decisión del engine**: `AudioMode` + `set_group_budget`.

## 10. Compromiso (pide el consumidor / decide el engine) — resumen para acordar

| Necesidad NES | Petición nivel A (vocabulario de juego) | Decisión del engine | Degradación |
|---|---|---|---|
| NMI/VBlank | `run(frame_cb)` / `on_vblank`; `present` | doble buffer + swap VBlank | `present_indices` (c2p) |
| BG nametable + scroll 8-way | `Layer.scroll_to/set_tile/set_attr` + `{max_scroll,max_speed,planes}` + `ScrollKind` | XYUnlimited/XYLimited/Fine | blits por cambio / software |
| Atributo 16×16 | `set_palette_index`/`set_attr` por celda | tabla paralela al tilemap | subpaleta única |
| 64 sprites, 8/línea, flip, prio | `Sprite{...}` + `SpriteEngine` + `upload(oam)` | HW sprites + BOBs | BOB / software |
| Paleta + splits + énfasis | `palette.set` + banda/efecto | Copper/FramePlan | paleta global; énfasis ignorado |
| HUD fijo + scroll | `Band`/`viewport` | Copper split | HUD como sprites |
| Chip RAM del BG | declarar ventana+guardas | `res::Budget` → Ok/Degradado/Rechazado | ventana menor |
| SFX (pulso/ruido/DMC) | `bank.add` + `play(id)` (muestra offline) | SfxMixer + AudioMode | menos voces |
| Música | `play_music(module)` **o** `ITonePlayer` por canal | PtPlayer/P61 / canales reservados | solo SFX |

## 11. Qué pido cerrar (para el acuerdo)

1. **Contrato de frame/VBlank de nivel A** (§8.1): es lo que sostiene todo el port (el port ya corre
   su lógica en el evento VBlank). Sin una forma canónica de "callback por VBlank + present", hay
   que bajar a B.
2. **`Layer` con `set_tile`/`set_attr`/`scroll_to` + respuesta Ok/Degradado/Rechazado** (§8.2–8.4):
   el BG NES es scroll+escritura de celdas; necesito expresarlo sin nombrar VRAM.
3. **`SpriteEngine` con OAM upload y prioridad BG/sprite** (§8.3).
4. **Banda/split de nivel A** para HUD fijo + juego scrolleando (§8.5).
5. **Sonido**: confirmar el camino **muestra-offline + SfxMixer** para SFX y elegir entre
   **módulo** (a) o **`ITonePlayer` por canal** (b) para música (§9.2). La opción (b) da fidelidad
   APU; la (a) es más "engine-native".
6. **Fallos explícitos** (`Ok`/`Degradado`/`Rechazado`) en cada petición de recursos (§8.6).
