// Demo 304 - hud_drag
// ----------------------------------------------------------------------------
// Tutorial (H4 de ROADMAP_HUD.md): **inventario con arrastre** (drag & drop) por la
// maquina de puntero del modelo (`DragSession`, `HUD_SYSTEM.md` §7.2):
//   Pressed  --umbral-->  Dragging  --drop valido-->  Dropped
//      |                      |                          |
//      +--up (click)--> Idle   +--cancel--> Cancelled <--+
//
// La franja (2 planos con paleta propia en la linea 208) muestra una rejilla 6x2 con
// objetos del kernel `eng::rpg` (puente `hud_slots_from_bag`, H5) y una **linea de
// estado** `D:nn SRC:nn DST:nn` (fase del arrastre y slots). El **fantasma** del objeto
// arrastrado se pinta con el atlas de iconos siguiendo al puntero (color 0 =
// transparente); soltar en una celda mueve el objeto en el inventario RPG, soltar fuera
// cancela.
//
// Guion determinista: cada ~2 s arrastra un objeto de una celda a otra (alternando un
// drop valido y una cancelacion) para que el gate vea la maquina completa.
//
//   bash ./tools/build/build-demo.sh demos/features/ui/amiga/304_hud_drag --debug
//   bash ./tools/run/run-demo.sh demos/features/ui/amiga/304_hud_drag --keep-running

#include <eng/api/api.hpp> // incluye eng::hud y eng::rpg (puente)
#include <eng/debug/peripheral.hpp> // telemetria (fps10/contadores)
#include <eng/platform/amiga/entry.hpp>
#include <eng/platform/amiga/input_poll.hpp> // poll_mouse/poll_input
#include <eng/rpg/hud_bridge.hpp>
#include <eng/scene/display.hpp>

// **Atlas de iconos del HUD** (H6): blob generado por `src/prebuild.sh` e incbinado
// (formato HUD_SYSTEM §8.2: 2 planos, palabras interleaved por filas, bit 15 = pixel 0,
// color 0 = transparente). El ejecutable vive en Chip: direccionable por la banda.
__asm__(".globl g_hud_atlas_words\ng_hud_atlas_words:\n"
	".align 2\n"
	".incbin \"out/assets/hud/test_icons/test_icons.atlas.bin\"\n");
extern "C" const eng::u16 g_hud_atlas_words[];

namespace {

using eng::s16;
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
constexpr u16 kBandBytesPerRow = kWidth / 8u;
constexpr u32 kBandBytes = static_cast<u32>(kBandBytesPerRow) * kBandPlanes * kBandHeight;

constexpr u8 kHudBg = 0u;
constexpr u8 kHudFg = 1u;
constexpr u8 kHudAccent = 2u;
constexpr u8 kHudWarn = 3u;

constexpr eng::Palette32 kGamePalette {{
	0x000, 0x112, 0x224, 0x336, 0x448, 0x55a, 0x66c, 0x77e,
	0x135, 0x246, 0x357, 0x468, 0x579, 0x68a, 0x79b, 0x8ac,
}};
constexpr eng::Palette32 kHudPalette {{
	0x000, 0xfff, 0xff0, 0xf00,
}};

constexpr eng::debug::Version kVersion {1u, 0u};

// Geometria del atlas (manifiesto `test_icons.atlas.json`).
constexpr u16 kAtlasFrameW = 16u;
constexpr u16 kAtlasFrameH = 16u;
constexpr u8 kAtlasPlanes = 2u;
constexpr u16 kAtlasCount = 4u;
constexpr u16 kAtlasStrideWords = 16u;
constexpr u16 kAtlasFrameWords = static_cast<u16>(kAtlasPlanes * kAtlasStrideWords);

// --- Rejilla del inventario (como la 306) -----------------------------------------
constexpr u8 kGridCols = 6u;
constexpr u8 kGridRows = 2u;
constexpr u8 kCell = 16u;
constexpr s16 kGridX = 8;
constexpr s16 kGridY = 6;
constexpr u16 kGridW = static_cast<u16>(kGridCols) * kCell;
constexpr u16 kGridH = static_cast<u16>(kGridRows) * kCell;
constexpr u16 kSlotCount = static_cast<u16>(kGridCols) * kGridRows;
constexpr s16 kTileMargin = 2;

constexpr s16 kStateX = 112;
constexpr s16 kStateY = 20;

// Umbral del arrastre (px): por debajo, soltar = click (la sesion vuelve a Idle).
constexpr s16 kDragStartPx = 6;

/// Juego minimo: campo estatico + inventario RPG con arrastre.
struct HudDragDemo {
	void init(auto& app) {
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

		// Contenido RPG (kernel): catalogo minimo + bolsa con objetos.
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
		    m_type_gem == eng::rpg::no_item_type || m_type_potion == eng::rpg::no_item_type) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		{
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
			m_equipment.slots[0u] = m_bag.at(0u);
		}
		constexpr u16 kIcons[5] = {0u, 1u, 2u, 3u, 4u};

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

		// Composicion (patron 302/306).
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

		eng::Screen screen = app.screen();
		screen.clear(0u);
		(void)screen.fill(eng::Box {0, 180, kWidth, 28u}, 1u);
		(void)screen.fill(eng::Box {40, 120, 40u, 24u}, 3u);
		(void)screen.fill(eng::Box {200, 96, 64u, 16u}, 5u);
		draw_static_hud();
		draw_version_prefix();
		draw_version_number(0u);
		draw_fps(0u);
		m_fps_win.reset(app.frame(), eng::debug::prof_clock());

		m_policy = eng::hud::HudInput::Policy::from_region(m_hud.region(m_region));

		// Estado inicial pintado + iconos.
		m_prev_sel = m_hud.grid(m_grid).selection;
		for (u16 i = 0u; i < kSlotCount; ++i) {
			paint_cell(i, false, false);
		}
		draw_state_numbers();
		(void)m_strip.apply(m_hud.ops());
		paint_icon_pass();
		m_hud.clear_ops();
		m_hud.clear_dirty();

		m_ready = true;
		eng::debug::DebugPeripheral::counter_name(0u, reinterpret_cast<eng::u32>("fps10"));
		eng::debug::DebugPeripheral::counter_name(1u, reinterpret_cast<eng::u32>("drag_ok"));
		eng::debug::DebugPeripheral::counter_name(2u, reinterpret_cast<eng::u32>("drag_bad"));
		eng::amiga::mark_ready();
	}

	void update(auto& app) {
		if (!m_ready) {
			return;
		}
		eng::amiga::poll_mouse(m_mouse, m_mouse_poll);
		eng::amiga::poll_input(m_input_agg);
		const eng::input::PadState& pad = m_input_agg.pad0;

		// Raton (deltas por contador): mover / down / up con la maquina de arrastre.
		const bool mouse_moved = m_mouse.dx != 0 || m_mouse.dy != 0;
		const bool left_down = m_mouse.left_button;
		const bool left_edge = left_down != m_prev_left;
		if (mouse_moved) {
			m_mx = clamp_s16(static_cast<s16>(m_mx + m_mouse.dx), 0,
					 static_cast<s16>(kWidth - 1u));
			m_my = clamp_s16(static_cast<s16>(m_my + m_mouse.dy),
					 static_cast<s16>(kBandTop), static_cast<s16>(kHeight - 1u));
			eng::ui::UiEvent ev {};
			ev.kind = eng::ui::UiEventKind::MouseMove;
			ev.x = m_mx;
			ev.y = static_cast<s16>(m_my - static_cast<s16>(kBandTop));
			consume(app, ev);
			m_idle_frames = 0u;
			m_attract_frame = 0u;
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
		// Joystick: flanco de fuego = click del foco (la rejilla de navegacion la mueve
		// `HudInput`; el arrastre es de raton en esta demo).
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

		// Guion determinista: cada 100 frames un arrastre (alternando drop y cancel).
		if (m_idle_frames < kAttractAfterFrames) {
			++m_idle_frames;
		} else {
			attract_step(app);
			++m_attract_frame;
		}
		(void)app;
	}

	void render(auto& app) {
		if (!m_ready) {
			return;
		}
		(void)app.draw_world();
		app.present();

		// Deltas: la celda de origen/destino se repinta al cambiar la sesion; el fantasma
		// se pinta en la pasada de iconos (siguiendo al puntero).
		const eng::hud::DragSession& d = m_hud.drag();
		const bool phase_changed = d.phase != m_prev_phase;
		const bool slot_changed = d.source_slot != m_prev_src || m_prev_dst != m_dst;
		if (phase_changed || slot_changed) {
			if (m_prev_src < kSlotCount) {
				paint_cell(m_prev_src, false, false);
			}
			if (d.source_slot < kSlotCount) {
				paint_cell(d.source_slot, true, false);
			}
			if (m_prev_dst < kSlotCount) {
				paint_cell(m_prev_dst, false, false);
			}
			if (m_dst < kSlotCount) {
				paint_cell(m_dst, false, true);
			}
			draw_state_numbers();
			m_prev_phase = d.phase;
			m_prev_src = d.source_slot;
			m_prev_dst = m_dst;
		}
		if (m_hud.any_dirty()) {
			(void)m_strip.apply(m_hud.ops());
			paint_icon_pass();
			m_hud.clear_ops();
			m_hud.clear_dirty();
		}

		if (app.frame() / 100u != m_hud_second) {
			m_hud_second = app.frame() / 100u;
			draw_version_number(app.frame());
		}
		eng::u32 fps10 = 0u;
		if (m_fps_win.update(app.frame(), eng::debug::prof_clock(), fps10)) {
			draw_fps(fps10);
			eng::debug::DebugPeripheral::counter_value(0u, fps10);
		}
	}

private:
	/// Consume un evento con la maquina de arrastre + `HudInput` (click/seleccion).
	void consume(auto& app, const eng::ui::UiEvent& ev) {
		const eng::hud::DragSession& d = m_hud.drag();
		if (ev.kind == eng::ui::UiEventKind::MouseDown) {
			// Solo desde Idle: si la celda tiene objeto, inicia sesion; si no, click normal.
			if (d.phase == eng::hud::DragPhase::Idle) {
				const eng::hud::HudHit h = eng::hud::hud_hit_test_region(
					m_hud, m_region, ev.x, ev.y);
				if (h.hit && h.grid.valid() && h.slot < kSlotCount) {
					const eng::hud::HudSlotView& sv =
						m_hud.grid(m_grid).slots[h.slot];
					if ((sv.flags & eng::hud::kHudSlotEmpty) == 0u &&
					    sv.item_id != 0u) {
						if (m_hud.drag_begin(m_grid, h.slot, sv.item_id, sv.qty,
								     ev.x, ev.y)) {
							return; // sesion iniciada (Pressed)
						}
					}
				}
			}
		}
		if (ev.kind == eng::ui::UiEventKind::MouseMove) {
			if (d.phase == eng::hud::DragPhase::Pressed ||
			    d.phase == eng::hud::DragPhase::Dragging) {
				(void)m_hud.drag_move(ev.x, ev.y, kDragStartPx);
				// Destino bajo el puntero (para el realce y el drop).
				const eng::hud::HudHit h = eng::hud::hud_hit_test_region(
					m_hud, m_region, ev.x, ev.y);
				m_dst = (h.hit && h.grid.valid() && h.slot < kSlotCount)
						? h.slot
						: kNoSlot;
				return;
			}
		}
		if (ev.kind == eng::ui::UiEventKind::MouseUp) {
			if (d.phase == eng::hud::DragPhase::Pressed) {
				(void)m_hud.drag_release(); // click (sin umbral)
			} else if (d.phase == eng::hud::DragPhase::Dragging) {
				const eng::hud::HudHit h = eng::hud::hud_hit_test_region(
					m_hud, m_region, ev.x, ev.y);
				const bool valid = h.hit && h.grid.valid() &&
						   h.slot < kSlotCount && h.slot != d.source_slot;
				if (valid) {
					(void)m_hud.drag_drop();
					if (move_item(d.source_slot, h.slot)) {
						++m_drag_ok;
					} else {
						++m_drag_bad;
					}
				} else {
					(void)m_hud.drag_cancel();
					++m_drag_ok; // cancelar fuera/sobre el origen es correcto
				}
				eng::debug::DebugPeripheral::counter_value(1u, m_drag_ok);
				eng::debug::DebugPeripheral::counter_value(2u, m_drag_bad);
			}
			(void)m_hud.drag_reset();
			m_dst = kNoSlot;
			return;
		}
		(void)m_input.handle(m_hud, m_policy, ev);
		(void)app;
	}

	/// Mueve el objeto del slot `from` al slot `to` en el inventario RPG: quita la entrada
	/// de la bolsa y la reinserta en la posicion destino (intercambio simple sin pilas).
	[[nodiscard]] bool move_item(u16 from, u16 to) {
		if (from >= m_bag.size() || from == to) {
			return false;
		}
		const eng::rpg::ItemHandle h = m_bag.at(from);
		if (!m_bag.remove(h)) {
			return false;
		}
		// Reinsertar en la posicion `to` (o al final si queda detras): la bolsa conserva
		// el orden de insercion, asi que se reconstruye con una copia temporal acotada.
		eng::rpg::ItemHandle tmp[kBagCap] {};
		eng::u8 n = 0u;
		for (eng::usize i = 0u; i < m_bag.size() && n < kBagCap; ++i) {
			tmp[n++] = m_bag.at(i);
		}
		m_bag = eng::rpg::ItemBag<kBagCap> {};
		eng::u8 inserted = 0u;
		for (eng::u8 i = 0u; i <= n; ++i) {
			if (i == to && inserted == 0u) {
				(void)m_bag.put(h);
				inserted = 1u;
				continue;
			}
			if (i < n) {
				(void)m_bag.put(tmp[i]);
			}
		}
		if (inserted == 0u) {
			(void)m_bag.put(h);
		}
		return refresh_views();
	}

	/// Reconstruye las vistas del puente y las enlaza a la rejilla.
	[[nodiscard]] bool refresh_views() {
		constexpr u16 kIcons[5] = {0u, 1u, 2u, 3u, 4u};
		eng::hud::HudSlotView views[kSlotCount] {};
		(void)eng::rpg::hud_slots_from_bag(
			m_bag, m_store, eng::Span<const u16> {kIcons, 5u},
			eng::Span<eng::hud::HudSlotView> {views, kSlotCount}, &m_equipment);
		for (u16 i = 0u; i < kSlotCount; ++i) {
			m_view_buf[i] = views[i];
		}
		return m_hud.bind_grid(m_grid, eng::Span<const eng::hud::HudSlotView> {
							 m_view_buf, kSlotCount});
	}

	/// Guion: arrastra el primer objeto con contenido a una celda destino (alternando un
	/// drop valido en una celda y una cancelacion fuera de la rejilla).
	void attract_step(auto& app) {
		const u16 f = static_cast<u16>(m_attract_frame % 200u);
		if (f == 0u) {
			// Elegir el primer slot con objeto.
			m_a_src = kNoSlot;
			for (u16 i = 0u; i < kSlotCount; ++i) {
				const eng::hud::HudSlotView& sv = m_hud.grid(m_grid).slots[i];
				if ((sv.flags & eng::hud::kHudSlotEmpty) == 0u && sv.item_id != 0u) {
					m_a_src = i;
					break;
				}
			}
			if (m_a_src != kNoSlot) {
				const eng::hud::HudSlotView& sv = m_hud.grid(m_grid).slots[m_a_src];
				(void)m_hud.drag_begin(m_grid, m_a_src, sv.item_id, sv.qty,
						       cell_cx(m_a_src), cell_cy(m_a_src));
			}
		} else if (f < 40u) {
			// Movimiento diagonal por pasos (supera el umbral en el 2.o paso).
			const s16 t = static_cast<s16>(f);
			(void)m_hud.drag_move(
				static_cast<s16>(cell_cx(m_a_src) + t * 3),
				static_cast<s16>(cell_cy(m_a_src) + t), kDragStartPx);
		} else if (f == 40u) {
			const bool cancel = (m_attract_frame / 200u) & 1u;
			if (cancel) {
				(void)m_hud.drag_cancel();
				++m_drag_ok;
			} else {
				// Drop en una celda valida (la opuesta a la de origen).
				const u16 dst = static_cast<u16>((m_a_src + 7u) % kSlotCount);
				(void)m_hud.drag_move(cell_cx(dst), cell_cy(dst), kDragStartPx);
				if (m_hud.drag_drop() && move_item(m_a_src, dst)) {
					++m_drag_ok;
				} else {
					++m_drag_bad;
				}
			}
			eng::debug::DebugPeripheral::counter_value(1u, m_drag_ok);
			eng::debug::DebugPeripheral::counter_value(2u, m_drag_bad);
		} else if (f == 60u) {
			(void)m_hud.drag_reset();
		}
		(void)app;
	}

	[[nodiscard]] static s16 cell_cx(u16 slot) {
		return static_cast<s16>(kGridX + static_cast<s16>(slot % kGridCols) * kCell +
					kCell / 2);
	}
	[[nodiscard]] static s16 cell_cy(u16 slot) {
		return static_cast<s16>(kGridY + static_cast<s16>(slot / kGridCols) * kCell +
					kCell / 2);
	}

	/// Pinta una celda (fondo + marcos); el icono va en la pasada posterior.
	void paint_cell(u16 slot, bool source, bool target) {
		if (slot >= kSlotCount) {
			return;
		}
		const s16 cx = static_cast<s16>(kGridX + static_cast<s16>(slot % kGridCols) * kCell);
		const s16 cy = static_cast<s16>(kGridY + static_cast<s16>(slot / kGridCols) * kCell);
		push_fill(cx, cy, kCell, kCell, kHudBg);
		push_icon(slot);
		if (source) {
			push_frame(cx, cy, kCell, kCell, kHudWarn); // origen de la sesion (rojo)
		}
		if (target) {
			push_frame(cx, cy, kCell, kCell, kHudAccent); // destino valido bajo el puntero
		}
	}

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

	/// Pasada de iconos: trama de cada celda marcada + el **fantasma** del arrastre (la
	/// trama del objeto en la posicion del puntero) + la barra de cantidad.
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
			paint_icon(surf, sv.icon_frame, cx, cy, false);
			if (sv.qty > 1u) {
				paint_qty(surf, sv.qty, cx, cy);
			}
		}
		// Fantasma: la trama del objeto arrastrado sigue al puntero (mientras `Dragging`).
		const eng::hud::DragSession& d = m_hud.drag();
		if (d.phase == eng::hud::DragPhase::Dragging) {
			paint_icon(surf, static_cast<u16>(d.item_id - 1u),
				   static_cast<s16>(d.x - kCell / 2), static_cast<s16>(d.y - kCell / 2),
				   true);
		}
		m_icon_n = 0u;
	}

	void paint_icon(eng::playfield::Surface& surf, u16 frame, s16 cx, s16 cy, bool ghost) {
		frame = static_cast<u16>(frame % kAtlasCount);
		for (u16 row = 0u; row < kAtlasFrameH; ++row) {
			const eng::u16* const w =
				&g_hud_atlas_words[frame * kAtlasFrameWords + row * kAtlasPlanes];
			const u16 p0 = w[0];
			const u16 p1 = w[1];
			for (u16 px = 0u; px < kAtlasFrameW; ++px) {
				const u16 bit = static_cast<u16>(0x8000u >> px);
				u8 c = static_cast<u8>(((p0 & bit) != 0u ? 1u : 0u) |
						       ((p1 & bit) != 0u ? 2u : 0u));
				if (c == 0u) {
					continue; // color 0 = transparente
				}
				if (ghost) {
					c = kHudAccent; // el fantasma se resalta
				}
				(void)surf.set_pixel(static_cast<s16>(cx + static_cast<s16>(px)),
						     static_cast<s16>(cy + static_cast<s16>(row)), c);
			}
		}
	}

	void paint_qty(eng::playfield::Surface& surf, u16 qty, s16 cx, s16 cy) {
		const u16 wq = static_cast<u16>(qty * 2u > 12u ? 12u : qty * 2u);
		for (u16 k = 0u; k < wq; ++k) {
			(void)surf.set_pixel(static_cast<s16>(cx + kTileMargin + static_cast<s16>(k)),
					     static_cast<s16>(cy + kCell - kTileMargin - 1),
					     kHudAccent);
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

	void draw_static_hud() {
		eng::hud::HudDrawOp frame {};
		frame.kind = eng::hud::HudOpKind::Frame;
		frame.box = eng::Box {0, 0, kWidth, kBandHeight};
		frame.color = kHudFg;
		(void)m_hud.push_op(frame);
		(void)m_strip.apply(m_hud.ops());
		m_hud.clear_ops();
		m_hud.clear_dirty();
	}

	/// Linea de estado `D:nn SRC:nn DST:nn` (fase y slots): validable sin decodificar pixeles.
	void draw_state_numbers() {
		char line[24] {};
		u8 n = 0u;
		const char* pre = "D:";
		for (const char* p = pre; *p != '\0'; ++p) {
			line[n++] = *p;
		}
		line[n++] = static_cast<char>('0' + static_cast<u8>(m_hud.drag().phase) % 10u);
		const char* mid = " SRC:";
		for (const char* p = mid; *p != '\0'; ++p) {
			line[n++] = *p;
		}
		n = append2(line, n, m_hud.drag().source_slot);
		const char* mid2 = " DST:";
		for (const char* p = mid2; *p != '\0'; ++p) {
			line[n++] = *p;
		}
		n = append2(line, n, m_dst);
		line[n++] = ' ';
		auto surf = m_strip.surface();
		(void)surf.draw_text_words(kStateX, kStateY, line, kHudAccent);
	}

	static u8 append2(char* line, u8 n, u16 v) {
		if (v == kNoSlot || v > 99u) {
			line[n++] = '-';
			line[n++] = '-';
		} else {
			line[n++] = static_cast<char>('0' + (v / 10u) % 10u);
			line[n++] = static_cast<char>('0' + v % 10u);
		}
		return n;
	}

	void draw_version_prefix() {
		char line[32] {};
		u8 n = 0u;
		for (const char* p = "HUD DRAG v"; *p != '\0' && n < sizeof(line) - 1u; ++p) {
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

	void draw_fps(u32 fps10) {
		char digits[4] {};
		digits[0] = static_cast<char>('0' + (fps10 / 100u));
		digits[1] = static_cast<char>('0' + ((fps10 / 10u) % 10u));
		digits[2] = '.';
		digits[3] = static_cast<char>('0' + (fps10 % 10u));
		auto surf = m_strip.surface();
		(void)surf.draw_text_words(272, 40, digits, kHudFg);
	}

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

	static constexpr u16 kNoSlot = eng::hud::kHudNoSlot;
	static constexpr u32 kAttractAfterFrames = 100u; // ~2 s a 50 fps
	static constexpr eng::u8 kBagCap = 6u;           // capacidad de la bolsa (RPG)

	// --- Estado del demo ------------------------------------------------------------
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_band_planes {};
	eng::hud::HudStrip m_strip {};
	eng::hud::Hud<2u, 24u, 64u, 48u, 48u> m_hud {};
	eng::hud::RegionHandle m_region {};
	eng::hud::GridHandle m_grid {};
	eng::hud::HudInput m_input {};
	eng::hud::HudInput::Policy m_policy {};
	eng::input::MouseState m_mouse {};
	eng::amiga::MousePollState m_mouse_poll {};
	eng::input::InputAggregator m_input_agg {};
	eng::rpg::ItemCatalog<4u> m_catalog {};
	eng::rpg::ItemStore<8u> m_store {};
	eng::rpg::ItemBag<kBagCap> m_bag {};
	eng::rpg::Equipment<1u> m_equipment {};
	eng::rpg::ItemTypeId m_type_key {};
	eng::rpg::ItemTypeId m_type_coin {};
	eng::rpg::ItemTypeId m_type_gem {};
	eng::rpg::ItemTypeId m_type_potion {};
	eng::hud::HudSlotView m_view_buf[kSlotCount] {};
	s16 m_mx = 160;
	s16 m_my = static_cast<s16>(kBandTop + 20);
	bool m_prev_left = false;
	u16 m_prev_joy = 0u;
	u32 m_idle_frames = 0u;
	u32 m_attract_frame = 0u;
	eng::hud::DragPhase m_prev_phase = eng::hud::DragPhase::Idle;
	u16 m_prev_src = kNoSlot;
	u16 m_prev_dst = kNoSlot;
	u16 m_dst = kNoSlot;
	u16 m_a_src = kNoSlot;
	u16 m_prev_sel = 0u;
	u32 m_drag_ok = 0u;
	u32 m_drag_bad = 0u;
	u16 m_icon_pass[kSlotCount] {};
	u8 m_icon_n = 0u;
	u32 m_hud_second = ~0u;
	eng::debug::HudFpsWindow m_fps_win {};
	bool m_ready = false;
};

} // namespace

// Estado del juego en BSS (convencion del repo).
HudDragDemo g_game {};

int main() {
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = 4u;
	display.layout = eng::graphics::PlaneLayout::Interleaved;
	display.palette = kGamePalette;
	return eng::amiga::run_demo(g_game, display);
}
