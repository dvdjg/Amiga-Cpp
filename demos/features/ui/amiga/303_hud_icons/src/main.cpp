// Demo 303 - hud_icons
// ----------------------------------------------------------------------------
// Tutorial (H2 de ROADMAP_HUD.md): **inventario de iconos con seleccion y foco**.
// La franja (2 planos con paleta propia en la linea 208) muestra una rejilla 6x2 de
// celdas de 16x16; `HudInput` (eng/hud/input.hpp) consume los eventos de raton y
// joystick y mueve la **seleccion** y el **foco** del modelo (`IconGrid`), que el
// presentador pinta por deltas. El juego no lee registros: sondea la entrada con
// `eng::amiga::poll_mouse`/`poll_input` y sintetiza `UiEvent` (contrato compartido
// con `eng::ui`).
//
// Estados visuales de una celda: objeto = cuadro 12x12 del color del item; vacia =
// fondo; **hover** = marco blanco; **foco** = marco rojo; **seleccion** = fondo
// amarillo con el objeto en blanco.
//
//   bash ./tools/build/build-demo.sh demos/features/ui/amiga/303_hud_icons --debug
//   bash ./tools/run/run-demo.sh demos/features/ui/amiga/303_hud_icons --keep-running

#include <eng/api/api.hpp> // incluye eng::hud (con HudInput desde H2)
#include <eng/debug/peripheral.hpp> // telemetria (fps10 por contador)
#include <eng/platform/amiga/entry.hpp>
#include <eng/platform/amiga/input_poll.hpp> // poll_mouse/poll_input (patron de la 215)
#include <eng/scene/display.hpp>

namespace {

using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::u8;

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;

// --- Franja del HUD: lineas 208..255 (48 lineas), 2 planos, paleta propia ---------
constexpr u16 kDiwstrtY = 0x2cu;
constexpr u16 kBandTop = 208u;
constexpr u16 kBandTopRaster = static_cast<u16>(kDiwstrtY + kBandTop);
constexpr u16 kBandHeight = 48u;
constexpr u8 kBandPlanes = 2u;
constexpr u16 kBandBytesPerRow = kWidth / 8u; // 40 B por fila y plano
constexpr u32 kBandBytes = static_cast<u32>(kBandBytesPerRow) * kBandPlanes * kBandHeight;

// Indices logicos del tema (paleta de la franja).
constexpr u8 kHudBg = 0u;
constexpr u8 kHudFg = 1u;
constexpr u8 kHudAccent = 2u;
constexpr u8 kHudWarn = 3u;

// Paleta del campo de juego (4 planos) y de la franja (2 planos; blanco/amarillo/rojo).
constexpr eng::Palette32 kGamePalette {{
	0x000, 0x112, 0x224, 0x336, 0x448, 0x55a, 0x66c, 0x77e,
	0x135, 0x246, 0x357, 0x468, 0x579, 0x68a, 0x79b, 0x8ac,
}};
constexpr eng::Palette32 kHudPalette {{
	0x000, 0xfff, 0xff0, 0xf00,
}};

constexpr eng::debug::Version kVersion {1u, 0u};

// Telemetria: fps x10 por contador (medida fiable, sin depender del video).
const char kCntFps10[] = "fps10";


// --- Rejilla del inventario -------------------------------------------------------
// 6 columnas x 2 filas de celdas de 16x16 en (8,8) de la banda: area 96x32.
constexpr u8 kGridCols = 6u;
constexpr u8 kGridRows = 2u;
constexpr u8 kCell = 16u;
constexpr s16 kGridX = 8;
constexpr s16 kGridY = 8;
constexpr u16 kGridW = static_cast<u16>(kGridCols) * kCell;
constexpr u16 kGridH = static_cast<u16>(kGridRows) * kCell;
constexpr u16 kSlotCount = static_cast<u16>(kGridCols) * kGridRows;
// Objetos por slot (0 = vacio): tres "tipos" con colores 1..3, repartidos a mano.
constexpr u8 kItems[kSlotCount] = {1u, 2u, 3u, 0u, 1u, 0u, 2u, 3u, 0u, 1u, 0u, 3u};
// Tira del objeto dentro de la celda (12x12 centrado, margen 2 px).
constexpr s16 kTileMargin = 2;
constexpr u16 kTile = kCell - 2u * kTileMargin;

// Posiciones del texto de estado (x alineada a 16 px para `draw_text_words`).
constexpr s16 kStateX = 112;
constexpr s16 kStateY = 20;

/// Juego minimo: campo estatico (fondo) + inventario interactivo en la franja.
struct HudIconsDemo {
	void init(auto& app) {
		// (1) Memoria de la franja: bloque Chip tipado; el lienzo lo liga `HudStrip`.
		auto& mm = app.memory_manager();
		m_band_planes = mm.chip().template reserve<eng::PlaneTag>(kBandBytes, 16u);
		if (!m_band_planes.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		eng::hud::HudStripConfig strip_cfg {};
		strip_cfg.width = kWidth;
		strip_cfg.height = kBandHeight;
		strip_cfg.planes = kBandPlanes;
		strip_cfg.background = kHudBg;
		if (!m_strip.bind(m_band_planes, strip_cfg)) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		m_strip.clear();

		// (2) Region con **captura de entrada** (la franja consume el raton: el campo de
		// juego no es interactivo) y modelo: rejilla de inventario.
		eng::hud::HudRegionSpec region {};
		region.placement = eng::hud::HudPlacement::Band;
		region.band = eng::hud::HudBand {kBandTop, kBandHeight, kBandPlanes, true};
		region.capture_input = true;
		region.budget.max_draw_ops = 48u;
		m_region = m_hud.add_region(region);
		if (!m_region.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		m_grid = m_hud.add_grid(kGridRows, kGridCols, kCell, kCell);
		if (!m_grid.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		for (u16 i = 0u; i < kSlotCount; ++i) {
			m_slots[i].item_id = kItems[i];
			m_slots[i].icon_frame = kItems[i];
			m_slots[i].flags = kItems[i] == 0u ? eng::hud::kHudSlotEmpty
							   : eng::hud::kHudSlotEnabled;
		}
		if (!m_hud.bind_grid(m_grid, eng::Span<const eng::hud::HudSlotView> {m_slots,
										    kSlotCount})) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		// Hit de la rejilla: la celda se calcula por formula al recibir el evento.
		eng::hud::HudGridHit gh {};
		gh.area = eng::Box {kGridX, kGridY, kGridW, kGridH};
		gh.cols = kGridCols;
		gh.rows = kGridRows;
		gh.pitch_x = kCell;
		gh.pitch_y = kCell;
		gh.first_id = 0u;
		gh.grid = m_grid;
		(void)m_hud.push_grid_hit(gh);

		// (3) Composicion: campo de juego + franja con paleta propia (patron de la 302).
		eng::scene::Band game_band = eng::scene::band_from_view(
			app.device().scene().playfield().hardware_view(), 0u);
		eng::scene::Band hud_band = eng::scene::band_of_planes(
			m_band_planes, kBandPlanes, kBandBytesPerRow, kBandTopRaster);
		hud_band.palette = kHudPalette.words();
		hud_band.palette_colors = 4u;
		eng::scene::RasterLayout layout {};
		(void)layout.add(game_band);
		(void)layout.add(hud_band);
		layout[0u].palette = kGamePalette.words();
		layout[0u].palette_colors = 16u;
		if (!app.present_layout(layout)) {
			eng::amiga::mark_failed(eng::debug::BootFail::Display);
			return;
		}

		// (4) Fondo estatico del campo (backdrop) y parte estatica de la franja.
		eng::Screen screen = app.screen();
		screen.clear(0u);
		(void)screen.fill(eng::Box {0, 180, kWidth, 28u}, 1u);
		(void)screen.fill(eng::Box {40, 120, 40u, 24u}, 3u);
		(void)screen.fill(eng::Box {200, 96, 64u, 16u}, 5u);
		(void)screen.fill(eng::Box {120, 150, 24u, 40u}, 2u);
		draw_static_hud();
		draw_version_prefix();
		draw_version_number(0u);
		draw_fps(0u);
		m_fps_win.reset(app.frame(), eng::debug::prof_clock());

		// (5) Entrada: `HudInput` con la rejilla de navegacion y la politica derivada del
		// contrato de region (`capture_input`).
		m_input.set_focus_grid(m_grid);
		m_policy = eng::hud::HudInput::Policy::from_region(m_hud.region(m_region));

		// Estado inicial pintado (celdas + estado de seleccion/foco) y **aplicado**: los ops
		// encolados se materializan aqui; si no, quedarian pendientes hasta el primer evento
		// con dirty (bug real de la primera version: la rejilla no se veia hasta ~2 s).
		m_prev_sel = m_hud.grid(m_grid).selection;
		m_prev_foc = m_hud.grid(m_grid).focus;
		for (u16 i = 0u; i < kSlotCount; ++i) {
			paint_cell(i, i == m_prev_sel, i == m_prev_foc, false);
		}
		draw_state_numbers();
		(void)m_strip.apply(m_hud.ops());
		m_hud.clear_ops();
		m_hud.clear_dirty();

		m_ready = true;
		eng::debug::DebugPeripheral::counter_name(0u, reinterpret_cast<eng::u32>(kCntFps10));
		eng::debug::DebugPeripheral::counter_name(2u, reinterpret_cast<eng::u32>("vbl"));
		eng::debug::DebugPeripheral::counter_name(3u, reinterpret_cast<eng::u32>("frame"));
		eng::amiga::mark_ready();
	}

	void update(auto& app) {
		if (!m_ready) {
			return;
		}
		// (1) Sondeo de entrada del frame (raton + puerto 0). Una vez por frame.
		eng::amiga::poll_mouse(m_mouse, m_mouse_poll);
		eng::amiga::poll_input(m_input_agg);
		const eng::input::PadState& pad = m_input_agg.pad0;
		// **Puntero de hardware** (GUI, `GameDisplay::hardware_cursor`): sigue al ratón con
		// sus deltas; el recorte a pantalla lo hace el propio puntero. El cursor lógico de
		// la banda (m_mx/m_my) se calcula abajo, más restringido.
		if (auto* cursor = app.cursor()) {
			cursor->move_by(m_mouse.dx, m_mouse.dy);
		}

		// (2) Raton: el cursor vive en la banda (x 0..319, y 208..255); solo se emiten
		// eventos cuando algo cambia (el raton de Amiga da deltas por contador).
		const bool mouse_moved = m_mouse.dx != 0 || m_mouse.dy != 0;
		const bool left_down = m_mouse.left_button;
		const bool left_edge = left_down != m_prev_left;
		if (mouse_moved) {
			m_mx = clamp_s16(static_cast<s16>(m_mx + m_mouse.dx), 0, static_cast<s16>(kWidth - 1u));
			m_my = clamp_s16(static_cast<s16>(m_my + m_mouse.dy), static_cast<s16>(kBandTop),
					 static_cast<s16>(kHeight - 1u));
			m_idle_frames = 0u;
			m_attract_frame = 0u;
			eng::ui::UiEvent ev {};
			ev.kind = eng::ui::UiEventKind::MouseMove;
			ev.x = m_mx;
			ev.y = static_cast<s16>(m_my - static_cast<s16>(kBandTop));
			consume(app, ev);
		}
		if (left_edge) {
			eng::ui::UiEvent ev {};
			ev.kind = left_down ? eng::ui::UiEventKind::MouseDown
					    : eng::ui::UiEventKind::MouseUp;
			ev.x = m_mx;
			ev.y = static_cast<s16>(m_my - static_cast<s16>(kBandTop));
			consume(app, ev);
			m_idle_frames = 0u;
			m_attract_frame = 0u;
			m_prev_left = left_down;
		}

		// (3) Joystick: **flanco** de cada direccion/fuego (una pulsacion = un paso; la
		// repeticion seria del servicio de entrada, no del widget).
		const u16 joy_now = joy_bits(pad);
		if (joy_now != m_prev_joy) {
			const u16 pressed = static_cast<u16>(joy_now & ~m_prev_joy);
			if (pressed != 0u) {
				eng::ui::UiEvent ev {};
				ev.kind = eng::ui::UiEventKind::JoyButton;
				ev.joy_buttons = pressed;
				consume(app, ev);
				m_idle_frames = 0u;
				m_attract_frame = 0u;
			}
			m_prev_joy = joy_now;
		}

		// (4) **Guion de demostracion** (attract): tras ~2 s sin entrada real, un recorrido
		// determinista por el perimetro de la rejilla (5 derecha, abajo, 5 izquierda,
		// arriba) desde el foco inicial (0,0) — todos los pasos caben — con un paso cada
		// 20 frames de attract y un disparo cada 60 (selecciona la celda con foco). El
		// contador es PROPIO del attract (no el frame absoluto): asi el recorrido empieza
		// siempre en fase y no se queda atascado en los bordes.
		if (m_idle_frames < kAttractAfterFrames) {
			++m_idle_frames;
		} else {
			if ((m_attract_frame % 12u) == 0u) {
				const u8 step = static_cast<u8>((m_attract_frame / 12u) % 12u);
				eng::ui::UiEvent ev {};
				ev.kind = eng::ui::UiEventKind::JoyButton;
				ev.joy_buttons = tour_bits(step);
				consume(app, ev);
			}
			if ((m_attract_frame % 48u) == 0u) {
				eng::ui::UiEvent ev {};
				ev.kind = eng::ui::UiEventKind::JoyButton;
				ev.joy_buttons = eng::hud::kHudJoyFire;
				consume(app, ev);
			}
			++m_attract_frame;
		}

		// (5) El juego es la fuente de verdad del **contenido**: aqui no cambia (el
		// inventario es estatico); la seleccion/foco los mueve `HudInput`.
		(void)app;
	}

	void render(auto& app) {
		if (!m_ready) {
			return;
		}
		// **Orden por banda** (HUD_SYSTEM §9.3): capa viva del juego primero (aqui el campo
		// es estatico, sin jobs), franja despues por CPU.
		(void)app.draw_world();
		app.present();
		// Deltas del HUD: celdas cuyo estado (seleccion/foco/hover) cambio este frame.
		const eng::hud::IconGrid& grid = m_hud.grid(m_grid);
		const bool sel_changed = grid.selection != m_prev_sel;
		const bool foc_changed = grid.focus != m_prev_foc;
		if (sel_changed) {
			paint_cell(m_prev_sel, false, m_prev_sel == m_prev_foc, m_prev_hover == m_prev_sel);
			paint_cell(grid.selection, true, grid.selection == grid.focus,
				   m_prev_hover == grid.selection);
			m_prev_sel = grid.selection;
		}
		if (foc_changed) {
			paint_cell(m_prev_foc, m_prev_foc == m_prev_sel, false,
				   m_prev_hover == m_prev_foc);
			paint_cell(grid.focus, grid.focus == grid.selection, true,
				   m_prev_hover == grid.focus);
			m_prev_foc = grid.focus;
		}
		if (sel_changed || foc_changed) {
			draw_state_numbers();
		}
		// El hover (raton) se repinta al cambiar de celda.
		if (m_hover != m_prev_hover) {
			if (m_prev_hover < kSlotCount) {
				paint_cell(m_prev_hover, m_prev_hover == m_prev_sel,
					   m_prev_hover == m_prev_foc, false);
			}
			if (m_hover < kSlotCount) {
				paint_cell(m_hover, m_hover == m_prev_sel, m_hover == m_prev_foc, true);
			}
			m_prev_hover = m_hover;
		}
		if (m_hud.any_dirty()) {
			(void)m_strip.apply(m_hud.ops());
			m_hud.clear_ops();
			m_hud.clear_dirty();
		}

		// Franja de depuracion obligatoria (AGENTS §0): numero de frames por palabras.
		if (app.frame() / 100u != m_hud_second) {
			m_hud_second = app.frame() / 100u;
			draw_version_number(app.frame());
		}
		// fps: la ventana de 1 s se comprueba cada frame (~15 ciclos); se repinta al cerrar.
		eng::u32 fps10 = 0u;
		if (m_fps_win.update(app.frame(), eng::debug::prof_clock(), fps10)) {
			draw_fps(fps10);
			eng::debug::DebugPeripheral::counter_value(0u, fps10);
		}
		// **Latidos de VBlank** (reloj de 50 Hz exactos del mini-SO) y **frames** del motor:
		// el ratio `frame`/`vbl` mide la perdida real de campos (DT-036) sin depender del
		// reloj de pared del periferico (que subestima con el emulador a tiempo real).
		eng::debug::DebugPeripheral::counter_value(2u, eng::os::frame_count());
		eng::debug::DebugPeripheral::counter_value(3u, app.frame());
	}

private:
	/// Consume un evento con `HudInput` y actualiza el **hover** del demo (la celda bajo el
	/// raton). La seleccion/foco viven en el modelo (los mueve el input).
	void consume(auto& app, const eng::ui::UiEvent& ev) {
		const eng::hud::HudInput::Result r = m_input.handle(m_hud, m_policy, ev);
		if (ev.kind == eng::ui::UiEventKind::MouseMove ||
		    ev.kind == eng::ui::UiEventKind::MouseDown) {
			if (r.consumed && r.grid.valid()) {
				m_hover = r.slot;
			} else {
				m_hover = kNoSlot;
			}
		}
		(void)app;
	}

	/// Bits del recorrido del guion: paso `step` de un perimetro 5x2 (5 derecha, abajo,
	/// 5 izquierda, arriba).
	[[nodiscard]] static u16 tour_bits(u8 step) {
		static const eng::s8 kTour[12][2] = {
			{1, 0},  {1, 0},  {1, 0},  {1, 0}, {1, 0}, {0, 1},
			{-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {0, -1},
		};
		const eng::s8 dx = kTour[step % 12u][0];
		const eng::s8 dy = kTour[step % 12u][1];
		if (dx > 0) return eng::hud::kHudJoyRight;
		if (dx < 0) return eng::hud::kHudJoyLeft;
		if (dy > 0) return eng::hud::kHudJoyDown;
		return eng::hud::kHudJoyUp;
	}

	/// Bits de `UiEvent::joy_buttons` desde el pad 0 (contrato del puente os -> ui).
	[[nodiscard]] static u16 joy_bits(const eng::input::PadState& pad) {
		u16 b = 0u;
		if (pad.up) b = static_cast<u16>(b | eng::hud::kHudJoyUp);
		if (pad.down) b = static_cast<u16>(b | eng::hud::kHudJoyDown);
		if (pad.left) b = static_cast<u16>(b | eng::hud::kHudJoyLeft);
		if (pad.right) b = static_cast<u16>(b | eng::hud::kHudJoyRight);
		if (pad.fire) b = static_cast<u16>(b | eng::hud::kHudJoyFire);
		return b;
	}

	[[nodiscard]] static s16 clamp_s16(s16 v, s16 lo, s16 hi) {
		return v < lo ? lo : (v > hi ? hi : v);
	}

	/// Pinta una celda del inventario con su estado actual (base + hover + foco + seleccion).
	/// Los ops van por valor y el presentador los aplica en orden: lo ultimo pintado manda.
	/// Lenguaje visual (4 colores, verificado por conteo de pixeles): objeto = cuadro 12x12
	/// **amarillo**; **hover** = marco blanco; **foco** = marco rojo; **seleccion** = celda
	/// blanca con el objeto en rojo.
	void paint_cell(u16 slot, bool selected, bool focused, bool hovered) {
		if (slot >= kSlotCount) {
			return;
		}
		const u8 item = kItems[slot];
		const s16 cx = static_cast<s16>(kGridX + static_cast<s16>(slot % kGridCols) * kCell);
		const s16 cy = static_cast<s16>(kGridY + static_cast<s16>(slot / kGridCols) * kCell);
		push_fill(cx, cy, kCell, kCell, selected ? kHudFg : kHudBg);
		if (item != 0u) {
			push_fill(static_cast<s16>(cx + kTileMargin), static_cast<s16>(cy + kTileMargin),
				  kTile, kTile, selected ? kHudWarn : kHudAccent);
		}
		if (hovered) {
			push_frame(cx, cy, kCell, kCell, kHudFg);
		}
		if (focused) {
			push_frame(cx, cy, kCell, kCell, kHudWarn);
		}
	}

	void push_fill(s16 x, s16 y, u16 w, u16 h, u8 color) {
		eng::hud::HudDrawOp op {};
		op.kind = eng::hud::HudOpKind::Fill;
		op.box = eng::Box {x, y, w, h};
		op.color = color;
		(void)m_hud.push_op(op);
	}

	void push_frame(s16 x, s16 y, u16 w, u16 h, u8 color) {
		eng::hud::HudDrawOp op {};
		op.kind = eng::hud::HudOpKind::Frame;
		op.box = eng::Box {x, y, w, h};
		op.color = color;
		(void)m_hud.push_op(op);
	}

	/// Etiqueta estatica como operacion de texto (texto **por valor** en el op).
	void push_text(s16 x, s16 y, const char* text, u8 color) {
		eng::usize n = 0u;
		while (text[n] != '\0' && n < eng::hud::kHudTextMax) {
			++n;
		}
		eng::hud::HudDrawOp op {};
		op.kind = eng::hud::HudOpKind::Text;
		op.box = eng::Box {x, y, static_cast<u16>(n * 8u), 8u};
		op.color = color;
		(void)op.set_text(eng::Span<const char> {text, n});
		(void)m_hud.push_op(op);
	}

	/// Parte estatica de la franja: marco, etiqueta y el fondo de la zona de estado.
	void draw_static_hud() {
		eng::hud::HudDrawOp frame {};
		frame.kind = eng::hud::HudOpKind::Frame;
		frame.box = eng::Box {0, 0, kWidth, kBandHeight};
		frame.color = kHudFg;
		(void)m_hud.push_op(frame);
		push_text(112, 8, "ITEMS", kHudFg);
		(void)m_strip.apply(m_hud.ops());
		m_hud.clear_ops();
		m_hud.clear_dirty();
	}

	/// Texto de estado `SEL nn FOC nn` en x=112 (celdas de 16 px): la validacion puede
	/// contrastarlo con el modelo sin decodificar pixeles.
	void draw_state_numbers() {
		char line[16] {};
		const char* prefix = "SEL ";
		u8 n = 0u;
		for (const char* p = prefix; *p != '\0'; ++p) {
			line[n++] = *p;
		}
		const u16 sel = m_hud.grid(m_grid).selection;
		if (sel == eng::hud::kHudNoSlot) {
			line[n++] = '-';
			line[n++] = '-';
		} else {
			line[n++] = static_cast<char>('0' + (sel / 10u) % 10u);
			line[n++] = static_cast<char>('0' + sel % 10u);
		}
		line[n++] = ' ';
		const char* mid = "FOC ";
		for (const char* p = mid; *p != '\0'; ++p) {
			line[n++] = *p;
		}
		const u16 foc = m_hud.grid(m_grid).focus;
		line[n++] = static_cast<char>('0' + (foc / 10u) % 10u);
		line[n++] = static_cast<char>('0' + foc % 10u);
		auto surf = m_strip.surface();
		(void)surf.draw_text_words(kStateX, kStateY, line, kHudAccent);
	}

	/// Prefijo fijo de la linea de depuracion (AGENTS §0) y etiqueta de fps (x=208).
	void draw_version_prefix() {
		char line[32] {};
		u8 n = 0u;
		for (const char* p = "HUD ICONS v"; *p != '\0' && n < sizeof(line) - 1u; ++p) {
			line[n++] = *p;
		}
		line[n++] = static_cast<char>('0' + (kVersion.major % 10u));
		line[n++] = '.';
		line[n++] = static_cast<char>('0' + (kVersion.minor % 10u));
		line[n++] = '.';
		for (u8 d = 0u; d < 4u; ++d) {
			u32 div = 1000u;
			for (u8 k = 0u; k < d; ++k) {
				div /= 10u;
			}
			line[n++] = static_cast<char>('0' + ((kVersion.build / div) % 10u));
		}
		for (const char* p = " F"; *p != '\0' && n < sizeof(line) - 1u; ++p) {
			line[n++] = *p;
		}
		auto surf = m_strip.surface();
		(void)surf.draw_text_words(0, 40, line, kHudFg);
		(void)surf.draw_text_words(208, 40, " FPS  ", kHudFg);
	}

	/// Numero de frames en x=160 (longitud constante, sin borrado).
	void draw_version_number(u32 frame) {
		char digits[8] {};
		digits[0] = ' ';
		for (u8 d = 0u; d < 5u; ++d) {
			u32 div = 10000u;
			for (u8 k = 0u; k < d; ++k) {
				div /= 10u;
			}
			digits[1u + d] = static_cast<char>('0' + ((frame / div) % 10u));
		}
		auto surf = m_strip.surface();
		(void)surf.draw_text_words(160, 40, digits, kHudFg);
	}

	/// fps x10 (un decimal) en x=256 (etiqueta fija en x=208).
	void draw_fps(u32 fps10) {
		char digits[7] {};
		digits[0] = static_cast<char>('0' + (fps10 / 100u));
		digits[1] = static_cast<char>('0' + ((fps10 / 10u) % 10u));
		digits[2] = '.';
		digits[3] = static_cast<char>('0' + (fps10 % 10u));
		digits[4] = ' ';
		digits[5] = ' ';
		auto surf = m_strip.surface();
		(void)surf.draw_text_words(256, 40, digits, kHudFg);
	}

	static constexpr u16 kNoSlot = 0xffffu;
	/// Frames sin entrada real antes de que arranque el guion de demostracion (~2 s).
	static constexpr u32 kAttractAfterFrames = 100u;

	eng::hud::Hud<2u, 24u, 64u, 48u, 48u> m_hud {};
	eng::hud::HudStrip m_strip {};
	eng::hud::RegionHandle m_region {};
	eng::hud::GridHandle m_grid {};
	eng::hud::HudSlotView m_slots[kSlotCount] {};
	eng::hud::HudInput m_input {};
	eng::hud::HudInput::Policy m_policy {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_band_planes {};
	// Entrada: raton (deltas + botones) y pad 0.
	eng::input::MouseState m_mouse {};
	eng::amiga::MousePollState m_mouse_poll {};
	eng::input::InputAggregator m_input_agg {};
	s16 m_mx = static_cast<s16>(kWidth / 2u);
	s16 m_my = static_cast<s16>(kBandTop + kBandHeight / 2u);
	bool m_prev_left = false;
	u16 m_prev_joy = 0u;
	u32 m_idle_frames = 0u;
	u32 m_attract_frame = 0u;
	// Estado de pintado (deltas).
	u16 m_hover = kNoSlot;
	u16 m_prev_hover = kNoSlot;
	u16 m_prev_sel = 0u;
	u16 m_prev_foc = 0u;
	u32 m_hud_second = ~0u;
	eng::debug::HudFpsWindow m_fps_win {};
	bool m_ready = false;
};

// Estado del juego en BSS (convencion del repo: `run_demo` ya deja `AmigaBackend`,
// `DemoHost` y `App` en la pila del lanzador; el estado del juego no depende de el).
HudIconsDemo g_game {};

} // namespace

int main() {
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = 4u;
	display.layout = eng::graphics::PlaneLayout::Interleaved;
	display.palette = kGamePalette;
	// Colores del par de sprites 0/1 (COLOR16/17): cuerpo blanco y contorno negro del puntero
	// de hardware (la flecha clásica de `eng::ui::Pointer`).
	display.palette.color[16] = 0xfffu;
	display.palette.color[17] = 0x000u;
	display.hardware_cursor = true;
	// **Bucle por sondeo** (familia 212/DT-011, clase 119/400/303): bajo el SO vivo la
	// espera por senal pierde el despertar y la espera activa tampoco salva a esta demo
	// (paron determinista a los ~209 frames, medido 2026-10-19: vbl y frame congelados);
	// el sondeo (`run_polling`, sin depender de la IRQ de VBlank) es la unica variante
	// estable de la investigacion 212.
	return eng::amiga::run_demo(g_game, display, eng::amiga::VblankWait::Polling);
}
