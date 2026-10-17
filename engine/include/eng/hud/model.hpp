#pragma once

/// \file model.hpp
/// **Modelo de HUD** (`eng::hud`): componentes de presentación de juego (medidores, contadores,
/// rejillas de iconos, slots, etiquetas, botones y paneles) con *handles* tipados, capacidades
/// fijas y *dirty* explícito, más la `DragSession` global y las listas de operaciones y *hit-test*
/// de capacidad fija. Es **puro** (sin hardware, sin `eng::ui`, sin `eng::rpg`): el juego es la
/// única fuente de verdad y el HUD refleja valores o **vistas** (`HudSlotView`). Diseño:
/// `docs/engine/architecture/HUD_SYSTEM.md` §5 y §7.
///
/// Verificación: **HOST-467**. **NO VERIFICADA** por demo (H1 `302_hud_band`, `ROADMAP_HUD.md`).

#include <eng/core/types/box.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/hud/region.hpp>
#include <eng/hud/theme.hpp>

namespace eng::hud {

/// Índice inválido de un `HudHandle` (0xff = sin componente).
inline constexpr eng::u8 kHudNoIndex = 0xffu;

/// Slot inválido de una rejilla (0xffff = sin slot).
inline constexpr eng::u16 kHudNoSlot = 0xffffu;

/// Dígitos máximos de un contador: 32 cubre `s32` en base 2; el buffer añade el signo.
inline constexpr eng::u8 kHudMaxDigits = 32u;

/// Caracteres del buffer de un contador (32 dígitos + signo).
inline constexpr eng::u8 kHudCounterChars = static_cast<eng::u8>(kHudMaxDigits + 1u);

/// Umbral de arrastre por defecto (px): por debajo del umbral un *down*+*up* es un *click*
/// (histéresis contra el temblor del ratón; `HUD_SYSTEM.md` §7.2).
inline constexpr eng::s16 kHudDragStartPx = 4;

/// Etiqueta de tipo de un `HudHandle` (uno por tipo de componente).
template <class Tag>
struct HudHandle {
	eng::u8 index = kHudNoIndex; ///< índice en el almacén del componente (0xff = inválido)

	/// ¿El handle apunta a un componente? (la *liveness* la confirma el `Hud`).
	[[nodiscard]] constexpr bool valid() const noexcept { return index != kHudNoIndex; }

	/// Igualdad por índice (mismo almacén de componentes).
	[[nodiscard]] friend constexpr bool operator==(HudHandle a, HudHandle b) noexcept {
		return a.index == b.index;
	}

	/// Desigualdad por índice.
	[[nodiscard]] friend constexpr bool operator!=(HudHandle a, HudHandle b) noexcept {
		return a.index != b.index;
	}
};

struct MeterTag {};
struct CounterTag {};
struct GridTag {};
struct LabelTag {};
struct ButtonTag {};
struct PanelTag {};
struct RegionTag {};

using MeterHandle = HudHandle<MeterTag>;     ///< handle de medidor
using CounterHandle = HudHandle<CounterTag>; ///< handle de contador
using GridHandle = HudHandle<GridTag>;       ///< handle de rejilla de iconos
using LabelHandle = HudHandle<LabelTag>;     ///< handle de etiqueta
using ButtonHandle = HudHandle<ButtonTag>;   ///< handle de botón
using PanelHandle = HudHandle<PanelTag>;     ///< handle de panel
using RegionHandle = HudHandle<RegionTag>;   ///< handle de región

/// Medidor (vida, energía, munición): valor visible, máximo y estilo de presentación.
struct Meter {
	eng::s32 value = 0;                ///< valor mostrado (el juego es la fuente de verdad)
	eng::s32 max = 0;                  ///< máximo (0 = sin escala)
	MeterStyle style = MeterStyle::Linear; ///< estilo de presentación (del tema)
	eng::u8 pulse_ttl = 0u;            ///< frames de pulso visual restantes (0 = sin pulso)
	bool dirty = true;                 ///< true si cambió desde el último `clear_dirty`
};

/// Contador (puntos, tiempo): valor, formato y texto formateado en un buffer de capacidad fija.
struct Counter {
	eng::s32 value = 0;                       ///< valor mostrado
	eng::u8 digits = 1u;                      ///< ancho mínimo en dígitos (1..kHudMaxDigits)
	eng::u8 base = 10u;                       ///< base (2..36)
	bool pad_zero = false;                    ///< true = rellena con ceros hasta `digits`
	bool dirty = true;                        ///< true si cambió desde el último `clear_dirty`
	eng::u8 length = 0u;                      ///< longitud del texto formateado
	char text[kHudCounterChars] {};           ///< texto formateado (sin heap; válido hasta el siguiente `set`)
};

/// Vista de un slot de inventario/equipo: descriptor copiable que el juego refresca por frame.
/// El HUD **no** guarda el objeto (`ItemInstance`); solo lo que hace falta para pintarlo.
struct HudSlotView {
	eng::u16 item_id = 0u;    ///< id lógico del objeto (0 = vacío)
	eng::u16 icon_frame = 0u; ///< frame del atlas de iconos
	eng::u16 qty = 0u;        ///< cantidad apilada
	eng::u8 flags = 0u;       ///< bits `kHudSlot*` (vacío, habilitado, equipado, seleccionado)
};

/// Bit de `HudSlotView::flags`: el slot está vacío.
inline constexpr eng::u8 kHudSlotEmpty = 0x01u;
/// Bit de `HudSlotView::flags`: el slot acepta interacción.
inline constexpr eng::u8 kHudSlotEnabled = 0x02u;
/// Bit de `HudSlotView::flags`: el objeto está equipado.
inline constexpr eng::u8 kHudSlotEquipped = 0x04u;
/// Bit de `HudSlotView::flags`: el slot está resaltado por el juego.
inline constexpr eng::u8 kHudSlotSelected = 0x08u;

/// Rejilla de iconos (inventario, *crafting*, hechizos): celdas, selección y foco independientes.
struct IconGrid {
	eng::u8 rows = 0u;               ///< filas (≥1)
	eng::u8 cols = 0u;               ///< columnas (≥1)
	eng::u8 pitch_x = 16u;           ///< paso horizontal entre celdas (px)
	eng::u8 pitch_y = 16u;           ///< paso vertical entre celdas (px)
	eng::u16 selection = kHudNoSlot; ///< slot seleccionado (0xffff = ninguno)
	eng::u16 focus = 0u;             ///< slot con foco de joystick/teclado
	eng::u8 scroll_row = 0u;         ///< primera fila visible
	eng::Span<const HudSlotView> slots {}; ///< vistas enlazadas por el juego (observador)
	bool dirty = true;               ///< true si cambió desde el último `clear_dirty`
};

/// Etiqueta de texto estático (o refrescado por el juego); el texto es un observador.
struct Label {
	eng::Span<const char> text {}; ///< texto (sin terminador; el juego lo mantiene vivo)
	bool dirty = true;             ///< true si cambió desde el último `clear_dirty`
};

/// Botón de HUD (barra de verbos, menú de acción): etiqueta, habilitado y pulsado.
struct Button {
	eng::Span<const char> text {}; ///< etiqueta (observador)
	bool enabled = true;           ///< false = no interactivo
	bool pressed = false;          ///< true mientras está pulsado
	bool dirty = true;             ///< true si cambió desde el último `clear_dirty`
};

/// Panel de HUD (columna/fila/rejilla): agrupa y recorta; **no** es una ventana de `eng::ui`.
struct Panel {
	eng::Box bounds {};     ///< rectángulo del panel en la región
	eng::u8 columns = 1u;   ///< columnas del layout interno (1 = columna)
	bool dirty = true;      ///< true si cambió desde el último `clear_dirty`
};

/// Tipo de operación de dibujo del HUD.
enum class HudOpKind : eng::u8 {
	Fill = 0, ///< relleno de un rectángulo con un índice de color
	Frame,    ///< marco de un rectángulo
	Blit,     ///< copia desde el atlas de iconos (`asset` = frame)
	Text,     ///< texto (`text` = caracteres)
	Digits,   ///< dígitos formateados de un contador (`text` = buffer)
	Sprite,   ///< sprite de hardware (`asset` = canal)
	Palette,  ///< intención de paleta de la región
};

/// Operación de dibujo emitida por el modelo; el presentador la materializa (CPU/Blitter/Copper).
/// `text` es un observador válido hasta `present()` en el mismo frame.
struct HudDrawOp {
	HudOpKind kind = HudOpKind::Fill; ///< tipo de operación
	eng::Box box {};                  ///< región afectada
	eng::u8 color = 0u;               ///< índice lógico de color
	eng::u8 flags = 0u;               ///< bits del presentador (máscara, alineación…)
	eng::u16 asset = 0u;              ///< frame del atlas o canal de sprite
	eng::Span<const char> text {};    ///< texto/dígitos (observador)
};

/// Entrada de *hit-test* rectangular de una región (rectángulos ordenados por capa).
struct HudHitRect {
	eng::Box box {};      ///< zona interactiva
	eng::u16 id = 0u;     ///< id lógico del objetivo (slot, botón…)
	eng::u8 layer = 0u;   ///< orden dentro de la región; mayor = delante
};

/// Entrada de *hit-test* de una rejilla: la celda se calcula por fórmula al recibir el evento
/// (sin generar N rectángulos). `first_id` es el id lógico del slot [0][0].
struct HudGridHit {
	eng::Box area {};        ///< zona de la rejilla
	eng::u8 cols = 0u;       ///< columnas
	eng::u8 rows = 0u;       ///< filas
	eng::u8 pitch_x = 16u;   ///< paso horizontal
	eng::u8 pitch_y = 16u;   ///< paso vertical
	eng::u16 first_id = 0u;  ///< id del primer slot
};

/// Fase de la sesión de arrastre (máquina de puntero de `HUD_SYSTEM.md` §7.2).
enum class DragPhase : eng::u8 {
	Idle = 0,  ///< sin interacción
	Pressed,   ///< botón pulsado sobre un objetivo (puede ser *click*)
	Dragging,  ///< umbral superado; el puntero está capturado
	Dropped,   ///< soltado sobre un destino válido (terminal hasta `drag_reset`)
	Cancelled, ///< cancelado (terminal hasta `drag_reset`)
};

/// Sesión de arrastre global: **una sola** en todo el HUD.
struct DragSession {
	DragPhase phase = DragPhase::Idle; ///< fase actual
	GridHandle source {};              ///< rejilla de origen
	eng::u16 source_slot = kHudNoSlot; ///< slot de origen
	eng::u16 item_id = 0u;             ///< objeto arrastrado
	eng::u16 qty = 0u;                 ///< cantidad arrastrada
	eng::s16 start_x = 0;              ///< punto de pulsación (x)
	eng::s16 start_y = 0;              ///< punto de pulsación (y)
	eng::s16 x = 0;                    ///< posición actual del puntero (x)
	eng::s16 y = 0;                    ///< posición actual del puntero (y)
	bool dirty = true;                 ///< true si cambió desde el último `clear_dirty`
};

/// Formatea `c.value` en `c.text` según base, dígitos y relleno. Usa **sumas repetidas** para
/// construir las potencias (base ≤ 36) y **sustracción repetida** para extraer los dígitos: sin
/// multiplicación ni división (`__mulsi3`/`__udivsi3`) en la actualización, y solo se ejecuta
/// cuando el valor cambia, no por frame. Devuelve la longitud del texto.
[[nodiscard]] inline eng::u8 counter_format(Counter& c) noexcept {
	const bool negative = c.value < 0;
	eng::u32 mag = negative ? (0u - static_cast<eng::u32>(c.value))
				: static_cast<eng::u32>(c.value);
	eng::u32 pow[kHudMaxDigits] {};
	pow[0] = 1u;
	eng::u8 max_pow = 1u;
	while (max_pow < kHudMaxDigits) {
		const eng::u32 base_p = pow[max_pow - 1u];
		if (base_p > mag) {
			break; // la potencia ya supera la magnitud: no hay más dígitos
		}
		eng::u32 next = 0u;
		bool over = false;
		for (eng::u8 j = 0u; j < c.base; ++j) {
			if (next > mag - base_p) {
				over = true; // `base_p * base` supera la magnitud: no hace falta la potencia
				break;
			}
			next += base_p;
		}
		if (over) {
			break;
		}
		pow[max_pow] = next;
		++max_pow;
	}
	const eng::u8 needed = max_pow;
	char digits[kHudMaxDigits] {};
	for (eng::u8 i = 0u; i < needed; ++i) {
		const eng::u32 place = pow[needed - 1u - i];
		eng::u8 d = 0u;
		while (mag >= place) {
			mag -= place;
			++d;
		}
		digits[i] = (d < 10u) ? static_cast<char>('0' + d)
				      : static_cast<char>('A' + (d - 10u));
	}
	const eng::u8 width = (c.pad_zero && c.digits > needed) ? c.digits : needed;
	eng::u8 pos = 0u;
	if (negative) {
		c.text[pos++] = '-';
	}
	for (eng::u8 i = width; i > needed; --i) {
		c.text[pos++] = '0';
	}
	for (eng::u8 i = 0u; i < needed; ++i) {
		c.text[pos++] = digits[i];
	}
	c.length = pos;
	c.dirty = true;
	return pos;
}

/// Modelo de HUD de capacidad fija (sin heap). El juego crea componentes en el *setup*, actualiza
/// valores o vistas y consulta las listas de operaciones y *hit-test*; el presentador las
/// materializa. `MaxWidgets`/`MaxRegions` están limitados a 255 (el índice del handle es `u8`).
template <eng::u16 MaxRegions = 2u, eng::u16 MaxWidgets = 24u, eng::u16 MaxSlots = 64u,
	  eng::u16 MaxDrawOps = 64u, eng::u16 MaxHitRects = 48u>
class Hud {
	static_assert(MaxWidgets >= 1u && MaxWidgets <= 255u, "MaxWidgets fuera de rango (1..255)");
	static_assert(MaxRegions >= 1u && MaxRegions <= 255u, "MaxRegions fuera de rango (1..255)");

public:
	// --- Regiones ---------------------------------------------------------------------------

	/// Añade una región validada. Devuelve un handle inválido si la especificación no es válida
	/// o no cabe (`MaxRegions`).
	[[nodiscard]] RegionHandle add_region(const HudRegionSpec& spec) noexcept {
		if (!region_valid(spec) || m_regions_used >= MaxRegions) {
			return {};
		}
		m_regions[m_regions_used] = spec;
		return RegionHandle {static_cast<eng::u8>(m_regions_used++)};
	}

	/// Región por handle (el llamador garantiza un handle válido).
	[[nodiscard]] const HudRegionSpec& region(RegionHandle handle) const noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_regions_used);
		return m_regions[handle.index];
	}

	/// Número de regiones declaradas.
	[[nodiscard]] eng::u16 region_count() const noexcept { return m_regions_used; }

	// --- Medidores --------------------------------------------------------------------------

	/// Añade un medidor; handle inválido si no cabe (`MaxWidgets`).
	[[nodiscard]] MeterHandle add_meter(MeterStyle style = MeterStyle::Linear) noexcept {
		if (m_widgets_used >= MaxWidgets) {
			return {};
		}
		Meter& m = m_meters[m_meters_used];
		m = Meter {};
		m.style = style;
		++m_widgets_used;
		return MeterHandle {static_cast<eng::u8>(m_meters_used++)};
	}

	/// Medidor por handle.
	[[nodiscard]] const Meter& meter(MeterHandle handle) const noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_meters_used);
		return m_meters[handle.index];
	}

	/// Fija el valor de un medidor. Devuelve `true` solo si el valor visible cambió (y marca
	/// *dirty*); fijar el mismo valor no repinta.
	[[nodiscard]] bool set_meter(MeterHandle handle, eng::s32 value) noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_meters_used);
		Meter& m = m_meters[handle.index];
		if (m.value == value) {
			return false;
		}
		m.value = value;
		mark_dirty(m);
		return true;
	}

	/// Fija valor y máximo de un medidor; `true` si cambió alguno de los dos.
	[[nodiscard]] bool set_meter(MeterHandle handle, eng::s32 value, eng::s32 max) noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_meters_used);
		Meter& m = m_meters[handle.index];
		if (m.value == value && m.max == max) {
			return false;
		}
		m.value = value;
		m.max = max;
		mark_dirty(m);
		return true;
	}

	// --- Contadores -------------------------------------------------------------------------

	/// Añade un contador con base (2..36), dígitos mínimos (1..kHudMaxDigits) y relleno de ceros.
	/// Handle inválido si los parámetros no son válidos o no cabe (`MaxWidgets`).
	[[nodiscard]] CounterHandle add_counter(eng::u8 base = 10u, eng::u8 digits = 1u,
						bool pad_zero = false) noexcept {
		if (m_widgets_used >= MaxWidgets || base < 2u || base > 36u || digits < 1u ||
		    digits > kHudMaxDigits) {
			return {};
		}
		Counter& c = m_counters[m_counters_used];
		c = Counter {};
		c.base = base;
		c.digits = digits;
		c.pad_zero = pad_zero;
		++m_widgets_used;
		return CounterHandle {static_cast<eng::u8>(m_counters_used++)};
	}

	/// Contador por handle.
	[[nodiscard]] const Counter& counter(CounterHandle handle) const noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_counters_used);
		return m_counters[handle.index];
	}

	/// Texto formateado de un contador (observador válido hasta el siguiente `set_counter`).
	[[nodiscard]] eng::Span<const char> counter_text(CounterHandle handle) const noexcept {
		const Counter& c = counter(handle);
		return eng::Span<const char> {c.text, c.length};
	}

	/// Fija el valor de un contador; formatea el texto **solo si cambia** (y marca *dirty*).
	[[nodiscard]] bool set_counter(CounterHandle handle, eng::s32 value) noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_counters_used);
		Counter& c = m_counters[handle.index];
		if (c.value == value) {
			return false;
		}
		c.value = value;
		(void)counter_format(c);
		mark_dirty(c);
		return true;
	}

	// --- Rejillas ---------------------------------------------------------------------------

	/// Añade una rejilla de `rows`×`cols` celdas con paso (`pitch_x`, `pitch_y`) en píxeles.
	/// Handle inválido si la geometría no es válida o no cabe (`MaxWidgets`).
	[[nodiscard]] GridHandle add_grid(eng::u8 rows, eng::u8 cols, eng::u8 pitch_x = 16u,
					  eng::u8 pitch_y = 16u) noexcept {
		if (m_widgets_used >= MaxWidgets || rows == 0u || cols == 0u || pitch_x == 0u ||
		    pitch_y == 0u) {
			return {};
		}
		IconGrid& g = m_grids[m_grids_used];
		g = IconGrid {};
		g.rows = rows;
		g.cols = cols;
		g.pitch_x = pitch_x;
		g.pitch_y = pitch_y;
		++m_widgets_used;
		return GridHandle {static_cast<eng::u8>(m_grids_used++)};
	}

	/// Rejilla por handle.
	[[nodiscard]] const IconGrid& grid(GridHandle handle) const noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_grids_used);
		return m_grids[handle.index];
	}

	/// Enlaza las **vistas** de slot que el juego refresca (observador, sin copia). Devuelve
	/// `false` si el total de slots enlazados supera `MaxSlots`.
	[[nodiscard]] bool bind_grid(GridHandle handle, eng::Span<const HudSlotView> slots) noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_grids_used);
		IconGrid& g = m_grids[handle.index];
		const eng::u16 old_size = static_cast<eng::u16>(g.slots.size());
		const eng::u16 new_size = static_cast<eng::u16>(slots.size());
		if (m_slots_used - old_size + new_size > MaxSlots) {
			return false;
		}
		m_slots_used = static_cast<eng::u16>(m_slots_used - old_size + new_size);
		g.slots = slots;
		mark_dirty(g);
		return true;
	}

	/// Selecciona un slot (dentro de las vistas enlazadas); `true` si cambió.
	[[nodiscard]] bool grid_select(GridHandle handle, eng::u16 slot) noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_grids_used);
		IconGrid& g = m_grids[handle.index];
		if (slot >= g.slots.size() || g.selection == slot) {
			return false;
		}
		g.selection = slot;
		mark_dirty(g);
		return true;
	}

	/// Mueve el foco por filas/columnas con recorte a la rejilla (sin división: el recorrido se
	/// hace por sustracción repetida). `true` si el foco cambió.
	[[nodiscard]] bool grid_move_focus(GridHandle handle, eng::s8 dx, eng::s8 dy) noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_grids_used);
		IconGrid& g = m_grids[handle.index];
		const eng::u16 count = static_cast<eng::u16>(g.slots.size());
		if (count == 0u || g.cols == 0u || g.rows == 0u) {
			return false;
		}
		eng::u16 idx = g.focus;
		eng::u8 row = 0u;
		while (idx >= g.cols && row + 1u < g.rows) {
			idx = static_cast<eng::u16>(idx - g.cols);
			++row;
		}
		eng::u8 col = static_cast<eng::u8>(idx);
		const eng::s16 nr = static_cast<eng::s16>(row) + dy;
		const eng::s16 nc = static_cast<eng::s16>(col) + dx;
		if (nr < 0 || nr >= static_cast<eng::s16>(g.rows) || nc < 0 ||
		    nc >= static_cast<eng::s16>(g.cols)) {
			return false;
		}
		eng::u16 next = static_cast<eng::u16>(nc);
		for (eng::u8 i = 0u; i < static_cast<eng::u8>(nr); ++i) {
			next = static_cast<eng::u16>(next + g.cols);
		}
		if (next >= count || g.focus == next) {
			return false;
		}
		g.focus = next;
		mark_dirty(g);
		return true;
	}

	// --- Etiquetas, botones y paneles -------------------------------------------------------

	/// Añade una etiqueta; handle inválido si no cabe (`MaxWidgets`).
	[[nodiscard]] LabelHandle add_label(eng::Span<const char> text) noexcept {
		if (m_widgets_used >= MaxWidgets) {
			return {};
		}
		Label& l = m_labels[m_labels_used];
		l = Label {};
		l.text = text;
		++m_widgets_used;
		return LabelHandle {static_cast<eng::u8>(m_labels_used++)};
	}

	/// Etiqueta por handle.
	[[nodiscard]] const Label& label(LabelHandle handle) const noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_labels_used);
		return m_labels[handle.index];
	}

	/// Añade un botón; handle inválido si no cabe (`MaxWidgets`).
	[[nodiscard]] ButtonHandle add_button(eng::Span<const char> text) noexcept {
		if (m_widgets_used >= MaxWidgets) {
			return {};
		}
		Button& b = m_buttons[m_buttons_used];
		b = Button {};
		b.text = text;
		++m_widgets_used;
		return ButtonHandle {static_cast<eng::u8>(m_buttons_used++)};
	}

	/// Botón por handle.
	[[nodiscard]] const Button& button(ButtonHandle handle) const noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_buttons_used);
		return m_buttons[handle.index];
	}

	/// Añade un panel; handle inválido si no cabe (`MaxWidgets`).
	[[nodiscard]] PanelHandle add_panel(eng::Box bounds) noexcept {
		if (m_widgets_used >= MaxWidgets) {
			return {};
		}
		Panel& p = m_panels[m_panels_used];
		p = Panel {};
		p.bounds = bounds;
		++m_widgets_used;
		return PanelHandle {static_cast<eng::u8>(m_panels_used++)};
	}

	/// Panel por handle.
	[[nodiscard]] const Panel& panel(PanelHandle handle) const noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_panels_used);
		return m_panels[handle.index];
	}

	// --- Arrastre ---------------------------------------------------------------------------

	/// Inicia una sesión de arrastre desde un slot (solo desde `Idle`). Pasa a `Pressed`;
	/// el umbral decide si termina en *click* o en `Dragging`.
	[[nodiscard]] bool drag_begin(GridHandle source, eng::u16 slot, eng::u16 item_id, eng::u16 qty,
				      eng::s16 x, eng::s16 y) noexcept {
		if (m_drag.phase != DragPhase::Idle) {
			return false;
		}
		m_drag = DragSession {};
		m_drag.phase = DragPhase::Pressed;
		m_drag.source = source;
		m_drag.source_slot = slot;
		m_drag.item_id = item_id;
		m_drag.qty = qty;
		m_drag.start_x = x;
		m_drag.start_y = y;
		m_drag.x = x;
		m_drag.y = y;
		mark_dirty(m_drag);
		return true;
	}

	/// Actualiza la posición del puntero. Desde `Pressed` promociona a `Dragging` si se supera
	/// `threshold_px` en cualquier eje; `false` si no hay sesión activa.
	[[nodiscard]] bool drag_move(eng::s16 x, eng::s16 y, eng::s16 threshold_px) noexcept {
		if (m_drag.phase != DragPhase::Pressed && m_drag.phase != DragPhase::Dragging) {
			return false;
		}
		if (m_drag.phase == DragPhase::Pressed) {
			const eng::s16 adx = static_cast<eng::s16>(x >= m_drag.start_x ? x - m_drag.start_x
										: m_drag.start_x - x);
			const eng::s16 ady = static_cast<eng::s16>(y >= m_drag.start_y ? y - m_drag.start_y
										: m_drag.start_y - y);
			if (adx < threshold_px && ady < threshold_px) {
				m_drag.x = x;
				m_drag.y = y;
				return true;
			}
			m_drag.phase = DragPhase::Dragging;
			mark_dirty(m_drag);
		}
		m_drag.x = x;
		m_drag.y = y;
		return true;
	}

	/// Suelta el botón sin superar el umbral: *click* válido desde `Pressed` (vuelve a `Idle`).
	/// Desde `Dragging` es `false`: la sesión debe terminar con `drag_drop` o `drag_cancel`.
	[[nodiscard]] bool drag_release() noexcept {
		if (m_drag.phase != DragPhase::Pressed) {
			return false;
		}
		m_drag.phase = DragPhase::Idle;
		mark_dirty(m_drag);
		return true;
	}

	/// Suelta sobre un destino (solo desde `Dragging`): pasa a `Dropped` (terminal hasta `reset`).
	[[nodiscard]] bool drag_drop() noexcept {
		if (m_drag.phase != DragPhase::Dragging) {
			return false;
		}
		m_drag.phase = DragPhase::Dropped;
		mark_dirty(m_drag);
		return true;
	}

	/// Cancela la sesión (desde `Pressed` o `Dragging`): pasa a `Cancelled`.
	[[nodiscard]] bool drag_cancel() noexcept {
		if (m_drag.phase != DragPhase::Pressed && m_drag.phase != DragPhase::Dragging) {
			return false;
		}
		m_drag.phase = DragPhase::Cancelled;
		mark_dirty(m_drag);
		return true;
	}

	/// Devuelve la sesión a `Idle` tras `Dropped`/`Cancelled`; `true` si había algo que resetear.
	[[nodiscard]] bool drag_reset() noexcept {
		if (m_drag.phase != DragPhase::Dropped && m_drag.phase != DragPhase::Cancelled) {
			return false;
		}
		m_drag = DragSession {};
		mark_dirty(m_drag);
		return true;
	}

	/// Sesión de arrastre actual (solo lectura).
	[[nodiscard]] const DragSession& drag() const noexcept { return m_drag; }

	// --- Operaciones y *hit-test* -----------------------------------------------------------

	/// Encola una operación de dibujo; `false` si la lista está llena (`MaxDrawOps`).
	[[nodiscard]] bool push_op(const HudDrawOp& op) noexcept {
		if (m_ops_used >= MaxDrawOps) {
			return false;
		}
		m_ops[m_ops_used++] = op;
		return true;
	}

	/// Vacía la lista de operaciones (tras materializarlas en el frame).
	void clear_ops() noexcept { m_ops_used = 0u; }

	/// Operaciones encoladas (observador).
	[[nodiscard]] eng::Span<const HudDrawOp> ops() const noexcept {
		return eng::Span<const HudDrawOp> {m_ops.data(), m_ops_used};
	}

	/// Encola una entrada de *hit-test* rectangular; `false` si la lista está llena.
	[[nodiscard]] bool push_hit(const HudHitRect& hit) noexcept {
		if (m_hits_used >= MaxHitRects) {
			return false;
		}
		m_hits[m_hits_used++] = hit;
		return true;
	}

	/// Vacía la lista de *hit-test* (tras consumir los eventos del frame).
	void clear_hits() noexcept { m_hits_used = 0u; }

	/// Entradas de *hit-test* encoladas (observador).
	[[nodiscard]] eng::Span<const HudHitRect> hits() const noexcept {
		return eng::Span<const HudHitRect> {m_hits.data(), m_hits_used};
	}

	/// Encola una entrada de *hit-test* de rejilla; `false` si la lista está llena.
	[[nodiscard]] bool push_grid_hit(const HudGridHit& hit) noexcept {
		if (m_grid_hits_used >= MaxHitRects) {
			return false;
		}
		m_grid_hits[m_grid_hits_used++] = hit;
		return true;
	}

	/// Vacía la lista de *hit-test* de rejillas.
	void clear_grid_hits() noexcept { m_grid_hits_used = 0u; }

	/// Entradas de *hit-test* de rejilla encoladas (observador).
	[[nodiscard]] eng::Span<const HudGridHit> grid_hits() const noexcept {
		return eng::Span<const HudGridHit> {m_grid_hits.data(), m_grid_hits_used};
	}

	// --- Suciedad ---------------------------------------------------------------------------

	/// Revisión de suciedad: sube una vez por cada cambio visible (los *dirty* repetidos sin
	/// cambio no cuentan). El presentador la usa para saltarse frames estáticos.
	[[nodiscard]] eng::u32 dirty_revision() const noexcept { return m_dirty_revision; }

	/// ¿Hay algún componente pendiente de repintar?
	[[nodiscard]] bool any_dirty() const noexcept {
		for (eng::u8 i = 0u; i < m_meters_used; ++i) {
			if (m_meters[i].dirty) {
				return true;
			}
		}
		for (eng::u8 i = 0u; i < m_counters_used; ++i) {
			if (m_counters[i].dirty) {
				return true;
			}
		}
		for (eng::u8 i = 0u; i < m_grids_used; ++i) {
			if (m_grids[i].dirty) {
				return true;
			}
		}
		for (eng::u8 i = 0u; i < m_labels_used; ++i) {
			if (m_labels[i].dirty) {
				return true;
			}
		}
		for (eng::u8 i = 0u; i < m_buttons_used; ++i) {
			if (m_buttons[i].dirty) {
				return true;
			}
		}
		for (eng::u8 i = 0u; i < m_panels_used; ++i) {
			if (m_panels[i].dirty) {
				return true;
			}
		}
		return m_drag.dirty;
	}

	/// Marca todo como repintado (el presentador lo llama tras materializar el frame). No altera
	/// los valores: un `set` con el mismo valor sigue sin marcar *dirty*.
	void clear_dirty() noexcept {
		for (eng::u8 i = 0u; i < m_meters_used; ++i) {
			m_meters[i].dirty = false;
		}
		for (eng::u8 i = 0u; i < m_counters_used; ++i) {
			m_counters[i].dirty = false;
		}
		for (eng::u8 i = 0u; i < m_grids_used; ++i) {
			m_grids[i].dirty = false;
		}
		for (eng::u8 i = 0u; i < m_labels_used; ++i) {
			m_labels[i].dirty = false;
		}
		for (eng::u8 i = 0u; i < m_buttons_used; ++i) {
			m_buttons[i].dirty = false;
		}
		for (eng::u8 i = 0u; i < m_panels_used; ++i) {
			m_panels[i].dirty = false;
		}
		m_drag.dirty = false;
	}

private:
	/// Marca un componente como sucio y sube la revisión: un solo punto de conteo por cambio
	/// visible (los `set` con el mismo valor no llegan aquí).
	template <class T>
	void mark_dirty(T& component) noexcept {
		component.dirty = true;
		++m_dirty_revision;
	}

	eng::util::Array<HudRegionSpec, MaxRegions> m_regions {};
	eng::u16 m_regions_used = 0u;

	eng::util::Array<Meter, MaxWidgets> m_meters {};
	eng::util::Array<Counter, MaxWidgets> m_counters {};
	eng::util::Array<IconGrid, MaxWidgets> m_grids {};
	eng::util::Array<Label, MaxWidgets> m_labels {};
	eng::util::Array<Button, MaxWidgets> m_buttons {};
	eng::util::Array<Panel, MaxWidgets> m_panels {};
	eng::u8 m_meters_used = 0u;
	eng::u8 m_counters_used = 0u;
	eng::u8 m_grids_used = 0u;
	eng::u8 m_labels_used = 0u;
	eng::u8 m_buttons_used = 0u;
	eng::u8 m_panels_used = 0u;
	eng::u16 m_widgets_used = 0u;
	eng::u16 m_slots_used = 0u;

	DragSession m_drag {};

	eng::util::Array<HudDrawOp, MaxDrawOps> m_ops {};
	eng::util::Array<HudHitRect, MaxHitRects> m_hits {};
	eng::util::Array<HudGridHit, MaxHitRects> m_grid_hits {};
	eng::u16 m_ops_used = 0u;
	eng::u16 m_hits_used = 0u;
	eng::u16 m_grid_hits_used = 0u;

	eng::u32 m_dirty_revision = 0u;
};

} // namespace eng::hud
