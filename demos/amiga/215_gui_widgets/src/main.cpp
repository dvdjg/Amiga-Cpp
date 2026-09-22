// Demo 215 - Widgets de `eng::ui` sobre EHB (320x256).
//
// Objetivo: validar EN HARDWARE (68000) la cadena de la libreria GUI:
//   arbol de widgets (Panel/Label/Button/CheckBox/RadioButton/EditBox/Slider)
//   -> UiPainter (chrome + texto con Font8) -> Surface -> planos EHB.
//
// El dibujo va por `scene.surface()`: la demo pide rellenos, biseles, lineas y texto y el
// engine enruta al layout, sin que la app vea planos ni punteros. Los *fills* de caja van por
// el **Blitter D-only** (`RectFillSink` + `blitter_fill_rect`, sincrono); las lineas y el texto
// por CPU. La parte estatica se pinta una sola vez; por frame solo se repinta la zona animada
// (pista del slider), con repintado por zona.
//
// El **cursor es un sprite de hardware** (canal 0): estructura DMA en Chip RAM + `SPR0PT` en la
// copperlist + `DMACON` SPREN; sigue al raton (`poll_mouse`) reescribiendo POS/CTL. La captura
// PNG del runner **no incluye sprites** (verificado: la demo 206 tampoco los muestra), asi que el
// cursor se valida por los registros/copperlist (self-test) y no por el gate de pixeles.
#include <eng/api/api.hpp>          // fachada: escena, dibujo, paleta, GUI (eng::ui), run_status
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/platform/input_poll.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

namespace {

namespace scene = eng::graphics::composition;
namespace field = eng::field;
namespace ui = eng::ui;

constexpr eng::u16 kWidth = 320;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 6;

/// Escena EHB 320x256 (6 planos) sobre `scene::compose`: perfil y presupuesto validados
/// en compilacion.
constexpr scene::SceneResources kRes = scene::planar(kWidth, kHeight, kPlanes);
static_assert(scene::valid_scene(kRes, scene::ocs_a500), "215: EHB 320x256 en A500");

/// Paleta EHB de 32 colores base (los indices 32..63 son su mitad de brillo). El indice 0
/// es el fondo del panel; 1/2 el bisel; 3 el texto; 4/5 el relleno normal/activo; 6 el
/// fondo de los campos; 7 el anillo de foco.
constexpr eng::Palette32 kPalette {{
	0x012, 0xeee, 0x001, 0xfff, 0x248, 0x46a, 0x111, 0xf80,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	// COLOR16-19: par de sprite 0/1 (cursor de hardware). 16 = color 1 (blanco),
	// 17 = color 2 (negro, contorno), 18/19 sin usar.
	0xfff, 0x000, 0xf00, 0x0f0,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

/// Cursor de hardware: 16x16, 1 palabra por linea (SPRxDATA = color 1, SPRxDATB = color 2).
constexpr eng::u16 kCursorH = 16u;
/// Flecha de raton (16 filas; bit15 = pixel izquierdo). DAT = cuerpo (blanco),
/// DATB = contorno (negro) en los pixeles alrededor del cuerpo.
constexpr eng::u16 kCursorDat[kCursorH] = {
	0x8000u, 0xc000u, 0xe000u, 0xf000u, 0xf800u, 0xfc00u, 0xfe00u, 0xff00u,
	0xff80u, 0xff00u, 0xf000u, 0xd800u, 0x8c00u, 0x0c00u, 0x0600u, 0x0000u,
};

/// Tema de la demo: mapea los roles del tema a los indices de la paleta.
ui::UiTheme make_theme() {
	ui::UiTheme t;
	t.bg = 0u;
	t.shine = 1u;
	t.shadow = 2u;
	t.text = 3u;
	t.fill = 4u;
	t.fill_active = 5u;
	t.edit_bg = 6u;
	t.focus_ring = 7u;
	t.text_dim = 2u;
	return t;
}

/// Callback del `field::RectFillSink`: rellena el rect por el Blitter D-only del backend.
bool rect_fill_cb(void* ctx, eng::u8* base, eng::u8 planes, eng::u32 plane_stride,
		  eng::u32 row_stride, eng::u16 row_bytes, eng::u16 bw, eng::u16 bh,
		  eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h, eng::u8 color) {
	auto* b = static_cast<eng::amiga::MinimalBackend*>(ctx);
	return b->blitter_fill_rect(base, planes, plane_stride, row_stride, row_bytes, bw, bh, x, y, w,
				    h, color, true);
}

struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);

		m_memory_ok = backend.configure_memory({
			scene::chip_bytes_for(kRes), // Chip: bitplanes EHB + copper doble + margen.
			8u * 1024u,                  // Slow.
			4u * 1024u,                  // Frame scratch.
		});

		// Cursor por sprite de hardware (`eng::ui::HardwareCursor`): estructura DMA en Chip RAM,
		// emitida en la copperlist como etapa de `compose` (SPR0PT + DMACON con SPREN).
		m_sprite_block = backend.memory().chip.allocate_block<eng::SpriteTag>(ui::HardwareCursor::kBytes, 16);
		if (!m_sprite_block.valid() ||
		    !m_cursor.bind(m_sprite_block.view.data(), ui::HardwareCursor::kBytes)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021503u);
			return;
		}
		m_cursor.set_bitmap(kCursorDat, nullptr);
		m_cursor.set_position(m_cx, m_cy);
		if (!verify_cursor()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021503u);
			return;
		}
		const auto cursor_stage = [this](scene::Scene& sc) { m_cursor.emit_into(sc.scheduler()); };

		m_scene_ok = m_memory_ok &&
			     scene::compose(m_scene, backend.memory(), kRes, scene::ocs_a500,
					    scene::display(scene::kPal320x256, scene::kBplcon0_Ehb),
					    scene::palette(kPalette, 0u, 32u), cursor_stage);

		if (!m_scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000215u);
			return;
		}

		// La copperlist debe apuntar SPR0PT a la estructura del cursor y habilitar SPREN.
		if (!verify_cursor_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021510u);
			return;
		}

		// Self-test de hardware del relleno de rect D-only por Blitter (motor del RectFillSink).
		if (!verify_blitter_fill(backend)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021502u);
			return;
		}

		// Raster Blitter (los fills de caja van por el Blitter D-only, sincrono) + sink de rect.
		// Las lineas y el texto siguen por CPU (sin FramePlan): el rect D-only no es asincrono.
		m_scene.set_rect_fill_sink(eng::field::RectFillSink {&backend, &rect_fill_cb});
		m_scene.set_raster(&eng::field::kBlitterRaster,
				   eng::field::RasterPolicy {eng::field::AccelMode::Auto, 64u, true});

		build_tree();
		if (!verify_ui()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021501u);
			return;
		}

		draw_static();
		m_scene.takeover(backend);
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_scene.words()));
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		(void)backend; // la lista es estatica: `takeover` ya la instalo
		update_cursor(); // el cursor de hardware sigue al raton
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_scene.ok()) {
			return;
		}
		(void)backend;
		const eng::u32 f = context.frame.frame_index;

		// La pista del slider es lo unico animado: se limpia su zona y se repinta solo el
		// slider (repintado por zona), no el arbol completo a 50 fps.
		const eng::s16 v = static_cast<eng::s16>((f * 3u) % 101u);
		if (v != m_slider_value) {
			m_slider_value = v;
			field::Surface c = m_scene.surface();
			ui::UiPainter p(c, nullptr, m_theme);
			eng::Box z = m_slider.bounds;
			z.x = static_cast<eng::s16>(z.x - 2);
			z.w = static_cast<eng::u16>(z.w + 4u);
			z.y = static_cast<eng::s16>(z.y - 2);
			z.h = static_cast<eng::u16>(z.h + 4u);
			p.fill(z, m_theme.bg);
			ui::draw_widget(m_slider, p);
		}

		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Monta el arbol de widgets (todos viven en `DemoGame`, sin heap).
	void build_tree() {
		m_root.bounds = ui::Rect {16, 16, 288, 224};

		m_title.bounds = ui::Rect {24, 24, 240, 10};
		m_title.text = "ENGINE GUI - DEMO 215";

		m_button.bounds = ui::Rect {24, 42, 84, 14};
		m_button.text = "Aceptar";

		m_check.bounds = ui::Rect {124, 44, 120, 10};
		m_check.label = "Sonido";
		m_check.value = &m_sound;

		m_radio_a.bounds = ui::Rect {24, 66, 140, 10};
		m_radio_a.label = "Opcion A";
		m_radio_a.group_id = 1u;
		m_radio_a.value = &m_radio_a_on;

		m_radio_b.bounds = ui::Rect {24, 82, 140, 10};
		m_radio_b.label = "Opcion B";
		m_radio_b.group_id = 1u;
		m_radio_b.value = &m_radio_b_on;

		m_edit.bounds = ui::Rect {24, 102, 200, 14};
		m_edit.buf = m_text;
		m_edit.cap = sizeof(m_text);
		m_edit.len = 11u; // "Hola Amiga\0"

		m_slider.bounds = ui::Rect {24, 126, 220, 10};
		m_slider.value = &m_slider_value;
		m_slider.min = 0;
		m_slider.max = 100;

		m_status.bounds = ui::Rect {24, 150, 260, 10};
		m_status.text = "Listo. Tab cambia el foco.";

		// Prueba de la fuente cirilica en hardware (HOST-264): el literal UTF-8 se
		// decodifica y se pinta con los glifos U+04xx de `Font8`.
		m_cyr.bounds = ui::Rect {24, 166, 260, 10};
		m_cyr.text = "Привет, Амига! Ёж";

		m_root.add_child(&m_cyr);
		m_root.add_child(&m_status);
		m_root.add_child(&m_slider);
		m_root.add_child(&m_edit);
		m_root.add_child(&m_radio_b);
		m_root.add_child(&m_radio_a);
		m_root.add_child(&m_check);
		m_root.add_child(&m_button);
		m_root.add_child(&m_title);

		m_ctx.set_root(&m_root);
		m_ctx.set_focus(&m_button);
	}

	/// Mueve el cursor con el raton (deltas de `JOY0DAT`) y reescribe POS/CTL en la estructura.
	void update_cursor() {
		eng::input::MouseState mouse;
		eng::amiga::poll_mouse(mouse, m_mouse_poll);
		eng::s16 cx = static_cast<eng::s16>(m_cx + mouse.dx);
		eng::s16 cy = static_cast<eng::s16>(m_cy + mouse.dy);
		if (cx < 0) cx = 0;
		if (cx > static_cast<eng::s16>(kWidth - 1u)) cx = static_cast<eng::s16>(kWidth - 1u);
		if (cy < 0) cy = 0;
		if (cy > static_cast<eng::s16>(kHeight - kCursorH)) cy = static_cast<eng::s16>(kHeight - kCursorH);
		m_cx = cx;
		m_cy = cy;
		m_cursor.set_position(m_cx, m_cy);
	}

	/// Self-test de la emision: `SPR0PT` apunta a la estructura y la ULTIMA escritura de `DMACON`
	/// habilita SPREN (evidencia; los registros no se capturan en PNG).
	bool verify_cursor_copper() const {
		const eng::u16* w = m_scene.active_words();
		const eng::u16 n = m_scene.words();
		const eng::uintptr sp = reinterpret_cast<eng::uintptr>(m_sprite_block.view.data());
		bool spr0 = false;
		eng::u16 last_dmacon = 0u;
		for (eng::u16 i = 0u; i + 1u < n; i += 2u) {
			if (w[i] == 0x0120u && w[i + 1u] == static_cast<eng::u16>(sp >> 16)) {
				spr0 = true;
			}
			if (w[i] == 0x0096u) {
				last_dmacon = w[i + 1u];
			}
		}
		return spr0 && (last_dmacon & 0x0020u) != 0u;
	}

	/// Self-test de la estructura del cursor: tiene pixeles y un terminador DMA nulo.
	bool verify_cursor() {
		const eng::u16* w = m_cursor.words();
		if (w == nullptr) {
			return false;
		}
		bool any = false;
		for (eng::u16 l = 0u; l < ui::HardwareCursor::kSize; ++l) {
			if (w[2u + l * 2u] != 0u) {
				any = true;
			}
		}
		const eng::u16 t = static_cast<eng::u16>(2u + ui::HardwareCursor::kSize * 2u);
		return any && w[t] == 0u && w[t + 1u] == 0u;
	}

	/// Self-test EN HARDWARE del relleno de rect D-only por Blitter (`blitter_fill_rect`):
	/// llena el rect (10,2)-(29,4) de un plano 64x16 y comprueba los bits dentro y fuera. Valida
	/// el motor que consume el `RectFillSink` (equivalencia con el relleno CPU esperado).
	bool verify_blitter_fill(eng::amiga::MinimalBackend& backend) {
		constexpr eng::u16 fw = 64;
		constexpr eng::u16 fh = 16;
		constexpr eng::u16 frow = fw / 8u; // 8 bytes/fila
		constexpr eng::u32 fplane = static_cast<eng::u32>(frow) * fh;
		auto blk = backend.memory().chip.allocate_block<eng::PlaneTag>(fplane + 16u, 16);
		if (!blk.valid()) {
			return false;
		}
		for (eng::u32 i = 0; i < fplane; ++i) {
			blk.view.data()[i] = 0u;
		}
		if (!backend.blitter_fill_rect(blk.view.data(), 1u, fplane, frow, frow, fw, fh, 10, 2, 20u,
					       3u, 1u, true)) {
			return false;
		}
		auto on = [&](eng::u16 x, eng::u16 y) {
			return (blk.view.data()[static_cast<eng::u32>(y) * frow + (x >> 3)] &
				(0x80u >> (x & 7u))) != 0u;
		};
		return on(10, 2) && on(29, 2) && on(10, 4) && on(29, 4) &&
		       !on(9, 2) && !on(30, 2) && !on(10, 1) && !on(10, 5) && !on(0, 0);
	}

	/// encuentra el boton en su centro y un click sobre la casilla alterna su valor. Si la
	/// geometria o el despacho fallaran en m68k, la demo iria a Failed en vez de Ready.
	bool verify_ui() {
		ui::Widget* hit = m_ctx.hit_test(&m_root, 60, 49);
		if (hit != &m_button) {
			return false;
		}
		const bool before = m_sound;
		ui::UiEvent down {};
		down.kind = ui::UiEventKind::MouseDown;
		down.x = 128;
		down.y = 48;
		m_ctx.dispatch(down);
		ui::UiEvent up {};
		up.kind = ui::UiEventKind::MouseUp;
		up.x = 128;
		up.y = 48;
		m_ctx.dispatch(up);
		if (m_sound == before) {
			return false;
		}
		// Fuente cirilica (HOST-264): А (U+0410) y я (U+044F) deben tener glifo.
		bool cyr_ok = false;
		for (eng::u8 r = 0; r < eng::Font8::kRows; ++r) {
			if (eng::Font8::row(0x0410u, r) != 0u && eng::Font8::row(0x044Fu, r) != 0u) {
				cyr_ok = true;
			}
		}
		return cyr_ok;
	}

	/// Pinta el arbol completo una sola vez (la UI es estatica salvo la pista del slider).
	void draw_static() {
		field::Surface c = m_scene.surface();
		ui::UiPainter p(c, nullptr, m_theme);
		ui::draw_tree(m_root, p);
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;

	bool m_sound = true;
	bool m_radio_a_on = true;
	bool m_radio_b_on = false;
	eng::s16 m_slider_value = 0;
	char m_text[16] = "Hola Amiga";

	ui::Panel m_root {};
	ui::Label m_title {};
	ui::Button m_button {};
	ui::CheckBox m_check {};
	ui::RadioButton m_radio_a {};
	ui::RadioButton m_radio_b {};
	ui::EditBox m_edit {};
	ui::Slider m_slider {};
	ui::Label m_status {};
	ui::Label m_cyr {};

	ui::UiTheme m_theme = make_theme();
	ui::UiContext m_ctx {};
	scene::Scene m_scene {};

	eng::Block<eng::SpriteTag> m_sprite_block {}; ///< estructura DMA del cursor (Chip RAM)
	ui::HardwareCursor m_cursor {};               ///< cursor por sprite de hardware
	eng::amiga::MousePollState m_mouse_poll {};
	eng::s16 m_cx = 160; ///< posicion del cursor (sigue al raton)
	eng::s16 m_cy = 128;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	DemoGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
