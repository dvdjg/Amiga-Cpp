# Sistema de HUD de juego (`eng::hud`)

Diseño canónico del **HUD de juego** del engine: barras y medidores, contadores, rejillas de iconos, inventario, selección y *drag & drop*, etiquetas, botones y paneles, tanto en **franja propia con modo gráfico independiente** como **sobreimpresos** sobre el campo de juego o en **pantalla modal**. El plan de implementación vive en [`ROADMAP_HUD.md`](../../guides/roadmap/ROADMAP_HUD.md).

El HUD es una **capa de escena**, no un *toolkit* de ventanas en miniatura: el modelo describe el estado visible, la región declara dónde se compone y con qué presupuesto, y la presentación materializa las operaciones con los mecanismos que ya existen (`Surface`, `Rasterizer`, `FramePlan`, `CanvasPlayfield`, sprites, Copper y `copper::Plan`). La lógica de juego sigue siendo la única fuente de verdad; el HUD la refleja y nunca la posee.

## 1. Objetivo y alcance

### 1.1 Qué entra

- **HUD de acción**: contador de puntos, barra de vida/energía, vidas o *pips*, iconos fijos y cursor.
- **HUD de puzle y aventura ligera**: rejilla de iconos, selección, inventario en franja o panel, barra de verbos, etiquetas y *tooltips*.
- **HUD de CRPG**: slots de inventario/equipo, panel de personaje, diario resumido, cantidades apiladas, selección con ratón o joystick y pantalla modal de inventario.
- **Presentación flexible**: franja con planos y paleta propios, sobreimpresión sobre el campo de juego, capa de sprites y panel modal a pantalla completa.
- **Reutilización**: el mismo componente lógico (`Meter`, `Counter`, `IconGrid`, `Slot`) se presenta con *tiles*, Blitter, sprite o Copper según el tema y el presupuesto de la región.

### 1.2 Qué no entra

- Un gestor de ventanas, menús de aplicación, edición de texto o diálogos del sistema: eso es [`eng::ui`](GUI_LIBRARY.md).
- Reglas de juego, inventario, economía, crafting o progresión: eso es [`eng::rpg`](RPG_LAYER.md) o la capa de juego correspondiente.
- Un intérprete de *scripts* de aventura gráfica: el HUD ofrece presentación e interacción; la lógica de verbos y respuestas pertenece al juego.
- Transparencias alfa, *anti-aliasing*, *layout* por *constraints* o escalado arbitrario de iconos.
- Un compositor de escena nuevo: las regiones se materializan con [`scene::LayerPlan`](../../../engine/include/eng/scene/plan.hpp) y el compositor vigente ([`SCENE_COMPOSITION.md`](SCENE_COMPOSITION.md)).

### 1.3 Reglas de diseño

- **Cero heap, cero `std::string`, cero `virtual` y cero RTTI en el camino caliente**; capacidades fijas y configuración en compilación.
- **La lógica de juego manda**: el HUD mantiene estado de presentación (valor mostrado, selección, *hover*, arrastre), nunca el estado autoritativo de vida, puntuación u objetos.
- **La región no dibuja**: declara colocación, modo, canales y presupuesto; el presentador emite operaciones y el compositor de escena decide.
- **Todo buffer que lee DMA es Chip y va tipado**: atlas de iconos, estructuras de sprite y bitmaps de banda usan `ChipView`/`Block<Tag, MemoryKind::Chip>`; una `Span` sin banco que termine en DMA no debe compilar.
- **Solo se repinta lo sucio**: `DirtyList` por región y operaciones generadas únicamente por componentes cuyo estado visible cambió.
- **Presupuesto medido, degradación determinista**: si una región excede su presupuesto, se degradan primero los adornos visuales, nunca los valores ni el *hit-test*.

## 2. Relación con lo que ya existe (no duplicar)

| Pieza existente | Uso en el HUD | Lo que el HUD no debe reimplementar |
|---|---|---|
| [`scene::LayerPlan`](../../../engine/include/eng/scene/plan.hpp) y `LayerRole::Overlay` | Declarar la capa del HUD y su colocación (`top`/`height`/`field`). | Un compositor paralelo de regiones. |
| [`field::CanvasPlayfield`](../../../engine/include/eng/field/canvas_playfield.hpp) + `Surface` | Lienzo de franja, panel y *backing* de overlay; primitivas de dibujo. | Un `Surface` propio o un buffer de píxeles nuevo. |
| `graphics::ModeSwitchZone` / `XlimitedOverlayConfig` | Franja con planos o paleta distintos; split de Copper validado. | Registros `BPLCON0`/`BPLxPT` escritos a mano. |
| [`field::DirtyList`](../../../engine/include/eng/ui/dirty.hpp) y `eng::Box` | Suciedad por región y rectángulo único del engine. | Un rectángulo o una lista de *dirty* propios. |
| [`eng::ui::UiEvent`](../../../engine/include/eng/ui/event.hpp) / `UiAction` (G9) | Contrato de entrada normalizado (puntero, acción, *tick*). | Un segundo sistema de eventos o *scancodes* en los widgets. |
| [`GlyphCache`](../../../engine/include/eng/graphics/glyph_cache.hpp) y fuentes `Font8`/`Font5x7` | Texto y dígitos por *cookie-cut* o CPU según umbral. | Un rasterizador de fuentes nuevo. |
| [`HardwareCursor`](../../../engine/include/eng/ui/hardware_cursor.hpp) | Cursor del puntero por sprite de hardware. | Un cursor por BOB cuando hay un canal libre. |
| `SpriteLineLayer` y el *ledger* de canales ([SPRITE_CHANNEL_WINDOWS.md](SPRITE_CHANNEL_WINDOWS.md)) | *Pips*, iconos fijos y *ghost* de arrastre por sprite. | Reparto de canales por frame sin *ledger*. |
| [`copper::Plan`](DISPLAY_COMPOSITION.md) | Split de banda, paleta, publicación y doble buffer. | Escrituras de Copper desde un widget. |
| [`Rasterizer`/`FramePlan`](RASTER.md) | Materialización CPU/Blitter de rellenos, copias y texto. | Un motor de *blits* propio. |
| [`eng::rpg`](RPG_LAYER.md) | Datos de inventario y equipo; el HUD recibe **vistas**. | Almacenar instancias de objeto en el HUD. |
| [`eng::debug::draw_hud`](../../../engine/include/eng/debug/hud.hpp) | Franja de nombre/versión/fps de las demos. | Un HUD de depuración nuevo. |

### 2.1 Frontera con `eng::ui`

| `eng::ui` | `eng::hud` |
|---|---|
| GUI de aplicación: ventanas, *backing store*, edición de texto, menús, *focus scopes*. | Presentación de estado de juego: medidores, contadores, rejillas, slots y arrastre. |
| Árbol intrusivo de widgets y despacho por `UiContext`. | Modelo plano de componentes con *handles* y operaciones de dibujo; sin árbol de ventanas. |
| `ProgressBar` es un widget genérico de valor. | `Meter` es una visualización de juego con estilos (*linear*, *segmented*, *pips*, *raster*) y sin edición. |
| Captura de puntero y propagación `capture → target → bubble` (G9). | Reutiliza ese contrato; añade foco de rejilla para joystick y política de *drop*. |

El HUD **no depende** de `UiContext`, `Widget`, `Window` ni del compositor de ventanas. `eng::ui` tampoco depende de `eng::hud`. Comparten `eng::Box`, `DirtyList`, el contrato de eventos, las fuentes y las rutas de rasterizado. Un panel modal complejo puede usar `eng::ui` para ventanas de aplicación, pero la barra de vida de un *shooter* no debe arrastrar ese stack.

### 2.2 Frontera con `eng::rpg`

El HUD **no incluye** cabeceras de `eng::rpg` ni duplica objetos. El juego construye un `Span<const HudSlotView>` desde su inventario y lo enlaza a la rejilla; el HUD solo lee esa vista para pintar icono, cantidad y estado. El adaptador opcional (`eng/rpg/hud_bridge.hpp`) vive en la fase H5 y convierte `ItemInstance`/`Inventory` en vistas, sin invertir la dependencia.

## 3. Capas y flujo

```text
┌──────────────────────────────────────────────────────────────┐
│ Juego / eng::rpg                                             │
│   estado autoritativo: vida, score, inventario, equipo       │
└──────────────────────────────┬───────────────────────────────┘
                               │ sync de vistas (solo lo visible)
┌──────────────────────────────▼───────────────────────────────┐
│ Modelo de HUD (portable, host-testable)                      │
│   Meter · Counter · IconGrid · Slot · DragSession · Label…   │
│   estado de presentación + dirty + handles                   │
└──────────────────────────────┬───────────────────────────────┘
                               │ HudDrawOp / HudHitEntry (capacidad fija)
┌──────────────────────────────▼───────────────────────────────┐
│ Región + presentación                                        │
│   Band (CanvasPlayfield) · Overlay (backing) · Sprites       │
│   CPU / Blitter / GlyphCache / copper::Plan (según umbral)   │
└──────────────────────────────┬───────────────────────────────┘
                               │ capas, canales y presupuesto
┌──────────────────────────────▼───────────────────────────────┐
│ Compositor de escena (ya existe)                             │
│   LayerPlan + FramePlan + copper::Plan + doble buffer        │
└──────────────────────────────────────────────────────────────┘
```

El **modelo** no sabe de planos, Copper ni sprites. La **región** no ejecuta *blits*. La **presentación** no decide reglas de juego. El **compositor** no conoce widgets: recibe capas, operaciones y presupuesto ya resueltos.

## 4. Contrato de región

Una región es un descriptor puro que el *planner* de escena compila a primitivas existentes:

```cpp
enum class HudPlacement : eng::u8 {
    Band,     ///< franja propia con planos/paleta opcionales
    Overlay,  ///< sobre el campo de juego visible
    Screen,   ///< panel modal a pantalla completa
};

enum class HudOverlayRestore : eng::u8 {
    GameRedraws, ///< el juego repinta su viewport cada frame; no hace falta backing
    Backing,     ///< el HUD guarda y restaura los píxeles que tapa
};

struct HudBand {
    eng::u16 top = 0;      ///< primera línea de display de la franja
    eng::u16 height = 0;   ///< alto en líneas; 0 = inválido para Band
    eng::u8 planes = 0;    ///< 0 = hereda la profundidad del campo de juego
    bool own_palette = false;
};

struct HudBudget {
    eng::u16 max_draw_ops = 0;
    eng::u16 max_blits_per_frame = 0;
    eng::u16 max_copper_moves_per_line = 0;
    eng::u16 max_backing_bytes = 0;
    eng::u8 sprite_channel_mask = 0; ///< bits SPR0..SPR7 reservados
    eng::u8 max_bobs = 0;
};

struct HudRegionSpec {
    HudPlacement placement = HudPlacement::Band;
    HudBand band {};
    HudOverlayRestore restore = HudOverlayRestore::GameRedraws;
    eng::u8 z = 0;                 ///< orden entre overlays; mayor = delante
    bool capture_input = false;    ///< consume la entrada de la región
    HudBudget budget {};
    eng::Ref<const HudTheme> theme {}; ///< tema (observador; la app es la dueña)
};
```

### 4.1 Colocaciones

- **`Band`**: franja con `CanvasPlayfield` propio, normalmente inferior (por ejemplo `top = 208`, `height = 48` en 320×256) o superior. Puede declarar menos planos y paleta propia; el split se emite con `ModeSwitchZone`/Copper. El campo de juego conserva su geometría y su anillo de scroll; la franja solo reduce el **área visible**, nunca la geometría interna del algoritmo.
- **`Overlay`**: operaciones sobre el campo de juego visible. Se usa para barras sobre el personaje, iconos contextuales, *tooltip* y *ghost* de arrastre. No cambia el modo gráfico. Exige política de restauración explícita y compite por BOBs/sprites con el juego.
- **`Screen`**: panel modal (inventario a pantalla completa, mapa, diario). Sustituye la escena visible o la cubre; el juego decide si pausa la simulación. Puede ser un `CanvasPlayfield` a pantalla completa o una escena propia; no es un `Window` de `eng::ui`.

### 4.2 Materialización

| Colocación | Capa de escena | Lienzo / dibujo | Cambio de modo | Entrada |
|---|---|---|---|---|
| `Band` | `LayerPlan{ role = Overlay, placement = {top,height}, content = Canvas }` | `CanvasPlayfield` + `Surface` | `ModeSwitchZone` si cambian planos/paleta | `capture_input` propio |
| `Overlay` | `LayerPlan{ role = Overlay, placement = {0,0}, content = Canvas }` o sprites | `Surface` del juego + *backing* opcional | No | Solo sobre zonas interactivas |
| `Screen` | Escena/capa a pantalla completa | `CanvasPlayfield` + `Surface` | Según la escena | Captura total mientras está abierta |

### 4.3 Invariantes de composición

- **La franja no encoge el anillo de scroll.** El alto visible del campo y la geometría del anillo son conceptos distintos; restar el HUD al anillo produce corrupción en técnicas como XYLimited ([XYLIMITED_ALGORITMO_GENERICO.md](XYLIMITED_ALGORITMO_GENERICO.md) §3.2).
- **El split se valida antes de arrancar.** El `WAIT` del Copper compara 8 bits de VPOS; `top + height`, el cambio de `BPLCON0` y la paleta se comprueban con los validadores de [`eng::setup`](COHERENCIA_DE_SETUP.md). Una región inválida no llega a pantalla.
- **La paleta de banda es propiedad explícita.** Si `own_palette` está activo, los índices usados por la franja no pueden depender de colores del juego; si no, el HUD usa la paleta del campo y no la reescribe.
- **Los canales de sprite se reservan en el *setup*.** El `sprite_channel_mask` se cruza con el *ledger* del juego; solaparse es un error de configuración, no un fallo en runtime.
- **El HUD no escribe registros.** Emite intenciones de paleta, *split*, publicación y operaciones de dibujo; el `copper::Plan` y el backend materializan.

## 5. Modelo de HUD

### 5.1 Identidad, capacidad y *handles*

Los componentes se crean en el *setup* y se identifican con *handles* tipados de índice pequeño (`MeterHandle`, `CounterHandle`, `GridHandle`). No hay claves de texto ni búsquedas por nombre en runtime. Las capacidades son parámetros de plantilla con valores por defecto sensatos:

```cpp
template <eng::u16 MaxRegions = 2, eng::u16 MaxWidgets = 24,
          eng::u16 MaxSlots = 64, eng::u16 MaxHitRects = 48>
class Hud { /* ... */ };
```

`MaxWidgets` incluye medidores, contadores, rejillas, etiquetas y botones; `MaxSlots` es el total de celdas visibles de todas las rejillas; `MaxHitRects` cubre las zonas interactivas que no son celdas de rejilla. Un desbordamiento en *setup* devuelve un error tipado; en runtime no se reserva memoria.

### 5.2 Componentes

| Componente | Estado mínimo | Eventos / efectos |
|---|---|---|
| `Meter` | `value`, `max`, `style` (`Linear`, `Segmented`, `Pips`, `Raster`), `pulse_ttl` | Cambio de valor o de máximo; pulso visual opcional. |
| `Counter` | `value: s32`, `digits`, `base` (2–36), `pad_zero`, `style` | Cambio de valor; formatea solo cuando cambia. |
| `IconGrid` | `rows`, `cols`, `pitch`, `first_slot`, `selection`, `focus`, `scroll_row` | Click en celda, cambio de selección/foco, inicio de arrastre. |
| `Slot` | Vista de `HudSlotView` (icono, cantidad, flags) | Acepta o rechaza *drop* según política del juego. |
| `DragSession` | `active`, `source`, `item_id`, `qty`, `anchor`, `ghost` | Inicio, movimiento, *drop*, cancelación. |
| `Label` | Texto estático o buffer de capacidad fija | Cambio de texto; *dirty* al cambiar. |
| `Button` | Etiqueta, `enabled`, `pressed` | *Press*/*release*, acción al soltar dentro. |
| `Panel` | Hijos y layout simple (fila/columna/rejilla) | Agrupa y recorta; no es una ventana. |
| `Cursor` | Posición y forma | Lo dibuja `HardwareCursor` o el presentador de overlay. |
| `Tooltip`/`Toast` | Texto + `ttl_frames` | Se muestra sobre la región; expira por `Tick`. |

### 5.3 Propiedad del estado

- El **juego** posee vida, puntuación, objetos y reglas. El HUD recibe valores (`meter.set(7, 12)`) o **vistas** (`Span<const HudSlotView>`).
- `HudSlotView` es un descriptor pequeño y copiable: `item_id`, `icon_frame`, `qty`, `flags` (vacío, habilitado, equipado, seleccionado, apilable). El HUD no guarda el `ItemInstance` ni punteros a él.
- El **estado de interacción** (hover, foco, selección, arrastre) sí pertenece al HUD: es presentación, no gameplay.
- Un `Meter` puede leer un valor externo si el juego lo prefiere (patrón de `Slider` sobre `s16*`), pero la API recomendada es `set()` para que el HUD detecte el cambio y marque *dirty* sin comparar punteros ajenos.

### 5.4 *Dirty*, operaciones y *hit map*

El modelo no dibuja: genera operaciones y entradas de *hit-test* en listas de capacidad fija.

```cpp
enum class HudOpKind : eng::u8 { Fill, Frame, Blit, Text, Digits, Sprite, Palette };

struct HudDrawOp {
    HudOpKind kind;
    eng::Box box;
    eng::u8 color;
    eng::u8 flags;
    eng::u16 asset;       ///< frame del atlas o canal de sprite
    const char* text;     ///< válido hasta present(); sin heap
    eng::u8 text_len;
};

struct HudHitRect {
    eng::Box box;
    eng::u16 id;
    eng::u8 layer;        ///< orden dentro de la región; mayor = delante
};
```

Las rejillas no generan N rectángulos: guardan un `HudGridHit` con origen, pitch, filas/columnas y el índice de la primera celda; el *slot* se calcula al recibir el evento (`(x - origin) / pitch`), no por frame. La división solo ocurre en entrada, no en el bucle de render, y el pitch se restringe o se mide según §5.5.

La lista de operaciones se genera solo para componentes sucios. El *hit map* de la región se construye en la misma pasada y queda disponible para el siguiente evento de entrada; no se reconstruye el árbol de widgets ni se recorre la pantalla completa.

### 5.5 Contadores, texto y coste

- El contador **formatea solo cuando cambia el valor**. No se convierte a texto en cada frame.
- Las potencias de la base se construyen por **sumas repetidas** (base ≤ 36) y los dígitos por **sustracción repetida**: sin multiplicación ni división (`__mulsi3`/`__udivsi3`) en la actualización, con coste acotado por `base × dígitos` y solo al cambiar el valor. Verificado en la compilación m68k de H0: el objeto de `eng/hud` no referencia helpers caros (`asm-audit`).
- El texto se dibuja con `draw_text`/`draw_text_words` para cadenas cortas y con `GlyphCache` cuando el umbral medido lo justifica; **nunca un *blit* por glifo**.
- Los dígitos y etiquetas viven en buffers de capacidad fija del componente; la operación `Text`/`Digits` referencia ese buffer y es válida hasta `present()` en el mismo frame.

### 5.6 `DragSession`

Hay **una sola sesión de arrastre global**. Estados: `Idle → Pressed → Dragging → Dropped | Cancelled`. Campos: región y slot de origen, `item_id`, cantidad, ancla, umbral, *ghost* y política de destino.

- El umbral de arrastre es un `constexpr` (`kDragStartPx`) justificado como histéresis: por debajo se considera *click*.
- El destino consulta `accepts(item_id, qty)` solo al entrar en la celda y al soltar, no en cada píxel.
- Cancelar (`Esc`, botón secundario del joystick o salida del panel) restaura el estado sin llamar a la política de *drop*.
- El *ghost* visual es un sprite de hardware si hay canal; si no, un BOB de overlay con presupuesto; si no, se ilumina el slot de origen y se omite el *ghost*.

## 6. Presentación y presupuesto

### 6.1 *Backends*

| *Backend* | Mejor para | Coste principal | Límite |
|---|---|---|---|
| `CanvasPlayfield` de banda | Contadores, barras por *tiles*, barra de verbos, rejilla de panel. | Blits/CPU en zona no jugable; DMA de planos. | Planos/paleta y split validados. |
| Sprite de hardware | Cursor, *pips* de vidas, pocos iconos fijos, *ghost*. | Canal de sprite y DMA por línea. | 8 canales compartidos; *ledger*. |
| BOB / *cookie-cut* | Iconos grandes, *ghost* y paneles sobre el juego. | Compite con BOBs del juego; restauración. | Presupuesto de BOBs y *dirty*. |
| Copper | Split de banda, paleta, publicación, barras raster de un color. | Movimientos por línea. | No pinta píxeles, glifos ni rellenos generales. |
| CPU | Texto corto, cambios pequeños, regiones no alineadas. | Ciclos de CPU. | Umbral medido; no en bucles por píxel. |
| Blitter | Rellenos y copias grandes alineadas, atlas de iconos. | Preparación, *scratch* Chip, espera. | Destino/origen alineados; barrera de frame. |

### 6.2 Política de materialización

La selección CPU/Blitter/Copper es del **presentador**, no del widget, y sigue la política medida de [`GUI_COMPLETENESS.md`](GUI_COMPLETENESS.md) y G10 del [`ROADMAP_GUI.md`](../../guides/roadmap/ROADMAP_GUI.md):

```text
texto corto / cambiante        -> CPU
texto largo o repetido         -> GlyphCache por Blitter
relleno/copia grande alineada  -> Blitter (Fill/Copy)
cambio pequeño no alineado     -> CPU
paleta, split, publicación     -> copper::Plan
cursor / pips / ghost ligero   -> sprite de hardware
```

El HUD no elige Copper para pintar un medidor salvo que el estilo sea explícitamente raster y el presupuesto lo permita: un medidor vertical de un color puede resolverse con un `MOVE` de frontera por frame; un medidor segmentado o con iconos no.

### 6.3 Franja (`Band`)

- La franja es un `CanvasPlayfield` con layout interleaved, igual que el resto de lienzos del engine. El presentador escribe operaciones en su `Surface` y el compositor emite `BPLxPT` para la banda.
- Con planos distintos al campo se usa `ModeSwitchZone` (o el mecanismo equivalente del compositor) y se validan `BPLCON0`, offsets y línea de corte.
- **Doble buffer de banda**: dos lienzos del mismo tamaño; el split publica el delantero y el presentador escribe el trasero. El intercambio se hace por punteros en el Copper (`BPLxPT` del tramo), no copiando el bitmap. Mecanismo previsto: el presentador reemite la zona de conmutación (`Scheduler::emit_mode_switch_zone`) con la base del lienzo trasero en la **copperlist trasera** (el doble buffer de copperlist ya existe); el coste por frame es de unos pocos `MOVE` y no de una copia. La evidencia del repo respalda el doble buffer: un lienzo vivo actualizado durante el barrido produce *tearing* (DT-001, FG de la demo 110: «el fix real es doble buffer del FG con flip en la copperlist»). Si la franja es estática, un solo lienzo y actualización en la ventana de VBlank es suficiente; la elección se cierra con medida en H1 (coste de reemisión vs. actualización in-place y ausencia de *tearing*), no por intuición.
- La franja inferior de 320×256 es el caso canónico (`top = 208`, `height = 48`); la superior (`top = 0`, `height = 32`) es válida si el corte y la ventana de display lo permiten. Las franjas laterales quedan fuera de alcance.
- El HUD de depuración obligatorio de las demos no se mezcla con la franja interactiva: se reserva una línea de 1 bpp o se usa la ventana de debug de WinUAE, según [`AGENTS.md` §0](../../../AGENTS.md).

### 6.4 Overlay

- `GameRedraws`: el juego limpia o repinta la zona tapada cada frame; el HUD solo vuelve a pintar sus operaciones. Es la opción por defecto y la más barata en Chip.
- `Backing`: el HUD guarda los píxeles de la zona que tapa antes de dibujar y los restaura al inicio del frame siguiente. Coste explícito: `ancho × alto × planos / 8` bytes en Chip más una copia por rectángulo sucio; entra en `max_backing_bytes`.
- Los elementos son opacos o con *cookie-cut*; no hay canal alfa. El overlay no escribe fuera de su `Box` declarado (validador de *setup* y comprobación en el presentador).
- El cursor y el *ghost* prefieren sprite; el resto de overlay compite con los BOBs del juego y se degrada primero.

### 6.5 Sprites y Copper

- El `sprite_channel_mask` se reserva en el *setup* y se cruza con el *ledger* (`SpriteChannelLedger`). El cursor de `HardwareCursor` consume un canal y debe estar incluido.
- Los *pips* de vidas son el caso ideal: uno o pocos sprites de 16 px de ancho, sin tocar los planos del juego.
- El Copper solo publica paleta, punteros, split y doble buffer; opcionalmente, una barra raster de un color con un `MOVE` por frontera. Cada movimiento cuenta en `max_copper_moves_per_line`.
- La lista de Copper se valida con `eng::setup::check_copper_list` antes de publicar.

### 6.6 Degradación determinista

Si una región supera su presupuesto, se aplica esta escalera en orden, sin cambiar el modelo:

1. *Ghost* animado → *ghost* estático.
2. Pulso/flash del medidor → relleno estático.
3. Actualización de contador cada frame → cada 2 frames.
4. Overlay animado → 25 Hz.
5. *Ghost* eliminado; solo se ilumina el slot de origen.
6. Adornos (marcos, sombras, *tooltips* no críticos) eliminados.

El *hit-test* y los valores mostrados **nunca** se degradan. El presentador incrementa un contador de degradación observable por la telemetría y por `VALIDATION.md` de la demo.

## 7. Entrada, *hit-test* y foco

### 7.1 *Hit map*

- Las regiones se consultan de delante hacia atrás (`Screen` → overlays por `z` → `Band` → juego).
- Dentro de una región, primero las rejillas (fórmula) y después la lista de `HudHitRect` ordenada por capa.
- Un `HudHitEntry` puede declarar `capture_input`: si la región captura, el evento se consume; si no, el HUD solo consume cuando el *hit* es interactivo y el resto pasa al juego.
- El *hit map* y el pintado comparten la misma transformación de coordenadas; no hay dos caminos que puedan desincronizarse.

### 7.2 Máquina de puntero

```text
Idle ──move──> Hover ──down──> Pressed ──move > umbral──> Dragging
                  │               │                           │
                  │               └──up mismo target──> Click  │
                  │                                           ├──drop válido──> Dropped
                  └──cancel───────────────────────────────────┴──cancel──────> Cancelled
```

- *Click* = `down` + `up` en el mismo objetivo y sin superar el umbral.
- *Drag* = se supera `kDragStartPx` con el botón pulsado; se crea la `DragSession` global y se captura el puntero hasta `drop` o cancelación.
- El doble click se mide en frames de VBlank (`double_click_frames`) y se usa solo donde el juego lo declare (por ejemplo, usar un objeto); no es un evento universal del HUD.
- La captura se libera en `PointerUp`, `PointerCancel`, cierre del panel, pérdida de foco de la aplicación o cambio de escena.
- **Subconjunto de entrada vigente (H2)**: hasta que G9 cierre el contrato de `eng::ui`, el HUD consume los tipos que `UiEventKind` ya define — `MouseMove`, `MouseDown`, `MouseUp`, `JoyButton` y `Tick` — y se alinea con G9 (`PointerMove`/`PointerDown`/`PointerUp`/`PointerCancel`/`Action`/`TextInput`) sin mantener una máquina paralela. `HudInput` es un consumidor del evento, no un segundo despachador.

### 7.3 Teclado y joystick

- El HUD consume `UiAction` (tras G9) para `Accept`, `Cancel`, `NextFocus`, `PreviousFocus` y navegación; no interpreta *rawkeys*. Mientras G9 no exponga `UiAction`, el subconjunto vigente usa `KeyDown`/`KeyUp` con la traducción de `eng::ui::keymap` y `JoyButton` para el pad.
- Sin ratón, cada rejilla mantiene un índice de **foco** independiente de la **selección**. El joystick mueve el foco por filas/columnas; `Fire` equivale a *click*; el botón secundario cancela.
- La repetición de movimiento la aporta el servicio de entrada o el `Tick` de VBlank; el widget no la implementa.
- `focus_index` es visible (marco o realce) y participa en el *dirty* de interacción.

### 7.4 Coordenadas

- El HUD trabaja en **espacio de pantalla** y, si se declara, en **espacio de región** (`HudPoint`/`HudBox` relativos a la región). La transformación es explícita y común para pintado y *hit-test*.
- Prohibido usar coordenadas del bitmap de scroll o del mundo dentro del inventario, del HUD o de la barra de verbos; el juego traduce si lo necesita.
- Un `Overlay` recibe coordenadas de pantalla y las recorta a su `Box`; una `Band` recibe coordenadas relativas a la franja.

### 7.5 Consumo y convivencia con el juego

- Si el puntero está sobre una región con `capture_input = true`, el juego no recibe ese evento.
- Si está sobre el *viewport* y no hay *hit* interactivo, la UI no consume; el juego recibe el evento y el HUD solo dibuja su cursor si lo tiene.
- Si un panel `Screen` está abierto, captura toda la entrada mientras el juego decide si pausa la simulación.
- La política es explícita por región; no se deduce del orden de dibujo.

## 8. Temas, *skins* y assets

### 8.1 `HudTheme`

Un tema enlaza roles lógicos con presentación concreta:

```cpp
struct HudTheme {
    eng::PaletteWords band_palette; ///< si la región tiene paleta propia
    eng::u8 bg, fg, accent, warn;   ///< índices lógicos
    eng::u8 meter_fill, meter_back;
    eng::u8 border, gap, meter_h;
    const IconAtlas* atlas;
    FontId font;
};
```

- Los presets son `constexpr` (`kHudThemeA500`, `kHudThemeMono`, `kHudThemeEhb` como orientación; el juego puede declarar el suyo).
- El mismo `Meter` se presenta como *tiles* en una franja, relleno por Blitter en un panel o *pips* de sprite según el tema; el modelo no cambia.
- Cambiar de tema marca todas las regiones sucias y recarga la paleta de banda en el siguiente split; no reinicia el estado de juego.

### 8.2 `IconAtlas` y pipeline

- El atlas es un bitmap planar en Chip (`ChipView<HudAtlasTag>`) con tabla de frames (`frame_w`, `frame_h`, `count`, `planes`, `masked`, `stride_words`). El presentador calcula el offset del frame; se prefieren dimensiones potencia de dos para usar desplazamientos.
- **Formato canónico del atlas (H6)**: un solo bloque de palabras interleaved por filas, con los frames contiguos; cada frame ocupa `stride_words` palabras **por plano** (`stride_words = ceil(frame_w/16) × frame_h`), y el offset del frame `f` es `f × planes × stride_words` palabras. `planes` ∈ 1..3 con máscara opcional (1 bit de máscara + 1–2 de color) o 1..6 sin máscara para paneles con paleta propia; `masked = true` exige un plano de máscara por frame (índice 0 = transparente). `frame_w`/`frame_h` son potencia de dos hasta 64×64; frames mayores van a un atlas propio. La tabla de frames es un `constexpr` del manifiesto (mismo orden que el pipeline), no un fichero que se parsee en runtime.
- El pipeline de assets vive en el host: `tools/assets/hud-atlas.mjs` (implementado) lee `assets/amiga/hud/`, cuantiza (exacta o *median-cut* sobre RGB444), convierte a planar interleaved, y emite el atlas, la tabla de frames y el JSON de procedencia a `out/assets/hud/<nombre>/`; `--check` verifica la sincronía byte a byte y `--selftest` fija geometría, bit order y round-trip 100 % (corre en la pasada completa de `tools/run-host-tests.sh`). El juego referencia el manifiesto; no hay decodificación ni asignación en runtime.
- Los iconos de 1–3 planos con máscara cubren inventario, vidas y cursores; los iconos de más planos se reservan para paneles con paleta propia.

### 8.3 *Skins* y reutilización

- `MeterStyle`, `CounterStyle` e `IconSet` son parte del tema, no del componente.
- El mismo HUD de CRPG puede usar *skin* «tiles 2 bpl» o «minimal sprites» sin tocar la lógica de inventario.
- Los temas no introducen dependencias de assets en el modelo: el atlas se pasa como vista; el modelo sigue siendo host-testable sin Chip.

## 9. Ciclo de frame

```text
VBlank / Tick
  -> entrada: os::Msg -> UiEvent/UiAction -> HudInput (hover, foco, drag)
  -> inicio de frame: restaurar overlay (si Backing) y limpiar dirty de la región
  -> juego: update + render del viewport (estado autoritativo)
  -> hud.sync(vistas): valores y slots
  -> hud.build_ops(): solo componentes sucios
  -> presentar por región: Canvas / sprites / intenciones Copper
  -> compositor de escena: FramePlan + copper::Plan (barrera de frame)
  -> publicar: flip de doble buffer / VBlank
  -> hit map del siguiente frame
```

- La restauración del overlay ocurre **antes** de que el juego repinte su zona si el modo es `Backing`; si el juego repinta todo el *viewport*, `GameRedraws` evita la copia.
- La franja se escribe en su lienzo trasero y se publica por el split; no se copia a la pantalla.
- El `FramePlan` y el plan de Copper se cierran antes de publicar; ninguna escritura CPU del HUD comparte región con un *blit* asíncrono sin barrera.
- El *hit map* se construye durante `build_ops` y se usa en el siguiente evento; no hay una pasada extra por frame.

### 9.1 Presupuesto y medida

- `HudStats` expone por región: operaciones, *blits*, movimientos de Copper, canales de sprite, bytes de *backing* y nivel de degradación. Se publica en la telemetría de la demo y en `VALIDATION.md`.
- Cada demo del roadmap mide fps/ciclos por frame, cadencia sin tirones, *flicker* y una **secuencia** de frames validada con Ollama, no una captura suelta.
- Los umbrales CPU/Blitter y el coste del *backing* se fijan con medidas en A500, no por intuición; ver [`METODO_OPTIMIZACION.md`](../../guides/optimization/METODO_OPTIMIZACION.md) y G10 de [`ROADMAP_GUI.md`](../../guides/roadmap/ROADMAP_GUI.md).

### 9.2 Coexistencia con el HUD de depuración

- La norma de [`AGENTS.md` §0](../../../AGENTS.md) obliga a mostrar nombre, versión y depuración en la franja inferior de toda demo. En una demo con HUD de juego, la franja interactiva no absorbe esa responsabilidad: se reserva una línea de 1 bpp encima/debajo del HUD o se usa la ventana de debug de WinUAE.
- `eng::debug::draw_hud` pinta por palabras (`draw_text_words`); nunca píxel a píxel.
- El HUD de depuración no debe tapar celdas interactivas ni participar en el *hit-test*.

## 10. Límites que el diseño impone

- **Un solo `DragSession` global**; no hay arrastres simultáneos.
- **Un solo dueño por región**: o el HUD o el juego escribe una zona; el *backing* y el orden de frame lo garantizan.
- **Sin repintado completo**: solo *dirty*; una franja entera por frame es un fallo de diseño.
- **Sin heap, `std::string`, `virtual` ni RTTI** en el camino de frame; capacidades fijas y *handles*.
- **Sin claves de texto en runtime**: *handles* o índices; las etiquetas son datos estáticos.
- **Sin texto por píxel ni un *blit* por glifo**; fuentes cacheadas y agrupación.
- **Sin `*`, `/` ni `%` de 32 bits en bucles por píxel/línea**; potencias y tablas `constexpr`, sustracción repetida en contadores.
- **Sin Copper desde un widget**: solo intenciones al plan.
- **Sin overlay sin política de restauración** (`GameRedraws` o `Backing`).
- **Sin coordenadas de scroll en el inventario**; espacios de pantalla/región tipados.
- **Sin lógica de juego en el HUD**: el HUD no modifica vida, puntuación ni objetos.
- **Sin dependencia de `eng::ui` para la barra de vida**: comparten contrato de entrada y primitivas, no el árbol de ventanas.
- **Sin regiones inválidas**: planos, split, paleta y canales se validan en *setup*.

## 11. Esquema resumido

```text
Juego / eng::rpg                    (estado autoritativo)
        │  set() / HudSlotView
        ▼
Modelo de HUD                       (portable, host-testable)
  Meter · Counter · IconGrid · Slot · DragSession · Label · Button · Panel
        │  dirty + HudDrawOp + HudHitEntry (capacidad fija)
        ▼
Región + presupuesto                (Band / Overlay / Screen)
        │
        ├── CanvasPlayfield + Surface ──> franja / panel
        ├── sprites / SpriteLineLayer ──> cursor, pips, ghost
        ├── backing del overlay ────────> restaurar + pintar
        └── copper::Plan ───────────────> paleta, split, publicación
        │
        ▼
Compositor de escena                (LayerPlan + FramePlan + doble buffer)
```

## 12. Referencias

- [`GUI_LIBRARY.md`](GUI_LIBRARY.md): chrome, fuentes, tema, eventos y compositor de ventanas de `eng::ui`.
- [`GUI_COMPLETENESS.md`](GUI_COMPLETENESS.md): contrato de eventos (G9), captura de puntero, foco y política CPU/Blitter/Copper.
- [`ROADMAP_GUI.md`](../../guides/roadmap/ROADMAP_GUI.md): fases G0–G10 y umbrales de materialización.
- [`SCENE_COMPOSITION.md`](SCENE_COMPOSITION.md) y [`DISPLAY_COMPOSITION.md`](DISPLAY_COMPOSITION.md): capas, buffers y `copper::Plan`.
- [`PLAYFIELD_SCROLL_ARCHITECTURE.md`](PLAYFIELD_SCROLL_ARCHITECTURE.md) y [`XYLIMITED_ALGORITMO_GENERICO.md`](XYLIMITED_ALGORITMO_GENERICO.md): geometría del campo, anillo y separación HUD/scroll.
- [`SPRITE_CHANNEL_WINDOWS.md`](SPRITE_CHANNEL_WINDOWS.md) y [`OBJECT_SYSTEM.md`](OBJECT_SYSTEM.md): reparto de canales, BOBs y presupuesto.
- [`RASTER.md`](RASTER.md): `Rasterizer`, `FramePlan` y rutas CPU/Blitter.
- [`RPG_LAYER.md`](RPG_LAYER.md): inventario, equipo y vistas de objeto.
- [`COHERENCIA_DE_SETUP.md`](COHERENCIA_DE_SETUP.md): validadores de `BPLCON0`, regiones y Copper.
- [`ROADMAP_HUD.md`](../../guides/roadmap/ROADMAP_HUD.md): plan de fases, tests y demos.
