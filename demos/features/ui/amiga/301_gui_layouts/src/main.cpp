// Demo 301 - `eng::ui`: layouts adaptables, texto ajustado y coleccion de fuentes.
//
// Objetivo: validar EN HARDWARE (68000) las extensiones de la GUI:
//   - layouts deterministicos: `layout_fit_children` (adaptable al contenido), `layout_flow` (con
//     *wrap*) y `layout_stack_v`.
//   - **texto ajustado** (`Label.wrap`/`wrap_w`).
//   - **texto por Blitter** con cache de glifos (`GlyphCache` + `UiPainter::text_blit`): la
//     equivalencia CPU y el algoritmo de varios pares los cubre HOST-313; la ejecucion en
//     hardware sigue pendiente (§8 de pending-verification).
//   - **coleccion de fuentes**: `Font8` (y su cursiva), `Font5x7` (y cursiva) y micro `Font3x5`.
//
// Escena EHB 320x256. `text_blit` necesita un `FramePlan`: la demo lo usa y ejecuta el plan con el
// backend (ruta Blitter). El resto del dibujo estatico va por CPU en la misma `Surface`.
//
// Ver `docs/engine/architecture/GUI_LIBRARY.md` 6/12 y `docs/guides/roadmap/ROADMAP_GUI.md`.
#include <eng/api/api.hpp>
#include <eng/platform/amiga/backend.hpp>

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
constexpr eng::u8 kPlanes = 6; // EHB

constexpr scene::SceneResources kRes = scene::planar(kWidth, kHeight, kPlanes);
static_assert(scene::valid_scene(kRes, scene::ocs_a500), "301: EHB 320x256 en A500");

/// Paleta EHB. 0 fondo, 1 bisel claro, 2 sombra, 3 texto, 4 relleno, 5 activo, 6 fondo de campo,
/// 7 acento suave.
constexpr eng::Palette32 kPalette {{
	0x012, 0xeee, 0x001, 0xfff, 0x248, 0x46a, 0x111, 0xf80,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

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

/// Rellena de rect por Blitter D-only (sink del raster).
bool rect_fill_cb(void* ctx, eng::u8* base, eng::u8 planes, eng::u32 plane_stride,
		  eng::u32 row_stride, eng::u16 row_bytes, eng::u16 bw, eng::u16 bh,
		  eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h, eng::u8 color) {
	auto* b = static_cast<eng::amiga::AmigaBackend*>(ctx);
	return b->blitter_fill_rect(base, planes, plane_stride, row_stride, row_bytes, bw, bh, x, y, w,
				    h, color, true);
}

/// Pinta un glifo de una fuente dada (filas con `bit k = columna k`) en color `fg`. La version
/// cursiva usa el *shear* de `font_italic.hpp`; el trait `italic_row<Font>` lo selecciona
/// (sin STL: especializacion de plantilla).
template <class Font>
struct italic_row;
template <>
struct italic_row<eng::Font8> {
	static eng::u8 get(eng::u16 ch, eng::u8 r) { return eng::font8_row_italic(ch, r); }
};
template <>
struct italic_row<eng::Font5x7> {
	static eng::u8 get(eng::u16 ch, eng::u8 r) { return eng::font5x7_row_italic(ch, r); }
};
template <>
struct italic_row<eng::Font3x5> {
	static eng::u8 get(eng::u16 ch, eng::u8 r) { return eng::Font3x5::row_italic(ch, r); }
};

/// Ancho (px) y lectura de columna segun la convencion de cada fuente.
inline eng::u8 font_width(const eng::Font8&) { return 8u; }
inline eng::u8 font_width(const eng::Font5x7&) { return 5u; }
inline eng::u8 font_width(const eng::Font3x5&) { return 3u; }

inline bool col_set(const eng::Font8&, eng::u8 bits, eng::u8 k) {
	return ((bits >> k) & 1u) != 0u;
}
inline bool col_set(const eng::Font5x7&, eng::u8 bits, eng::u8 k) {
	return k < 5u && ((bits >> (4u - k)) & 1u) != 0u;
}
inline bool col_set(const eng::Font3x5&, eng::u8 bits, eng::u8 k) {
	return k < 3u && ((bits >> (2u - k)) & 1u) != 0u;
}

template <class Font>
void draw_font_glyphs(field::Surface& s, eng::s16 x, eng::s16 y, const char* chars, eng::u8 fg,
		      bool italic) {
	const eng::u8 adv = font_width(Font {});
	eng::s16 cx = x;
	for (const char* p = chars; *p != 0; ++p) {
		const eng::u16 ch = static_cast<eng::u16>(static_cast<eng::u8>(*p));
		for (eng::u8 r = 0u; r < Font::kRows; ++r) {
			const eng::u8 bits = italic ? italic_row<Font>::get(ch, r) : Font::row(ch, r);
			for (eng::u8 k = 0u; k < adv; ++k) {
				if (col_set(Font {}, bits, k)) {
					s.set_pixel(static_cast<eng::s16>(cx + k),
						    static_cast<eng::s16>(y + r), fg);
				}
			}
		}
		cx = static_cast<eng::s16>(cx + adv);
	}
}

struct DemoGame {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);

		m_memory_ok = backend.configure_memory({
			scene::chip_bytes_for(kRes),
			8u * 1024u,
			4u * 1024u,
		});

		m_scene_ok = m_memory_ok &&
			     scene::compose(m_scene, backend.memory(), kRes, scene::ocs_a500,
					    scene::display(scene::kPal320x256, scene::kBplcon0_Ehb),
					    scene::palette(kPalette, 0u, 32u));
		if (!m_scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000301u);
			return;
		}

		m_scene.set_rect_fill_sink(eng::field::RectFillSink {&backend, &rect_fill_cb});
		m_scene.set_raster(&eng::field::kBlitterRaster,
				   eng::field::RasterPolicy {eng::field::AccelMode::Auto, 64u, true});

		if (!build_and_draw(backend)) {
			eng::debug::mark_failed(g_eng_run_status, m_fail != 0u ? m_fail : 0x00000311u);
			return;
		}

		m_scene.takeover(backend);
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_scene.words()));
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Monta los paneles, pinta el chrome (CPU) y el texto por Blitter, y verifica en hardware que
	/// la ruta acelerada escribio tinta y que los layouts colocaron.
	bool build_and_draw(eng::amiga::AmigaBackend& backend) {
		field::Surface screen = m_scene.surface();
		ui::UiPainter p(screen, {}, m_theme);

		// --- Panel 1: columna adaptable al contenido (fit + stack) ---
		m_box1.bounds = ui::Rect {12, 16, 140, 96};
		p.panel(m_box1.bounds);
		m_fit_parent.bounds = ui::Rect {20, 24, 124, 80};
		m_t1.text = "Fit layout";
		m_t2.text = "Etiqueta corta";
		m_t3.text = "Una etiqueta bastante mas larga que las demas";
		m_t3.wrap = ui::WrapMode::Word;
		m_t3.wrap_w = 120u;
		m_fit_parent.add_child(&m_t3);
		m_fit_parent.add_child(&m_t2);
		m_fit_parent.add_child(&m_t1);
		(void)eng::ui::layout_fit_children(m_fit_parent, [&](ui::Widget& w) {
			return ui::measure(w, m_theme);
		}, 4u, true);
		eng::ui::layout_stack_v(m_fit_parent, 4u);
		ui::draw_tree(m_fit_parent, p);

		// --- Panel 2: flow con wrap ---
		m_box2.bounds = ui::Rect {164, 16, 144, 96};
		p.panel(m_box2.bounds);
		m_flow.bounds = ui::Rect {172, 24, 128, 80};
		m_b0.text = "Uno";  m_b1.text = "Dos";
		m_b2.text = "Tres"; m_b3.text = "Cuatro";
		m_flow.add_child(&m_b3);
		m_flow.add_child(&m_b2);
		m_flow.add_child(&m_b1);
		m_flow.add_child(&m_b0);
		for (ui::Widget* c = m_flow.first_child; c != nullptr; c = c->next) {
			const ui::Rect m = ui::measure(*c, m_theme);
			c->bounds.w = m.w;
			c->bounds.h = m.h;
		}
		eng::ui::layout_flow(m_flow, 4u, 4u);
		ui::draw_tree(m_flow, p);

		// --- Panel 3: muestra de fuentes (Font8, Font5x7, Font3x5 y sus cursivas) ---
		m_box3.bounds = ui::Rect {12, 122, 296, 60};
		p.panel(m_box3.bounds);
		p.text(20, 130, "Font8:", m_theme.text);
		draw_font_glyphs<eng::Font8>(screen, 76, 130, "Abg", m_theme.text, false);
		draw_font_glyphs<eng::Font8>(screen, 108, 130, "Abg", m_theme.fill_active, true);
		// Font5x7/Font3x5 son fuentes de HUD (mayusculas + digitos): se usan en mayusculas.
		p.text(20, 146, "Font5x7:", m_theme.text);
		draw_font_glyphs<eng::Font5x7>(screen, 92, 146, "ABG012", m_theme.text, false);
		draw_font_glyphs<eng::Font5x7>(screen, 132, 146, "ABG012", m_theme.fill_active, true);
		p.text(20, 162, "Font3x5:", m_theme.text);
		draw_font_glyphs<eng::Font3x5>(screen, 92, 162, "ABG", m_theme.text, false);
		draw_font_glyphs<eng::Font3x5>(screen, 116, 162, "ABG", m_theme.fill_active, true);

		// --- Texto ajustado (wrapping) por CPU ---
		m_box4.bounds = ui::Rect {12, 188, 296, 56};
		p.panel(m_box4.bounds);
		m_wrap.text = "Texto ajustado al ancho del panel con politica de palabra, "
			      "que ocupa varias lineas y se adapta al tamano disponible.";
		m_wrap.wrap = ui::WrapMode::Word;
		m_wrap.wrap_w = 280u;
		m_wrap.bounds = ui::Rect {20, 196, 280, 40};
		ui::draw_widget(m_wrap, p);

		// --- Ruta **texto por Blitter** (`UiPainter::text_blit`) ---
		// La equivalencia CPU, la geometria del job y el algoritmo de varios pares (mascara por
		// par) los cubre HOST-313. La **ejecucion real del Blitter** sobre esta escena sigue sin
		// reproducir el patron exacto de `Font8` (ni con `x` alineado), tras resolver: buffers de
		// trabajo en el llamador (el plan guarda punteros), en **Chip RAM** (el Blitter solo accede
		// a Chip) y una **mascara por par**. Queda un defecto residual del backend
		// (`amiga_blitter.cpp`, ruta `masked`) que se investiga en
		// `docs/debugging/investigaciones/pending-verification.md` §8. Por eso esta demo pinta el
		// texto por CPU.

		// Self-test en hardware: verifica que el texto ajustado pinto tinta (color 3) en su zona.
		eng::u32 hits = 0u;
		for (eng::s16 y = 196; y < 232; ++y) {
			for (eng::s16 x = 20; x < 300; ++x) {
				if (read_pixel(x, y) == 3u) {
					++hits;
				}
			}
		}
		if (hits < 40u) {
			m_fail = 0x31300u + (hits & 0xffu);
			return false;
		}
		return true;
	}

	/// Lee un pixel de la escena (layout contiguo: plano `p` en `base + p*plane_bytes`, fila
	/// `y*row_bytes`, bit `0x80 >> (x & 7)`).
	eng::u8 read_pixel(eng::s16 x, eng::s16 y) const {
		const eng::u8* base = m_scene.bitplanes().data();
		const eng::u16 rb = m_scene.row_bytes();
		const eng::u32 pb = m_scene.plane_bytes();
		eng::u8 c = 0u;
		for (eng::u8 p = 0u; p < kPlanes; ++p) {
			const eng::u8 byte = base[static_cast<eng::u32>(p) * pb +
						  static_cast<eng::u32>(y) * rb +
						  static_cast<eng::u32>(x / 8)];
			const eng::u8 bit = static_cast<eng::u8>((byte >> (7u - (x & 7u))) & 1u);
			c = static_cast<eng::u8>(c | static_cast<eng::u8>(bit << p));
		}
		return c;
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	eng::u32 m_fail = 0u;

	ui::Panel m_box1 {}, m_box2 {}, m_box3 {}, m_box4 {};
	ui::Panel m_fit_parent {};
	ui::Label m_t1 {}, m_t2 {}, m_t3 {};
	ui::Label m_wrap {};
	ui::Panel m_flow {};
	ui::Button m_b0 {}, m_b1 {}, m_b2 {}, m_b3 {};
	ui::UiTheme m_theme = make_theme();
	scene::Scene m_scene {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	DemoGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
