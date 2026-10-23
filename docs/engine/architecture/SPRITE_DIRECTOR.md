# SpriteDirector: fachada de sprites por intención

`eng::scene::SpriteDirector` (`engine/include/eng/scene/sprite_director.hpp`) es la fachada pública nueva para administrar sprites hardware y BOBs. El juego declara **contenido** (`VisualId`/`CompositeId`) y **qué pretende** (`SpriteHint`); el engine decide canales, reuso, paleta, BOB y coste. `SpriteScene` y `CompositeScene` quedan como implementación avanzada para código histórico; no deben usarse en código nuevo y se retirarán de la fachada pública tras migrar las demos.

## Capacidades de plantilla

`SpriteDirector<MaxSprites, MaxVisuals, MaxComposites, MaxBands, MaxPlacements, MaxPaletteEvents>` fija en compilación todas las capacidades; no hay heap ni crecimiento dinámico. Son `u16` y deben ser mayores que 0; el total `MaxSprites + MaxComposites` debe caber en el pool (≤ 65534) y `MaxPlacements` debe cubrir el peor caso del frame.

| Parámetro | Por defecto | Qué limita | Coste en el objeto | Efecto si se queda corto |
|---|---:|---|---|---|
| `MaxSprites` | 64 | Entidades simples y actores internos de `SpriteScene`. | Pool de actores + arrays de intents/slots/placements (≈4 por actor) + cadenas. | `add` devuelve id inválido. |
| `MaxVisuals` | 128 | `Visual` simples registrados (compartibles). | Pool de descriptores `Visual` + generaciones/free-list. | `add_visual` devuelve id inválido. |
| `MaxComposites` | 16 | `CompositeVisual` y entradas de `CompositeScene`. | Pool de compuestos + escena de partes/intents. | `add_composite`/`add` rechazan. |
| `MaxBands` | 16 | `BandPalette` copiadas por `set_band_palettes`. | `m_bands[MaxBands]` (top/height/vistas/scroll). | Las sobrantes se ignoran; el reporte no las cuenta. |
| `MaxPlacements` | 312 | Buffer combinado de `HwSpritePlacement` del frame. | `MaxPlacements × sizeof(HwSpritePlacement)` + ordenación por inserción. | Se trunca y el reporte marca `Truncated`/`ok == false`. |
| `MaxPaletteEvents` | 64 | Buffer de eventos de paleta de actores y bandas. | Dos buffers de `MaxPaletteEvents × sizeof(SpritePaletteEvent)`. | Se omiten eventos y el reporte avisa `Truncated`. |

La huella del objeto crece con estos arrays y con las escenas internas; elegir valores ajustados al juego reduce BSS. `kMaxEntities` es la suma de entidades y se usa como capacidad del pool generacional; los `static_assert` de `SpriteDirector` y de `eng::util::Pool` cierran los valores no representables. La documentación del header detalla cada parámetro con su rango, default, motivo y coste.

Ejemplo canónico (ajustar capacidades al juego, no al máximo):

```cpp
eng::scene::SpriteDirector<32u, 64u, 8u, 8u, 160u, 32u> director;
const eng::scene::VisualId nave = director.add_visual(nave_visual);
const eng::scene::SpriteId id = director.add({
	.content = eng::scene::SpriteVisualRef::simple(nave),
	.hint = {.policy = eng::scene::SpritePolicy::Sprite},
	.x = 100, .y = 40,
});
```

## Contenido y handles

`SceneAssets<MaxVisuals, MaxComposites>` reúne `VisualStore` y `CompositeStore`. Ambos usan `eng::util::Pool` con handles generacionales: `VisualId`/`CompositeId` dejan de ser válidos al reciclar el slot, así que una referencia vieja no “resucita”. El store copia la **vista descriptora** (`Visual`/`CompositeVisual`), no los píxeles; el llamador mantiene vivos los buffers. `SpriteVisual` es la vista resuelta y `SpriteVisualRef` la referencia por valor; la API no expone `const Visual*`.

## Intención

`SpriteHint` agrupa `policy` (Auto/Sprite/Bob/Cpu/Layer), `attach`, `reuse` (Auto/VerticalDma/Chain/PerLine/Horizontal/None), `gap_lines`, `channel`, `rank`, `sprite_priority`, grupo y tira horizontal (`strip_id`/`strip_index`/`strip_span`). `choose_sprite_emitter` elige `Rearm`, `DmaChain`, `PerLine`, `Horizontal`, `Bob` o `Cpu` a partir de los hints y de la infraestructura disponible. `SpritePolicy::Bob`, `Reuse::None` y `Attach::Off` sobre 4 planos usan `ActorDesc::force_bob`, que el compositor marca `as_bob` sin consumir canal.

## Composición

`SpriteDirector::emit` compone primero los compuestos (`CompositeScene`) y después los simples (`SpriteScene`) con un `SpriteChannelLedger` compartido: los canales ocupados por un compuesto no se ofrecen a un simple. Después combina placements (ordenados por `vstart`), eventos de paleta y la presentación en una sola llamada a `SpriteManager::emit_placements_into`. Las cadenas DMA se preparan para la escena simple; un canal con placements compuestos no usa cadena.

El gap vertical de 1 línea lo exige `SpriteAllocator` (y `SpriteDmaChain`). El reporte cuenta `gap_violations` si una combinación externa lo rompe o si un hint pide `gap_lines < kSpriteVerticalGapLines`.

## Paletas por banda

`BandPalette { top, height, sprites, playfield, scroll_y }` se evalúa cada frame en `top + scroll_y`. El playfield se añade al `copper::Plan` como `PaletteLine`; los sprites se devuelven como `SpritePaletteEvent` (registros `COLOR16..31`) para que el emisor los fusione con los rearmes. `emit_band_palettes` avisa de bandas fuera de rango o solapadas (`BandOutOfRange`/`BandOverlap`). `CopperCue` es la forma genérica `{line, first, colors}`.

## Reporte y presupuesto

`SpriteLoadReport` es el contrato único de runtime y preflight: entidades, partes, HW, BOB, degradados, pares *attached*, tiras, bandas, canales, reuso máximo, pico por scanline, gap, intents/palabras de Copper, eventos de paleta, trabajos/palabras de Blitter, emisores y bits de aviso. `SpriteBudget` fija canales, scanline, palabras de Blitter, intents de Copper, eventos de paleta, gap, solapes de banda y deadline de Blitter. `apply_budget` marca avisos y `ok`.

`evaluate_static(Span<const SpriteStaticDesc>, SpriteBudget)` es `constexpr`: permite `static_assert` de una escena estática con el mismo gap y los mismos topes. `SpriteStaticDesc` no contiene píxeles ni punteros.

## Single buffer, Blitter y presentación

`BlitDeadline` acota palabras y orden de ventana; `blit_deadline_window` reutiliza `safe_blitter_window` para un blit de CPU. El director comprueba el deadline contra las palabras de Blitter medidas por delta del `FramePlan`. La política de presentación (`SpritePresent`) se retiró de la API hasta que exista una materialización propia; el llamador decide cuándo publica la copperlist con su driver.

## Reuso avanzado

`Reuse::Horizontal` se materializa como tira de canales contiguos cuando `strip_span > 1` (varios tramos de 16 px). `Reuse::PerLine` usa una `SpriteLineLayer` opcional (`SpriteDesc::line_layer`), que se emite tras los placements en `present`; si falta, el reporte avisa `PerLineMissing`. Las capas `SpriteManager`, `SpriteDmaChain`, `SpriteLineLayer`, `SpriteBend`, `FreeForm`, `RiskyWoods` y `FastBob` se mantienen como estrategias; el director no las fusiona.

## Límites

- `VisualStore` guarda la vista descriptora; si se cambia el contenido de un `Visual` ya dado de alta, el actor simple conserva la copia anterior hasta rehacer el alta (o usar el camino avanzado).
- `CompositeScene` mantiene su propio estado y política por parte; el director no replica hitboxes/eventos de Copper de compuestos en la API simple.
- `Reuse::Horizontal` cubre la tira entre canales; el multiplexado horizontal del **mismo** canal sigue siendo una técnica avanzada de `SpriteLineLayer`/`SpriteBend`.
- `SpriteLoadReport::copper_words_estimate` es una estimación (`intents·4 + colores·2`); la verdad materializada es `copper::Plan::words()` tras `materialize()`.

## Tests y demos

- HOST-485: handles, simple+compuesto, attached, tira, BOB forzado, gap, banda con scroll, reporte, `evaluate_static` y `present`.
- Demo de estrés: `demos/techniques/amiga/sprites/225_sprite_director_stress`.
