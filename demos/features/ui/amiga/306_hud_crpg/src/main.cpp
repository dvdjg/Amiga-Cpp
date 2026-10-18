// Demo 306 - hud_crpg
// ----------------------------------------------------------------------------
// Tutorial (H5 de ROADMAP_HUD.md): **pantalla de CRPG/aventura** con barra de
// verbos, inventario RPG y tooltip. La franja (2 planos con paleta propia en la
// linea 208) muestra:
//   - una **barra de verbos** (LOOK/TAKE/USE/OPEN/TALK) del modelo H5
//     (`add_verb_bar`: los botones y su geometria viven en el modelo);
//   - una **rejilla 6x2** alimentada por el **puente RPG** (`hud_slots_from_bag`:
//     `ItemBag`/`ItemStore` del kernel `eng::rpg` -> vistas `HudSlotView`);
//   - un **tooltip** con TTL (`tooltip_show`/`tooltip_tick`) con el verbo/item en
//     curso;
//   - una linea de estado `V:n S:nn` (validable sin decodificar pixeles).
//
// Entrada por **dos vias coherentes** (HUD_SYSTEM §5.8):
//   - `HudInput` (raton y joystick reales): hover, click y pasos de rejilla;
//   - `HudFocus` (anillo geometrico, `eng/ui/interaction.hpp`): el guion
//     determinista mueve el foco con flechas y activa con Fire/Enter, tocando
//     verbos y celdas — los mismos efectos en el modelo que el raton.
//
//   bash ./tools/build/build-demo.sh demos/features/ui/amiga/306_hud_crpg --debug
//   bash ./tools/run/run-demo.sh demos/features/ui/amiga/306_hud_crpg --keep-running

#include <eng/api/api.hpp> // incluye eng::hud (H2+H5) y eng::rpg (puente)
#include <eng/debug/peripheral.hpp> // telemetria (fps10 por contador)
#include <eng/platform/amiga/entry.hpp>
#include <eng/platform/amiga/input_poll.hpp> // poll_mouse/poll_input (patron de la 215)
#include <eng/rpg/hud_bridge.hpp> // puente RPG -> HUD (H5)
#include <eng/scene/display.hpp>

// **Atlas de iconos del HUD** (H6): blob de 2 planos generado por `src/prebuild.sh`
// (`tools/assets/hud-atlas.mjs`) e incbinado; el ejecutable vive en Chip, asi que las
// palabras quedan direccionables por el camino de la banda (formato HUD_SYSTEM §8.2:
// palabras interleaved por filas, bit 15 = pixel 0, color 0 = transparente).
__asm__(".globl g_hud_atlas_words\ng_hud_atlas_words:\n"
	".align 2\n"
	".incbin \"out/assets/hud/test_icons/test_icons.atlas.bin\"\n");
extern "C" const eng::u16 g_hud_atlas_words[];

namespace {

using eng::s16;
using eng::u16;
using eng::u32;
using eng::u8;

// Geometria del atlas (debe coincidir con el manifiesto `test_icons.atlas.json`).
constexpr u16 kAtlasFrameW = 16u;
constexpr u16 kAtlasFrameH = 16u;
constexpr u8 kAtlasPlanes = 2u;
constexpr u16 kAtlasCount = 4u;
constexpr u16 kAtlasStrideWords = 16u; // palabras por frame y plano (16 filas x 1 palabra)
// Palabras por frame: planos x stride (interleaved por filas: [fila0 p0][fila0 p1]...).
constexpr u16 kAtlasFrameWords = static_cast<u16>(kAtlasPlanes * kAtlasStrideWords);

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

// --- Geometria del HUD en la banda ------------------------------------------------
// Rejilla del inventario: 6x2 celdas de 16x16 en (8,3) -> area 96x32.
constexpr u8 kGridCols = 6u;
constexpr u8 kGridRows = 2u;
constexpr u8 kCell = 16u;
constexpr s16 kGridX = 8;
constexpr s16 kGridY = 3;
constexpr u16 kGridW = static_cast<u16>(kGridCols) * kCell;
constexpr u16 kGridH = static_cast<u16>(kGridRows) * kCell;
constexpr u16 kSlotCount = static_cast<u16>(kGridCols) * kGridRows;
constexpr s16 kTileMargin = 2;
constexpr u16 kTile = kCell - 2u * kTileMargin;

// Barra de verbos: 5 verbos de 40 px en (112,3), alto 11.
constexpr u8 kVerbCount = 5u;
constexpr s16 kVerbX = 112;
constexpr s16 kVerbY = 3;
constexpr u16 kVerbCell = 40u;
constexpr u16 kVerbH = 11u;
// Los hits de los verbos usan ids logicos >= 100 (las celdas usan 0..11).
constexpr u16 kVerbIdBase = 100u;

// --- Segunda region (Overlay sobre el campo): dos botones en pantalla ---------------------
constexpr s16 kOvX = 216;
constexpr s16 kOvY = 8;
constexpr u16 kOvW = 96u;
constexpr u16 kOvH = 12u;
constexpr u16 kOvIdBase = 200u; // ids logicos de los botones del overlay (EQUIP/DROP)

// Linea de estado (x alineada a 16 px para `draw_text_words`) y tooltip.
constexpr s16 kStateX = 112;
constexpr s16 kStateY = 17;
constexpr s16 kTipX = 112;
constexpr s16 kTipY = 27;
constexpr u16 kTipW = 200u;

/// Juego minimo: campo estatico + pantalla CRPG interactiva en la franja.
struct HudCrpgDemo {
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

		// (2) Region con **captura de entrada** (la franja consume el raton) y modelo.
		eng::hud::HudRegionSpec region {};
		region.placement = eng::hud::HudPlacement::Band;
		region.band = eng::hud::HudBand {kBandTop, kBandHeight, kBandPlanes, true};
		region.capture_input = true;
		region.budget.max_draw_ops = 64u;
		m_region = m_hud.add_region(region);
		if (!m_region.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}

		// (3) Contenido RPG: catalogo minimo, bolsa con objetos y equipo.
		//     El HUD recibe VISTAS (HudSlotView) via el puente; no conoce eng::rpg.
		{
			eng::rpg::ItemDef def {};
			def.stack_max = 5u;
			m_type_key = m_catalog.add(def);
			m_type_coin = m_catalog.add(def);
			def.stack_max = 1u;
			m_type_gem = m_catalog.add(def);
			def.stack_max = 3u;
			m_type_potion = m_catalog.add(def);
		}
		if (m_type_key == eng::rpg::no_item_type || m_type_coin == eng::rpg::no_item_type ||
		    m_type_gem == eng::rpg::no_item_type ||
		    m_type_potion == eng::rpg::no_item_type) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}		{
			const eng::rpg::ItemInstance items[5] = {
				{m_type_key, 1u}, {m_type_coin, 4u}, {m_type_gem, 1u},
				{m_type_potion, 2u}, {m_type_coin, 3u},
			};
			for (u8 i = 0u; i < 5u; ++i) {
				const eng::rpg::ItemHandle h = m_store.spawn(items[i]);
				if (!h.valid() || !m_bag.put(h)) {
					eng::amiga::mark_failed(eng::debug::BootFail::Game);
					return;
				}
			}
			m_equipment.slots[0u] = m_bag.at(0u); // la llave, equipada
		}
		// Tabla de iconos del juego: frame = tipo (0 = sin objeto).
		constexpr u16 kIcons[5] = {0u, 1u, 2u, 3u, 4u};

		// (4) Rejilla del inventario + puente RPG -> vistas.
		m_grid = m_hud.add_grid(kGridRows, kGridCols, kCell, kCell);
		if (!m_grid.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		eng::hud::HudSlotView slots[kSlotCount] {};
		(void)eng::rpg::hud_slots_from_bag(
			m_bag, m_store, eng::Span<const u16> {kIcons, 5u},
			eng::Span<eng::hud::HudSlotView> {slots, kSlotCount}, &m_equipment);
		if (!m_hud.bind_grid(m_grid, eng::Span<const eng::hud::HudSlotView> {slots,
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
		gh.region = m_region;
		(void)m_hud.push_grid_hit(gh);

		// (4b) **Segunda region** (Overlay, coordenadas de pantalla): dos botones sobre el
		// campo (EQUIP/DROP) con sus hits etiquetados: el juego despacha las regiones de
		// delante hacia atras (overlay -> banda) con `hud_hit_test_region` (H5).
		eng::hud::HudRegionSpec ov_spec {};
		ov_spec.placement = eng::hud::HudPlacement::Overlay;
		ov_spec.z = 1u; // delante de la banda en el despacho
		m_region2 = m_hud.add_region(ov_spec);
		if (!m_region2.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		for (u8 i = 0u; i < 2u; ++i) {
			eng::hud::HudHitRect hr {};
			hr.box = eng::Box {kOvX, static_cast<s16>(kOvY + i * kOvH), kOvW, kOvH};
			hr.id = static_cast<u16>(kOvIdBase + i);
			hr.layer = static_cast<u8>(1u + i);
			hr.region = m_region2;
			(void)m_hud.push_hit(hr);
		}

		// (5) Barra de verbos (H5): botones y geometria en el modelo.
		{
			static const char* const kVerbs[kVerbCount] = {"LOOK", "TAKE", "USE", "OPEN",
								       "TALK"};
			m_verbs = m_hud.add_verb_bar(eng::Span<const char* const> {kVerbs, kVerbCount},
						     eng::Box {kVerbX, kVerbY, static_cast<u16>(kVerbCount) *
										    kVerbCell,
							       kVerbH},
						     kVerbCell);
			if (!m_verbs.valid()) {
				eng::amiga::mark_failed(eng::debug::BootFail::Game);
				return;
			}
			// Un hit por verbo con la geometria del modelo (mismo mapa para raton y foco).
			const eng::hud::VerbBar& bar = m_hud.verb_bar(m_verbs);
			for (u8 i = 0u; i < bar.count; ++i) {
				const eng::hud::Button& b =
					m_hud.button(m_hud.verb_button(m_verbs, i));
				eng::hud::HudHitRect hr {};
				hr.box = b.box;
				hr.id = static_cast<u16>(kVerbIdBase + i);
				hr.layer = 1u; // delante de la rejilla
				hr.region = m_region;
				(void)m_hud.push_hit(hr);
			}
		}
		// Tooltip con TTL (H5): el demo lo muestra al cambiar verbo/celda.
		m_tooltip = m_hud.add_tooltip();
		if (!m_tooltip.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}

		// (6) Composicion: campo de juego + franja con paleta propia (patron de la 302).
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

		// (7) Fondo estatico del campo (backdrop) y parte estatica de la franja.
		eng::Screen screen = app.screen();
		screen.clear(0u);
		(void)screen.fill(eng::Box {0, 180, kWidth, 28u}, 1u);
		(void)screen.fill(eng::Box {40, 120, 40u, 24u}, 3u);
		(void)screen.fill(eng::Box {200, 96, 64u, 16u}, 5u);
		(void)screen.fill(eng::Box {120, 150, 24u, 40u}, 2u);
		// Overlay (segunda region): panel + dos botones pintados una vez sobre el campo.
		(void)screen.fill(eng::Box {static_cast<s16>(kOvX - 4), static_cast<s16>(kOvY - 4),
					    static_cast<u16>(kOvW + 8u),
					    static_cast<u16>(2u * kOvH + 8u)}, 4u);
		for (u8 i = 0u; i < 2u; ++i) {
			(void)screen.fill(eng::Box {kOvX, static_cast<s16>(kOvY + i * kOvH), kOvW,
						    static_cast<u16>(kOvH - 2u)},
					  static_cast<u8>(6u + i));
		}
		draw_static_hud();
		draw_version_prefix();
		draw_version_number(0u);
		draw_fps(0u);
		m_fps_win.reset(app.frame(), eng::debug::prof_clock());

		// (8) Entrada: `HudInput` (raton/joystick reales) con la rejilla de navegacion
		//     y la politica derivada del contrato de region (`capture_input`).
		m_input.set_focus_grid(m_grid);
		m_policy = eng::hud::HudInput::Policy::from_region(m_hud.region(m_region));
		// El anillo de foco (HudFocus) parte del primer hit (verbo LOOK).
		(void)m_focus.focus_first(m_hud);

		// Estado inicial pintado (celdas + verbos + estado + tooltip) y **aplicado**.
		m_prev_sel = m_hud.grid(m_grid).selection;
		m_prev_foc = m_hud.grid(m_grid).focus;
		for (u16 i = 0u; i < kSlotCount; ++i) {
			paint_cell(i, i == m_prev_sel, i == m_prev_foc, false);
		}
		for (u8 i = 0u; i < kVerbCount; ++i) {
			paint_verb(i, i == m_hud.verb_bar(m_verbs).active, false, false);
		}
		draw_state_numbers();
		show_tooltip_for_state();
		(void)m_strip.apply(m_hud.ops());
		paint_icon_pass();
		m_hud.clear_ops();
		m_hud.clear_dirty();

		m_ready = true;
		eng::debug::DebugPeripheral::counter_name(0u, reinterpret_cast<eng::u32>(kCntFps10));
		eng::debug::DebugPeripheral::counter_name(1u, reinterpret_cast<eng::u32>("state"));
		eng::debug::DebugPeripheral::counter_name(2u, reinterpret_cast<eng::u32>("aframe"));
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

		// (2) Raton: el cursor vive en la banda (x 0..319, y 208..255); solo se emiten
		// eventos cuando algo cambia (el raton de Amiga da deltas por contador).
		const bool mouse_moved = m_mouse.dx != 0 || m_mouse.dy != 0;
		const bool left_down = m_mouse.left_button;
		const bool left_edge = left_down != m_prev_left;
		if (mouse_moved) {
			m_mx = clamp_s16(static_cast<s16>(m_mx + m_mouse.dx), 0,
					 static_cast<s16>(kWidth - 1u));
			m_my = clamp_s16(static_cast<s16>(m_my + m_mouse.dy),
					 static_cast<s16>(kBandTop), static_cast<s16>(kHeight - 1u));
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

		// (3) Joystick: **flanco** de cada direccion/fuego (una pulsacion = un paso).
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

		// (4) **Guion de demostracion** (attract): tras ~2 s sin entrada real, el anillo
		// de foco (`HudFocus`, la capa de foco del HUD) recorre verbos y celdas con
		// flechas y **activa** con Fire: el mismo camino que el raton (HUD_SYSTEM §5.8).
		if (m_idle_frames < kAttractAfterFrames) {
			++m_idle_frames;
		} else {
			if ((m_attract_frame % 16u) == 0u) {
				const u8 step = static_cast<u8>((m_attract_frame / 16u) % 12u);
				(void)m_focus.move(m_hud, tour_dx(step), tour_dy(step));
				apply_focus_to_widgets();
			}
			if ((m_attract_frame % 48u) == 0u) {
				activate_focus();
			}
			++m_attract_frame;
		}
		// Telemetria permanente: estado (verbo*100 + seleccion) y fase del guion; el gate
		// visual los usa como **evidencia de memoria** (la captura por paso puede congelar
		// el video: DT-023/035). `aframe` avanzando prueba que el guion recorre y activa.
		{
			const eng::hud::VerbBar& bar = m_hud.verb_bar(m_verbs);
			const u16 sel = m_hud.grid(m_grid).selection;
			eng::debug::DebugPeripheral::counter_value(
				1u, static_cast<u32>(bar.active) * 100u +
					    static_cast<u32>(sel == eng::hud::kHudNoSlot ? 99u : sel));
			eng::debug::DebugPeripheral::counter_value(2u, m_attract_frame);
		}
		(void)app;
	}

	void render(auto& app) {
		if (!m_ready) {
			return;
		}
		// **Cambio de tema en caliente** (H6): clasico <-> ambar cada 300 frames; el modelo
		// no cambia, solo los indices que pinta el presentador. Al cambiar, repintado
		// completo de la banda (clear + contenido).
		const u8 want = static_cast<u8>((app.frame() / 300u) & 1u);
		if (want != m_prev_theme) {
			m_prev_theme = want;
			m_theme = (want == 0u) ? eng::hud::hud_theme_classic()
					       : eng::hud::hud_theme_amber();
			// El tema es **contrato de la region** (H6): el modelo guarda la instantanea.
			(void)m_hud.set_region_theme(m_region, m_theme);
			repaint_all();
		}
		// **Orden por banda** (HUD_SYSTEM §9.3): capa viva del juego primero (aqui el
		// campo es estatico, sin jobs), franja despues por CPU.
		(void)app.draw_world();
		app.present();

		// Deltas del HUD: celdas y verbos cuyo estado cambio este frame.
		const eng::hud::IconGrid& grid = m_hud.grid(m_grid);
		const eng::hud::VerbBar& bar = m_hud.verb_bar(m_verbs);
		const bool sel_changed = grid.selection != m_prev_sel;
		const bool foc_changed = grid.focus != m_prev_foc;
		const bool verb_changed = bar.active != m_prev_verb;
		// Foco del anillo sobre la barra de verbos (rect con id >= kVerbIdBase): se pinta el
		// marco rojo del verbo enfocado (el anillo puede estar en la rejilla o en los verbos).
		const eng::hud::HudFocusTarget& ft = m_focus.target();
		const s16 vfoc = (ft.kind == eng::hud::HudFocusTarget::Kind::Rect &&
				  ft.id >= kVerbIdBase && ft.id < kVerbIdBase + kVerbCount)
					 ? static_cast<s16>(ft.id - kVerbIdBase)
					 : -1;
		const bool vfoc_changed = vfoc != m_prev_vfoc;
		if (sel_changed) {
			paint_cell(m_prev_sel, false, m_prev_sel == m_prev_foc,
				   m_prev_hover == m_prev_sel);
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
		if (verb_changed) {
			paint_verb(m_prev_verb, false, false, false);
			paint_verb(bar.active, true, false, false);
			m_prev_verb = bar.active;
		}
		if (vfoc_changed) {
			if (m_prev_vfoc >= 0) {
				paint_verb(static_cast<u8>(m_prev_vfoc),
					   static_cast<u8>(m_prev_vfoc) == bar.active, false, false);
			}
			if (vfoc >= 0) {
				paint_verb(static_cast<u8>(vfoc), static_cast<u8>(vfoc) == bar.active,
					   true, false);
			}
			m_prev_vfoc = vfoc;
		}
		if (sel_changed || foc_changed || verb_changed || vfoc_changed) {
			draw_state_numbers();
			show_tooltip_for_state();
		}
		// El hover (raton) se repinta al cambiar de celda.
		if (m_hover != m_prev_hover) {
			if (m_prev_hover < kSlotCount) {
				paint_cell(m_prev_hover, m_prev_hover == m_prev_sel,
					   m_prev_hover == m_prev_foc, false);
			}
			if (m_hover < kSlotCount) {
				paint_cell(m_hover, m_hover == m_prev_sel, m_hover == m_prev_foc,
					   true);
			}
			m_prev_hover = m_hover;
		}
		// Tooltip (H5): TTL por frame; se repinta la linea al expirar o cambiar.
		(void)m_hud.tooltip_tick();
		const bool tip_now = m_hud.tooltip_visible(m_tooltip);
		if (tip_now != m_tip_visible) {
			m_tip_visible = tip_now;
			paint_tooltip_line();
		}
		if (m_hud.any_dirty()) {
			(void)m_strip.apply(m_hud.ops());
			paint_icon_pass();
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
	}

private:
	/// Repintado **completo** de la banda (cambio de tema): limpia el lienzo y vuelve a
	/// emitir contenido estatico y dinamico con los indices del tema vigente.
	void repaint_all() {
		m_strip.clear();
		draw_static_hud();
		for (u16 i = 0u; i < kSlotCount; ++i) {
			paint_cell(i, i == m_prev_sel, i == m_prev_foc, i == m_prev_hover);
		}
		for (u8 i = 0u; i < kVerbCount; ++i) {
			paint_verb(i, i == m_prev_verb, false, false);
		}
		draw_state_numbers();
		paint_tooltip_line();
		(void)m_strip.apply(m_hud.ops());
		paint_icon_pass();
		m_hud.clear_ops();
		m_hud.clear_dirty();
	}

	/// Consume un evento con `HudInput` (raton/joystick reales) y actualiza el **hover**
	/// del demo; los verbos (id >= `kVerbIdBase`) y las celdas producen los mismos efectos
	/// que el anillo (`verb_select` / `grid_select`).
	void consume(auto& app, const eng::ui::UiEvent& ev) {
		// **Despacho multi-region** (H5): las regiones se consultan de delante hacia atras
		// (overlay -> banda, HUD_SYSTEM §7.1); el overlay resuelve sus botones con su id.
		const eng::hud::HudHit ov =
			eng::hud::hud_hit_test_region(m_hud, m_region2, ev.x, ev.y);
		if (ov.hit) {
			const s16 idx = static_cast<s16>(ov.id - kOvIdBase);
			if (ev.kind == eng::ui::UiEventKind::MouseMove) {
				m_ov_hover = idx;
			} else if (ev.kind == eng::ui::UiEventKind::MouseDown ||
				   ev.kind == eng::ui::UiEventKind::JoyButton) {
				m_ov_hover = idx;
				show_tooltip_raw(idx == 0 ? "ACTION:EQUIP" : "ACTION:DROP");
			}
			return;
		}
		if (ev.kind == eng::ui::UiEventKind::MouseMove) {
			m_ov_hover = -1;
		}
		const eng::hud::HudInput::Result r = m_input.handle(m_hud, m_policy, ev);
		if (ev.kind == eng::ui::UiEventKind::MouseMove ||
		    ev.kind == eng::ui::UiEventKind::MouseDown) {
			if (r.consumed && r.grid.valid()) {
				m_hover = r.slot;
			} else {
				m_hover = kNoSlot;
			}
		}
		if (r.consumed && r.id >= kVerbIdBase && r.id < kVerbIdBase + kVerbCount) {
			if (ev.kind == eng::ui::UiEventKind::MouseDown ||
			    ev.kind == eng::ui::UiEventKind::JoyButton) {
				(void)m_hud.verb_select(m_verbs,
							static_cast<u8>(r.id - kVerbIdBase));
				show_tooltip_for_state();
			}
		}
		(void)app;
	}

	/// Activa el elemento con foco del anillo: celda -> `grid_select`; verbo -> `verb_select`
	/// (el anillo devuelve el `id` del rect, el mismo camino que el click).
	void activate_focus() {
		const eng::hud::HudFocusResult r = m_focus.activate(m_hud);
		if (!r.activated) {
			return;
		}
		if (r.grid.valid()) {
			show_tooltip_for_state();
			return;
		}
		if (r.id >= kVerbIdBase && r.id < kVerbIdBase + kVerbCount) {
			(void)m_hud.verb_select(m_verbs, static_cast<u8>(r.id - kVerbIdBase));
			show_tooltip_for_state();
		}
	}

	/// Sincroniza el foco de navegacion de la barra con el objetivo del anillo (el modelo
	/// tiene su propio `focus`; se alinea con un desplazamiento relativo).
	void apply_focus_to_widgets() {
		const eng::hud::HudFocusTarget& t = m_focus.target();
		if (t.kind != eng::hud::HudFocusTarget::Kind::Rect) {
			return;
		}
		if (t.id < kVerbIdBase || t.id >= kVerbIdBase + kVerbCount) {
			return;
		}
		const u8 want = static_cast<u8>(t.id - kVerbIdBase);
		const u8 cur = m_hud.verb_bar(m_verbs).focus;
		if (want != cur) {
			(void)m_hud.verb_move_focus(m_verbs,
						    static_cast<eng::s8>(static_cast<s16>(want) -
									 static_cast<s16>(cur)));
		}
	}

	/// Muestra el tooltip (H5) con el verbo activo y la celda seleccionada.
	void show_tooltip_for_state() {
		const eng::hud::VerbBar& bar = m_hud.verb_bar(m_verbs);
		const eng::hud::IconGrid& grid = m_hud.grid(m_grid);
		char line[24] {};
		u8 n = 0u;
		const char* v = "VERB:";
		for (const char* p = v; *p != '\0' && n < sizeof(line) - 1u; ++p) {
			line[n++] = *p;
		}
		// Nombre del verbo activo (4 caracteres de la etiqueta del boton).
		const eng::hud::Button& b = m_hud.button(m_hud.verb_button(m_verbs, bar.active));
		const eng::Span<const char> label = b.text_view();
		for (eng::usize i = 0u; i < label.size() && n < sizeof(line) - 1u; ++i) {
			line[n++] = label[i];
		}
		const char* mid = " ITEM:";
		for (const char* p = mid; *p != '\0' && n < sizeof(line) - 1u; ++p) {
			line[n++] = *p;
		}
		const u16 sel = grid.selection;
		if (sel == eng::hud::kHudNoSlot) {
			line[n++] = '-';
			line[n++] = '-';
		} else {
			line[n++] = static_cast<char>('0' + (sel / 10u) % 10u);
			line[n++] = static_cast<char>('0' + sel % 10u);
		}
		show_tooltip_raw(line, n);
	}

	/// Muestra el tooltip con un texto crudo (p. ej. la accion del overlay).
	void show_tooltip_raw(const char* text, u8 len = 0u) {
		u8 n = len;
		if (n == 0u) {
			while (text[n] != '\0' && n < 23u) {
				++n;
			}
		}
		// TTL: 150 frames (~3 s); el presentador pinta mientras `tooltip_visible`.
		(void)m_hud.tooltip_show(m_tooltip, eng::Span<const char> {text, n},
					 eng::Box {kTipX, kTipY, kTipW, 8u}, m_theme.fg, 150u);
	}

	/// Pinta la linea del tooltip (fondo + texto) como ops; al ocultarse, solo fondo.
	void paint_tooltip_line() {
		push_fill(kTipX, kTipY, kTipW, 8u, m_theme.bg);
		if (m_tip_visible) {
			const eng::hud::Tooltip& t = m_hud.tooltip(m_tooltip);
			push_text(kTipX, kTipY, t.text_view(), m_theme.fg);
		}
	}

	/// Pinta una celda del inventario: fondo (seleccion), barra de cantidad y marcos de hover
	/// (blanco) y foco (rojo); el **icono** (atlas H6) va en una pasada posterior al `apply`
	/// (color 0 = transparente).
	void paint_cell(u16 slot, bool selected, bool focused, bool hovered) {
		if (slot >= kSlotCount) {
			return;
		}
		const s16 cx = static_cast<s16>(kGridX + static_cast<s16>(slot % kGridCols) * kCell);
		const s16 cy = static_cast<s16>(kGridY + static_cast<s16>(slot / kGridCols) * kCell);
		push_fill(cx, cy, kCell, kCell, selected ? m_theme.fg : m_theme.bg);
		push_icon(slot);
		if (hovered) {
			push_frame(cx, cy, kCell, kCell, m_theme.fg);
		}
		if (focused) {
			push_frame(cx, cy, kCell, kCell, m_theme.warn);
		}
	}

	/// Marca una celda para la **pasada de iconos** (sin duplicados; tope = celdas).
	void push_icon(u16 slot) {
		for (u8 i = 0u; i < m_icon_n; ++i) {
			if (m_icon_pass[i] == slot) {
				return;
			}
		}
		if (m_icon_n < kSlotCount) {
			m_icon_pass[m_icon_n++] = slot;
		}
	}

	/// **Pasada de iconos** (H6): lee el atlas (2 planos, interleaved por filas, bit 15 =
	/// pixel 0, color 0 = transparente) y pinta la trama de cada celda marcada en el lienzo
	/// de la banda; la barra de cantidad va encima. Debe correr DESPUES de `m_strip.apply`
	/// (los ops de fondo ya estan puestos).
	void paint_icon_pass() {
		auto surf = m_strip.surface();
		const eng::hud::IconGrid& grid = m_hud.grid(m_grid);
		for (u8 i = 0u; i < m_icon_n; ++i) {
			const u16 slot = m_icon_pass[i];
			if (slot >= kSlotCount || slot >= grid.slots.size()) {
				continue;
			}
			const eng::hud::HudSlotView& sv = grid.slots[slot];
			if ((sv.flags & eng::hud::kHudSlotEmpty) != 0u || sv.item_id == 0u) {
				continue;
			}
			const s16 cx = static_cast<s16>(kGridX +
							static_cast<s16>(slot % kGridCols) * kCell);
			const s16 cy = static_cast<s16>(kGridY +
							static_cast<s16>(slot / kGridCols) * kCell);
			const u16 frame = static_cast<u16>(sv.icon_frame % kAtlasCount);
			for (u16 row = 0u; row < kAtlasFrameH; ++row) {
				const eng::u16* const w = &g_hud_atlas_words[frame * kAtlasFrameWords +
									     row * kAtlasPlanes];
				const u16 p0 = w[0];
				const u16 p1 = w[1];
				for (u16 px = 0u; px < kAtlasFrameW; ++px) {
					const u16 bit = static_cast<u16>(0x8000u >> px);
					const u8 c = static_cast<u8>(
						((p0 & bit) != 0u ? 1u : 0u) |
						((p1 & bit) != 0u ? 2u : 0u));
					if (c != 0u) {
						(void)surf.set_pixel(
							static_cast<s16>(cx + static_cast<s16>(px)),
							static_cast<s16>(cy + static_cast<s16>(row)), c);
					}
				}
			}
			// Barra de cantidad (2 px por unidad) sobre el borde inferior de la trama.
			if (sv.qty > 1u) {
				const u16 wq = static_cast<u16>(sv.qty * 2u > kTile ? kTile
										     : sv.qty * 2u);
				for (u16 k = 0u; k < wq; ++k) {
					(void)surf.set_pixel(
						static_cast<s16>(cx + kTileMargin + static_cast<s16>(k)),
						static_cast<s16>(cy + kCell - kTileMargin - 1),
						m_theme.accent);
				}
			}
		}
		m_icon_n = 0u;
	}

	/// Pinta un verbo: fondo (activo = amarillo), marco de foco (rojo) y etiqueta centrada.
	void paint_verb(u8 index, bool active, bool focused, bool hovered) {
		if (index >= kVerbCount) {
			return;
		}
		const eng::hud::Button& b = m_hud.button(m_hud.verb_button(m_verbs, index));
		const s16 bx = b.box.x;
		const s16 by = b.box.y;
		const u16 bw = b.box.w;
		push_fill(bx, by, bw, kVerbH, active ? m_theme.accent : m_theme.bg);
		if (hovered) {
			push_frame(bx, by, bw, kVerbH, m_theme.fg);
		}
		if (focused) {
			push_frame(bx, by, bw, kVerbH, m_theme.warn);
		}
		const eng::Span<const char> label = b.text_view();
		const u16 tw = static_cast<u16>(label.size() * 8u);
		push_text(static_cast<s16>(bx + static_cast<s16>(bw / 2u) -
					   static_cast<s16>(tw / 2u)),
			  static_cast<s16>(by + 2), label, active ? m_theme.bg : m_theme.fg);
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

	/// Texto como operacion (texto **por valor** en el op).
	void push_text(s16 x, s16 y, eng::Span<const char> text, u8 color) {
		eng::hud::HudDrawOp op {};
		op.kind = eng::hud::HudOpKind::Text;
		op.box = eng::Box {x, y, static_cast<u16>(text.size() * 8u), 8u};
		op.color = color;
		(void)op.set_text(text);
		(void)m_hud.push_op(op);
	}

	/// Parte estatica de la franja: marco, etiquetas fijas y separadores.
	void draw_static_hud() {
		eng::hud::HudDrawOp frame {};
		frame.kind = eng::hud::HudOpKind::Frame;
		frame.box = eng::Box {0, 0, kWidth, kBandHeight};
		frame.color = m_theme.fg;
		(void)m_hud.push_op(frame);
		(void)m_strip.apply(m_hud.ops());
		m_hud.clear_ops();
		m_hud.clear_dirty();
	}

	/// Texto de estado `V:n S:nn` (verbos y celda): la validacion puede contrastarlo con
	/// el modelo sin decodificar pixeles.
	void draw_state_numbers() {
		char line[16] {};
		u8 n = 0u;
		const char* prefix = "V:";
		for (const char* p = prefix; *p != '\0'; ++p) {
			line[n++] = *p;
		}
		const u8 v = m_hud.verb_bar(m_verbs).active;
		line[n++] = static_cast<char>('0' + (v % 10u));
		const char* mid = " S:";
		for (const char* p = mid; *p != '\0'; ++p) {
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
		auto surf = m_strip.surface();
		(void)surf.draw_text_words(kStateX, kStateY, line, m_theme.accent);
	}

	/// Prefijo fijo de la linea de depuracion (AGENTS §0) y etiqueta de fps.
	void draw_version_prefix() {
		char line[32] {};
		u8 n = 0u;
		for (const char* p = "HUD CRPG v"; *p != '\0' && n < sizeof(line) - 1u; ++p) {
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
		(void)surf.draw_text_words(224, 40, " FPS  ", kHudFg);
	}

	/// Numero de frames en x=176 (longitud constante, sin borrado).
	void draw_version_number(u32 frame) {
		char digits[8] {};
		for (u8 i = 0u; i < 5u; ++i) {
			const u32 div = (i == 0u) ? 10000u : (i == 1u) ? 1000u
					: (i == 2u) ? 100u : (i == 3u) ? 10u : 1u;
			digits[i] = static_cast<char>('0' + ((frame / div) % 10u));
		}
		auto surf = m_strip.surface();
		(void)surf.draw_text_words(176, 40, digits, kHudFg);
	}

	/// fps con un decimal en x=272 (longitud constante).
	void draw_fps(u32 fps10) {
		char digits[4] {};
		digits[0] = static_cast<char>('0' + (fps10 / 100u));
		digits[1] = static_cast<char>('0' + ((fps10 / 10u) % 10u));
		digits[2] = '.';
		digits[3] = static_cast<char>('0' + (fps10 % 10u));
		auto surf = m_strip.surface();
		(void)surf.draw_text_words(272, 40, digits, kHudFg);
	}

	/// Desplazamiento del guion: recorrido por el perimetro de la rejilla (5 derecha,
	/// abajo, 5 izquierda, arriba) — al llegar al borde derecho, el anillo salta a los
	/// verbos (geometrico: centro contra centro).
	[[nodiscard]] static eng::s8 tour_dx(u8 step) {
		static const eng::s8 kDx[12] = {1, 1, 1, 1, 1, 0, -1, -1, -1, -1, -1, 0};
		return kDx[step % 12u];
	}
	[[nodiscard]] static eng::s8 tour_dy(u8 step) {
		static const eng::s8 kDy[12] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, -1};
		return kDy[step % 12u];
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

	static constexpr u16 kNoSlot = eng::hud::kHudNoSlot;
	static constexpr u16 kAttractAfterFrames = 100u; // ~2 s a 50 fps

	[[nodiscard]] static s16 clamp_s16(s16 v, s16 lo, s16 hi) {
		return v < lo ? lo : (v > hi ? hi : v);
	}

	// --- Estado del demo ------------------------------------------------------------
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_band_planes {};
	eng::hud::HudStrip m_strip {};
	eng::hud::Hud<2u, 24u, 64u, 48u, 48u> m_hud {};
	eng::hud::RegionHandle m_region {};
	eng::hud::RegionHandle m_region2 {}; ///< overlay (segunda region: botones en pantalla)
	eng::hud::HudTheme m_theme = eng::hud::hud_theme_classic(); ///< tema vigente (H6)
	u8 m_prev_theme = 0u;  ///< 0 = clasico, 1 = ambar (para el cambio en caliente)
	s16 m_ov_hover = -1;   ///< boton del overlay bajo el puntero (-1 = ninguno)
	u16 m_icon_pass[kSlotCount] {}; ///< celdas marcadas para la pasada de iconos (H6)
	u8 m_icon_n = 0u;
	eng::hud::GridHandle m_grid {};
	eng::hud::VerbBarHandle m_verbs {};
	eng::hud::TooltipHandle m_tooltip {};
	eng::hud::HudInput m_input {};
	eng::hud::HudInput::Policy m_policy {};
	eng::hud::HudFocus m_focus {};
	eng::input::MouseState m_mouse {};
	eng::amiga::MousePollState m_mouse_poll {};
	eng::input::InputAggregator m_input_agg {};
	// Contenido RPG (kernel `eng::rpg`): catalogo, store, bolsa y equipo.
	eng::rpg::ItemCatalog<4u> m_catalog {};
	eng::rpg::ItemStore<8u> m_store {};
	eng::rpg::ItemBag<6u> m_bag {};
	eng::rpg::Equipment<1u> m_equipment {};
	eng::rpg::ItemTypeId m_type_key {};
	eng::rpg::ItemTypeId m_type_coin {};
	eng::rpg::ItemTypeId m_type_gem {};
	eng::rpg::ItemTypeId m_type_potion {};
	s16 m_mx = 160;
	s16 m_my = static_cast<s16>(kBandTop + 20);
	bool m_prev_left = false;
	u16 m_prev_joy = 0u;
	u32 m_idle_frames = 0u;
	u32 m_attract_frame = 0u;
	u16 m_hover = kNoSlot;
	u16 m_prev_hover = kNoSlot;
	u16 m_prev_sel = 0u;
	u16 m_prev_foc = 0u;
	u8 m_prev_verb = 0u;
	s16 m_prev_vfoc = -1; ///< verbo con foco del anillo en el frame anterior (-1 = ninguno)
	bool m_tip_visible = false;
	u32 m_hud_second = ~0u;
	eng::debug::HudFpsWindow m_fps_win {};
	bool m_ready = false;
};

} // namespace

// Estado del juego en BSS (convencion del repo: `run_demo` ya deja `AmigaBackend`,
// `DemoHost` y `App` en la pila del lanzador; el estado del juego no depende de el).
HudCrpgDemo g_game {};

int main() {
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = 4u;
	// Interleaved: la escena expone su `CanvasPlayfield` y `band_from_view` lo compone.
	display.layout = eng::graphics::PlaneLayout::Interleaved;
	display.palette = kGamePalette;
	return eng::amiga::run_demo(g_game, display);
}
