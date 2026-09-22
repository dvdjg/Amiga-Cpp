# Librería GUI para juegos en Amiga (`eng::ui`)

Diseño de una **librería de interfaz de usuario** para juegos y demos que toman la máquina (sin
Workbench): desde primitivas de dibujo de *chrome* (cajas, marcos, líneas, texto) hasta la
composición de widgets (botones, *checkboxes*, *radio buttons*, campos de texto), con **tema o
branding configurable**, **gestión de eventos** (ratón, teclado, joystick), **foco**, **dirty
rects** y **ventanas movibles/redimensionables con compositor y *backing store* por ventana**.

La librería **no reinventa el dibujo**: se apoya en `field::Surface` + `field::Rasterizer`
(CPU/Blitter) + `graphics::FramePlan`, que ya existen. El propio engine lo dejó dicho: «un
`Widget` es una `Surface` + `draw()` + `hit_test(punto)`» (`surface.hpp`). Tampoco depende del
SO: la entrada puede llegar del mini-SO de mensajes ([MINI_OS_MESSAGE_LOOP.md](MINI_OS_MESSAGE_LOOP.md))
o de cualquier otra fuente, porque los widgets solo consumen `UiEvent`.

## 1. Objetivo y alcance

- **Chrome Amiga**: rellenos, marcos *raised/recessed*, líneas, texto con la fuente del engine,
  glifos 1-bit (tick de checkbox, flechas, *radio*).
- **Widgets**: `Panel`, `Label`, `Button`, `CheckBox`, `RadioButton`, `EditBox`; extensibles a
  `Slider`, `List`, `Scrollbar`.
- **Tema**: colores lógicos → índices de paleta y métricas; presets de branding (Workbench 1.3,
  2.x/3.x, tema propio de la demo) sin colores mágicos.
- **Eventos**: *hit-test* de delante hacia atrás, foco por teclado (`Tab`), grupos de radio,
  *drag* de ventana y *resize*.
- **Ventanas**: `Window`, `Popup`, `Toast`, `Dialog` (modal) con **compositor y backing store**:
  mover/redimensionar/cambiar Z **no** invalida a las vecinas; se recuperan trozos ya
  rasterizados de los backings.
- **Presupuesto A500**: sin heap en el camino caliente, dirty rects, reutilización del Blitter
  para *fills*, marcos y *copies*.

Fuera de alcance: Intuition, *anti-aliasing*, transparencias alfa, *layout* por *constraints*,
texto con *shaping*.

## 2. Relación con lo que ya existe (no duplicar)

| Pieza existente | Qué aporta | Papel en `eng::ui` |
|---|---|---|
| `field::Surface` (`field/surface.hpp`) | Contexto de dibujo con clip: `set_pixel`, `fill_rect`, `draw_line`, `fill_polygon`, `draw_text`, `draw_text5`, `draw_text_literal`, `blit`, `blit_masked` | **Único destino de dibujo**. El `UiPainter` es un envoltorio fino que añade azúcar de UI (marcos, paneles, texto con fondo). |
| `field::Rasterizer` / `RasterOp` (`field/raster.hpp`) | Elección CPU/Blitter por política; operaciones lógicas | Los *fills*/líneas de la GUI entran por aquí; nada de escribir planos a mano. |
| `graphics::FramePlan` (`graphics/frame_plan.hpp`) | Cola de trabajos de Blitter (copies, líneas, fills con patrón…) | El compositor y los marcos grandes encolan aquí; el backend decide la materialización. |
| `Font8` y `Font5x7` (`graphics/font8.hpp`, `font5x7.hpp`) | Fuentes 8×8 (LATIN-1) y 5×7, `row(cp, fila)` | Fuente de la GUI. **No se redibuja ninguna fuente**; solo se mide y se compone. |
| `field::SurfaceRect` (`{s32 x,y; u16 w,h}`) | Rectángulo lógico para `Surface` | Base del `ui::Rect` (ver §4). |
| `field::FlatPlayfield` / `field::CanvasPlayfield` | Playfield con framebuffer propio + `bind` | **Backing de cada ventana** (§14): un playwright plano por ventana, y los widgets pintan sobre su `Surface`. |
| `eng::os` (mini-SO) | Mensajes (input, VBlank, timers, disco) | Fuente **opcional** de `UiEvent`; la GUI también funciona con entrada síncrona. |
| `field::DirtyRect` (`frame_plan.hpp`) | Rect sucio del engine (`left/top/right/bottom`) | Referencia de intención; la GUI usa su propia lista de dirty en coordenadas de ventana (§9). |

Regla: **un hecho, un sitio.** La GUI no implementa rasterizado, ni fuentes, ni blits: pide a
`Surface`. Lo nuevo de esta librería es *chrome*, widgets, tema, eventos y composición.

## 3. Capas

```text
┌───────────────────────────────────────────────┐
│ App / pantallas (callbacks, lógica)           │
├───────────────────────────────────────────────┤
│ Widgets (Button, EditBox, Check, Radio, …)    │   ← datos planos + funciones
├───────────────────────────────────────────────┤
│ Compositor + backing store por ventana        │   ← copies, sin repaint de vecinas
├───────────────────────────────────────────────┤
│ UiContext (dirty, foco, hit-test, dispatch)   │
├───────────────────────────────────────────────┤
│ UiPainter (chrome) + UiTheme                  │
├───────────────────────────────────────────────┤
│ Surface / Rasterizer / FramePlan  (ya existe) │
├───────────────────────────────────────────────┤
│ Playfield / memoria Chip  (ya existe)         │
└───────────────────────────────────────────────┘
   Entrada: eng::os (mensajes) o síncrona → UiEvent
```

Principio: **los widgets no tocan hardware**. Solo emiten órdenes a `UiPainter`, que dibuja en
la `Surface` del destino (pantalla o backing de ventana).

## 4. Rectángulo de UI

La GUI usa **`eng::Box`** (`eng/core/box.hpp`), el rectángulo único de 16 bits del engine (ver
[ENGINE_STRUCTURE_REVIEW.md](ENGINE_STRUCTURE_REVIEW.md) D2): origen + tamaño, con `contains`
inclusivo, `inset`, `overlaps`, `intersect`, `merge` y `translate`. Para llamar a `Surface` se
convierte con `field::surface_rect_of(box)`; los dirty rects del engine se convierten con
`graphics::dirty_rect_of(box)`.

Así la GUI no introduce un quinto rectángulo: los rects de widget, de ventana y de dirty son
`Box`, y las conversiones viven en la capa que ya conocía cada tipo (`SurfaceRect`, `ClipRect`,
`DirtyRect`).

> En el resto de este documento, donde aparezca `Rect` léase `eng::Box`.

## 5. `UiPainter`: chrome sobre `Surface`

El painter no dibuja los píxeles: delega en `Surface`. Aporta las operaciones que un widget
necesita y que `Surface` no tiene como concepto (marcos, paneles, texto con fondo):

```cpp
namespace eng::ui {

/// Dibuja chrome de UI sobre una `Surface` (pantalla o backing de ventana).
class UiPainter {
public:
	UiPainter(eng::field::Surface& surface, eng::graphics::FramePlan* plan,
		  const UiTheme& theme) noexcept;

	[[nodiscard]] const eng::ui::Rect& clip() const noexcept;
	[[nodiscard]] const UiTheme& theme() const noexcept { return m_theme; }

	// --- Primitivas (delegan en Surface/Rasterizer) ---
	void fill(Rect r, eng::u8 color);
	void hline(eng::s16 x0, eng::s16 x1, eng::s16 y, eng::u8 color);
	void vline(eng::s16 x, eng::s16 y0, eng::s16 y1, eng::u8 color);
	void frame(Rect r, eng::u8 color);              // borde de 1 px
	void bevel_out(Rect r);                         // raised: shine arriba/izq, shadow abajo/der
	void bevel_in(Rect r);                          // recessed
	void panel(Rect r);                             // fill + bevel segun tema
	void button_face(Rect r, bool pressed);

	// --- Texto (reusa Font8/Font5x7; nunca redibuja fuentes) ---
	void text(eng::s16 x, eng::s16 y, const char* s, eng::u8 fg);
	void text_bg(eng::s16 x, eng::s16 y, const char* s, eng::u8 fg, eng::u8 bg);

	// --- Glifos 1-bit (tick, flechas, radio) ---
	void glyph(eng::s16 x, eng::s16 y, const eng::u16* bits, eng::u16 w, eng::u16 h,
		   eng::u8 fg);

private:
	eng::field::Surface& m_surface;
	eng::graphics::FramePlan* m_plan = nullptr;
	const UiTheme& m_theme;
};

} // namespace eng::ui
```

Implementación de las piezas no triviales:

- `bevel_out`/`bevel_in`: cuatro tramos de una línea (`hline`/`vline`) con `theme.shine` y
  `theme.shadow`; cada llamada va a `Surface::draw_line`, que con `plan != nullptr` y
  `BlitterRaster` se encola como `BlitJobKind::Line`.
- `panel`: un `fill_rect` (que con la política `Auto` y área suficiente va al Blitter) más el
  bevel.
- `text_bg`: `fill_rect` del fondo del texto (medido con `text_width`) y `draw_text` encima.
- `glyph`: por filas con `Surface::draw_glyph_row`-equivalente o un `blit_masked` de una máscara
  1-bit; los glifos pequeños (9×9) se quedan en CPU.

## 6. Fuente y medición de texto

No se añade fuente nueva: `Font8` (8×8, *advance* 8) y `Font5x7` (5×7) ya existen. La GUI añade
**medida** y **recorte de texto**:

```cpp
namespace eng::ui {

/// Ancho en píxeles de una cadena con la fuente por defecto (`Font8`, 8 px/carácter).
[[nodiscard]] eng::u16 text_width(const char* s) noexcept;

/// Dibuja `s` recortando a `clip` (sin partir glifos a medias).
void draw_text_clipped(UiPainter& p, Rect clip, eng::s16 x, eng::s16 y,
		       const char* s, eng::u8 fg);

} // namespace eng::ui
```

Para etiquetas estáticas se prefiere `Surface::draw_text_literal<"...">`, que decodifica el
UTF-8 **en compilación** (ya existe en el engine) y no procesa la cadena en runtime.

## 7. Tema / branding

Un tema es una tabla de **colores lógicos** (que mapean a índices de la paleta del playfield) más
las métricas. Los widgets consultan `theme()`, nunca colores mágicos:

```cpp
namespace eng::ui {

struct UiTheme {
	// Colores logicos (indices de la paleta del playfield).
	eng::u8 bg = 0;            ///< fondo de panel
	eng::u8 bg_shine = 1;      ///< borde claro
	eng::u8 bg_shadow = 2;     ///< borde oscuro
	eng::u8 fill = 0;          ///< relleno normal
	eng::u8 fill_active = 1;   ///< pulsado / seleccionado
	eng::u8 text = 3;
	eng::u8 text_dim = 2;      ///< deshabilitado
	eng::u8 edit_bg = 0;
	eng::u8 focus_ring = 1;

	// Metricas.
	eng::u8 border = 1;
	eng::u8 pad_x = 4;
	eng::u8 pad_y = 2;
	eng::u8 btn_h = 12;
	eng::u8 check_s = 9;
	eng::u8 radio_s = 9;

	enum class FrameStyle : eng::u8 { Flat, Raised, Recessed, Double };
	FrameStyle panel_frame = FrameStyle::Raised;
	FrameStyle button_frame = FrameStyle::Raised;
};

inline constexpr UiTheme kThemeWb13 { /* grises Workbench 1.3 */ };
inline constexpr UiTheme kThemeWb2  { /* 2.x/3.x, mas contraste */ };
inline constexpr UiTheme kThemeFlat { /* UI plana para juegos */ };

} // namespace eng::ui
```

Cambiar de tema = asignar otro `UiTheme` y marcar todo el árbol sucio.

## 8. Modelo de widgets

Se prioriza **datos planos + punteros a función** frente a herencia con `virtual`: en un 68000
sin *branch predictor* una tabla de funciones por tipo es predecible y no arrastra vtable por
objeto. El árbol es intrusivo (padre/hijo/siguiente), sin heap.

```cpp
namespace eng::ui {

enum class WidgetType : eng::u8 { Panel, Label, Button, Check, Radio, Edit, Slider, List };

enum WidgetFlags : eng::u16 {
	WfVisible      = 1u << 0,
	WfEnabled      = 1u << 1,
	WfDirty        = 1u << 2,
	WfFocused      = 1u << 3,
	WfPressed      = 1u << 4,
	WfModal        = 1u << 5,
	WfAcceptsFocus = 1u << 6,
};

struct Widget {
	WidgetType type = WidgetType::Panel;
	Rect bounds {};
	eng::u16 flags = WfVisible | WfEnabled | WfDirty;

	Widget* parent = nullptr;
	Widget* first_child = nullptr;
	Widget* next = nullptr;

	[[nodiscard]] constexpr bool has(eng::u16 f) const { return (flags & f) != 0u; }
	void set_flag(eng::u16 f) { flags = static_cast<eng::u16>(flags | f); }
	void clear_flag(eng::u16 f) { flags = static_cast<eng::u16>(flags & ~f); }
	void mark_dirty() { set_flag(WfDirty); }

	void add_child(Widget* c) noexcept {
		if (c == nullptr) return;
		c->parent = this;
		c->next = first_child;
		first_child = c;
		mark_dirty();
	}
};

} // namespace eng::ui
```

Los **datos específicos** de cada tipo van en campos del propio struct derivado (Button con
`text`/`on_click`, `CheckBox` con `bool* value`, `EditBox` con `char* buf`/`len`/`caret`, etc.),
no en una unión opaca: se gana claridad y se evita *tagged union* en el camino de pintado. El
despacho por tipo se hace con un `switch` sobre `type` (exhaustivo y chequeable por el
compilador) o con una **tabla estática de `draw`/`event`** indexada por `WidgetType`.

## 9. Dirty rects

No se redibuja toda la pantalla cada frame. Cada cambio de estado llama a `mark_dirty(widget)` y
la lista de dirty acumula rects (fijos, con fusión al añadir):

```cpp
namespace eng::ui {

/// Lista de regiones sucias con fusion simple (sin heap). `kMax` acotado.
template <eng::u8 Max = 8u>
struct DirtyList {
	Rect rects[Max] {};
	eng::u8 count = 0u;

	void clear() noexcept { count = 0u; }

	void add(Rect r) noexcept {
		if (r.empty()) return;
		for (eng::u8 i = 0u; i < count; ++i) {
			if (overlaps(rects[i], r)) {          // fusiona si tocan
				rects[i] = merge(rects[i], r);
				return;
			}
		}
		if (count < Max) {
			rects[count++] = r;
		} else {
			rects[0] = Rect {0, 0, 320, 256};     // desborde: full repaint
			count = 1u;
		}
	}
};

} // namespace eng::ui
```

El clear de cada dirty (un `fill_rect` con el color de fondo) es el mayor ahorro: equivale a un
borrado parcial de pantalla y, con `BlitterRaster` + `RasterPolicy::Auto`, va al Blitter cuando
el área lo justifica.

## 10. Eventos, hit-test y foco

```text
Entrada (eng::os Msg, o síncrona)
    → UiEvent
    → UiContext::dispatch()
        1. si hay modal, filtrar
        2. hit-test de delante hacia atrás
        3. foco (click en widget que acepta foco; Tab cicla)
        4. widget.on_event(ev)
        5. callbacks de la app
        6. mark_dirty si cambió estado
```

```cpp
namespace eng::ui {

enum class UiEventKind : eng::u8 {
	MouseMove, MouseDown, MouseUp, MouseDrag,
	KeyDown, KeyUp, Text, FocusLost, Tick,
};

struct UiEvent {
	UiEventKind kind = UiEventKind::MouseMove;
	eng::s16 x = 0;
	eng::s16 y = 0;
	eng::u16 key = 0;        ///< scancode Amiga o ASCII segun la capa de entrada
	eng::u8 buttons = 0;
	bool shift = false;
};

} // namespace eng::ui
```

- **Hit-test**: recorrer el árbol desde el último hijo (más al frente) y devolver el primer
  widget `Visible|Enabled` que contenga el punto. Los hijos se insertan al frente en
  `add_child`, de modo que el orden de la lista es el orden Z.
- **Foco**: `Tab`/`Shift+Tab` cicla solo entre widgets con `WfAcceptsFocus` dentro del modal
  superior (o del escritorio si no hay modal). Solo el foco recibe `KeyDown`.
- **Grupos de radio**: mismo `group_id`; al activar uno se desactivan los hermanos del grupo y
  se marcan sucios todos.
- **Caret**: el parpadeo es un `UiEventKind::Tick` derivado del VBlank (cada N frames), no un
  timer propio.

## 11. Widgets mínimos (orden de implementación)

1. **Panel / Group**: contenedor; dibuja el fondo y el bevel del tema.
2. **Label**: texto estático (`draw_text_literal` cuando el texto es un literal).
3. **Button**: cara + bevel según `WfPressed`; `on_click`.
4. **CheckBox**: caja + tick 1-bit; alterna un `bool*`.
5. **RadioButton**: círculo/glifo + `group_id`; activa uno y desactiva el grupo.
6. **EditBox**: fondo *recessed*, caret, inserción/borrado, buffer externo, *scroll* horizontal.
7. Después: **Slider**, **List**, **Scrollbar** si el juego los pide.

Cada widget expone conceptualmente `measure()` (tamaño preferido), `draw(UiPainter&)` y
`on_event(const UiEvent&)` (devuelve `true` si consumió el evento).

El `EditBox` trabaja sobre un `char*` externo (`buf`, `cap`, `len`, `caret`, `view`); no
reserva memoria. Las teclas de edición vienen del mapa Amiga (Backspace, Delete, Left/Right,
Return, Esc) y el texto imprimible inserta en el `caret` desplazando el resto.

## 12. Layout

Sin motor de *constraints*: para A500 basta con

- **Absoluto**: `bounds` fijas (lo más simple y predecible).
- **Pila vertical/horizontal**: los hijos se colocan en fila con el `gap` y los paddings del
  tema; el tamaño del botón sale de `theme.btn_h` y de `text_width`.
- **Anclaje**: pegar a un borde del padre con un *offset* (p. ej. un botón abajo a la derecha).

```cpp
void layout_stack_v(Widget* group, eng::u8 gap) noexcept;
void layout_stack_h(Widget* group, eng::u8 gap) noexcept;
```

## 13. Ventanas

```text
UiContext
  ├─ desktop (Panel raiz opcional)
  └─ window stack (orden Z: fondo → frente)
        Window | Popup | Toast | Dialog
           └─ hijos (Button, EditBox, ...)
```

- **Window**: marco + título + contenido; no modal.
- **Popup**: menú/lista corta; se cierra al pulsar fuera o con `Esc`.
- **Toast**: efímero, no capta input, se autodestruye por TTL en frames.
- **Dialog**: modal; solo él recibe eventos.

Reglas de entrada: `top_modal()` filtra el *hit-test*; `Esc` cierra el popup/diálogo superior;
`Tab` cicla el foco solo dentro del modal actual.

## 14. Compositor con backing store por ventana

Modelo **retained compositor**: cada ventana tiene su propio **bitmap** (en Chip RAM); la
pantalla solo **compone** rectángulos. Mover, redimensionar o cambiar Z **no** invalida a las
vecinas: se copian trozos ya rasterizados. Es lo contrario del *damage/refresh* de Intuition.

```text
   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
   │ Ventana A    │   │ Ventana B    │   │ Ventana C    │
   │ backing      │   │ backing      │   │ backing      │
   └──────┬───────┘   └──────┬───────┘   └──────┬───────┘
          │                  │                  │
          └──────────┬───────┴──────────────────┘
                     ▼
             Compositor (Z-order, copies)
                     ▼
              Pantalla / playfield
```

### 14.1 Backing como playfield del engine

La clave para no reinventar el dibujo: el backing de una ventana es un **`field::FlatPlayfield`**
enlazado a un bloque de Chip RAM (con `bind`, `row_bytes` alineado a palabra, `depth` planos). Los
widgets pintan con `Surface` sobre ese playfield; el compositor copia desde ese playfield a la
pantalla con `Surface::blit` (que va al Blitter) o un *copy* planar equivalente. Así el mismo
`UiPainter` y los mismos widgets funcionan tanto si el destino es la pantalla como si es el
backing de una ventana.

```cpp
namespace eng::ui {

/// Backing de una ventana: un playfield plano con su `Surface` de dibujo.
struct WindowBacking {
	eng::field::FlatPlayfield playfield {};
	eng::field::Surface surface {};     ///< superficie sobre `playfield`
	eng::Block<eng::PlaneTag> memory {}; ///< Chip RAM reservada (depth planos)
	eng::u16 width = 0;
	eng::u16 height = 0;
	eng::u8 depth = 0;
	bool valid = false;                 ///< false tras resize hasta reenlazar
	bool needs_repaint = true;          ///< el contenido debe redibujarse al backing
};

} // namespace eng::ui
```

### 14.2 Datos por ventana

- `frame`: rect en pantalla de la ventana completa (incluye decoración).
- `client`: rect en pantalla del área cliente; su tamaño define el backing.
- `backing`: playfield + superficie del área cliente (o de la ventana entera; se recomienda
  **backing = cliente** y pintar el marco en el compose, que es barato).
- `content_dirty`: dirty en coordenadas del backing (lo que cambió dentro).
- `flags`: `Visible`, `Enabled`, `Focused`, `Modal`, más `FixedSize`/`Resizable`.

### 14.3 Operaciones

```cpp
class Compositor {
public:
	void set_screen(eng::field::Surface& screen) noexcept;
	void set_desktop(eng::u8 color) noexcept;

	CompWindow* add() noexcept;                       ///< al frente; nullptr si el pool está lleno
	void raise(CompWindow& w) noexcept;               ///< sube Z y daña su rect
	void move_window(CompWindow& w, eng::s16 nx, eng::s16 ny) noexcept;
	bool resize_window(CompWindow& w, eng::u16 nw, eng::u16 nh) noexcept; ///< false si no cabe

	void damage_screen(Rect r) noexcept;

	void present() noexcept;                          ///< compone por CPU (píxel a píxel)
	void present_blit(eng::graphics::FramePlan& plan) noexcept; ///< copia por Surface::blit (CPU/Blitter)
};
```

`present_blit` copia cada backing con `Surface::blit` (con `BlitterRaster` encola `CopyRect` en el
`FramePlan`, que el llamador ejecuta con `backend.execute_frame_plan`); si el rect no es copiable por
el Blitter (destino no alineado a palabra), cae al copiado por píxel de ese rect. Equivalencia con
`present()` en HOST-300 y **verificado en hardware** con la demo `300_gui_compositor` (tres ventanas
que se mueven y se recomponen por el Blitter).

**Mover** (sin repaint de vecinas):

```text
old = w.frame
w.frame = { nx, ny, w, h }
damage_screen(old ∪ w.frame)
```

En `present`, para cada región dañada de pantalla se recompone de abajo arriba (fondo → frente):
se recorre la pila en orden Z, se interseca cada ventana con la región, y por cada intersección
se copia el trozo del backing a la pantalla. Los huecos sin ventana se rellenan con el color del
escritorio. Nadie recibe «tápame ese área»: los bits siguen en el backing de la ventana de
detrás.

**Redimensionar**:

```text
si el tamaño cambia:
   reenlazar (o reasignar) el backing del pool fijo
   backing.needs_repaint = true           // solo ESTA ventana redibuja su contenido
w.frame = nuevo tamaño
invalidate_content(w, cliente completo)
damage_screen(old ∪ new)
```

**Cambio de Z**: `raise(w)` + `damage_screen(w.frame)`; el compose vuelve a copiar esa región en
el orden nuevo.

### 14.4 Compose de una región dañada

```text
para cada region D de pantalla dañada:
    fill(D, color_escritorio)                        // Blitter (rect fill)
    para cada ventana w en orden Z ascendente:
        I = interseccion(D, w.client_en_pantalla)
        si I vacia: continuar
        src = I relativo a w.client                  // coords del backing
        copiar backing[src] -> pantalla[I]           // Surface::blit / Blitter
    // decoracion: marco + barra de titulo de las ventanas que tocan D
```

Optimizaciones: alinear los *copies* a palabras (16 px) para el Blitter; fusionar `screen_dirty`
al añadir; y, si una ventana **opaca** cubre toda `D`, omitir las de debajo de delante hacia
atrás.

### 14.5 Drag y resize

- **Drag**: `MouseDown` en la barra de título → modo `Dragging` (guardar offset). `MouseMove` →
  `move_window` como máximo una vez por VBlank. `MouseUp` → fin. Durante el *drag* **no** se
  invalida el contenido: la imagen se desliza intacta.
- **Resize**: `MouseDown` en un borde → modo `Resizing` (máscara de borde L/R/T/B). Para A500 se
  recomienda **rubber-band**: dibujar solo el rectángulo de guías mientras se arrastra y hacer un
  único `resize_window` + repaint de contenido al soltar; el *live resize* (redibujar el
  contenido en cada paso) es caro con pocos recursos.

### 14.6 Memoria y límites (A500)

- Presupuesto: p. ej. **2 ventanas grandes** (320×256×4 bpp ≈ 40 KB cada una) o 4 ventanas
  ≤ 160×100, más un toast pequeño. Los backings son Chip RAM (los lee el Blitter).
- **Pool preasignado** de backings; `resize` falla (o se *clampa*) si no cabe en el pool.
- Mismo `depth` que el playfield visible para copias sin conversión de *pen*.
- Ventanas minimizadas sin backing (solo icono); al restaurar, reenlazar + repaint completo de
  esa ventana.

### 14.7 El cursor, como sprite de hardware

El puntero del ratón no debe ensuciar el framebuffer: se dibuja como **sprite de hardware**, de modo
que mover el ratón no genera *damage* ni obliga a recomponer. La utilidad reutilizable es
**`eng::ui::HardwareCursor`** (`eng/ui/hardware_cursor.hpp`): un sprite 16×16 de 1 palabra por línea
que encapsula la **estructura DMA** (POS, CTL, DAT/DATB y terminador) y la emisión de `SPRxPT` +
`DMACON` (SPREN) a la copperlist. No posee memoria: el llamador le da un buffer de **Chip RAM** con
`bind` (los sprites solo ven Chip RAM) y lo mueve con `set_position`. Ver HOST-301 y la demo 215.

```cpp
eng::ui::HardwareCursor cur;
cur.bind(chip_bytes, eng::ui::HardwareCursor::kBytes); // Chip RAM del llamador
cur.set_bitmap(dat, datb);                              // cuerpo (color 1) + contorno (color 2)
cur.set_position(mx, my);                               // sigue al ratón (poll_mouse)
// en la copperlist (etapa de compose): cur.emit_into(sc.scheduler());
```

Con el Blitter, un puntero software solo se justificaría en un modo sin sprites libres. La captura
PNG del runner **no incluye sprites**, así que el cursor se valida por registros/copperlist.

## 15. Integración con el mini-SO (`eng::os`)

La GUI **no requiere** el mini-SO, pero encaja con él sin cambios: el puente `os::Msg` →
`UiEvent` ([MINI_OS_MESSAGE_LOOP.md](MINI_OS_MESSAGE_LOOP.md) §8) es lo único que cambia; los
widgets reciben los mismos `UiEvent` tanto si vienen de mensajes como de entrada síncrona. El
VBlank del mini-SO hace de tick para el caret y los *toasts*, igual que `UiEventKind::Tick`.

El compositor encaja en ese bucle así:

```text
Msgs (input, VBlank, timers)
    → UiContext: foco, drag de titulo, hit-test
        - si cambia contenido de un widget: compositor.invalidate_content(win, local)
        - si move/resize/z:                 compositor.move/resize/raise (solo damage pantalla)
    → present (en VBlank):
        1. por cada ventana con content_dirty: pintar widgets → su backing
        2. por cada screen_dirty:             componer backings → pantalla
```

Paso 1 es el pintado de UI (destino = backing); paso 2 es solo *copies*.

## 16. Integración con el engine (overlay o fullscreen)

- **Overlay/HUD**: la GUI ocupa un `Surface` sobre el playfield visible y comparte *frame* con el
  juego; el juego y la GUI se turnan el `FramePlan`. Dirty pequeño para no tocar el fondo.
- **Fullscreen modal** (menú): cuando hay un `Dialog` modal, el juego puede pausar y la GUI
  pantalla completa con dirty de pantalla.
- **Ventanas movibles**: el compositor (`eng::ui::Compositor`) posee sus backings y compone sobre
  el playfield visible; el juego sigue dibujando debajo.

Para los *fills* y marcos grandes el `BlitterRaster` ya decide por `RasterPolicy::Auto` y
`min_blit_pixels`. Una mejora de rendimiento pendiente en el raster es un **`fill_rect` D-only**
(minterm `$FF`, sin fuentes) para cajas axis-aligned, que es más barato que el camino de
polígono de 4 vértices.

## 17. Presupuesto y rendimiento en A500

| Operación de UI | Camino |
|---|---|
| Clear de dirty / panel grande | `fill_rect` → Blitter (rect fill) |
| Barra de título | `fill_rect` |
| Bevel (raised/recessed) | 4 líneas → `FramePlan` (una racha de `Line`) |
| Texto / tick de checkbox / caret | CPU (barato por píxel contado) |
| Copia backing → pantalla | `Surface::blit` → Blitter (`CopyRect`) |
| Puntero del ratón | sprite de hardware (no toca el framebuffer) |

Reglas: no redibujar el árbol completo; usar dirty; agrupar jobs por plano; y medir con el
profiler (`docs/guides/optimization/METODOLOGIA_PROFILING.md`).

## 18. Qué NO hacer en A500

- Nada de `std::string`, `new`/`delete` ni heap por widget en el camino caliente.
- Nada de `virtual`/`dynamic_cast` en el pintado; `switch` por `WidgetType` o tabla estática.
- Nada de *alpha*, *anti-aliasing* ni sombras gordas.
- Nada de redibujar el árbol completo cada frame.
- Nada de depender de Intuition (se toma la máquina).
- Nada de *live resize* caro: *rubber-band*.

## 19. Notas de implementación (C++23 y sobrecarga cero)

Notas propias para que la librería sea barata y encaje con el engine:

- **Temas y métricas `constexpr`**: los presets (`kThemeWb13`, `kThemeWb2`, `kThemeFlat`) son
  `inline constexpr`; como el `UiContext` guarda el tema activo por valor, cambiar de *branding*
  es copiar una struct pequeña. Nada de variables globales mutables.
- **Diseño con *designated initializers***: construir un widget es
  `Button { .text = "Play", .on_click = f }`, legible y sin coste. Para etiquetas estáticas,
  `draw_text_literal<"...">` decodifica el UTF-8 en compilación (ya en `Surface`).
- **`concept` para lo dibujable**: un `concept Drawable = requires(T t, UiPainter& p) { t.draw(p); }`
  permite que el contexto acepte widgets del usuario sin herencia; la validación ocurre en
  compilación, como `GameModule` en el engine.
- **Tabla estática en vez de `virtual`**: `switch (w->type)` es exhaustivo (el compilador avisa
  de un tipo nuevo sin rama) y el compilador puede plegarlo por tipo con plantillas. Si se
  prefiere una tabla, un `constexpr` array de `void(*)(Widget&, UiPainter&)` indexado por
  `WidgetType` da despacho indirecto **predecible**.
- **Sin heap, capacidad fija**: árbol intrusivo, `DirtyList<Max>`, pila de ventanas `Window*[N]`,
  pool de backings. Todo con cotas en compilación.
- **Dirty fusionado en el `add`** (`constexpr` helpers `overlaps`/`merge`/`intersect`): evita
  *passes* redundantes y mantiene la lista pequeña.
- **Blitter por lotes**: el `FramePlan` ya encola y el backend agrupa (rachas de `Line`); los
  *fills* de las regiones sucias y los *copies* del compose se encolan en el **mismo** `FramePlan`
  del frame para que el backend los materialice sin esperas intermedias.
- **Reutilizar `FlatPlayfield`** para los backings: el mapping lógico→físico y el clip los pone
  el engine; la GUI solo pinta con `Surface`.
- **Puntero como sprite**: usa el sprite de hardware para no ensuciar el framebuffer.
- **IRQ-safe por diseño**: la GUI corre en el *hilo principal* del frame (o en el tick de
  VBlank); no se toca desde la ISR. La entrada llega por mensajes (mini-SO) o por *snapshot*, así
  que el pintado y la lógica no compiten.
- **Medición antes que micro-optimizar**: el coste real de la UI es el área de *fill*/*copy*; el
  dirty y la política CPU/Blitter son las palancas, no el *codegen* del pintado.

## 20. Referencias

- Rasterizado y seam CPU/Blitter: [RASTER.md](RASTER.md).
- Contexto de dibujo y fuentes: `engine/include/eng/field/surface.hpp`, `eng/graphics/font8.hpp`,
  `eng/graphics/font5x7.hpp`.
- Trabajo de Blitter y Copper por frame: [DISPLAY_COMPOSITION.md](DISPLAY_COMPOSITION.md) y
  `graphics/frame_plan.hpp`.
- Entrada y mensajes: [MINI_OS_MESSAGE_LOOP.md](MINI_OS_MESSAGE_LOOP.md) y
  `eng/include/eng/input/input.hpp`.
- Sprites (cursor): `docs/reference/amiga/techniques/` y demos 053/054/087.
- Plan de fases: [../../guides/roadmap/ROADMAP_GUI.md](../../guides/roadmap/ROADMAP_GUI.md).
