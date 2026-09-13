# Sistema de tipos internos (guía de diseño, tipo Rust)

El engine usa hoy muchas interfaces internas con punteros crudos y escalares sin
semántica: `u8*`, `const u16*`, `void*`, `u16 width/height/row_bytes` intercambiables. Eso
permite errores que un sistema de tipos debería atrapar **en compilación**: pasar un buffer
de audio como origen de un Blitter gráfico, intercambiar origen y destino de un blit, dar
un `frontbuffer` donde se espera una base de `BPLxPT`, o confundir ancho con alto.

Este documento es el **inventario y la propuesta** de un sistema de tipos de dominio para las
interfaces internas, al estilo de una aplicación Rust segura: tipos nuevos que envuelven
punteros/rangos/escalares, conversiones **explícitas** en la frontera, y el acceso crudo
confinado a una sola capa (`unsafe` explícito). No sustituye a `Span` (que ya resuelve
"puntero + tamaño"): lo **especializa** por dominio.

Referencia de estilo vigente: `CODING_STYLE.md` §"Seguridad de tipos sobre punteros crudos".
Modelo de capas: `ENGINE_2D_ABSTRACCIONES.md` §3 (`Owner`/`Ref`/`Span`).

## 1. Objetivo y criterio

Una interfaz es "a prueba de balas" si **no se puede llamar mal aunque se quiera**: el
compilador rechaza mezclar dominios, invertir roles o pasar una geometría por otra. El
criterio:

- **Tipos de dominio, no `T*`**: cada buffer/registro tiene su tipo (`Pattern`, `AudioSample`,
  `Palette`, `CopperList`…) y no son intercambiables.
- **Semántica en el tipo**: `BitmapBase` (base de `BPLxPT`) ≠ `FrontBase` (buffer de escritura)
  ≠ `PlaneBase` (vista de un plano) ≠ `ChipAddress` (dirección DMA).
- **Solo se tipan buffers/punteros, no escalares**: los tipos de dominio envuelven rangos de
  memoria (`Bytes`/`Words`), direcciones/base y roles. **No** se envuelven enteros sueltos
  (ancho/alto/stride/planes): no aportan seguridad real, ensucian las llamadas y obligan a casts.
- **El valor está en los valores devueltos**: los productores devuelven tipos de dominio
  (p. ej. `Bitmap::bitplanes() -> PlaneBytes`, `MemoryBlock::buffer<Tag>()`), de modo que los
  consumidores se conectan **sin casts** y el compilador rechaza mezclas de dominio.
- **Conversión explícita**: cambiar de dominio (`Pattern` → `PatternWords`) o de vista
  (`Bytes<Tag>` → `Words<Tag>`) requiere un método con nombre; nunca hay conversiones implícitas
  entre dominios.
- **`unsafe` en una capa**: solo el backend Amiga (Blitter/Copper/DMA) y `BlitJob` manejan lo
  crudo, y lo hacen a través de un único conversor documentado.
- **Coste cero**: cada tipo es un `struct` trivialmente copiable del mismo tamaño que envuelve;
  sin virtuals, sin heap, `constexpr` donde aplique. Se verifica con `-S` que no añade
  instrucciones (regla de rendimiento de `AGENTS.md`).

## 2. Modelo safe ↔ unsafe

```text
  CAPA SAFE (produce/consume tipos de dominio)        CAPA UNSAFE (raw, documentada)
  ┌───────────────────────────────────────────┐       ┌──────────────────────────────┐
  │ Surface / PlaneView / SoftDpfComposition  │       │ BlitJob { const u16* ... }    │
  │ Pattern, PaletteWords, PatternWords       │──────►│ CopperBuilder (BPLxPT)        │
  │ BitmapBase, FrontBase, PlaneIndex         │  raw()│ amiga_minimal (registros)     │
  │ SpriteWords, AudioSample, CopperWords     │       │ c2p / blitter / audio_paula   │
  └───────────────────────────────────────────┘       └──────────────────────────────┘
        el error de dominio no compila                       el invariante está documentado
```

La frontera es **una sola dirección**: los tipos safe exponen `raw()`/`to_raw()` (const), y
solo la capa unsafe los construye desde memoria (arena/backend) con `from_raw()` documentado.

## 3. Catálogo de tipos propuestos

### 3.1 Vistas tipadas (sustituyen `Span<u8>`/`Span<const u16>` sin dominio)

```cpp
namespace eng {

template <class Tag> class Bytes;      // Span<u8> mutable
template <class Tag> class ByteView;   // Span<const u8> (o Bytes<const Tag>)
template <class Tag> class Words;      // Span<u16> mutable
template <class Tag> class WordView;   // Span<const u16>

} // namespace eng
```

- Cada una envuelve un `Span<u8>`/`Span<u16>` y **solo** expone operaciones del dominio
  (`size`, `subspan`, `at` con `illegal`, `fill`).
- **Ergonomía tipo `Span`**: constructor de array nativo que **deduce el tamaño**
  (`Pattern p{arr};`), iteradores `begin/end/cbegin/cend` (range-for y algoritmos), `front`/`back`,
  `subspan(off)` y typedefs `value_type`/`iterator`/`size_type`. Todo `constexpr` y a coste cero.
- `Bytes<Tag>::words<Tag>()` / `WordView<Tag>::bytes()` hacen la reinterpretación **explícita**
  (alineación y tamaño comprobados con `static_assert`/runtime); `as_const()` pasa de mutable a
  vista de solo lectura **conservando el tag**.
- `raw()` es el único camino a `Span<u8>`/`Span<const u16>` y se documenta como frontera.

Tags (structs vacíos, cero coste) y alias de dominio:

| Alias | Envuelve | Sustituye a | Riesgo que elimina |
|---|---|---|---|
| `Pattern` / `PatternWords` | bytes / words | `const u8*` + `pattern_row_bytes` | copiar un patrón a un destino que no es fondo |
| `TileIndexed` | bytes | `const u8* indexed` | pasar un tilebank como sample |
| `PlanarRegion` / `PlanarPlane` | bytes | `u8* m_frontbuffer`, `bitplanes` | escribir fuera del plano/layout |
| `ChunkyBuffer` | bytes | `const void* chunky` | alimentar el C2P con datos planares |
| `PaletteWords` | words | `const u16* palette` | usar un tileset como paleta |
| `SpriteWords` | words | `const u16* sprite_data` | pasar palabras de tile a un sprite |
| `CopperWords` | words | `const u16* copper` | programar el Copper con datos que no son listas |
| `AudioSample` | bytes | `const u8* sample` | **pasar audio como origen de un Blitter** |
| `MusicModule` | bytes | `const void* module` | dar un sample a un replayer |
| `UafPayload` | bytes | `const u8*` en `Blob` | leer offsets sobre un buffer cualquiera |

### 3.2 Tipos de dirección/base

| Tipo | Envuelve | Semántica |
|---|---|---|
| `BitmapBase` | `u8*` | `Bitmap::allocation_start()` (lo que va a `BPLxPT`) |
| `FrontBase` | `u8*` | `bytes().data()` (con `frontbase_offset`) |
| `PlaneBase` | `const u8*` | base del plano `p` dentro de un bitmap (solo lectura para el mapper) |
| `ChipAddress` | `uintptr` | dirección DMA-visible (chip RAM) |
| `CpuAddress` | `uintptr` | dirección solo-CPU |
| `ByteOffset` / `WordOffset` | `u32` | desplazamiento (no mezclable con `PlaneIndex`) |

`BitmapBase` y `FrontBase` son **distintos a propósito**: el bug "usar el frontbuffer como base
de `BPLxPT`" deja de compilar.

### 3.3 Escalares: **no** se envuelven

Los parámetros escalares (ancho, alto, `row_bytes`, `plane_bytes`, número de planos, número de
words, stride…) van como `u8`/`u16`/`u32` **a secas**. Envolverlos en tipos fuertes (`PixelWidth`,
`RowBytes`, `PlaneCount`…) no añade seguridad real, obliga a escribir casts en cada llamada y
oscurece el código (ver `CODING_STYLE.md`). Como mucho se documenta la unidad en el nombre del
parámetro.

La seguridad de "no intercambiar parámetros" se consigue donde importa: **los buffers y las
direcciones sí son tipos de dominio** y los productores los devuelven ya tipados (p. ej.
`bitplanes() -> PlaneBytes`), así que una llamada normal no necesita ningún cast.

### 3.4 Handles y bloques (ownership)

| Tipo | Sustituye a | Nota |
|---|---|---|
| `Handle<Tag>` (`BitmapHandle`, `TileBankHandle`, `SpriteHandle`, `SoundHandle`) | índice `u16` suelto | evita usar un índice de sprite como índice de tile |
| `Block<Tag>` | `MemoryBlock { void* data; ... }` | bloque de arena tipado; `bytes<Tag>()` |
| `Ref<T>` | `T*` no-propietario | ya especificado en `ENGINE_2D_ABSTRACCIONES.md` §3 |
| `TaskToken<T>` | `void* user` en callbacks | tarea de fondo con datos tipados |

### 3.5 Blits y contexto de dibujo

- `BlitJob` mantiene campos crudos (`const u16* source`, `u16* destination`) porque es el
  **comando del backend**, pero sus productores (`Surface`, `PlaneView`, `SoftDpfComposition`)
  solo aceptan tipos de dominio:
  ```cpp
  BlitJob copy(eng::Pattern src, PlaneView dst, u8 plane, Rect region);
  ```
- Se distingue **rol** además de contenido: `BlitSource` (const) y `BlitDest` (mut) evitan
  intercambiar origen y destino.

## 4. Auditoría por subsistema

### 4.1 `PlaneView` / `SoftDpfComposition` (punto de partida del usuario)

| Actual | Propuesta |
|---|---|
| `bind_single(u8* main_real, u8* main_front)` | `bind(BitmapBase, FrontBase)` |
| `bind_raw(u8*, u8*, u8*, u8*)` | `bind(BitmapBase, FrontBase, BitmapBase, FrontBase)` |
| `display_base() -> u8*` | `display() -> BitmapBase` (o `ChipAddress` para el Copper) |
| `write_base() -> u8*` | `back() -> FrontBase` |
| `make_copy_rect_job(const u8* pattern, ...)` | `make_copy_rect_job(Pattern, u16 row_bytes, ...)` |

### 4.2 Bitmap / arena / memoria

| Actual | Propuesta |
|---|---|
| `Bitmap::allocation_start() -> u8*` | `BitmapBase` |
| `Bitmap::bytes() -> Span<u8>` | `Bytes<PlanarRegion>` (mutable) / `ByteView<PlanarRegion>` |
| `MemoryBlock { void* data; u32 size; MemoryKind }` | `Block<Tag>` con `bytes<Tag>()` |
| `LinearArena::allocate(...) -> MemoryBlock` | `allocate<Tag>(Bytes, align) -> Block<Tag>` |
| `emit_world_rect(const u16* src, ...)` / `..._masked` | `emit_world_rect(WordView<TileBank>, ...)` |

### 4.3 Backend gráfico (frontera unsafe, se documenta y se mantiene fina)

| Actual | Propuesta / hecho |
|---|---|
| `blitter_clear(u8* dst, u8 planes, u16 row_bytes, u32 plane_bytes, u16 w, u16 h)` | `blitter_clear(PlaneBytes, u8 planes, u16 row_bytes, u32 plane_bytes, u16 w, u16 h)` |
| `blit_fill_from_mask(const u8* mask, u8* dst, ...)` | `blit_fill_from_mask(MaskBytes, PlaneBytes, u8 planes, ...)` |
| `blitter_line(u8* plane, u16 row_bytes, ...)` | `blitter_line(PlaneBytes, u16 row_bytes, ...)` |
| `c2p(const void* chunky, void* planes)` | `c2p(u32 w, u32 h, u32 stride, ChunkyView, PlaneBytes)` |
| `move_bitplane_pointer(u8 plane, const void* address)` | `u8 plane` + `ChipAddress` |
| `bitplanes() -> u8*` (escenas) | `bitplanes() -> PlaneBytes` (devuelto, sin cast) |
| `MemoryBlock::data` crudo | `MemoryBlock::buffer<Tag>()` / `view<Tag>()` |

### 4.4 Copper / escenas EHB/HAM/tile

| Actual | Propuesta |
|---|---|
| `CopperBuilder(m_words)` sobre `u16*` | `Words<CopperTag>`; `patch_move32(..., const void*)` → `ChipAddress` |
| `emit_palette(const u16* colors, u8 first, u8 count)` | `PaletteWords`, `u8 first`, `u8 count` |
| `ehb_scene::bitplanes() -> u8*` | `PlaneBytes` |

### 4.5 Contenido / assets / streaming

| Actual | Propuesta |
|---|---|
| UAF `read_be16(const u8*)` / `Blob` | `ByteView<UafPayload>` (hecho) |
| `WorldView::decode_chunk(..., u16* dst, u32 dst_count)` | `decode_chunk(..., Words<TileBankBuffer>)` (hecho) |
| `ChunkCache::Loader { LoadResult (*)(void*, s32, s32, u16*) }` | `Loader` con `Words<TileBankBuffer>` (hecho) |
| `xlimited_build_blocks_bitmap(..., const u8* indexed, u32 stride)` | `TileIndexed`, `u32 stride` |
| `Surface::draw_text(s32, s32, const char*, u8)` | `u8 color` |

### 4.6 Audio y tareas de fondo

| Actual | Propuesta |
|---|---|
| `SampleEvent { const u8* sample }` / `AudioMixer::setup(void*, void*, void*)` | `AudioSample`, `MixBuffer`, `PluginBuffer`, `PluginData` |
| `MusicPlayer::init(const void* module, const void* samples, ...)` | `MusicModule`, `AudioSample` |
| `BackgroundQueue::add(TaskStep, void* data)` | `add<TaskData>(u16 (*)(TaskData*, TaskSlice), Ref<TaskData>)` |
| `MinimalBackend::set_vblank_service(void (*)(void*, u16), void*)` | `Service<Context>` tipado |

## 5. Ejemplo: `PlaneView` tipado

```cpp
struct PlaneTag {};
using PlaneBytes = eng::Bytes<PlaneTag>;         // buffer de un plano (mutable)
using PlaneViewConst = eng::ByteView<PlaneTag>;  // vista de solo lectura

class PlaneView {
public:
    void bind(eng::BitmapBase real, eng::FrontBase front);            // single
    void bind(eng::BitmapBase real, eng::FrontBase front,
              eng::BitmapBase extra_real, eng::FrontBase extra_front); // doble buffer

    /// Base del buffer delantero para `BPLxPT` (dirección DMA, solo lectura por CPU).
    [[nodiscard]] eng::ChipAddress display() const;
    /// Buffer trasero donde escribe el Blit (bytes del plano de fondo).
    [[nodiscard]] PlaneBytes back() const;
    void flip() noexcept;
};
```

El productor (`SoftDpfComposition::copy`) recibe `Pattern` y `u8 plane`, no `const u8*`; el `BlitJob`
resultante sigue crudo, generado **dentro** de la capa segura.

## 6. Coste en 68000 y verificación

- Los envoltorios son `struct` de un solo miembro: mismo tamaño y misma copia que el tipo
  envuelto; `constexpr` en todo lo que no valide runtime. Verificado por `sizeof` en HOST-040.
- **Verificación obligatoria** (regla de `AGENTS.md`): comparar el asm con `-S`/`-fverbose-asm`
  antes/después de cada migración y anotarlo en `docs/guides/optimization/OPTIMIZACION_GPP_68000.md`.
- La capa unsafe no añade instrucciones: `raw()`/`data()` son una lectura de miembro.

## 7. Compatibilidad con la frontera pública

- `CODING_STYLE.md` §"Frontera de API pública" ya prohíbe que la app vea hardware. Estos tipos
  son **internos** (engine/field, engine/graphics, engine/assets, backend); no aparecen en
  `eng/api/`.
- `Span` sigue siendo el tipo de "rango contiguo genérico"; los tipos de dominio son su
  especialización. No se elimina `Span`, se envuelve.
- Los tests host ganan porque un test puede crear un `Pattern` de juguete y comprobar que la
  API no acepta un `AudioSample` (fallo de compilación intencionado).

## 8. Migración por fases

1. **Fundamento**: `eng/core/typed.hpp` con `Bytes/ByteView/Words/WordView` (array/iteradores) +
   `eng/core/domains.hpp` con los tags/alias y tipos de dirección/base (`ChipAddress`…), más un test
   host puro.
2. **Frontera de memoria**: `BitmapBase`/`FrontBase`, `Bitmap`, `Block<Tag>`/`LinearArena`.
3. **PlaneView + SoftDpfComposition**: primer consumidor real (los punteros `u8*` pasan a
   `BitmapBase`/`FrontBase`/`PlaneBytes`).
4. **Blits/`FramePlan`**: `BlitSource`/`BlitDest` y productores tipados; `BlitJob` crudo.
5. **Contenido/streaming**: `WorldView`, `ChunkLoader`, UAF (`ByteView<UafPayload>`).
6. **Backend**: blitter/C2P/audio/copper reciben los tipos de dominio en su firma pública
   interna y convierten a crudo en el último punto.
7. **Audios/tareas**: `AudioSample`/`MusicModule`, `Service<Context>`, `TaskToken<T>`.

Cada fase: build `--debug/--release`, tests host verdes, demos 107/111/112/201/202 analizan OK, y
`-S` sin regresión. Ningún cambio de comportamiento visual.

### 8.1 Estado de implementación

- **Fase 1 — hecha**: `eng/core/typed.hpp` (vistas con tag, array/iteradores, direcciones/base) y
  `eng/core/domains.hpp` (tags/alias de dominio). Test HOST-040.
- **Fase 2 — hecha**: `Bitmap::base()`/`front()`; `PlaneView` y `SoftDpfComposition` usan
  `BitmapBase`/`FrontBase` en `bind*`/`display_base`/`write_base` (`XLimitedPlayfield` cruza a
  crudo solo en `hardware_view`). HOST-038/039 actualizados; 112 sin regresión.
  **Productores tipados**: `MemoryBlock::buffer<Tag>()`/`view<Tag>()`, y escenas/bitmaps devuelven
  `PlaneBytes` (`bitplanes()`, `plane(i)`); los consumidores conectan sin cast.
- **Fase 3 — hecha (roles)**: `BlitSource`/`BlitDest` en `BlitJob` (el backend lee `job.source.words`);
  `SoftDpfComposition::make_copy_*` recibe `Pattern` (vista **con tamaño**) y escalares `u16`, con
  **validación de rango** (violación → `illegal`). Falta tipar `Surface`/`FramePlan` (paleta/copper).
- **Fase 4 — hecha**: `Blob`/`Reader`/`BlobWriter`, las vistas UAF y `WorldView::read` usan
  `eng::UafPayload` (`ByteView<UafTag>`); `ChunkCache::Loader`, `StreamingWorldMap::Source`,
  `WorldMapChunkLoader` y `WorldView::decode_chunk<Tag>` usan `eng::TileBankBuffer`; el **pool** de
  `ChunkCache`/`StreamingWorldMap` es `TileBankBuffer` (el llamador lo entrega de su arena/array,
  sin casts). El builder de tilebank indexado y `XlimitedSceneConfig::indexed_tiles` usan
  `eng::TileBankBytes`. HOST-012/026/029/030/031/035/037 y 078/111 migrados.
- **Fase 5 — hecha**: audio puro (`SampleEvent`/`AudioPlan::Channel` → `eng::AudioSample`,
  `MusicEvent` → `eng::MusicModule`) y backend blitter/C2P en su firma interna
  (`blitter_clear`/`blitter_line`/`blit_fill_from_mask`/`fill_triangles_blitter` con
  `PlaneBytes`/`PlaneCount`/`RowBytes`/`ByteSize`/`PixelWidth/Height`/`MaskBytes`; `c2p_1x1_*` con
  `ChunkyView`/`PlaneBytes`); la conversión a crudo (`data()`/`value`) queda dentro del backend.
  Demos 057/061/062/063/078/079/081 y `audio_paula` migradas; `C2p4State` (staging del C2P) sigue
  crudo por ser punteros de hardware. HOST-005 y demos alcanzan READY.
- **Fase 6 — parcial**: `BackgroundQueue` usa tareas **tipadas** `TaskToken<T>` /
  `TaskFn<T>` (firma `u16(T& data, const TaskSlice&)`, **referencia**, no puntero) y fábrica
  `task_token(data, fn)`; el engine copia el token en el slot y lo invoca con la firma de `T`
  (sin `void*` en la API). HOST-017 y demo 081 migrados. Pendiente: `Service<Context>` en los
  servicios del backend.
- **Fase 7 — hecha (API)**: `eng::Block<Tag>` (`typed.hpp`) y reservas tipadas
  `LinearArena::allocate_block<Tag>()` / `MemoryBlock::block<Tag>()`; HOST-041. Pendiente: migrar
  consumidores de arena a `allocate_block` donde simplifique.


## 9. Reglas para `CODING_STYLE.md` (resumen)

1. Ninguna interfaz interna nueva acepta `T*`/`void*` si existe (o puede crearse) un tipo de
   dominio; los buffers van por `Bytes<Tag>`/`Words<Tag>`.
2. Nunca dos parámetros del mismo tipo escalar que signifiquen cosas distintas (`width`/`height`):
   usar unidades fuertes.
3. `from_raw`/`raw()` son explícitos y documentados; solo el backend los usa.
4. Un tipo nuevo solo entra si su invariante es comprobable (test host o `static_assert`).

## 10. Relación con el resto

- Estilo y frontera: `CODING_STYLE.md`.
- Capas y `Owner`/`Ref`/`Span`: `ENGINE_2D_ABSTRACCIONES.md` §3/§5.1.
- Modelo de playfields (PlaneView/SoftDpf): `PLAYFIELD_SCROLL_ARCHITECTURE.md` §3.2.
- Formato de mundo y loader: `WORLD_FORMAT.md`, `STREAMING_LOADER.md`.
- Contenido y streaming: `CONTENT_AND_TILEMAP.md`.
- Rendimiento 68000: `docs/guides/optimization/OPTIMIZACION_GPP_68000.md`.
