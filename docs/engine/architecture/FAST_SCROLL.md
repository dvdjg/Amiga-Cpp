# Política de scroll rápido (modo Sonic/Superfrog)

El núcleo X-Limited/XYLimited pinta la franja entrante con un **sub-paso atómico de 1 px**
(*paint-then-advance*): reparte el trabajo de Blitter a lo largo de `BLOCKWIDTH`/`BLOCKHEIGHT`
pasos para poder cambiar de dirección en cualquier píxel sin dejar huecos. Es la política
correcta para scroll fino, pero se queda corta cuando el juego avanza **varios tiles por frame**
(Sonic, Superfrog, overscan PAL a 50 fps). Esta ficha define una política de relleno
**compile-time** que mantiene el mismo núcleo y solo cambia *cuánto trabajo por frame* se hace y
*cuán anchas* son las bandas de guarda.

Encaja en el modelo de `PLAYFIELD_SCROLL_ARCHITECTURE.md` §2: es una política (tenant del
`ScrollStrategy`) sobre la superficie de anillo, no un playfield ni una escena nuevos. Los
invariantes del anillo (`XYLIMITED_ALGORITMO_GENERICO.md` §1.2) no cambian.

## 1. Regímenes de velocidad

| Régimen | Velocidad | Relleno | Guarda | Dirección |
|---|---|---|---|---|
| **Progresivo** (actual) | 1 px/frame | 1 sub-paso de 1 px; el trabajo se diluye | mínima (1 bloque de staging) | libre, en cualquier píxel |
| **TileBurst(1)** | 1 tile/frame (16 px) | una **columna/fila completa** por frame | 1 bloque de *lookahead* | se lacea a frontera de tile |
| **TileBurst(N)** | N tiles/frame | N columnas/filas por frame | ≥ N+1 bloques | se lacea a frontera de tile |
| **StripPrerender(C)** | > 2-3 tiles/frame | C columnas/filas pre-renderizadas, solo se mueven punteros | ≥ C+1 bloques | laces a frontera de tile |

Los tres últimos son el mismo mecanismo con distinto presupuesto: **pre-pintar por delante** en
la banda de guarda y luego avanzar la cámara, en lugar de pintar el píxel que se acaba de
revelar. La cámara avanza en **fronteras de tile** (paso grueso), lo que simplifica la costura:
en la frontera el *plane-shift* es 0 y la franja entra alineada.

## 2. El algoritmo actual y por qué es conservador

`XLimitedPlayfield::update_scroll` acepta hasta `max_step` px/frame, pero los aplica como
`max_step` sub-pasos de 1 px, cada uno con su blit mínimo (`scroll_engine.hpp`). A 16 px/frame son
16 blits pequeños por eje: mucho *overhead* de programación de Blitter para muy poco dato por
blit. Además la banda de guarda se dimensiona para el caso progresivo
(`EXTRAWIDTH = 32/64 px`, `EXTRAHEIGHT = 2*bloque`), insuficiente para pre-pintar varias columnas.

```text
  Progresivo (1 px/paso)              TileBurst (1 columna/frame)
  cam ──► [x][x][x][.][.][.][.][.]    cam ──► [C][C][C][C][.][.][.][.]
          ^ pinta el px revelado              ^ pre-pinta toda la columna
            (coste ∝ N sub-pasos)               antes de que la ventana la revele
```

## 3. Modelo: políticas compile-time

La estrategia `RingScroll` se parametriza con tres políticas (sin virtuals, sin `switch` runtime;
ver `CODING_STYLE.md`). Las tres son tipos/constantes NTTP:

```text
RingScroll<Axis, Direction, Fill, Guard>
  ├─ Fill  = Progressive | TileBurst<MaxTiles> | StripPrerender<Cols>
  ├─ Guard = guard_tiles (ancho/alto extra de la banda de guarda)
  └─ (Axis, Direction ya existen en el modelo objetivo)
```

Contrato nuevo del `ScrollEmitter` (§3.1 de `PLAYFIELD_SCROLL_ARCHITECTURE.md`): además de
"pinta la franja entrante 1 px", expone una operación de **pre-relleno**:

```text
ScrollEmitter
  ├─ emit_strip(target, axis, px)          // progresivo (1 px, plane-shift = px)
  └─ emit_tile_column(target, axis, n)     // n columnas/filas COMPLETAS en la guarda
                                           // (plane-shift = 0 en frontera)
```

`ScrollTarget` reporta la geometría de guarda (`guard_tiles`) y el `ScrollStrategy::step` decide,
según `Fill`, cuántas columnas pre-pintar y cuánto avanzar la cámara.

### 3.1 Cómo lo selecciona el desarrollador (implementado)

La selección es un **tipo** (`engine/include/eng/field/scroll_profile.hpp`); una línea en la demo:

```cpp
using Scroll = eng::field::ScrollFast2;   // o ScrollProgressive, ScrollFast1, ScrollFast4...
eng::field::XlimitedScene<kScrollConsts, eng::field::TileLayerMap, Scroll> scene {};
```

| Alias | Relleno | Paso | Guarda (tiles) |
|---|---|---|---|
| `ScrollProgressive` (defecto) | progresivo (1 px/sub-paso) | `max_step` de la config | la del fetch (32/64 px) |
| `ScrollFast1` | 1 tile/frame | 1 tile | 2 |
| `ScrollFast2` | 2 tiles/frame | 2 tiles | 3 |
| `ScrollFast4` | 4 tiles/frame | 4 tiles | 5 |

Un perfil a medida: `ScrollProfile<TileBurstFill<3>, GuardTiles<4>, /*DirectionLatched=*/true>`; el
`static_assert` del perfil exige `guarda >= relleno + 1`. El perfil por defecto **no impone** paso
ni guarda, así que reproduce exactamente el comportamiento clásico.

**Estado de implementación.**

- **Hecho**: selección estática (`ScrollProgressive`/`ScrollFastN`, `scroll_profile.hpp`); paso por
  frame (`max_step = fill_tiles * tile`); guarda X (el perfil pide más ancho que el fetch);
  **avance por tiles completos** (`snap_to_tiles`) con la **dirección laceda a frontera**
  (`direction_latched`, seguro gracias a que el paso es múltiplo de tile); **staging vertical por
  perfil** (`y_staging_tiles`: 2 en el clásico, `guard_tiles` en los rápidos); **avance en ráfaga
  por X** (`ScrollEngine::burst_right`: calcula la geometría del cruce UNA vez en lugar de por cada
  píxel → menos procesamiento por px). Tests HOST-032/033/034; demos 111 en `ScrollFast1`/
  `ScrollFast2` avanzan 16/32 px por frame sin huecos y la 111 por defecto no cambia.
- **Alcance del "burst"**: en el corkscrew cada fila del anillo es un **blit de bloque** distinto.
  `burst_right` emite **el mismo número de blits** que los sub-pasos de 1 px (probado equivalente en
  HOST-034: dibujos, `save_word` y estado); su valor es (a) **menos cálculo por px** (geometría del
  cruce una vez) y (b) ser el **punto donde vivirá la fusión de tiles** (un blit por varias filas).
- **Pendiente (parked)**:
  1. **Fusión de tiles** en `burst_right`: emitir un blit por varias filas de bloque cuando el
     origen/destino son contiguos (requiere resolver el salto de bloque en el banco/interleave).
  2. **`StripPrerender`**: anillo de estrips pre-renderizados y solo punteros (>2-3 tiles/frame).
  3. **Burst en el resto de sentidos**: `burst_left` y burst vertical (hoy la ráfaga es solo X
     derecha; izquierda/arriba/abajo siguen en sub-pasos de 1 px, correctos).
  4. **Demo corkscrew con Y rápido** (`display_height=0` para que el perfil derive el staging y
     `ScrollConsts.display_height` coherente): única vía de validar en hardware la guarda Y.
  5. **Tear-free** a alta velocidad (pre-relleno en blanking o doble buffer del fondo).

## 4. Corrección: qué invariantes hay que preservar

1. **Nunca mostrar un píxel sin pintar.** La franja debe estar pintada en la guarda **antes** de
   que la ventana la revele:
   `guard_tiles >= ceil(max_px_por_frame / tile) + 1` (el `+1` cubre la franja en vuelo).
2. **Lookahead ≥ velocidad.** En `TileBurst(N)`, cada frame se pre-pintan las N columnas que la
   ventana va a revelar en ese frame y en el siguiente.
3. **Costura / saveword.** El guardado y restauración de la word pisada solo se hace al cambiar de
   dirección. Con dirección laceda a frontera de tile, el cambio de dirección es un caso raro
   (no cada píxel), lo que elimina casi todos los casos especiales del 8-way.
4. **El anillo no cambia de invariantes.** `display_height = viewport_visible + staging`, el wrap
   de staging en el bucle del display y el bias visible siguen aplicando
   (`XYLIMITED_ALGORITMO_GENERICO.md` §1.2). La guarda ampliada es **ancho de bitmap**, no una
   segunda regla de dimensionado del anillo.

## 5. Tear-free a alta velocidad

A 1 px/frame el *paint-then-advance* basta porque la cámara no alcanza lo no pintado. A varios
px/frame, si el pre-relleno ocurre **después** de que el haz pase por la zona, se ve un frame
parcial. Opciones (de menor a mayor coste):

- **Pre-relleno con lookahead** y ventana de ejecución fuera del visible (VBlank/blanking): el
  presupuesto de blanking ya se usa hoy (`make_bg_plane_copy_rect_job` en la demo 112).
- **Doble buffer** del bitmap (o del plano de fondo, soft DPF): el pre-relleno escribe el buffer
  trasero y el display lee el delantero; el *swap* es un cambio de puntero.
  Es el mismo mecanismo de `PlaneView`/`SoftDpfComposition` (ver §8).
- **`DMAF_BLITHOG` temporal** durante el burst: el Blitter no cede el bus al resto del DMA
  mientras rellena la guarda crítica (más ancho de banda efectivo, peor para bitplanes/audio en
  ese tramo).

## 6. Optimizaciones que combinan

- **Blitter + CPU**: alternar tiles (unos por Blitter, otros por CPU) para solapar trabajo; útil
  cuando el burst compite con el display.
- **Direcciones de destino precalculadas** de la banda de guarda: evitar recalcular módulos y
  punteros cada frame (cachear la base por columna/fila del anillo).
- **Ancla del burst** en frontera de tile: `plane-shift = 0`, sin barrel shifter.
- **`StripPrerender`**: para > 2-3 tiles/frame, mantener C columnas/filas ya dibujadas y solo mover
  los punteros de lectura; equivale a un anillo de estrips pre-renderizados.
- **Tiles más grandes (32×32)** como "paso grueso" si el arte lo permite: menos columnas por
  frame para la misma velocidad de pixels (ya analizado en el roadmap, F6).

## 7. Presupuesto y coste

El coste por frame pasa de `N` blits pequeños (progresivo) a `ceil(px/tile)` blits de columna
completa, más el dato por blit:

| Régimen | Blits/frame (X) | Dato por blit | Comentario |
|---|---|---|---|
| Progresivo 16 px | 16 | 1 tira de 1 px | máximo overhead de programación |
| TileBurst(1) | 1 | columna completa (16 px de alto de bloque) | mínimo overhead, mismo dato |
| TileBurst(2) | 2 | 2 columnas | guarda ≥ 3 bloques |
| StripPrerender(4) | 0 (solo punteros) | — | guarda ≥ 5 bloques; memoria de strip |

El trabajo total es el mismo (una columna por tile cruzado); lo que cambia es el **número de
operaciones de Blitter** y el **ancho de guarda**. A igualdad de pixels, menos blits = menos
overhead, que es justo lo que limita al engine en los cruces (ver F6 del roadmap).

## 8. Relación con lo existente

- **`max_step`** (`XLimitedPlayfield`) pasa a expresarse como `Fill::TileBurst<N>`; el caso
  `Progressive` reproduce el comportamiento actual (compatible hacia atrás).
- **Soft DPF / RoboCod**: el doble buffer del plano de fondo ya existe (`PlaneView`,
  `SoftDpfComposition`); el modo rápido lo reutiliza para el *tear-free*.
- **Tiles 32×32** (F6 del roadmap): si el arte lo permite, reduce las columnas por frame; la
  política es ortogonal al tamaño de tile (`guard_tiles * tile`).
- **F6 «Sonic»**: la medición E1 (a 5-6 px/frame no hacía falta draw-ahead) corresponde a
  `TileBurst(1)` con lookahead de 1; el modo rápido generaliza esa medición.

## 9. Fases y criterios de aceptación

1. **Política y contrato**: `Fill`/`Guard` en el modelo objetivo y en `ScrollTarget`/`ScrollEmitter`
   (`PLAYFIELD_SCROLL_ARCHITECTURE.md`), con `Progressive` = actual.
   **Hecho (selección estática)**: `scroll_profile.hpp` (`ScrollProfile`, `ScrollProgressive`,
   `ScrollFastN`) conectado a `XLimitedPlayfield`/`XlimitedScene` (paso + guarda X); HOST-032.
2. **`TileBurst(1)`**: pre-pintar 1 columna/fila y avanzar 1 tile; guarda de 1 bloque; dirección
   laceda a frontera. Validar continuidad a 16 px/frame en 112/202 (o demo nueva) sin huecos.
3. **Guarda ampliada + `TileBurst(N)`**: parametrizar `EXTRAWIDTH`/`EXTRAHEIGHT` por `guard_tiles`
   y validar 2-4 tiles/frame, con telemetría de `blit_jobs`/`blit_words` por frame.
4. **Tear-free**: pre-relleno en blanking o doble buffer del bitmap del fondo; medir con `fps.mjs`
   y comparar borde constante (sin flash) contra el modo progresivo.
5. **`StripPrerender`** si un juego pide > 2-3 tiles/frame: anillo de estrips y solo punteros.

Criterios: build `--debug/--release` OK, tests host verdes (el `ScrollTarget`/`Emitter` con mock),
sin regresión de fps en 107/201/202, y ausencia de huecos/tearing en secuencia a la velocidad
objetivo.

## 10. Referencias

- Modelo objetivo (estrategias, guarda, contrato algoritmo↔superficie):
  `PLAYFIELD_SCROLL_ARCHITECTURE.md` §2/§3.1.
- Algoritmo e invariantes del anillo: `XYLIMITED_ALGORITMO_GENERICO.md`.
- Estado/medición del modo rápido (F6) y tiles 32×32:
  `docs/guides/roadmap/ROADMAP_UNIFICADO.md`.
- Plan de refactor por fases: `docs/guides/roadmap/REFACTOR_PLAYFIELD_SCROLL.md`.
- Soft DPF / doble buffer: `docs/reference/amiga/techniques/robocod-layered-scroll.md`,
  `docs/debugging/112_bg-flicker.md`.
