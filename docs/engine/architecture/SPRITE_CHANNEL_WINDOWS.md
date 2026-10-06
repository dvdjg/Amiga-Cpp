# Sprites hardware por ventanas de reprogramación (fondos y objetos)

Este documento especifica cómo el engine reparte los **8 canales de sprite** entre **ventanas de reprogramación** que pueden usar **técnicas distintas**, de modo que en una misma pantalla convivan un fondo por sprites (Risky Woods / Free Form), objetos tradicionales (sprite libre o BOB) y attached de 15 colores, **sin que el juego conozca registros ni canales**.

Es la pieza que convierte el multiplexado de sprites (por canal y por objeto) en un **reparto bidimensional canal × intervalo de líneas**: cada ventana reclama los canales que necesita para su fondo y los demás quedan libres para objetos en ese intervalo, reutilizables por encima y por debajo. Complementa `OBJECT_SYSTEM.md` (representación y degradación de objetos) y `VISUAL_EFFECT_SPRITE_DESIGN.md` (`Visual`, `CopperIntent`, `HwSpriteTemplate`), y se apoya en la referencia de hardware `docs/reference/amiga/techniques/sprite-layer.md` y `sprite-horizontal-multiplex.md`.

Estado: **diseño objetivo**. Las piezas marcadas EXISTE están implementadas; PROPUESTO es el contrato a implementar.

## 1. Principio rector

El chipset tiene **8 canales** de sprite, y cada uno es una secuencia DMA que el Copper puede **reprogramar en cualquier línea**: rearmar el puntero (`SPRxPT`) o reescribir `SPRxPOS`/`SPRxCTL`/`SPRxDATA`/`SPRxDATB`. Por tanto **en hardware no existen "bandas"**: lo que aquí llamamos **ventana** (`SpriteChannelWindow`) es solo la agrupación de un intervalo `[top,bottom)` con una **corrida de canales** que, durante ese tramo del barrido, se reprograma de forma semejante para un **fondo** por sprites.

El engine no debe obligar a elegir **una** técnica para toda la pantalla: la unidad de decisión es la **ventana**, y varias ventanas pueden **solaparse en vertical** mientras usen **canales distintos** (cada canal es independiente). El único límite real es *por canal*: dos fondos no pueden reusar el mismo canal en líneas solapadas (lo detecta `ChannelBusy`).

```text
  una pantalla (320×256)
  ┌───────────────────────────────────────────────┐
  │ ventana A  [0,60)     objetos: sprite/BOB      │  8 canales libres para objetos
  ├───────────────────────────────────────────────┤
  │ ventana B  [60,100)   FONDO Risky Woods        │  canales 0..5 = fondo
  │                       + 2 objetos              │  canales 6..7 libres para objetos
  ├───────────────────────────────────────────────┤
  │ ventana C  [100,140)  FONDO Free Form (8 ch)   │  sin canales libres
  ├───────────────────────────────────────────────┤
  │ ventana D  [140,200)  objetos: sprite/BOB      │  los 8 canales se reutilizan
  └───────────────────────────────────────────────┘
```

El diagrama muestra ventanas disjuntas por claridad; también pueden solaparse en vertical si usan canales distintos. La regla de oro se mantiene: la aplicación describe **contenido portable** (`Visual`), **posición** y **prioridad**; el engine **arbitra** canales y técnicas, y puede degradar un objeto a BOB sin que la app lo sepa.

## 2. Vocabulario

| Tipo | Cometido | Dónde |
|---|---|---|
| `SpriteWindowTechnique` | técnica de fondo por sprites de una ventana (`None`/`Layer`/`RiskyWoods`/`FreeForm`) | `graphics/sprite_channel_window.hpp` |
| `SpriteChannelWindow` | ventana `[top,bottom)` que reprograma una corrida de canales para su fondo | `graphics/sprite_channel_window.hpp` |
| `SpriteChannelLedger` | ocupación **canal × intervalo de líneas** (el recurso compartido) | `graphics/sprite_channel_window.hpp` |
| `plan_sprite_windows` | valida y vuelca las ventanas al ledger | `graphics/sprite_channel_window.hpp` |
| `SpriteAllocator::assign` | reparte los `SpriteIntent` (objetos) en los canales libres del ledger | `graphics/sprite_allocator.hpp` |

`SpriteChannelWindow` no es un `scene::Band` (geometría de display) ni un `copper::BandScope` (reserva de registros de Copper): es la **reserva de canales de sprite** durante un intervalo. Los tres conviven; el planner puede derivar los tres del mismo tramo.

## 3. Modelo de recursos: el ledger canal × intervalo

El multiplexado vertical de objetos ya se modela con un único escalar por canal (*ocupado hasta la línea X*). Ese modelo **no basta** cuando un fondo ocupa los canales en un intervalo **intermedio**: un objeto por encima de la ventana debe poder usar el canal, aunque el fondo lo ocupe más abajo.

El `SpriteChannelLedger` guarda, por canal, una lista corta y ordenada de intervalos `[top,bottom)` ocupados (por construcción hay pocas ventanas, así que caben en un array fijo, sin heap):

```text
  canal 0  ├──────── fondo Risky Woods [60,100) ────────┤
  canal 1  ├──────── fondo Risky Woods [60,100) ────────┤
  ...
  canal 5  ├──────── fondo Risky Woods [60,100) ────────┤
  canal 6  (libre)   ← los 2 canales que quedan para objetos en [60,100)
  canal 7  (libre)
```

Consultas puras: `free(ch,top,bottom)`, `free_channel(top,bottom)`, `free_run(count,top,bottom)`, `free_mask(line)`. Los objetos **no** se guardan en el ledger (serían demasiados intervalos por canal); el asignador los multiplexa con su propio escalar y usa el ledger **solo como pre-ocupación de los fondos**.

## 4. Reparto híbrido

El reparto tiene dos fases, ambas puras (sin hardware):

1. **Fondos primero.** `plan_sprite_windows` valida las ventanas (rangos, corridas de canal, alineación de attached) y las reserva en el ledger con `occupy_run`. Las ventanas pueden venir en cualquier orden y **solaparse en vertical si usan canales distintos**; el único conflicto real (mismo canal en líneas solapadas) se rechaza con `ChannelBusy`.
2. **Objetos después.** `SpriteAllocator::assign(intents, out, ledger)` recorre los `SpriteIntent` ordenados por `top` y asigna a cada uno el **primer canal que está a la vez libre en el ledger para su intervalo y no ocupado por un objeto anterior** (`busy_until[ch] <= top`). Si no hay canal, el intent va a `as_bob` (degradación transparente a BOB).

Así, en la ventana B del ejemplo, un objeto solo puede ocupar los canales 6 o 7; con tres objetos solapados, el tercero degrada a BOB. Por encima y por debajo de la ventana, los 8 canales vuelven a estar disponibles (el escalar `busy_until` se agota y el ledger no tiene intervalos ahí).

```text
  assign(intents, out, ledger):
    busy_until[8] = {0}
    para cada intent (ordenado por top):
      canal = primer c con  busy_until[c] <= top  Y  ledger.free(c, top, bottom)
      si no hay canal: out = {0, as_bob=true}
      si lo hay:       busy_until[canal] = bottom
```

Las **tiras horizontales** y los **pares attached** usan las variantes de corrida (`free_run`/`free` sobre el canal par e impar) y actualizan `busy_until` de todos los canales que toman.

## 5. Técnicas de fondo

Cada ventana declara su técnica; el driver correspondiente emite la copperlist del intervalo (no el ledger, que solo reserva). Las tres técnicas son variantes de un mismo patrón: **los canales se reposicionan y/o recargan su DATA dentro de la línea**.

| Técnica | Canales | Qué hace por línea | Patrón | Coste Copper |
|---|---|---|---|---|
| `Layer` | N contiguos | un `WAIT` + `SPRxPOS/CTL/DATB/DATA` por canal; una instancia por canal | un patrón de 16 px por canal (N×16 px) | bajo (1 armado/canal) |
| `RiskyWoods` | N contiguos (en pares attached) | arma el canal una vez por DMA y **solo mueve `SPRxPOS`** repetidamente cada ≥24 px | **repetitivo** (64 px con attached) | medio (1 MOVE por repetición) |
| `FreeForm` | N contiguos | por cada instancia: `WAIT` + `SPRxPOS` + `SPRxDATA/DATB` (datos **distintos** por columna) | **libre**, sin repetición | alto (4 MOVEs por instancia) |

Detalle de coste y carrera contra el haz: `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`. Regla de hardware que el driver debe respetar: **cada canal necesita una estructura DMA válida con cabecera `POS`+`CTL` y terminador** (si no, el DMA avanza por memoria y deja una columna fantasma; ver `docs/reference/emulators/winuae/sprite-dma.md`).

La variante `RiskyWoods` y la `FreeForm` son **carrera contra el haz**, no presupuesto por frame: el driver debe verificar la separación mínima (≥24 px) y el número de MOVEs por línea. La `Layer` es la más barata y la base del `effects::SpriteLayer` actual. Para una capa o HUD con **imagen propia por scanline** (Parasol Stars / Brian the Lion) el módulo es `graphics/sprite_line_layer.hpp` (`SpriteLineLayer`).

### La ventana de canal y la banda de display son ejes distintos

`SpriteChannelWindow`/`SpriteChannelLedger` reparten **canales de sprite** por intervalo (quién puede dibujar en cada canal). El contrato `Effect` de `SpriteLayer`/`RiskyWoodsLayer` reclama además una **banda de display** (`copper::BandScope` + `Plan::reserve_band`) para detectar efectos que escriben **los mismos registros** en las mismas líneas. Son ejes independientes: la banda de display es un tramo de raster del Copper; la ventana es ocupación canal × intervalo. Un efecto de fondo por sprites declara en su `BandScope` la **máscara de sus canales** (`graphics::sprite_channel_register_mask`), de modo que dos fondos en **canales distintos** pueden **solapar scanlines** sin conflicto; si comparten canal —o si el otro efecto reclama "cualquier registro" (`register_mask == 0`)— `reserve_band` los marca en conflicto.

## 6. Attached (15 colores)

Dos canales del **mismo par** (0+1, 2+3, 4+5, 6+7) se unen poniendo el bit `ATTACH` en el `SPRxCTL` del impar: el par pasa de 2 objetos de 3 colores a **1 objeto de 15 colores**. En una ventana:

- `SpriteChannelWindow::attach = true` exige `channel_first` par y `channel_count` par (la ventana consume pares completos).
- El driver emite los dos canales del par con `SPRxCTL` del impar en `ATTACH` (bit 7) y la **misma** `SPRxPOS`/`SPRxCTL` de rango; la DATA se reparte entre los 4 bitplanes (el par aporta bits 0–1, el impar bits 2–3).
- La paleta del par vive en `COLOR16–31`; cambiar el color de un canal afecta a **su par** ("Color Bleed", `sprite-layer.md` §3).

El motor declara `attach` en `SpriteConfig`, `HwSpriteTemplate`, `SpriteIntent` y `HwSpritePlacement`; la proyección de plantilla a intenciones, el asignador de pares y el cableado en la emisión (`SpriteManager::apply` y `emit_template_into`) existen. El **cocinado de la DATA de 4 planos** está en `graphics/sprite_attached.hpp` (`cook_attached_pair`): divide el arte en las dos **estructuras DMA** (par = planos 0-1; impar = planos 2-3 + `ATTACH`, con terminador) sobre memoria **Chip tipada** (`ChipView<SpriteTag>`), cubierto por HOST-427. La demo `214_attached_object` valida el objeto de 15 colores en vivo (par *attached* + contraste de chispas de 3 colores, con los sprites delante del playfield).

**End-to-end por el camino de actores:** un `Visual` con `attached = true` (4 planos, `w <= 16`) produce **dos intents** contiguos (`build_sprite_intents`), el allocator asigna la pareja y `compose_sprites` cocina el par en el pool Chip del llamador (`SpriteComposeScratch::cooked` / `SpriteScene::set_cooked_pool`) publicando **dos placements**. El armado usa `SpriteManager::emit_placements_into` (primera config de cada canal en una línea temprana + **rearme por franja** del multiplexado vertical). Cubierto por HOST-072/HOST-428 y por la demo `216_attached_actors` (dos gemas de 15 colores + chispas), validada con secuencia y visión local.

## 7. Animación del bitmap del sprite (estilo Jim Power)

Un sprite hardware lee su DATA de Chip RAM; **animar** un sprite es cambiar el puntero `SPRxPT` (o recargar `SPRxDATA/DATB`) por frame. El engine tiene el contenido animado (`Animation`/`Frame`, `Visual::frame_count`/`frame_stride` y `actor_current_frame`).

**Contrato (implementado):** `compose_sprites` publica la DATA del **frame vigente** dentro de la hoja (`pixels.data() + índice * frame_stride`), igual que un BOB animado; el desplazamiento sale del `frame_stride` (0 = sin animación/base y si la hoja no cubre el frame se cae a la base, rechazo controlado). Cada vez que la animación avanza, la siguiente composición apunta al frame nuevo y el emisor rearma el canal con esa DATA. Cubierto por HOST-072 (`test_compose_sprite_frame_data`) y por la demo 054 (hoja de 2 frames, barras que cambian de anchura por frame; validado en vivo con gate de píxeles).

## 8. API unificada de objetos (sprite libre / attached / BOB)

El juego describe objetos con **un solo descriptor** (`ActorDesc`: `Visual`, posición, `z`, `sprite_priority`, animación, políticas) y llama **una sola** operación de composición. El engine decide la materialización:

```text
  ActorDesc (contenido + posición + prioridad)
      │  compose_sprites(plan, store, ctx, ledger)
      ├── cabe como sprite (canal libre en su intervalo) ─► HwSpritePlacement ─► SpriteManager
      │        (libre = 1 canal; attached = par de canales; tira = corrida)
      └── no cabe (sin canal o sin presupuesto) ──────────► BOB (BlitJob en el FramePlan)
```

El **ledger** es la única entrada nueva: la misma llamada sirve para una ventana con fondo (canales reducidos) y para una sin fondo (8 canales). Así, "sprite libre", "sprite combinado/attached" y "BOB" son **políticas del mismo camino**, no APIs distintas. En el engine, `scene::compose_sprites` recibe el ledger como parámetro opcional (`eng::Ref<const SpriteChannelLedger>`, por defecto nulo): con él, cada actor solo puede usar los canales libres de su intervalo. El `BobLayer`/`FastBobLayer` (`scene/bobs.hpp`) sigue siendo la capa ligera de BOBs puros, pero el camino de objetos con degradación es `compose_sprites`.

## 9. Estado y fases

| Pieza | Estado | Dónde |
|---|---|---|
| Multiplexado vertical de objetos + degradado a BOB | EXISTE | `sprite_allocator.hpp`, `actor_sprite.hpp` |
| Tira horizontal de canales contiguos | EXISTE | `SpriteIntent::strip_*`, `SpriteAllocator` |
| Pares attached (asignación) | EXISTE | `sprite_allocator.hpp` |
| `attach` en `SpriteConfig`/`HwSpriteTemplate`/`HwSpritePlacement` | EXISTE (declarado) | `sprite.hpp`, `sprite_manager.hpp` |
| `attach` cableado en la emisión (`apply`/`emit_template_into`) | EXISTE | `sprite_manager.hpp` |
| Ledger canal × intervalo (`SpriteChannelLedger`) y `plan_sprite_windows` | EXISTE | `graphics/sprite_channel_window.hpp` (HOST-416) |
| Reparto híbrido (`SpriteAllocator::assign` con ledger) | EXISTE | `sprite_allocator.hpp` (HOST-416) |
| Canal preferido, prioridad (`assign_rank`), grupos con trayectoria y ocupación exacta por línea | EXISTE | `sprite_allocator.hpp` (HOST-003) |
| Límites de hardware de sprites (canales, reuso, planos) | EXISTE | `graphics/sprite_limits.hpp` |
| Driver de fondo `Layer` (8 canales, una instancia/canal) | EXISTE | `effects::SpriteLayer` (`api/effects.hpp`) |
| Capa/HUD por parcheo de POS+DATA por línea | EXISTE | `graphics/sprite_line_layer.hpp` (HOST-418) |
| Driver de fondo `RiskyWoods` (reposición repetida) | EXISTE | `effects::RiskyWoodsLayer` (`api/effects.hpp`, HOST-417) |
| Armado de **objeto** de sprite (PT/POS/CTL en una línea temprana; `ATTACH` en el impar) | EXISTE | `SpriteManager::arm_object` + `emit_armed_into` (`sprite_manager.hpp`, HOST-428; demos 054/214) |
| **Armado de placements + rearme vertical** del canal reutilizado (multiplexado del allocator) | EXISTE | `SpriteManager::emit_placements_into` (HOST-428; demo 216) |
| **Plantilla de franjas por actores** (cadena al mismo canal + rearme + paleta por franja) | EXISTE | `ActorDesc::sprite_template`, `chain_id` en `SpriteIntent`/allocator y `SpritePaletteEvent` (HOST-003/072/428; demo 217) |
| **Par *attached* end-to-end por actores** (dos intents/placements + cocinado desde `Visual`) | EXISTE | `actor_store.hpp`/`actor_sprite.hpp` + `SpriteScene::set_cooked_pool` (HOST-072/427/428; demo 216) |
| Driver de fondo `FreeForm` (datos distintos por columna) | PROPUESTO | driver de fondo |
| Animación del bitmap del sprite (§7) | EXISTE | `compose_sprites` (frame vigente por `frame_stride`; HOST-072; demos 054/216) |
| Demo con gate visual del híbrido | EXISTE | `demos/techniques/amiga/sprites/216_attached_actors` (secuencia + Ollama) |

## 10. Trampas de hardware que el sistema debe absorber

- **Una estructura DMA válida por canal** (cabecera + terminador): sin ella el DMA del canal lee basura como cabecera (columna fantasma). Ver `winuae/sprite-dma.md`.
- **Separación mínima ≥24 px** entre usos del mismo canal en una línea (carrera Copper↔haz).
- **`SPRxPOS`/`SPRxCTL` son write-only** en la práctica: las posiciones y la DATA viven en Chip RAM.
- **X solo en píxeles lores pares** (`HSTART` va ÷2).
- **La paleta del par es compartida**: dos objetos que compartan canal no pueden tener paletas distintas en líneas solapadas.
- **`SPREN` es un único bit**: un canal se apaga con `SPRxPT = 0` o `VSTOP <= VSTART`.

## 11. Referencias

- `docs/reference/amiga/techniques/sprite-layer.md` — formato, colores, attached, prioridad, multiplexado.
- `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md` — Risky Woods / Free Form y coste por línea.
- `docs/reference/emulators/winuae/sprite-dma.md` — estructura DMA y columna fantasma.
- `OBJECT_SYSTEM.md` — representación, transparencia y degradación de objetos.
- `engine/include/eng/graphics/{sprite_channel_window,sprite_allocator,sprite_line_layer,sprite_limits,sprite,sprite_manager}.hpp`.
