// Demo 300 - Compositor de `eng::ui` con backing store y copias por Blitter.
//
// Objetivo: validar EN HARDWARE (68000) el **compositor** de la libreria GUI: varias ventanas,
// cada una con su **backing** en Chip RAM (los widgets se pintan ahi una sola vez), que se mueven
// sin repintar a las vecinas. La composicion a pantalla usa `Compositor::present_blit`, que copia
// cada backing con `Surface::blit`: con el `BlitterRaster` instalado encola un `CopyRect` por plano
// en el `FramePlan` (ruta del Blitter), y el plan se ejecuta con `backend.execute_frame_plan`.
// Las ventanas se mueven en pasos de 16 px para que las copias queden alineadas a palabra.
//
// Ver `docs/engine/architecture/GUI_LIBRARY.md` §14 y `docs/guides/roadmap/ROADMAP_GUI.md` (G8).
#include <eng/api/api.hpp>          // fachada: escena, GUI (eng::ui), run_status
#include <eng/platform/amiga_minimal.hpp>

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

constexpr scene::SceneResources kRes = scene::planar(kWidth, kHeight, kPlanes);
static_assert(scene::valid_scene(kRes, scene::ocs_a500), "300: EHB 320x256 en A500");

/// Paleta EHB (32 colores base). Indices: 0 fondo/escritorio, 1 bisel claro, 2 bisel oscuro,
/// 3 texto, 4 relleno de ventana, 5 barra de titulo, 6 borde, 7 foco.
constexpr eng::Palette32 kPalette {{
	0x012, 0xeee, 0x001, 0xfff, 0x248, 0x46a, 0xf80, 0x0f0,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

/// Callback del `field::RectFillSink`: rellena el rect por el Blitter D-only del backend. Lo usa
/// el compositor para limpiar el escritorio (en vez de `set_pixel` por pixel).
bool rect_fill_cb(void* ctx, eng::u8* base, eng::u8 planes, eng::u32 plane_stride,
		  eng::u32 row_stride, eng::u16 row_bytes, eng::u16 bw, eng::u16 bh,
		  eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h, eng::u8 color) {
	auto* b = static_cast<eng::amiga::MinimalBackend*>(ctx);
	return b->blitter_fill_rect(base, planes, plane_stride, row_stride, row_bytes, bw, bh, x, y, w,
				    h, color, true);
}

ui::UiTheme make_theme() {
	ui::UiTheme t;
	t.bg = 4u;
	t.shine = 1u;
	t.shadow = 2u;
	t.text = 3u;
	t.fill = 4u;
	t.fill_active = 5u;
	t.edit_bg = 0u;
	t.focus_ring = 7u;
	t.text_dim = 2u;
	return t;
}

constexpr eng::u16 kWinW = 96u;
constexpr eng::u16 kWinH = 48u;
constexpr eng::u8 kWinCount = 3u;
/// Bytes de un backing 96x48x6 (row_bytes = ((96/8)+3)&~3 = 12).
constexpr eng::u32 kBackingBytes = static_cast<eng::u32>(12u) * kWinH * kPlanes;

/// Un "escritorio" con ventanas movibles sobre un compositor.
struct CompositorDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);

		m_memory_ok = backend.configure_memory({
			80u * 1024u, // Chip: 6 bitplanes EHB (61 KB) + backings + copper.
			8u * 1024u,  // Slow.
			4u * 1024u,  // Frame scratch.
		});

		m_scene_ok = m_memory_ok &&
			     scene::compose(m_scene, backend.memory(), kRes, scene::ocs_a500,
					    scene::display(scene::kPal320x256, scene::kBplcon0_Ehb),
					    scene::palette(kPalette, 0u, 32u));

		if (!m_scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000300u);
			return;
		}

		// Copias por Blitter (`CopyRect` en el FramePlan) y clear del escritorio por Blitter D-only.
		backend.install_raster(m_scene);
		m_scene.set_rect_fill_sink(eng::field::RectFillSink {&backend, &rect_fill_cb});
		m_screen = m_scene.surface(); // vista estable sobre el playfield de la escena
		m_comp.set_screen(m_screen);
		m_comp.set_desktop(0u);

		if (!build_windows(backend)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000301u);
			return;
		}

		// Self-test: `present_blit` de una ventana alineada encola `CopyRect` (ruta Blitter) y el
		// plan ejecutado deja pixeles de ventana en pantalla.
		const eng::u32 vc = verify_blit_path(backend);
		if (vc != 0u) {
			eng::debug::mark_failed(g_eng_run_status, vc);
			return;
		}

		m_scene.takeover(backend);
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_scene.words()));
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		(void)backend;
		animate(static_cast<eng::u16>(context.frame.frame_index));
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_scene.ok()) {
			return;
		}
		// Compone las regiones danadas por la animacion y ejecuta el plan (copias por Blitter).
		eng::graphics::FramePlan plan {};
		m_comp.present_blit(plan);
		if (plan.blit_job_count() != 0u) {
			(void)backend.execute_frame_plan(plan);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Reserva y enlaza los backings; pinta el contenido de cada ventana una sola vez.
	bool build_windows(eng::amiga::MinimalBackend& backend) {
		m_win[0].frame = ui::Rect {16, 16, kWinW, kWinH};
		m_win[1].frame = ui::Rect {208, 16, kWinW, kWinH};
		m_win[2].frame = ui::Rect {112, 120, kWinW, kWinH};
		static const char* const kTitles[kWinCount] = {"Ventana A", "Ventana B", "Ventana C"};

		for (eng::u8 i = 0u; i < kWinCount; ++i) {
			m_mem[i] = backend.memory().chip.allocate_block<eng::PlaneTag>(kBackingBytes + 16u, 16);
			ui::CompWindow* w = m_comp.add();
			if (w == nullptr || !m_mem[i].valid() ||
			    !w->backing.bind(m_mem[i].view.data(), kBackingBytes, kWinW, kWinH, kPlanes)) {
				return false;
			}
			w->frame = m_win[i].frame;
			m_win[i].win = w;
			draw_window(*w, kTitles[i]);
		}
		return true;
	}

	/// Pinta el contenido de la ventana en su backing (barra de titulo + texto + boton).
	static void draw_window(ui::CompWindow& w, const char* title) {
		ui::UiPainter p(w.backing.surface, nullptr, s_theme);
		const ui::Rect r {0, 0, kWinW, kWinH};
		p.panel(r);
		// Barra de titulo.
		p.fill(ui::Rect {0, 0, kWinW, 12}, s_theme.fill_active);
		p.text(6, 2, title, s_theme.text);
		// Contenido: una etiqueta y un boton.
		p.text(6, 18, "backing store", s_theme.text);
		const ui::Rect btn {6, 30, 60, 12};
		p.button_face(btn, false);
		p.text(12, 32, "Aceptar", s_theme.text);
	}

	/// Mueve las ventanas en pasos de 16 px (copias alineadas a palabra) y dana su region.
	/// La posicion depende directamente del frame (avanza aunque la tasa sea baja).
	void animate(eng::u16 f) {
		// A: horizontal en pasos de 16 px, de x=16 a x=96 (sin solapar con B, en x=208).
		const eng::s16 ax = static_cast<eng::s16>(16 + (f % 6u) * 16u);
		m_comp.move_window(*m_win[0].win, ax, 16);
		// C: vertical en pasos de 16 px, de y=120 a y=168 (sin solapar con A/B).
		const eng::s16 cy = static_cast<eng::s16>(120 + (f % 4u) * 16u);
		m_comp.move_window(*m_win[2].win, 112, cy);
	}

	/// Comprueba el contenido del backing, que `present_blit` encola `CopyRect` (ruta Blitter) y
	/// que el plan ejecutado deja pixeles de ventana en pantalla. Devuelve 0 si todo va bien, o un
	/// codigo de fallo.
	eng::u32 verify_blit_path(eng::amiga::MinimalBackend& backend) {
		// 1) El backing de la ventana 0 tiene contenido (los widgets se pintaron).
		const eng::u8* b0 = m_mem[0].view.data();
		bool any = false;
		for (eng::u32 i = 0u; i < kBackingBytes; ++i) {
			if (b0[i] != 0u) {
				any = true;
			}
		}
		if (!any) {
			return 0x00000310u; // backing vacio
		}
		// 2) `present_blit` de una ventana alineada encola `CopyRect`.
		m_comp.damage_screen(ui::Rect {0, 0, kWidth, kHeight});
		eng::graphics::FramePlan plan {};
		m_comp.present_blit(plan);
		if (plan.blit_job_count() == 0u) {
			return 0x00000311u; // sin CopyRect
		}
		if (!backend.execute_frame_plan(plan)) {
			return 0x00000312u; // el plan no se ejecuto
		}
		// `present_blit` limpia el daño: re-marca las ventanas para el primer frame (solo sus
		// regiones, para que el primer compose no barra la pantalla entera por CPU).
		for (eng::u8 i = 0u; i < kWinCount; ++i) {
			m_comp.damage_screen(m_win[i].frame);
		}
		return 0u;
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	scene::Scene m_scene {};
	field::Surface m_screen {};
	ui::Compositor m_comp {};
	struct Win {
		ui::Rect frame {};
		ui::CompWindow* win = nullptr;
	};
	Win m_win[kWinCount] {};
	eng::Block<eng::PlaneTag> m_mem[kWinCount] {};
	static const ui::UiTheme s_theme;
};

const ui::UiTheme CompositorDemo::s_theme = make_theme();

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	CompositorDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
