# Variantes de scroll (matriz ScrollingTricks → engine)

Mapa de las variantes del paquete de referencia **ScrollingTricks** (Steger) a la infraestructura
del engine (`eng/field/*`, `eng/scene/*`), con el hueco de cada una y las especializaciones que
piden los juegos objetivo. Referencia externa: `ScrollingTricks/Docs/algorithms-uk.html`.

## 1. Matriz de la referencia

| Variante | Sentido | Bitmap (W×H) | Fetch | Video-splitting |
|---|---|---|---|---|
| `XUnlimited` | ←/→ | 704×256 | 1x 2x 4x | no |
| `XLimited` | ←/→ | 352×mapa | 1x 2x | no |
| `XLimited_64` | ←/→ | 384×mapa | 1x 2x 4x | no |
| `YUnlimited` | ↑/↓ | 320×576 | 1x 2x 4x | no |
| `YUnlimited2` | ↑/↓ | 320×288 | 1x 2x 4x | sí |
| `XYLimited` | cualquiera | 352×mapa | 1x 2x | sí |
| `XYLimited_64` | cualquiera | 384×mapa | 1x 2x 4x | sí |
| `XYUnlimited` | cualquiera | 352×289 | 1x 2x | sí |
| `XYUnlimited_64` | cualquiera | 384×289 | 1x 2x 4x | sí |
| `XYUnlimited2` | cualquiera | 352×289 | 1x 2x | sí |
| `XYUnlimited2_64` | cualquiera | 384×289 | 1x 2x 4x | sí |

Todas usan bloque de **16×16**, 4 planos, ventana **320×256**, monobuffer y bitmap **interleaved**
en la referencia; el engine generaliza tamaño de tile, planos y buffer.

## 2. Mapa a la infraestructura del engine

| Concepto de la referencia | En el engine |
|---|---|
| corkscrew 8-way (`XYLimited`) | `field/ScrollEngine` (+ `XLimitedPlayfield` como *sink*) |
| `XLimited` / `CopperRing` | `scene::ScrollKind::CopperRing` (reapuntado/módulo de `BPLxPT`) |
| `XYUnlimited` / split por línea (`CopperSplit`) | `scene::ScrollKind::CopperSplit` |
| columnas nuevas por Blitter (RoboCod) | `scene::ScrollKind::BlitterColumns` |
| fino por `BPLCON1` (`Fine`) | `scene::ScrollKind::Fine` |
| selección/degradación por presupuesto | `scene/scroll_plan.hpp` (`choose_scroll`/`degrade_scroll`) |
| paso > 1 px/frame | `field/ScrollProfile` (`ScrollFastN`) + `scroll_engine.hpp::burst_right` |
| variantes `_64` (fetch 1x4x) | `ScrollConsts`/fetch: **pendiente** de selección explícita |
| `YUnlimited2`/`XYUnlimited2` (video-splitting) | `ScrollKind::CopperSplit` + DPF: **pendiente** caso vertical |

## 3. Huecos y especializaciones que piden los juegos

### 3.1 XYLimited con tiles de 32×32 y pasos de hasta 16 px/frame

El modelo de perfiles (`ScrollProfile`) asume **paso = `fill_tiles` × tile** y ancla el avance a
**fronteras de tile** (`snap_to_tiles`, `plane-shift = 0`). Con tile de 32 px, un paso de 16 px es
**medio tile**: el `plane-shift` es 8, no 0, así que el caso "frontera de tile" no aplica y el
saveword/costura vuelve a ser por píxel.

Especificación:
- Nuevo modo de relleno `SubTileFill<PxPorFrame>` (paso en píxeles, no en tiles) que pre-pinta la
  franja de `Px` píxeles que revela la ventana, sobre tiles de 32×32.
- La guarda debe cubrir `ceil(px_por_frame / 16) + 1` **palabras** (no tiles), porque el avance no
  es múltiplo de tile.
- Optimización propia de 32×32: una columna de tile son 2 bloques de 16 px de ancho; el Blitter
  puede pintar el bloque de 32 px de alto en **un** blit por plano (en vez de dos de 16), y el
  interleave 1x4x permite fondear el salto de bloque sin re-codificar por fila.
- Restricción: `guard_words >= ceil(max_px/frame/16) + 1`; con 16 px/frame → guarda ≥ 2 palabras.

### 3.2 XLimited con Y mayor que el viewport (scroll horizontal)

- `XLimited` (CopperRing) es X; el eje Y debe poder ser **mayor que la altura visible** (mapa alto
  con cámara Y acotada), no solo 256. Hoy `ScrollConsts::display_height` fija la altura del anillo;
  hay que permitir `visible_height < map_height` con cámara Y limitada y anillo dimensionado a
  `visible_height + staging`.

### 3.3 YLimited con X más ancha que la pantalla (arcades verticales)

- `YUnlimited` es Y con bitmap de 320 px; los arcades verticales necesitan **X desplazable** con
  banda de guarda **más ancha que la pantalla** (p. ej. 384 px = 320 + 64, fetch 1x4x), aunque el
  scroll principal sea Y. Es `YUnlimited_64` + cámara X limitada a la banda.

## 4. Orden de implementación propuesto

1. **Selección explícita de variante** en la fachada (`ScrollKind` → `ScrollEngine`+`ScrollConsts`
   por sentido/fetch), con los `_64` (1x4x) como presets de fetch.
2. **`SubTileFill` (32×32, 16 px/frame)** sobre XYLimited, con guarda en palabras y blit de 32 px de
   alto por plano; test de continuidad a 16 px/frame (comparar con el progresivo de 1 px).
3. **XLimited-tall-Y** (anillo Y = viewport + staging con cámara Y limitada).
4. **YLimited-wide-X** (`_64` con banda 384 y cámara X acotada).
5. **`YUnlimited2`/video-splitting vertical** (copper + DPF).

Cada paso cierra con equivalencia contra el progresivo (sin huecos/tearing) y build/run/checks.

## 5. Referencias

- Política de velocidad y perfiles: `FAST_SCROLL.md`.
- Arquitectura del playfield/scroll: `PLAYFIELD_SCROLL_ARCHITECTURE.md`, `XYLIMITED_ALGORITMO_GENERICO.md`.
- Planificador de técnica: `scene/scroll_plan.hpp`.
- Referencia externa: `ScrollingTricks/` (11 variantes; `Docs/*-uk.html`).
