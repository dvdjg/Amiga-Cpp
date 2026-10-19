// Demo 305 - hud_overlay
// ----------------------------------------------------------------------------
// Tutorial (H4 de ROADMAP_HUD.md): **overlay sobre un juego en movimiento** con
// politica de restauracion `GameRedraws` y **escalera de degradacion** por presupuesto.
//
// El campo NO es estatico: un fondo planar de 640x256 **scrollea** 2 px/frame (ventana
// de 320 por blit con shift fino) y un actor recorre el campo en lissajous. Sobre el,
// un **overlay** (`HudPlacement::Overlay`) pinta un panel de estado (medidor + contador)
// en la esquina superior derecha. La restauracion es **`GameRedraws`** (HUD_SYSTEM §6.4):
// cada frame el juego re-blittea la ventana del fondo bajo el panel y el HUD pinta
// encima — sin *backing* extra.
//
// El panel se pinta con las **operaciones del modelo** (`HudDrawOp`) aplicadas por
// `apply_hud_ops` sobre una `Surface` de la escena: el mismo contrato que la franja,
// sin franja. El medidor respeta su **estilo** (*skins* H6: linear/segmentado/pips) y
// el demo lo cambia en caliente cada ~4 s.
//
// **Degradacion (HUD_SYSTEM §6.6)**: el demo mide su coste HUD por frame (reloj de
// ciclos) y, si supera `kBudgetCycles`, degrada (contador a 1/2 de tasa y medidor sin
// pulso); el estado va por contadores y en la linea de depuracion.
//
//   bash ./tools/build/build-demo.sh demos/features/ui/amiga/305_hud_overlay --debug
//   bash ./tools/run/run-demo.sh demos/features/ui/amiga/305_hud_overlay --keep-running

#include <eng/api/api.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/scene/display.hpp>

namespace {

using eng::s16;
using eng::u16;
using eng::u32;
using eng::u8;

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u8 kPlanes = 4u;
constexpr u32 kPlaneBytes = static_cast<u32>(kWidth / 8u) * kHeight;

// Fondo del juego: 640 px de ancho (ventana extra para el scroll).
constexpr u16 kBackW = 640u;
constexpr u16 kBackBytesPerRow = kBackW / 8u;
constexpr u32 kBackPlaneBytes = static_cast<u32>(kBackBytesPerRow) * kHeight;
constexpr u32 kBackBytes = kBackPlaneBytes * kPlanes;

// Actor: disco 16x16 de 2 planos (planar con palabra de guarda, contrato de bob.hpp).
constexpr u16 kActorW = 16u;
constexpr u16 kActorH = 16u;
constexpr u32 kActorRow = 4u;  // 1 palabra + guarda, x2 palabras por fila
constexpr u32 kActorPlane = kActorH * kActorRow;
constexpr u8 kActorPlanes = 4u; // el blit exige planos == destino (4)
constexpr u32 kActorBytes = kActorPlane * kActorPlanes;

constexpr eng::Palette32 kPalette {{
	0x013, 0xf00, 0x0f0, 0xff0, 0x333, 0x357, 0x579, 0x79b,
	0x246, 0x468, 0x68a, 0x8ac, 0xace, 0xcef, 0xeee, 0xfff,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

constexpr eng::debug::Version kVersion {1u, 0u};

// Indices logicos del HUD (la paleta del juego ya los define: 12..15 claros).
constexpr u8 kHudBg = 4u;   // gris del panel
constexpr u8 kHudFg = 15u;  // texto
constexpr u8 kHudAccent = 3u;
constexpr u8 kHudWarn = 1u;

// --- Overlay A (panel de estado, restauracion GameRedraws) ------------------------
constexpr s16 kPanelX = 208;
constexpr s16 kPanelY = 16;
constexpr u16 kPanelW = 96u;
constexpr u16 kPanelH = 36u;
// Parte que scrollea del campo: el resto (franja inferior) es estatica.
constexpr u16 kScrollH = 216u;

// --- Overlay B (panel de vidas, restauracion **Backing**) -------------------------
// El juego NO redibuja esta zona: el HUD guarda su fondo en un buffer Chip y lo restaura
// con un blit antes de repintar el panel (`HudOverlayRestore::Backing`).
constexpr s16 kPanelBX = 16;
constexpr s16 kPanelBY = 216;
constexpr u16 kPanelBW = 96u;
constexpr u16 kPanelBH = 24u;
constexpr u16 kBackingBytesPerRow = kPanelBW / 8u; // 12
constexpr u32 kBackingPlaneBytes = static_cast<u32>(kBackingBytesPerRow) * kPanelBH;
constexpr u32 kBackingBytes = kBackingPlaneBytes * 4u;
// Color del fondo estatico bajo el panel B (indice 4 de la paleta: gris).
constexpr u8 kStaticBg = 4u;

// Presupuesto del HUD por frame (ciclos): por encima, el demo degrada.
constexpr u32 kBudgetCycles = 8000u;

/// Juego minimo: fondo scrolleando + actor en lissajous + overlay con skins y degradacion.
struct HudOverlayDemo {
	void init(auto& app) {
		auto& mm = app.memory_manager();
		m_backdrop = mm.chip().template reserve<eng::PlaneTag>(kBackBytes, 16u);
		m_actor = mm.chip().template reserve<eng::PlaneTag>(kActorBytes, 16u);
		if (!m_backdrop.valid() || !m_actor.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		build_backdrop();
		build_actor();

		// Region Overlay con restauracion **GameRedraws** (el juego re-blittea debajo).
		eng::hud::HudRegionSpec spec {};
		spec.placement = eng::hud::HudPlacement::Overlay;
		spec.restore = eng::hud::HudOverlayRestore::GameRedraws;
		spec.capture_input = false;
		spec.budget.max_draw_ops = 32u;
		m_region = m_hud.add_region(spec);
		if (!m_region.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		m_meter = m_hud.add_meter();
		m_counter = m_hud.add_counter();
		if (!m_meter.valid() || !m_counter.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		(void)m_hud.set_meter(m_meter, 70u, 100u);

		// **Overlay B con restauracion `Backing`** (HUD_SYSTEM §6.4): el juego declara el
		// presupuesto de *backing* y el HUD guarda el fondo en un buffer Chip propio.
		eng::hud::HudRegionSpec spec_b {};
		spec_b.placement = eng::hud::HudPlacement::Overlay;
		spec_b.restore = eng::hud::HudOverlayRestore::Backing;
		spec_b.budget.max_backing_bytes = kBackingBytes;
		m_region_b = m_hud.add_region(spec_b);
		if (!m_region_b.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		// Buffer del *backing*: se **construye** con el mismo fondo estatico que el juego
		// pinta bajo el panel (una vez); el restaurar es un blit de aqui a la pantalla.
		m_backing = mm.chip().template reserve<eng::PlaneTag>(kBackingBytes, 16u);
		if (!m_backing.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		{
			u8* const base = m_backing.view.data();
			for (u8 p = 0u; p < 4u; ++p) {
				const u8 fill = ((kStaticBg >> p) & 1u) != 0u ? 0xffu : 0x00u;
				for (u32 i = 0u; i < kBackingPlaneBytes; ++i) {
					base[static_cast<u32>(p) * kBackingPlaneBytes + i] = fill;
				}
			}
		}
		m_backing_view.planes = m_backing.mem_view();
		m_backing_view.width = kPanelBW;
		m_backing_view.height = kPanelBH;
		m_backing_view.row_bytes = kBackingBytesPerRow;
		m_backing_view.plane_count = 4u;
		m_backing_view.layout = eng::graphics::PlaneLayout::Contiguous;
		m_backing_view.plane_step = kBackingPlaneBytes;

		eng::scene::Band game_band = eng::scene::band_from_view(
			app.device().scene().playfield().hardware_view(), 0u);
		eng::scene::RasterLayout layout {};
		(void)layout.add(game_band);
		layout[0u].palette = kPalette.words();
		layout[0u].palette_colors = 16u;
		if (!app.present_layout(layout)) {
			eng::amiga::mark_failed(eng::debug::BootFail::Display);
			return;
		}

		draw_version_prefix(app);
		draw_version_number(app, 0u);
		draw_fps(app, 0u);
		// Franja inferior **estatica** (no la cubre el scroll): el juego la pinta una vez y
		// no la redibuja; el panel B vive encima con su *backing*.
		{
			eng::Screen screen = app.screen();
			(void)screen.fill(eng::Box {0, static_cast<s16>(kScrollH), kWidth,
						    static_cast<u16>(kHeight - kScrollH)},
					  kStaticBg);
		}
		m_fps_win.reset(app.frame(), eng::debug::prof_clock());
		paint_overlay(app);
		m_bk_dirty = true; // el primer frame restaura el fondo del panel B y lo pinta

		m_ready = true;
		eng::debug::DebugPeripheral::counter_name(0u, reinterpret_cast<eng::u32>("vbl"));
		eng::debug::DebugPeripheral::counter_name(1u, reinterpret_cast<eng::u32>("frame"));
		eng::debug::DebugPeripheral::counter_name(2u, reinterpret_cast<eng::u32>("ov_bad"));
		eng::debug::DebugPeripheral::counter_name(3u, reinterpret_cast<eng::u32>("degraded"));
		eng::debug::DebugPeripheral::counter_name(4u, reinterpret_cast<eng::u32>("blit_bad"));
		eng::debug::DebugPeripheral::counter_name(5u, reinterpret_cast<eng::u32>("deg_changes"));
		eng::debug::DebugPeripheral::counter_name(6u, reinterpret_cast<eng::u32>("ov_ok"));
		eng::debug::DebugPeripheral::counter_name(7u, reinterpret_cast<eng::u32>("bk_bad"));
		m_ladder.configure(kBudgetCycles); // escalera del engine (H4) con este presupuesto
		eng::amiga::mark_ready();
	}

	void update(auto& app) {
		if (!m_ready) {
			return;
		}
		// El fondo scrollea 2 px/frame (bucle sobre la ventana extra).
		m_scroll = static_cast<u16>((m_scroll + 2u) % (kBackW - kWidth));
		// Actor en lissajous (x e y) sobre el fondo. El blit de bitmap exige x alineada a
		// 16 px (sin shift fino en el destino): el actor avanza en pasos de 16 y el
		// desplazamiento fino lo aporta el scroll del fondo.
		const u8 t = static_cast<u8>(app.frame() & 63u);
		m_ax = static_cast<s16>((24u + static_cast<u16>(tri(t)) * 40u / 64u) & ~15u);
		m_ay = static_cast<s16>(96u + static_cast<u16>(tri(static_cast<u8>((t + 21u) & 63u))) * 32u / 64u);

		// **Skins (H6)**: medidor y contador cambian de estilo en caliente cada ~200 frames
		// (medidor linear/segmented/pips/raster; contador numerico/bar/pips). El icon set
		// (mapeo tipo->frame del atlas) es del juego y cambiaria con el skin. El modelo no
		// cambia de forma ni de valor.
		const u8 skin = static_cast<u8>((app.frame() / 200u) % 4u);
		if (skin != m_skin) {
			m_skin = skin;
			(void)m_hud.set_meter_style(m_meter, skin == 0u ? eng::hud::MeterStyle::Linear
						      : skin == 1u ? eng::hud::MeterStyle::Segmented
						      : skin == 2u ? eng::hud::MeterStyle::Pips
								   : eng::hud::MeterStyle::Raster);
			(void)m_hud.set_counter_style(
				m_counter,
				skin == 1u ? eng::hud::CounterStyle::Bar
				: skin == 2u ? eng::hud::CounterStyle::Pips
					     : eng::hud::CounterStyle::Numeric,
				skin == 1u ? 1000u : 49u);
			++m_skin_changes;
		}

		// **Degradacion por presupuesto (HUD_SYSTEM §6.6/§6.7)**: la **escalera del engine**
		// (`eng/hud/budget.hpp`, H4) decide el nivel con histeresis (sube al superar el
		// presupuesto, baja por debajo de 7/8) para no oscilar con el ruido de la medida.
		// Aqui se aplican 2 estados (nivel > 0 = degradado: contador a 1/2 y sin pulso);
		// la politica visual es del juego; el modelo no cambia.
		if (m_ladder.update(m_last_hud_cycles)) {
			m_degraded = m_ladder.degraded();
			m_deg_changes = m_ladder.changes();
		}
		eng::debug::DebugPeripheral::counter_value(3u, m_degraded ? 1u : 0u);
		eng::debug::DebugPeripheral::counter_value(5u, m_deg_changes);
		// Cadencia honesta (DT-036): latidos del mini-SO y frames del motor; el ratio
		// `frame`/`vbl` no depende del reloj de pared del periférico.
		eng::debug::DebugPeripheral::counter_value(0u, eng::os::frame_count());
		eng::debug::DebugPeripheral::counter_value(1u, app.frame());
	}

	void render(auto& app) {
		if (!m_ready) {
			return;
		}

		// (1) Juego primero: la ventana del fondo con shift fino (solo la parte que
		// scrollea; la franja inferior es estatica y la cubre el panel B con `Backing`) y
		// el actor encima. Para el panel A, **este blit ES la restauracion `GameRedraws`**:
		// su area se repinta con el fondo antes de que el HUD pinte encima.
		eng::Screen screen = app.screen();
		const u16 coarse = static_cast<u16>(m_scroll & ~15u);
		const u8 fine = static_cast<u8>(m_scroll & 15u);
		eng::graphics::ChipBitmapView<eng::PlaneTag> window = m_backdrop_view;
		window.width = kWidth;
		window.height = kScrollH;
		if (!screen.bitmap(window, eng::Box {0, 0, kWidth, kScrollH}, coarse, fine)) {
			++m_blit_bad; // diagnostico: no fatal (el bucle sigue)
			eng::debug::DebugPeripheral::counter_value(4u, m_blit_bad);
		}
		if (!screen.bitmap(m_actor_view, eng::Box {m_ax, m_ay, kActorW, kActorH}, 0u, 0u)) {
			++m_blit_bad;
			eng::debug::DebugPeripheral::counter_value(4u, m_blit_bad);
		}
		app.draw_world();
		app.present();

		// (2) HUD: overlays por deltas. El presupuesto se mide SOLO sobre la seccion HUD
		// (los blits del juego no cuentan). Contador (degradado = 1/2 de tasa) y medidor
		// con pulso cuando no esta degradado.
		const u32 hud_t0 = eng::debug::prof_clock();
		const bool update_counter = !m_degraded || ((app.frame() & 1u) == 0u);
		if (update_counter) {
			(void)m_hud.set_counter(m_counter, static_cast<eng::s32>(app.frame()));
		}
		const eng::s32 pulse = m_degraded ? 0 : static_cast<eng::s32>((app.frame() / 16u) & 1u) * 10;
		(void)m_hud.set_meter(m_meter, static_cast<eng::s32>(70 + pulse), 100u);
		if (m_hud.any_dirty() || m_bk_dirty) {
			paint_overlay(app); // el panel B se repinta con la seccion HUD (y restaura su fondo)
			m_bk_dirty = false;
		}
		m_last_hud_cycles = eng::debug::prof_clock() - hud_t0;

		// (3) Franja de depuracion obligatoria (AGENTS §0) sobre el campo.
		if (app.frame() / 100u != m_hud_second) {
			m_hud_second = app.frame() / 100u;
			draw_version_number(app, app.frame());
		}
		eng::u32 fps10 = 0u;
		if (m_fps_win.update(app.frame(), eng::debug::prof_clock(), fps10)) {
			draw_fps(app, fps10);
			// (fps10 de pared retirado: vbl/frame son la medida honesta)
		}
	}

private:
	/// Triangular 0..63 desde `t & 63` (barato, sin tabla).
	[[nodiscard]] static u8 tri(u8 t) {
		const u8 m = static_cast<u8>(t & 63u);
		return m < 32u ? static_cast<u8>(m * 2u) : static_cast<u8>(126u - m * 2u);
	}

	/// Fondo del juego: bandas con relieve, rio y un sol, pre-renderizado en chunky y
	/// convertido a planar UNA vez (patron de la 214; el frame solo blittea la ventana).
	void build_backdrop() {
		static u8 chunky[kBackW * kHeight] {};
		for (u16 y = 0u; y < kHeight; ++y) {
			for (u16 x = 0u; x < kBackW; ++x) {
				u8 c = 0u;
				const bool ridge = y > 196u + ((x >> 5u) & 7u);
				const bool river = (y > 150u) && (y < 196u) && (((x / 3u) % 64u) < 20u);
				if (ridge) {
					c = 2u;
				} else if (river) {
					c = 6u;
				} else if (y > 120u) {
					c = 5u;
				} else {
					c = static_cast<u8>(1u + ((y >> 4u) & 3u));
				}
				chunky[static_cast<u32>(y) * kBackW + x] = c;
			}
		}
		eng::graphics::c2p_1x1_naive(kBackW, kHeight, kPlanes, kBackPlaneBytes,
					     eng::ChunkyView {chunky, sizeof(chunky)},
					     eng::PlaneBytes {m_backdrop.view.data(), kBackBytes});
		m_backdrop_view.planes = m_backdrop.mem_view();
		m_backdrop_view.width = kBackW;
		m_backdrop_view.height = kHeight;
		m_backdrop_view.row_bytes = kBackBytesPerRow;
		m_backdrop_view.plane_count = kPlanes;
		m_backdrop_view.layout = eng::graphics::PlaneLayout::Contiguous;
		m_backdrop_view.plane_step = kBackPlaneBytes;
	}

	/// Actor: disco 16x16 de 2 planos (borde 13, nucleo 15) en hoja planar con guarda.
	void build_actor() {
		u8* const sheet = m_actor.view.data();
		for (u32 i = 0u; i < kActorBytes; ++i) {
			sheet[i] = 0u;
		}
		const auto put = [&](u32 off, s16 dx, s16 dy) {
			const u16 x = static_cast<u16>(dx + 8);
			const u16 y = static_cast<u16>(dy + 8);
			const u32 row = off + static_cast<u32>(y) * kActorRow;
			sheet[row + (x >> 3u)] = static_cast<u8>(sheet[row + (x >> 3u)] |
								  (0x80u >> (x & 7u)));
		};
		for (s16 dy = -7; dy <= 7; ++dy) {
			for (s16 dx = -7; dx <= 7; ++dx) {
				const s16 d2 = static_cast<s16>(dx * dx + dy * dy);
				if (d2 <= 49) {
					put(0u, dx, dy);
					if (d2 <= 25) {
						put(kActorPlane, dx, dy);
					}
				}
			}
		}
		m_actor_view.planes = m_actor.mem_view();
		m_actor_view.width = kActorW;
		m_actor_view.height = kActorH;
		m_actor_view.row_bytes = kActorRow;
		m_actor_view.plane_count = kActorPlanes;
		m_actor_view.layout = eng::graphics::PlaneLayout::Contiguous;
		m_actor_view.plane_step = kActorPlane;
	}

	/// Pinta el overlay: emite las **operaciones del modelo** (panel, medidor segun su
	/// estilo, contador) y las aplica con `apply_hud_ops` sobre la superficie del panel
	/// (la escena), el mismo contrato que la franja sin franja.
	void paint_overlay(auto& app) {
		m_hud.clear_ops();

		// Panel: fondo + marco. Las ops van en coordenadas **absolutas** de pantalla (el
		// rect de la Surface es el clip, no el origen).
		push_fill(kPanelX, kPanelY, kPanelW, kPanelH, kHudBg);
		push_frame(kPanelX, kPanelY, kPanelW, kPanelH, kHudFg);

		// Medidor segun su **estilo** (skins H6): mismo modelo, otra presentacion.
		const eng::hud::Meter& m = m_hud.meter(m_meter);
		const u16 full = m.max > 0 ? static_cast<u16>((m.value * 80) / m.max) : 0u;
		const u8 bar_color = m.value <= 30 ? kHudWarn : kHudAccent;
		switch (m.style) {
		case eng::hud::MeterStyle::Segmented: {
			// 8 celdas de 8 px con hueco de 2: se encienden `full/10`.
			for (u8 i = 0u; i < 8u; ++i) {
				const u16 on = static_cast<u16>(i * 10u) < full;
				push_fill(static_cast<s16>(kPanelX + 4 + i * 11), static_cast<s16>(kPanelY + 6), 8u, 8u,
					  on ? bar_color : kHudBg);
			}
			break;
		}
		case eng::hud::MeterStyle::Pips: {
			// 5 pips cuadrados: se encienden `full/16`.
			for (u8 i = 0u; i < 5u; ++i) {
				const u16 on = static_cast<u16>(i * 16u) < full;
				push_fill(static_cast<s16>(kPanelX + 4 + i * 18), static_cast<s16>(kPanelY + 6), 10u, 8u,
					  on ? bar_color : kHudBg);
			}
			break;
		}
		case eng::hud::MeterStyle::Raster:
		case eng::hud::MeterStyle::Linear:
		default: {
			push_fill(static_cast<s16>(kPanelX + 4), static_cast<s16>(kPanelY + 6), 80u, 8u, kHudBg);
			if (full > 0u) {
				push_fill(static_cast<s16>(kPanelX + 4), static_cast<s16>(kPanelY + 6), full, 8u, bar_color);
			}
			break;
		}
		}

		// Contador segun su **estilo** (*skins* H6): numerico (digitos por valor, x
		// alineada a 16 px para el camino de words), barra o pips. El valor mostrado es
		// ciclico para que las escalas se vean.
		const eng::hud::Counter& cnt = m_hud.counter(m_counter);
		const eng::u16 cval = cnt.present_max > 0u
					      ? static_cast<eng::u16>(cnt.value % (cnt.present_max + 1u))
					      : 0u;
		if (cnt.style == eng::hud::CounterStyle::Bar) {
			const u16 w = cnt.present_max > 0u
					      ? static_cast<u16>((static_cast<u32>(cval) * 80u) /
								 cnt.present_max)
					      : 0u;
			push_fill(static_cast<s16>(kPanelX + 4), static_cast<s16>(kPanelY + 20), 80u, 8u,
				  kHudBg);
			if (w > 0u) {
				push_fill(static_cast<s16>(kPanelX + 4), static_cast<s16>(kPanelY + 20), w,
					  8u, kHudFg);
			}
		} else if (cnt.style == eng::hud::CounterStyle::Pips) {
			for (u8 i = 0u; i < 8u; ++i) {
				const bool on = static_cast<u16>(i * 7u) <= cval;
				push_fill(static_cast<s16>(kPanelX + 4 + i * 10),
					  static_cast<s16>(kPanelY + 20), 8u, 8u,
					  on ? kHudAccent : kHudBg);
			}
		} else {
			const eng::Span<const char> text = m_hud.counter_text(m_counter);
			if (!text.empty()) {
				eng::hud::HudDrawOp digits {};
				digits.kind = eng::hud::HudOpKind::Digits;
				digits.box = eng::Box {static_cast<s16>(kPanelX + 16),
						       static_cast<s16>(kPanelY + 20), 48u, 8u};
				digits.color = kHudFg;
				(void)digits.set_text(text);
				(void)m_hud.push_op(digits);
			}
		}
		// Estado de la degradacion (texto fijo de longitud constante).
		push_text(static_cast<s16>(kPanelX + 16), static_cast<s16>(kPanelY + 28), m_degraded ? "DEGRADADO " : "NORMAL    ", kHudAccent);

		auto& pf = app.device().scene().playfield();
		eng::playfield::Surface surf {
			pf, eng::playfield::SurfaceRect {kPanelX, kPanelY, kPanelW, kPanelH}};
		const eng::u16 applied = eng::hud::apply_hud_ops(surf, m_hud.ops());
		if (applied == m_hud.ops().size()) {
			++m_ov_ok;
		} else {
			++m_ov_bad;
		}
		eng::debug::DebugPeripheral::counter_value(6u, m_ov_ok);
		eng::debug::DebugPeripheral::counter_value(2u, m_ov_bad);
		m_hud.clear_ops();
		m_hud.clear_dirty();

		// Panel B (**`Backing`**): el juego NO redibuja su zona; antes de cada repintado se
		// restaura el fondo desde el buffer propio (blit de Chip a pantalla, la politica
		// `HudOverlayRestore::Backing`) y luego se pintan las ops encima.
		if (!app.screen().bitmap(m_backing_view,
					 eng::Box {kPanelBX, kPanelBY, kPanelBW, kPanelBH}, 0u, 0u)) {
			++m_bk_bad;
		} else {
			++m_bk_ok;
		}
		// (bk_ok retirado: el slot 6 lleva ov_ok; la restauracion se evidencia con bk_bad == 0)
		eng::debug::DebugPeripheral::counter_value(7u, m_bk_bad);
		push_fill(kPanelBX, kPanelBY, kPanelBW, kPanelBH, kHudBg);
		push_frame(kPanelBX, kPanelBY, kPanelBW, kPanelBH, kHudFg);
		{
			const u16 lives = static_cast<u16>(1u + (app.frame() / 64u) % 5u); // 1..5
			for (u8 i = 0u; i < lives; ++i) {
				push_fill(static_cast<s16>(kPanelBX + 4 + i * 18),
					  static_cast<s16>(kPanelBY + 8), 14u, 8u, kHudAccent);
			}
		}
		eng::playfield::Surface surf_b {
			pf, eng::playfield::SurfaceRect {kPanelBX, kPanelBY, kPanelBW, kPanelBH}};
		const eng::u16 applied_b = eng::hud::apply_hud_ops(surf_b, m_hud.ops());
		if (applied_b != m_hud.ops().size()) {
			++m_ov_bad;
		}
		m_hud.clear_ops();
		m_hud.clear_dirty();
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

	void push_text(s16 x, s16 y, const char* text, u8 color) {
		eng::u16 n = 0u;
		while (text[n] != '\0' && n < 12u) {
			++n;
		}
		eng::hud::HudDrawOp op {};
		op.kind = eng::hud::HudOpKind::Text;
		op.box = eng::Box {x, y, static_cast<u16>(n * 8u), 8u};
		op.color = color;
		(void)op.set_text(eng::Span<const char> {text, n});
		(void)m_hud.push_op(op);
	}

	/// Superficie del campo para la franja de depuracion (texto por palabras).
	eng::playfield::Surface field_surface(auto& app) {
		auto& pf = app.device().scene().playfield();
		return eng::playfield::Surface {
			pf, eng::playfield::SurfaceRect {0, 0, kWidth, kHeight}};
	}

	void draw_version_prefix(auto& app) {
		char line[32] {};
		u8 n = 0u;
		for (const char* p = "HUD OVERLAY v"; *p != '\0' && n < sizeof(line) - 1u; ++p) {
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
		auto surf = field_surface(app);
		(void)surf.draw_text_words(0, 240, line, kHudFg);
		(void)surf.draw_text_words(224, 240, " FPS  ", kHudFg);
	}

	void draw_version_number(auto& app, u32 frame) {
		char digits[8] {};
		for (u8 i = 0u; i < 5u; ++i) {
			const u32 div = (i == 0u) ? 10000u : (i == 1u) ? 1000u
					: (i == 2u) ? 100u : (i == 3u) ? 10u : 1u;
			digits[i] = static_cast<char>('0' + ((frame / div) % 10u));
		}
		auto surf = field_surface(app);
		(void)surf.draw_text_words(176, 240, digits, kHudFg);
	}

	void draw_fps(auto& app, u32 fps10) {
		char digits[4] {};
		digits[0] = static_cast<char>('0' + (fps10 / 100u));
		digits[1] = static_cast<char>('0' + ((fps10 / 10u) % 10u));
		digits[2] = '.';
		digits[3] = static_cast<char>('0' + (fps10 % 10u));
		auto surf = field_surface(app);
		(void)surf.draw_text_words(272, 240, digits, kHudFg);
	}

	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_backdrop {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_actor {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_backing {}; ///< backing del panel B
	eng::graphics::ChipBitmapView<eng::PlaneTag> m_backdrop_view {};
	eng::graphics::ChipBitmapView<eng::PlaneTag> m_actor_view {};
	eng::graphics::ChipBitmapView<eng::PlaneTag> m_backing_view {};
	eng::hud::Hud<2u, 24u, 64u, 48u, 48u> m_hud {};
	eng::hud::RegionHandle m_region {};
	eng::hud::RegionHandle m_region_b {}; ///< overlay B (politica Backing)
	eng::hud::MeterHandle m_meter {};
	eng::hud::CounterHandle m_counter {};
	u16 m_scroll = 0u;
	s16 m_ax = 24;
	s16 m_ay = 96;
	u8 m_skin = 0u;
	u32 m_skin_changes = 0u;
	u32 m_last_hud_cycles = 0u;
	bool m_degraded = false;
	u32 m_deg_changes = 0u;
	eng::hud::HudDegradeLadder m_ladder {}; ///< escalera de degradacion del engine (H4)
	u32 m_blit_bad = 0u;
	u32 m_ov_ok = 0u;
	u32 m_ov_bad = 0u;
	u32 m_bk_ok = 0u;    ///< restauraciones del backing del panel B correctas
	u32 m_bk_bad = 0u;   ///< fallos de restauracion (deberia ser 0)
	bool m_bk_dirty = false; ///< el panel B necesita restaurar + repintar
	u32 m_hud_second = ~0u;
	eng::debug::HudFpsWindow m_fps_win {};
	bool m_ready = false;
};

} // namespace

HudOverlayDemo g_game {};

int main() {
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = kPlanes;
	display.layout = eng::graphics::PlaneLayout::Contiguous; // las vistas del demo son contiguas (c2p y hojas)
	display.palette = kPalette;
	// **Espera de VBlank activa** (familia 212/DT-011, receta DT-042): el bucle por senal
	// puede perder el despertar bajo un SO vivo y clavar los frames; con blits por frame
	// (scroll + paneles) se usa la espera activa (IRQ + sondeo de VPOS).
	return eng::amiga::run_demo(g_game, display, eng::amiga::VblankWait::Active);
}
