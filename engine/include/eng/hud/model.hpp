#pragma once

/// \file model.hpp
/// **Modelo de HUD** (`eng::hud`): componentes de presentación de juego (medidores, contadores,
/// rejillas de iconos, slots, etiquetas, botones y paneles) con *handles* tipados, capacidades
/// fijas y *dirty* explícito, más la `DragSession` global y las listas de operaciones y *hit-test*
/// de capacidad fija. Es **puro** (sin hardware, sin `eng::ui`, sin `eng::rpg`): el juego es la
/// única fuente de verdad y el HUD refleja valores o **vistas** (`HudSlotView`). Diseño:
/// `docs/engine/architecture/HUD_SYSTEM.md` §5 y §7.
///
/// Verificación: **HOST-467**; demo `302_hud_band` (contador, medidor y pips en hardware).

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

/// Capacidad del texto **por valor** de etiquetas, botones y operaciones de dibujo: se copia
/// al emitir y **no** se guarda un observador. Cierra el fallo clásico de las listas de
/// operaciones: todas apuntando al mismo buffer mutable, de modo que el texto pintado acaba
/// siendo el último escrito (o basura si el buffer muere). Con el texto por valor, reutilizar
/// el buffer de origen es seguro por construcción. 24 cubre las etiquetas y el prefijo de la
/// línea de depuración de las demos.
inline constexpr eng::u8 kHudTextMax = 24u;

namespace hud_detail {

/// Copia `text` al buffer `dst`/`len` (sin heap). `false` si no cabe: **rechazo controlado**,
/// no se trunca en silencio (un texto recortado es un fallo de contenido, no de espacio).
constexpr bool copy_text(char (&dst)[kHudTextMax], eng::u8& len,
			 eng::Span<const char> text) noexcept {
	if (text.size() > static_cast<eng::usize>(kHudTextMax)) {
		return false;
	}
	len = static_cast<eng::u8>(text.size());
	for (eng::usize i = 0u; i < text.size(); ++i) {
		dst[i] = text[i];
	}
	return true;
}

} // namespace hud_detail

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
	CounterStyle style = CounterStyle::Numeric; ///< estilo de presentación (*skins*, H6)
	eng::u16 present_max = 0u;                ///< escala para `Pips`/`Bar` (0 = sin escala)
	bool dirty = true;                        ///< true si cambió desde el último `clear_dirty`
	eng::u8 length = 0u;                      ///< longitud del texto formateado
	char text[kHudCounterChars] {};           ///< texto formateado (sin heap; válido hasta el siguiente `set_counter`)
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
	eng::u16 hover = kHudNoSlot;     ///< slot bajo el puntero (0xffff = ninguno)
	eng::u8 scroll_row = 0u;         ///< primera fila visible
	eng::Span<const HudSlotView> slots {}; ///< vistas enlazadas por el juego (observador)
	bool dirty = true;               ///< true si cambió desde el último `clear_dirty`
};

/// Etiqueta de texto estático (o refrescado por el juego); el texto se copia **por valor**
/// al añadirla (el juego puede reutilizar su buffer sin cambiar lo pintado).
struct Label {
	eng::Box box {};                   ///< posición en la región (la fija el juego; hit-test y pintado)
	eng::u8 text_len = 0u;             ///< longitud válida de `text`
	char text[kHudTextMax] {};         ///< texto por valor (sin heap)
	bool dirty = true;                 ///< true si cambió desde el último `clear_dirty`
	/// Copia el texto a la etiqueta; `false` si no cabe en `kHudTextMax` (rechazo controlado).
	[[nodiscard]] bool set_text(eng::Span<const char> t) noexcept {
		return hud_detail::copy_text(text, text_len, t);
	}
	/// Texto vigente (observador a la propia etiqueta).
	[[nodiscard]] eng::Span<const char> text_view() const noexcept {
		return eng::Span<const char> {text, text_len};
	}
};

/// Botón de HUD (barra de verbos, menú de acción): etiqueta, habilitado, pulsado y **hover**.
/// `box` es la geometría (la comparten el pintado y el *hit-test*/foco, `HUD_SYSTEM.md` §7.4).
struct Button {
	eng::Box box {};                   ///< posición/tamaño en la región
	eng::u8 text_len = 0u;             ///< longitud válida de `text`
	char text[kHudTextMax] {};         ///< etiqueta por valor (sin heap)
	bool enabled = true;               ///< false = no interactivo
	bool pressed = false;              ///< true mientras está pulsado
	bool hover = false;                ///< true mientras el puntero está encima
	bool dirty = true;                 ///< true si cambió desde el último `clear_dirty`
	/// Copia la etiqueta al botón; `false` si no cabe en `kHudTextMax` (rechazo controlado).
	[[nodiscard]] bool set_text(eng::Span<const char> t) noexcept {
		return hud_detail::copy_text(text, text_len, t);
	}
	/// Etiqueta vigente (observador al propio botón).
	[[nodiscard]] eng::Span<const char> text_view() const noexcept {
		return eng::Span<const char> {text, text_len};
	}
};

/// Panel de HUD (columna/fila/rejilla): agrupa y recorta; **no** es una ventana de `eng::ui`.
struct Panel {
	eng::Box bounds {};     ///< rectángulo del panel en la región
	eng::u8 columns = 1u;   ///< columnas del layout interno (1 = columna)
	bool dirty = true;      ///< true si cambió desde el último `clear_dirty`
};

/// **Celda `index` de un panel** (layout por columnas, H5): posición por fórmula desde
/// `bounds`; `false` si la celda no cabe en el panel. Se llama al refrescar la rejilla o al
/// montar el layout (no por píxel); `columns` potencia de dos permite sustituir el `%`/`/`
/// por máscara/desplazamiento.
[[nodiscard]] constexpr bool panel_cell(const Panel& p, eng::u16 cell_w, eng::u16 cell_h,
					eng::u16 index, eng::Box& out) noexcept {
	if (p.columns == 0u || cell_w == 0u || cell_h == 0u) {
		return false;
	}
	const eng::u16 col = static_cast<eng::u16>(index % p.columns);
	const eng::u16 row = static_cast<eng::u16>(index / p.columns);
	const eng::s16 x = static_cast<eng::s16>(p.bounds.x + col * cell_w);
	const eng::s16 y = static_cast<eng::s16>(p.bounds.y + row * cell_h);
	if (static_cast<eng::s32>(x) + cell_w > static_cast<eng::s32>(p.bounds.x) + p.bounds.w ||
	    static_cast<eng::s32>(y) + cell_h > static_cast<eng::s32>(p.bounds.y) + p.bounds.h) {
		return false;
	}
	out = eng::Box {x, y, cell_w, cell_h};
	return true;
}

/// Etiqueta de tipo de tooltip/toast.
struct TooltipTag {};
/// Handle de tooltip/toast.
using TooltipHandle = HudHandle<TooltipTag>;

/// **Tooltip/toast**: aviso de texto con TTL por `Tick` (tooltip de hover, toast de evento,
/// subtítulo corto). El juego lo muestra con `tooltip_show` y lo mantiene con `tooltip_tick`
/// (una vez por frame); mientras `ttl_frames > 0` el presentador lo pinta en `box`. Texto
/// **por valor** como el resto del modelo.
struct Tooltip {
	eng::Box box {};           ///< posición/tamaño en la región (lo fija el juego)
	eng::u8 color = 0u;        ///< índice lógico de color
	eng::u16 ttl_frames = 0u;  ///< frames restantes (0 = oculto)
	eng::u8 text_len = 0u;     ///< longitud válida de `text`
	char text[kHudTextMax] {}; ///< texto por valor (sin heap)
	bool dirty = true;         ///< true si cambió desde el último `clear_dirty`
	/// Copia el texto; `false` si no cabe (rechazo controlado).
	[[nodiscard]] bool set_text(eng::Span<const char> t) noexcept {
		return hud_detail::copy_text(text, text_len, t);
	}
	/// Texto vigente (observador al propio tooltip).
	[[nodiscard]] eng::Span<const char> text_view() const noexcept {
		return eng::Span<const char> {text, text_len};
	}
};

/// Etiqueta de tipo de la barra de verbos.
struct VerbBarTag {};
/// Handle de la barra de verbos.
using VerbBarHandle = HudHandle<VerbBarTag>;

/// **Barra de verbos** (aventura gráfica, `ADVENTURE_LAYER.md`): una fila de verbos con uno
/// **activo** y foco de navegación independiente. El modelo guarda los botones (`Button` con
/// su etiqueta) y los índices; el presentador los pinta (los botones se emiten como ops del
/// panel/franja). `verb_select`/`verb_move_focus` marcan *dirty* solo al cambiar.
struct VerbBar {
	/// Verbos máximos de una barra (fila de una franja de 320 px a 8 px/verbo y holgura).
	static constexpr eng::u8 MaxVerbs = 12u;
	eng::u8 count = 0u;   ///< verbos registrados
	eng::u8 active = 0u;  ///< verbo seleccionado (índice)
	eng::u8 focus = 0u;   ///< foco de navegación (joystick/teclado)
	eng::util::Array<ButtonHandle, MaxVerbs> buttons {}; ///< botón de cada verbo
	bool dirty = true;    ///< true si cambió desde el último `clear_dirty`
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
/// El texto va **por valor** (`text`/`text_len`, copiado con `set_text`): la operación es
/// autocontenida y el llamador puede reutilizar su buffer sin afectar a lo ya emitido.
struct HudDrawOp {
	HudOpKind kind = HudOpKind::Fill; ///< tipo de operación
	eng::Box box {};                  ///< región afectada
	eng::u8 color = 0u;               ///< índice lógico de color
	eng::u8 flags = 0u;               ///< bits del presentador (máscara, alineación…)
	eng::u16 asset = 0u;              ///< frame del atlas o canal de sprite
	eng::u8 text_len = 0u;            ///< longitud válida de `text`
	char text[kHudTextMax] {};        ///< texto/dígitos por valor (sin heap)

	/// Copia el texto a la operación; `false` si no cabe en `kHudTextMax` (rechazo controlado).
	[[nodiscard]] bool set_text(eng::Span<const char> t) noexcept {
		return hud_detail::copy_text(text, text_len, t);
	}
	/// Texto vigente (observador a la propia operación).
	[[nodiscard]] eng::Span<const char> text_view() const noexcept {
		return eng::Span<const char> {text, text_len};
	}
};

/// Entrada de *hit-test* rectangular de una región (rectángulos ordenados por capa). `region`
/// etiqueta la región a la que pertenece (opcional; vacío = vale para cualquiera): con varias
/// regiones simultáneas, `hud_hit_test_region` filtra por ella y el juego despacha en orden
/// de delante hacia atrás (Screen → overlays → Band; `HUD_SYSTEM.md` §7.1).
struct HudHitRect {
	eng::Box box {};      ///< zona interactiva
	eng::u16 id = 0u;     ///< id lógico del objetivo (slot, botón…)
	eng::u8 layer = 0u;   ///< orden dentro de la región; mayor = delante
	RegionHandle region {}; ///< región de la entrada (vacío = cualquiera)
};

/// Entrada de *hit-test* de una rejilla: la celda se calcula por fórmula al recibir el evento
/// (sin generar N rectángulos). `first_id` es el id lógico del slot [0][0]; `grid` enlaza la
/// rejilla del modelo para que `HudInput` pueda mover su selección/foco (`input.hpp`).
struct HudGridHit {
	eng::Box area {};        ///< zona de la rejilla
	eng::u8 cols = 0u;       ///< columnas
	eng::u8 rows = 0u;       ///< filas
	eng::u8 pitch_x = 16u;   ///< paso horizontal
	eng::u8 pitch_y = 16u;   ///< paso vertical
	eng::u16 first_id = 0u;  ///< id del primer slot
	GridHandle grid {};      ///< rejilla del modelo (opcional; vacío = solo id lógico)
	RegionHandle region {};  ///< región de la entrada (vacío = cualquiera)
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
	GridHandle target {};              ///< rejilla bajo el puntero (la fija el juego)
	eng::u16 target_slot = kHudNoSlot; ///< celda bajo el puntero (kHudNoSlot = ninguna)
	bool accepts = false;              ///< veredicto de la política del juego para `target`
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
		// El tema de la region se guarda **por valor** (instantanea del contrato): si el
		// llamador pasa un `Ref`, se copia; sin tema, queda el defecto (roles clasicos).
		if (spec.theme.valid()) {
			m_region_themes[m_regions_used] = *spec.theme.get();
		} else {
			m_region_themes[m_regions_used] = HudTheme {};
		}
		return RegionHandle {static_cast<eng::u8>(m_regions_used++)};
	}

	/// **Tema de la region** (contrato del modelo, H6): instantánea por valor. Cambiarlo en
	/// caliente (`set_region_theme`) marca todo sucio: el juego repinta con los índices del
	/// tema nuevo y el modelo no cambia. `theme.hpp`.
	[[nodiscard]] const HudTheme& region_theme(RegionHandle handle) const noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_regions_used);
		return m_region_themes[handle.index];
	}

	/// Cambia el tema de una region en caliente. `false` si el handle no es válido o el tema
	/// es el mismo (no hay cambio que repintar); con cambio, el juego repinta con los índices
	/// nuevos (el modelo solo publica el contrato; el repintado es del juego).
	[[nodiscard]] bool set_region_theme(RegionHandle handle, const HudTheme& theme) noexcept {
		if (!handle.valid() || handle.index >= m_regions_used) {
			return false;
		}
		if (m_region_themes[handle.index] == theme) {
			return false; // sin cambio
		}
		m_region_themes[handle.index] = theme;
		return true;
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

	/// Cambia el **estilo** de presentación de un medidor (*skins*, H6): el modelo no cambia
	/// de forma, solo cómo lo pinta el presentador (`Linear`/`Segmented`/`Pips`/`Raster`).
	/// `true` si cambió (marca sucio); el repintado es del juego.
	[[nodiscard]] bool set_meter_style(MeterHandle handle, MeterStyle style) noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_meters_used);
		Meter& m = m_meters[handle.index];
		if (m.style == style) {
			return false;
		}
		m.style = style;
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
	[[nodiscard]] eng::Span<const char> counter_text(CounterHandle handle) const noexcept {		const Counter& c = counter(handle);
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

	/// Cambia el **estilo** de presentación de un contador (*skins*, H6): `Numeric` (dígitos),
	/// `Pips` o `Bar` contra `present_max`. El modelo no cambia de valor; el presentador usa
	/// `style`/`present_max` para materializarlo. `true` si cambió (marca sucio).
	[[nodiscard]] bool set_counter_style(CounterHandle handle, CounterStyle style,
					     eng::u16 present_max = 0u) noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_counters_used);
		Counter& c = m_counters[handle.index];
		if (c.style == style && c.present_max == present_max) {
			return false;
		}
		c.style = style;
		c.present_max = present_max;
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

	/// Fija el foco de la rejilla en `slot` (dentro de las vistas): `true` si cambió. Lo usa
	/// el anillo de foco (`focus.hpp`) al entrar/saltar entre rejillas.
	[[nodiscard]] bool grid_move_focus_to(GridHandle handle, eng::u16 slot) noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_grids_used);
		IconGrid& g = m_grids[handle.index];
		if (slot >= g.slots.size() || g.focus == slot) {
			return false;
		}
		g.focus = slot;
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

	/// Añade una etiqueta; handle inválido si no cabe (`MaxWidgets`) o el texto excede
	/// `kHudTextMax`. `box` es su geometría en la región (pintado y *hit-test* comparten).
	[[nodiscard]] LabelHandle add_label(eng::Span<const char> text,
					    eng::Box box = {}) noexcept {
		if (m_widgets_used >= MaxWidgets) {
			return {};
		}
		Label& l = m_labels[m_labels_used];
		l = Label {};
		l.box = box;
		if (!l.set_text(text)) {
			return {};
		}
		++m_widgets_used;
		return LabelHandle {static_cast<eng::u8>(m_labels_used++)};
	}

	/// Etiqueta por handle.
	[[nodiscard]] const Label& label(LabelHandle handle) const noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_labels_used);
		return m_labels[handle.index];
	}

	/// Añade un botón; handle inválido si no cabe (`MaxWidgets`) o la etiqueta excede
	/// `kHudTextMax`. `box` es su geometría en la región (pintado y *hit-test* comparten).
	[[nodiscard]] ButtonHandle add_button(eng::Span<const char> text,
					      eng::Box box = {}) noexcept {
		if (m_widgets_used >= MaxWidgets) {
			return {};
		}
		Button& b = m_buttons[m_buttons_used];
		b = Button {};
		b.box = box;
		if (!b.set_text(text)) {
			return {};
		}
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

	// --- Tooltips/toasts y barra de verbos (H5: aventura/CRPG) ------------------------------

	/// Añade un tooltip/toast (oculto hasta `tooltip_show`); handle inválido si no cabe.
	[[nodiscard]] TooltipHandle add_tooltip() noexcept {
		if (m_widgets_used >= MaxWidgets) {
			return {};
		}
		Tooltip& t = m_tooltips[m_tooltips_used];
		t = Tooltip {};
		t.dirty = false; // nace oculto: no hay nada que repintar
		++m_widgets_used;
		return TooltipHandle {static_cast<eng::u8>(m_tooltips_used++)};
	}

	/// Tooltip por handle.
	[[nodiscard]] const Tooltip& tooltip(TooltipHandle handle) const noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_tooltips_used);
		return m_tooltips[handle.index];
	}

	/// Muestra un tooltip/toast con TTL (frames): copia el texto y marca *dirty*. El juego
	/// lo refresca llamando otra vez (reinicia el TTL). `false` si el texto no cabe.
	[[nodiscard]] bool tooltip_show(TooltipHandle handle, eng::Span<const char> text, eng::Box box,
					eng::u8 color, eng::u16 ttl_frames) noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_tooltips_used);
		Tooltip& t = m_tooltips[handle.index];
		if (!t.set_text(text)) {
			return false;
		}
		t.box = box;
		t.color = color;
		t.ttl_frames = ttl_frames;
		mark_dirty(t);
		return true;
	}

	/// Avanza los TTLs **un frame** (llamar una vez por frame, tras `Tick`): decrementa y
	/// oculta los vencidos, marcando *dirty* al cambiar la visibilidad. Devuelve cuántos
	/// siguen visibles.
	[[nodiscard]] eng::u8 tooltip_tick() noexcept {
		eng::u8 visibles = 0u;
		for (eng::u8 i = 0u; i < m_tooltips_used; ++i) {
			Tooltip& t = m_tooltips[i];
			if (t.ttl_frames == 0u) {
				continue;
			}
			--t.ttl_frames;
			if (t.ttl_frames == 0u) {
				mark_dirty(t); // se oculta: el presentador debe borrarlo
			}
			++visibles;
		}
		return visibles;
	}

	/// ¿El tooltip está visible (TTL > 0)?
	[[nodiscard]] bool tooltip_visible(TooltipHandle handle) const noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_tooltips_used);
		return m_tooltips[handle.index].ttl_frames != 0u;
	}

	/// Añade una **barra de verbos**: crea un `Button` por etiqueta (capacidad
	/// `VerbBar::MaxVerbs`) y devuelve el handle de la barra; inválido si no caben (widgets o
	/// verbos). Las etiquetas se copian por valor en cada botón. `box` es el área de la barra
	/// y `cell_w` el ancho de cada verbo (0 = reparte `box.w` entre los verbos): la geometría
	/// de cada botón queda en el modelo (pintado, *hit-test* y **foco** comparten).
	[[nodiscard]] VerbBarHandle add_verb_bar(eng::Span<const char* const> labels,
						 eng::Box box = {}, eng::u16 cell_w = 0u) noexcept {
		if (labels.empty() || labels.size() > VerbBar::MaxVerbs ||
		    m_widgets_used + labels.size() > MaxWidgets) {
			return {};
		}
		const eng::u16 cell = cell_w != 0u
					      ? cell_w
					      : static_cast<eng::u16>(box.w / labels.size());
		VerbBar& bar = m_verb_bars[m_verb_bars_used];
		bar = VerbBar {};
		for (eng::usize i = 0u; i < labels.size(); ++i) {
			const char* label = labels[i];
			eng::usize n = 0u;
			while (label != nullptr && label[n] != '\0' && n < kHudTextMax) {
				++n;
			}
			Button& b = m_buttons[m_buttons_used];
			b = Button {};
			b.box = eng::Box {static_cast<eng::s16>(box.x + static_cast<eng::s16>(i) *
									       static_cast<eng::s16>(cell)),
					  box.y, cell, box.h};
			if (label == nullptr ||
			    !b.set_text(eng::Span<const char> {label, static_cast<eng::usize>(n)})) {
				return {};
			}
			bar.buttons[i] = ButtonHandle {static_cast<eng::u8>(m_buttons_used++)};
			++m_widgets_used;
		}
		bar.count = static_cast<eng::u8>(labels.size());
		++m_widgets_used; // la barra ocupa un widget
		return VerbBarHandle {static_cast<eng::u8>(m_verb_bars_used++)};
	}

	/// Barra de verbos por handle.
	[[nodiscard]] const VerbBar& verb_bar(VerbBarHandle handle) const noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_verb_bars_used);
		return m_verb_bars[handle.index];
	}

	/// Botón del verbo `index` de la barra (para pintarlo o consultar su etiqueta).
	[[nodiscard]] ButtonHandle verb_button(VerbBarHandle handle, eng::u8 index) const noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_verb_bars_used);
		const VerbBar& bar = m_verb_bars[handle.index];
		return index < bar.count ? bar.buttons[index] : ButtonHandle {};
	}

	/// Selecciona el verbo activo; `true` si cambió (marca *dirty*).
	[[nodiscard]] bool verb_select(VerbBarHandle handle, eng::u8 index) noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_verb_bars_used);
		VerbBar& bar = m_verb_bars[handle.index];
		if (index >= bar.count || bar.active == index) {
			return false;
		}
		bar.active = index;
		bar.focus = index;
		mark_dirty(bar);
		return true;
	}

	/// Mueve el foco de la barra con recorte a los extremos; `true` si cambió.
	[[nodiscard]] bool verb_move_focus(VerbBarHandle handle, eng::s8 dx) noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_verb_bars_used);
		VerbBar& bar = m_verb_bars[handle.index];
		const eng::s16 next = static_cast<eng::s16>(bar.focus) + dx;
		if (next < 0 || next >= static_cast<eng::s16>(bar.count)) {
			return false;
		}
		bar.focus = static_cast<eng::u8>(next);
		mark_dirty(bar);
		return true;
	}

	/// Verbo activo de la barra.
	[[nodiscard]] eng::u8 verb_active(VerbBarHandle handle) const noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_verb_bars_used);
		return m_verb_bars[handle.index].active;
	}

	/// **Hover** de un botón (el puntero encima): marca *dirty* solo al cambiar.
	[[nodiscard]] bool button_hover(ButtonHandle handle, bool on) noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_buttons_used);
		Button& b = m_buttons[handle.index];
		if (b.hover == on) {
			return false;
		}
		b.hover = on;
		mark_dirty(b);
		return true;
	}

	/// **Hover** de una celda de rejilla (`kHudNoSlot` = ninguna): marca *dirty* al cambiar.
	[[nodiscard]] bool grid_hover(GridHandle handle, eng::u16 slot) noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_grids_used);
		IconGrid& g = m_grids[handle.index];
		if (g.hover == slot) {
			return false;
		}
		g.hover = slot;
		mark_dirty(g);
		return true;
	}

	/// **Hover** del verbo `index` de la barra (y limpia el resto): *dirty* al cambiar.
	[[nodiscard]] bool verb_hover(VerbBarHandle handle, eng::u8 index) noexcept {
		ENG_ASSERT(handle.valid() && handle.index < m_verb_bars_used);
		const VerbBar& bar = m_verb_bars[handle.index];
		bool changed = false;
		for (eng::u8 i = 0u; i < bar.count; ++i) {
			Button& b = m_buttons[bar.buttons[i].index];
			const bool on = (i == index);
			if (b.hover != on) {
				b.hover = on;
				mark_dirty(b);
				changed = true;
			}
		}
		return changed;
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

	/// **Destino del arrastre bajo el puntero** (lo fija el juego en cada movimiento, tras
	/// consultar su política): `accepts` es el veredicto de esa política (`accepts(item_id,
	/// qty)` en la celda destino; `HUD_SYSTEM.md` §5.6). `false` si no hay sesión activa;
	/// con cambio, marca *dirty* (el presentador realza el destino).
	[[nodiscard]] bool set_drag_target(GridHandle target, eng::u16 slot, bool accepts) noexcept {
		if (m_drag.phase != DragPhase::Pressed && m_drag.phase != DragPhase::Dragging) {
			return false;
		}
		if (m_drag.target.index == target.index && m_drag.target_slot == slot &&
		    m_drag.accepts == accepts) {
			return false; // sin cambio
		}
		m_drag.target = target;
		m_drag.target_slot = slot;
		m_drag.accepts = accepts;
		mark_dirty(m_drag);
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
		for (eng::u8 i = 0u; i < m_tooltips_used; ++i) {
			if (m_tooltips[i].dirty) {
				return true;
			}
		}
		for (eng::u8 i = 0u; i < m_verb_bars_used; ++i) {
			if (m_verb_bars[i].dirty) {
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
		for (eng::u8 i = 0u; i < m_tooltips_used; ++i) {
			m_tooltips[i].dirty = false;
		}
		for (eng::u8 i = 0u; i < m_verb_bars_used; ++i) {
			m_verb_bars[i].dirty = false;
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
	eng::util::Array<HudTheme, MaxRegions> m_region_themes {}; ///< tema por region (por valor)
	eng::u16 m_regions_used = 0u;

	eng::util::Array<Meter, MaxWidgets> m_meters {};
	eng::util::Array<Counter, MaxWidgets> m_counters {};
	eng::util::Array<IconGrid, MaxWidgets> m_grids {};
	eng::util::Array<Label, MaxWidgets> m_labels {};
	eng::util::Array<Button, MaxWidgets> m_buttons {};
	eng::util::Array<Panel, MaxWidgets> m_panels {};
	eng::util::Array<Tooltip, MaxWidgets> m_tooltips {};
	eng::util::Array<VerbBar, MaxWidgets> m_verb_bars {};
	eng::u8 m_meters_used = 0u;
	eng::u8 m_counters_used = 0u;
	eng::u8 m_grids_used = 0u;
	eng::u8 m_labels_used = 0u;
	eng::u8 m_buttons_used = 0u;
	eng::u8 m_panels_used = 0u;
	eng::u8 m_tooltips_used = 0u;
	eng::u8 m_verb_bars_used = 0u;
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
