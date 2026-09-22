// ============================================================================
// Demo 215 - GUI del engine (G0-G6) sobre una escena planar
// ============================================================================
//
// Muestra la libreria `eng::ui` en hardware: `UiPainter`/`UiTheme`, widgets
// (`Panel`/`Label`/`Button`/`CheckBox`/`RadioButton`/`EditBox`), layout y una
// ventana con titulo, todo dibujado sobre `scene.surface()` (un `field::Surface`)
// sin que la demo vea planos ni punteros. Anima el estado de los widgets para
// que la secuencia lo demuestre.
//
//   bash ./tools/build/build-demo.sh demos/amiga/215_gui_widgets --debug
//   bash ./tools/run/run-demo.sh demos/amiga/215_gui_widgets --sequence-frames 4
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/ui/context.hpp>
#include <eng/ui/layout.hpp>
#include <eng/ui/painter.hpp>
#include <eng/ui/theme.hpp>
#include <eng/ui/widgets.hpp>
#include <eng/ui/window.hpp>

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

constexpr eng::u16 kWidth = 320;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 6; // EHB: base 0..31 para el tema

constexpr scene::SceneResources kRes = scene::planar(kWidth, kHeight, kPlanes);
static_assert(scene::valid_scene(kRes, scene::ocs_a500), "215: EHB 320x256 en A500");

// Paleta EHB (32 colores base). Indices del tema elegidos para contrastar.
constexpr eng::Palette32 kPalette {{
	0x123, 0xfff, 0x000, 0x777, 0x999, 0xff0, 0x000, 0x0f0,
	0x24a, 0xf00, 0x0f0, 0x048, 0x840, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

// Tema: indices logicos -> paleta.
constexpr eng::ui::UiTheme kTheme {
	.bg = 0u, .shine = 1u, .shadow = 2u, .fill = 3u, .fill_active = 4u,
	.text = 5u, .text_dim = 2u, .edit_bg = 6u, .focus_ring = 7u,
	.panel_frame = eng::ui::FrameStyle::Raised, .button_frame = eng::ui::FrameStyle::Raised,
};

struct GuiDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);

		m_memory_ok = backend.configure_memory({
			70u * 1024u, // Chip: 6 bitplanes EHB + copperlist
			8u * 1024u,
			4u * 1024u,
		});

		m_scene_ok = m_memory_ok &&
			     scene::compose(m_scene, backend.memory(), kRes, scene::ocs_a500,
					    scene::display(scene::kPal320x256, scene::kBplcon0_Ehb),
					    scene::palette(kPalette, 0u, 32u));

		if (!m_memory_ok || !m_scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021501u);
			return;
		}
		// GUI dibujada por CPU (líneas/texto con `m_plan == nullptr`): se deja el
		// rasterizador CPU por defecto (no `install_raster`, que pasaría a Blitter y
		// exigiría un `FramePlan` materializado).
		build_tree();
		m_scene.takeover(backend);
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_scene.words()));
	}

	void update(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		// Anima el estado de los widgets (para que la secuencia lo demuestre).
		const eng::u32 f = context.frame.frame_index;
		m_sound = ((f / 20u) & 1u) != 0u;
		m_opt_a = ((f / 40u) & 1u) == 0u;
		m_opt_b = !m_opt_a;
		if (((f / 10u) & 1u) != 0u) {
			m_button.set_flag(eng::ui::WfPressed);
		} else {
			m_button.clear_flag(eng::ui::WfPressed);
		}
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		if (!m_scene.ok()) {
			return;
		}
		field::Surface s = m_scene.surface();
		eng::ui::UiPainter p {s, nullptr, kTheme};
		eng::ui::draw_tree(m_desktop, p);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	void build_tree() {
		// Escritorio.
		m_desktop.bounds = eng::ui::Rect {0, 0, kWidth, kHeight};

		// Ventana con titulo.
		m_window.bounds = eng::ui::Rect {18, 18, 240u, 170u};
		m_window.title = "eng::ui  (G0-G6)";

		m_label.bounds = eng::ui::Rect {32, 44, 0u, 8u};
		m_label.text = "Demo 215 - GUI del engine";

		m_button.bounds = eng::ui::Rect {32, 60, 96u, 16u};
		m_button.text = "Play";

		m_check.bounds = eng::ui::Rect {32, 86, 120u, 10u};
		m_check.label = "Sonido";
		m_check.value = &m_sound;

		m_radio_a.bounds = eng::ui::Rect {32, 106, 100u, 10u};
		m_radio_a.label = "Opcion A";
		m_radio_a.group_id = 1u;
		m_radio_a.value = &m_opt_a;

		m_radio_b.bounds = eng::ui::Rect {150, 106, 100u, 10u};
		m_radio_b.label = "Opcion B";
		m_radio_b.group_id = 1u;
		m_radio_b.value = &m_opt_b;

		m_edit.bounds = eng::ui::Rect {32, 128, 160u, 16u};
		m_edit.buf = m_edit_buf;
		m_edit.cap = sizeof(m_edit_buf);
		m_edit.len = 3u;
		m_edit.caret = 3u;
		m_edit_buf[0] = 'a';
		m_edit_buf[1] = 'b';
		m_edit_buf[2] = 'c';
		m_edit_buf[3] = '\0';

		m_window.add_child(&m_label);
		m_window.add_child(&m_button);
		m_window.add_child(&m_check);
		m_window.add_child(&m_radio_a);
		m_window.add_child(&m_radio_b);
		m_window.add_child(&m_edit);
		m_desktop.add_child(&m_window);
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	bool m_sound = false;
	bool m_opt_a = true;
	bool m_opt_b = false;
	char m_edit_buf[8] {};
	scene::Scene m_scene {};

	eng::ui::Panel m_desktop {};
	eng::ui::Window m_window {};
	eng::ui::Label m_label {};
	eng::ui::Button m_button {};
	eng::ui::CheckBox m_check {};
	eng::ui::RadioButton m_radio_a {};
	eng::ui::RadioButton m_radio_b {};
	eng::ui::EditBox m_edit {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	GuiDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
