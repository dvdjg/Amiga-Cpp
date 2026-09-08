# Diseño de object/effects/copper: Visual, CopperIntent, SpriteTemplate y Effect

Este documento concreta la libreta de diseño del roadmap (`ROADMAP_ENGINE_CPP_AMIGA500.md` §20,
"Abstracciones que no debemos olvidar") en una estructura de clases y plantillas reutilizables.

Motiva la incorporación de las fuentes externas (demoscene-repo, amiga-bootcamp, ACE,
Sevgi_Engine) y define el contrato central que permite que un mismo actor juegue igual como
sprite hardware que como BOB, que un playfield reciba los mismos efectos de Copper que un
objeto, y que el programador de juego no vea registros, bitplanes ni copperlists.

## 1. Principio rector

En Amiga, un actor, un playfield o un efecto no se diferencian por lo que *son* sino por las
**contribuciones que emiten sobre registros custom en una ventana vertical de líneas raster**.

Un BOB con cambio de paleta por línea, un ciclo de agua, un split de cielo con offset-X por
línea y un sprite multiplexado son **el mismo dato**: una lista de intenciones anotadas con
`[línea_inicio, línea_fin, cambios]`.

De ahí la decisión central de este diseño: existe un vocabulario uniforme de **intenciones**
(`CopperIntent`, `BlitIntent`, `SpriteIntent`, `PaletteIntent`) que los schedulers (dueños
únicos de Copper/Blitter/Sprites) compilan a `FramePlan`, y que la lógica de juego nunca cruza.

```
lógica de juego ── pide intenciones ──► RenderScene retenido
        ▲                                        │
        │  (jamás registros/DMA)                 ▼ compila
        │                                   RenderCompiler
        │                                        │
        └──── arbitraje (dueños únicos) ◄───────┤  CopperIntent / BlitIntent
             CopperScheduler + Timeline         │  SpriteIntent / PaletteIntent
             BlitterQueue + Budget              ▼
             SpriteAllocator + multiplex   FramePlan (job concreto por frame)
             DmaBudget                           │
                                                 ▼
                                        PlatformBackend (Amiga OCS/ECS)
```

Esta separación ya existe de forma parcial: `FramePlan` (blits + paleta + dirty rects +
presupuesto), `CopperScheduler` (`emit_palette_zone`, `move_bitplane_pointer`, `wait_line`),
`SpriteManager` (`emit_into`, `dma_bits`), `DisplayDriver`/`GraphicsDriver` (ciclo
takeover/install). Lo que este diseño añade es el **vocabulario portable de intenciones** que
hoy está repartido entre esos métodos sueltos.

## 2. Mapa de capas (responsabilidades)

```
┌─────────────────────────────────────────────────────────────┐
│ GameWorld / lógica de juego                                 │  sin Amiga
│   EntityId + Componentes (transform, actor, collider, ...)  │
└──────────────────────────┬──────────────────────────────────┘
                           │ intenciones de alto nivel ("agua con ciclo")
┌──────────────────────────▼──────────────────────────────────┐
│ RenderScene / SceneGraph2D / Camera2D (retained, descriptivo)│  sin HW
└──────────────────────────┬──────────────────────────────────┘
                           │ compila a intenciones
┌──────────────────────────▼──────────────────────────────────┐
│ RenderCompiler  +  Effect (templates, sin HW)               │  sin HW
│   CopperIntent / BlitIntent / SpriteIntent / PaletteIntent  │
└──────────────────────────┬──────────────────────────────────┘
                           │ arbitraje (dueños únicos, presupuestado)
┌───────────────┬──────────┴──────┬───────────────┬────────────┐
│ CopperScheduler│ BlitterQueue   │ SpriteAllocator│ DmaBudget │  con modelo HW
│  + Timeline    │  + Budget      │  + multiplex   │           │
└───────────────┴─────────────────┴───────────────┴────────────┘
                           │  → FramePlan (ya existe)
┌──────────────────────────▼──────────────────────────────────┐
│ PlatformBackend (Amiga OCS/ECS; futuro Megadrive/PC)        │  con HW
│   MinimalBackend hoy; ACE como backend interno opcional     │
└──────────────────────────┬──────────────────────────────────┘
                           │  → UAF-R loader (chunks cocinados)
┌──────────────────────────▼──────────────────────────────────┐
│ Chip RAM / Slow RAM / registros / Copper / Blitter / Paula  │
└─────────────────────────────────────────────────────────────┘
```

Igual que en `CODING_STYLE.md`: la lógica de juego depende de abstracciones del engine, no del
Amiga; el Amiga es un backend. La misma descripción retenida debería poder compilarse algún día
contra otro backend (Mega Drive/Neo Geo/PC).

## 3. `Visual`: el contenido portable de un objeto

Un `Visual` describe *qué se ve* sin decir *cómo se materializa* (BOB, sprite o playfield es
decisión del driver/allocator). Es la pieza mínima y no guarda punteros a hardware.

```cpp
namespace eng::graphics {

/// Cómo se materializa un `Visual`. El dato es el mismo; cambia el backend.
enum class VisualKind : u8 {
    Bob,              // blitter cookie-cut con máscara
    HardwareSprite,   // 1 word de ancho (16 px) o 2 (32 px), 1..128 líneas
    Tile,             // tile 16x16 de un tilemap
    FillRect,         // rect de color plano
};

/// Descriptor portable de un objeto dibujable (retained, sin registros).
struct Visual {
    VisualKind kind = VisualKind::Bob;
    Span<const u16> pixels {};  // data planar cocinada (Chip RAM)
    Span<const u16> mask {};    // 1 plano, opcional (cookie-cut)
    u16 w = 0, h = 0;
    u8  bitplanes = 0;
    u16 offset_x = 0;           // shift de blit (X no alineada a 16)
    u16 palette_base = 16;      // COLORxx base
};

} // namespace eng::graphics
```

La regla de "mismo objeto, distintas pinturas" (navecita que pasa de sprite a BOB sin que se
note) se resuelve con que **el `Visual.id` no cambia**; solo cambia el `VisualKind` materializado
por el `SpriteAllocator`/`BlitterQueue` según disponibilidad. Como sprite y BOB comparten paleta
(sprites usan `COLOR16+`, el BOB puede usar la misma franja) y geometría, la transición es
imperceptible de forma natural.

## 4. `CopperIntent`: el objeto versátil (cambio de paleta / shift por línea)

`CopperIntent` es la unidad de "modificación de registros en una franja vertical". Es portable y
cubre exactamente las técnicas que piden estas fuentes externas (demoscene `04-plasma`,
`08-floor`, `50-roller`, `53-showpchg`; amiga-bootcamp "color multiplexing" y "sprite reuse").

El término ya se usa como destino en `docs/demos/effects/DEMOSCENE_REPO_INDEX.md` ("los efectos
emiten `FramePlan`/`BlitterQueue`/`CopperIntent`"). Aquí queda definido.

```cpp
namespace eng::graphics {

/// Qué registro/grupo de registros cambia una intención de Copper.
enum class CopperIntentKind : u8 {
    PaletteLine,    // cambiar COLORxx.. en la línea `top` (o en cada línea del tramo)
    PaletteSpan,    // cambiar COLORxx a mitad de línea (hpos): "copper bar"
    ShiftLines,     // offset-X por línea (onscroll/ondas): reescribe BPLxPT/BPLCON1
    BitplaneSplit,  // reapuntar planos a media pantalla (HUD, bandas estilo Risky Woods)
    SpriteRearm,    // reapuntar SPRxPT/POS/CTL a media pantalla (multiplexado)
    DmaControl,     // activar/desactivar BPLEN/SPREN/COPEN por línea
};

/// Cambio portable sobre una franja vertical de líneas raster.
///
/// Esta es la abstracción que unifica "BOB con cambio de paleta", "playfield con scroll por
/// línea" y "sprite con color por zona": todos son listas de `CopperIntent` anotadas por franja.
struct CopperIntent {
    CopperIntentKind kind = CopperIntentKind::PaletteLine;
    u16 top = 0, bottom = 0;    // ventana vertical [top, bottom)
    u16 hpos = 0;               // para PaletteSpan (pos horizontal, en unidades de WAIT)
    const u16* colors = nullptr; u8 first = 0, count = 0;   // Palette*
    s16 shift_x = 0;            // ShiftLines
    const u8* bitplanes = nullptr;                          // BitplaneSplit
    u8  sprite = 0;             // SpriteRearm (canal)
    const u16* sprite_ptr = nullptr;                        // SpriteRearm (nueva DATA)
};

} // namespace eng::graphics
```

Un **objeto versátil** es la composición `Visual` + `CopperIntent[]`:

```
ObjetoVersátil = Visual + lista ordenada de CopperIntent (ventanas verticales)
   └─ ej: navecita = Visual(Bob) + [PaletteLine en y=120, ShiftLines en y=180..190, ...]
```

El `CopperScheduler` recibe estos `CopperIntent` y decide, con la `Timeline`, si caben en el
H-BLANK de cada línea y cómo se mezclan con el resto de la escena. **Ningún `CopperIntent`
escribe registros por su cuenta**: el scheduler es el único que expande a `MOVE/WAIT`.

## 5. `SpriteTemplate`: plantillas para modelar todas las posibilidades del sprite

La petición de un "sistema de plantillas" para sprites hardware se concreta sobre la base
`SpriteManager` ya existente. El `SpriteManager` queda como **emitter final** (escribe
`SPRxPT/POS/CTL` y emite en el Copper); la `SpriteTemplate` es la **descripción portable** que
el `SpriteAllocator` procesa y con la que decide reuso/multiplexado.

```cpp
namespace eng::graphics {

/// Franja reutilizable de una imagen de sprite fuente.
struct SpriteSegment {
    u16 data_offset;    // words desde el inicio del bitmap fuente
    u16 height;         // líneas de esta franja dentro del sprite
    u16 y_in_bitmap;    // línea inicial dentro de la imagen fuente
};

/// Cambio de paleta asociado a una franja (color multiplexing por scanline).
struct SpritePaletteSwitch {
    u16 segment;        // segmento al que afecta
    u16 line;           // línea raster de disparo
    const u16* colors; u8 first, count;   // COLORxx.. a programar
};

/// Plantilla de un sprite hardware: describe cómo UNA imagen fuente se traduce a segmentos
/// reutilizables y a cambios de paleta por franja. Es portable: no escribe registros.
template <u8 MaxSegments, u8 MaxPaletteSwitches>
struct SpriteTemplate {
    Span<const u16> bitmap {};                  // imagen fuente (DAT/DATB), Chip RAM
    SpriteSegment segments[MaxSegments] {};     // troceado para reuso vertical
    SpritePaletteSwitch switches[MaxPaletteSwitches] {};
    u8 segment_count = 0;
    u8 switch_count = 0;
    u8 width_words = 1;        // 16 px o 32 px (SPRxCTL doble ancho)
    u8 attach = 0;             // 0 normal, 1 attached al canal anterior (15 colores)
};

} // namespace eng::graphics
```

Cubre los casos que se piden:

- **Reutilizar el mismo sprite para dibujar cosas distintas** (pintado arriba/abajo): varios
  `SpriteSegment` sobre una única `bitmap`, con `data_offset`/`height` — el allocator reapunta el
  canal en cada franja (técnica "chasing the raster" de amiga-bootcamp).
- **Cambios de paleta en ciertas líneas**: las `SpritePaletteSwitch` se convierten en
  `CopperIntent::PaletteLine` sobre la franja correspondiente, cayendo por el mismo scheduler.
- **Fondos estilo Risky Woods / sprite strips**: `MaxSegments` alto + multiplexado vertical; el
  `SpriteAllocator` reusa canales al cruzar el H-BLANK. El caso extremo es "sprite-as-playfield"
  (Jim Power), que es una `SpriteTemplate` con strips horizontales sobre los 8 canales.

Invariantes que la implementación debe heredar de la auditoría (antipatterns de amiga-bootcamp):

- **Reload cada frame**: los `SPRxPT` hay que reescribirlos al principio de cada VBL (el Copper
  lo hace; "The Phantom Sprite" es leer basura si el puntero no se recarga).
- **Colores compartidos por par**: los sprites N y N+1 comparten sus 3 `COLORxx`; cualquier
  `SpritePaletteSwitch` debe respetar el par ("The Color Bleed").
- **Chip RAM obligatoria**: `bitmap` de la plantilla es `Span<const u16>` a Chip RAM.
- **Sprite 0 = ratón**: reservar el 0 para el cursor del sistema salvo takeover total.

## 6. `Effect`: efectos de primer orden sobre el playfield/la escena

De la petición de "concepto de effect" surge un concept C++ que formaliza los efectos
reutilizables (ya listados en el roadmap §20.6). `PaletteCycleEffect` (ya existe) pasa a cumplirlo.

```cpp
namespace eng::graphics {

/// Contrato de un efecto: avanza su estado temporal y aporta intenciones a un FramePlan.
/// No escribe hardware: `apply_into` sólo registra intenciones (CopperIntent/BlitIntent/...).
template <typename E>
concept Effect = requires(E e, u16 frame, FramePlan& plan) {
    e.update(frame);        // avanza fase/estado con el índice de frame
    e.apply_into(plan);     // aporta CopperIntent / BlitIntent / SpriteIntent
};

} // namespace eng::graphics
```

Efectos previstos (cada uno produce `CopperIntent`s, no MOVEs):

```
eng::graphics::effects/
   palette_cycle.hpp     (ya existe) — agua/fuego/luces
   palette_zone.hpp      — cambio de paleta por bandas
   raster_gradient.hpp   — cielos/niebla/horizonte (per-line COLOR00)
   raster_distortion.hpp — ondas/heat haze (ShiftLines)
   copper_script.hpp     — script portable desde UAF (intenciones, no raw)
   sprite_multiplex.hpp  — reuso de canales por franjas
   blitter_transition.hpp — wipes/masks/reveals
```

## 7. Playfields con CopperIntent (simetría actor/playfield)

Un playfield con efectos de Copper es **lo mismo que un actor**: emite `CopperIntent`s (shift
por línea para scroll por scanline, paleta por banda, splits). Por eso `TileScrollScene` y
`XlimitedScene` derivan su `rebuild_copper` hacia el mismo vocabulario: en lugar de emitir
`BPLxPT/BPLCON1` directamente, generan `CopperIntent::ShiftLines`/`BitplaneSplit` que el
`CopperScheduler` expande.

Esta simetría es la clave de reutilización: el mismo `CopperIntent` describe un actor que se
mueve por línea, un fondo que ondea y un playfield con split de HUD.

## 8. Cargador UAF-R: capa de formatos avanzados

La "capa capaz de cargar formatos avanzados" (Universal-Asset-Format) es la **capa 0 de datos**:
un formato *cocinado* (chunks binarios, sin parsing pesado en Amiga) que el `AssetRuntime` mapea
directamente a `Visual`, `CopperIntent`, `SpriteTemplate` y `TileMap` vía offsets validados en el
exportador host. Especificado en el roadmap §10 (chunks: header, palettes, bitplanes, copper
templates, patch tables, sprites, BOBs, tile metadata, collision, strings).

Coherente con el pipeline de assets ya existente: los assets generados van a
`out/assets/<pipeline>/` y se incrustan con `incbin`/include; UAF-R es el **formato binario de
esos chunks ya cocinados**, nunca algo que se depare en runtime.

## 9. Encaje con las fuentes externas

- **demoscene-repo-orig**: se portan **librerías** por oleadas (ver `LIBRARIES-CPP23-IMPORT-ROADMAP.md`).
  Con este diseño, `libgfx`/`libblit` se mapean a `Visual`/`CopperIntent`/`BlitJob`/`SpriteTemplate`
  en vez de importarse como objetos fugaces; el `.asm` (c2p, p61...) se conserva en `support/`.
- **amiga-bootcamp**: es fuente de técnicas e invariantes (auditada en `sprites.md`,
  `copper_programming.md`): multiplexado, color multiplexing, "chasing the raster",
  sprite-as-playfield, y los antipatterns que este diseño convierte en invariantes explícitos.
- **ACE**: es un *backend posible* (roadmap §1). En este diseño es una implementación alternativa
  de `PlatformBackend`, no tipos visibles al juego. Hoy se usa `MinimalBackend`; ACE entraría
  como segundo backend o como base de un takeover más agresivo.
- **Sevgi_Engine**: fuente de prior art para el sistema de plantillas/objetos; se ingiere con el
  protocolo de `DOC-MAP §6` (auditar → mapear → portar solo lo que aporta → descartar duplicado).

## 10. Orden de implementación propuesto

El riesgo no está en el objeto versátil completo, sino en el vocabulario del que todo cuelga.
Orden recomendado:

1. **`CopperIntent` + concept `Effect`** (este documento): tipos puros, testables en host.
   Hacer que `PaletteCycleEffect` cumpla `Effect` y validar el concepto con `static_assert`.
2. **Expandir `CopperScheduler`** para aceptar `CopperIntent[]` ordenado por franja (WAITs
   intercalados), conservando `Timeline` para presupuesto.
3. **`Visual` + `CopperIntent` sobre un actor** en una demo (evolución de un BOB con cambio de
   paleta/shift por línea), validada por `build -> run -> analyze`.
4. **`SpriteTemplate` sobre `SpriteManager`** y la decisión hardware/BOB (`VirtualSprite`,
   roadmap Fase 5), cubriendo multiplexado y color multiplexing.
5. **Playfields emitiendo `CopperIntent`** (simetría actor/playfield).
6. **UAF-R loader** y exportador host por chunks.

Cada paso se valida con el pipeline del proyecto (`tools/test-regression.sh`) y un `static_assert`
de contrato, aplicando la regla de evidencia de `AGENTS.md`.

## 11. Criterio arquitectónico (recapitulación)

Si un efecto, actor o playfield necesita tocar directamente un registro de Copper/Blitter/sprite,
falta una abstracción. El juego pide "agua con ciclo de paleta", "actor con shift por línea" o
"split de cielo"; los schedulers deciden si cabe en el frame y cómo materializarlo. Un commit que
añada un `MOVE`/`WAIT` fuera del `CopperScheduler` se considera una regresión de diseño.
