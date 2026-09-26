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
