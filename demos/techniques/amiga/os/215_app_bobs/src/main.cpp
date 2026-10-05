// Demo 215 — capa de BOBs de la fachada: `Anim` ligada, orden por `z` y hojas heterogéneas.
//
// Tutorial (§5 de ROADMAP_GAME_API): un juego dibuja objetos con `screen().bobs(layer)` sin ver
// `FramePlan`, `BobTarget`, `BlitJob` ni minterms. Cada actor lleva su `graphics::Anim` (el engine
// avanza el frame con `layer.tick()`) y su `z` (el engine ordena). La capa es **heterogénea**:
// los actores eligen su hoja con `sheet_index`.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/os/215_app_bobs --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/os/215_app_bobs

#include <eng/api/api.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic, eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold), 0, 0,
};
}

namespace {

namespace graphics = eng::graphics;
using eng::s16;
using eng::u16;
using eng::u32;

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr eng::u8 kPlanes = 4u;

// Hoja planar OPACA (sin máscara): frame = [plano0][plano1] de `kH` filas; cada fila `kBase+1`
// palabras (la última, guarda del barrel shifter). `kFrames` frames densos por hoja.
constexpr u16 kObjW = 16u;
constexpr u16 kObjH = 16u;
constexpr eng::u8 kObjPlanes = 2u;
constexpr u32 kBase = (kObjW + 15u) / 16u;          // 1 palabra
constexpr u32 kRowBytes = (kBase + 1u) * 2u;        // 4 B (imagen + guarda)
constexpr u32 kPlaneBytes = kObjH * kRowBytes;      // 64
constexpr u32 kFrameStride = kPlaneBytes * kObjPlanes; // 128
constexpr u16 kFrames = 2u;
constexpr u32 kSheetBytes = kFrameStride * kFrames; // 256

struct BobsDemo {
	eng::scene::BobLayer m_bobs {};
	eng::Block<eng::BobTag> m_sheet_a {};
	eng::Block<eng::BobTag> m_sheet_b {};
	graphics::Sprite m_sprite_a {};
	graphics::Sprite m_sprite_b {};

	// Posiciones de prueba (mundo de pantalla). Dos actores se solapan para ver el orden por `z`.
	static constexpr eng::u8 kNActors = 4u;
	// Los actores 0 y 1 arrancan solapados y van juntos: se ve quien queda DELANTE (mayor `z`).
	s16 m_x[kNActors] = {48, 56, 216, 112};
	s16 m_y[kNActors] = {64, 56, 168, 112};
	s16 m_dx[kNActors] = {1, 1, 1, -1};
	s16 m_dy[kNActors] = {1, 1, -1, -1};
	static constexpr eng::u8 kFramesAnim[] = {0u, 1u};
	static constexpr eng::u8 kDurAnim[] = {15u, 15u};

	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);
		auto& chip = app.device().memory_manager().chip();
		m_sheet_a = chip.template reserve<eng::BobTag>(kSheetBytes, 16u);
		m_sheet_b = chip.template reserve<eng::BobTag>(kSheetBytes, 16u);
		if (!m_sheet_a.valid() || !m_sheet_b.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021501u);
			return;
		}
		// Hoja A: color 3 (planos 0 y 1). Hoja B: color 1 (solo plano 0).
		build_sheet(m_sheet_a.view.data(), 0b11u); // ambos planos -> color 3 (amarillo)
		build_sheet(m_sheet_b.view.data(), 0b01u); // solo plano 0 -> color 1 (rojo)
		m_sprite_a = make_sprite(m_sheet_a.mem_view_chip());
		m_sprite_b = make_sprite(m_sheet_b.mem_view_chip());

		m_bobs.set_sheet(0u, m_sprite_a);
		m_bobs.set_sheet(1u, m_sprite_b);
		m_bobs.resize(kNActors);
		for (eng::u8 i = 0u; i < kNActors; ++i) {
			eng::scene::BobActor& a = m_bobs[i];
			a.x = m_x[i];
			a.y = m_y[i];
			a.z = (i == 1u) ? 200u : 100u; // el actor 1 va DELANTE (mayor z)
			a.sheet_index = (i % 2u == 0u) ? 0u : 1u;
			a.anim.frames = eng::Span<const eng::u8> {kFramesAnim, 2u};
			a.anim.durations = eng::Span<const eng::u8> {kDurAnim, 2u};
			a.anim.loop = true;
		}
	}

	void update(auto& app) {
		eng::debug::mark_frame(g_eng_run_status, app.frame());
		for (eng::u8 i = 0u; i < kNActors; ++i) {
			m_x[i] = static_cast<s16>(m_x[i] + m_dx[i]);
			m_y[i] = static_cast<s16>(m_y[i] + m_dy[i]);
			if (m_x[i] < 0 || m_x[i] > static_cast<s16>(kWidth - kObjW)) m_dx[i] = static_cast<s16>(-m_dx[i]);
			if (m_y[i] < 0 || m_y[i] > static_cast<s16>(kHeight - kObjH)) m_dy[i] = static_cast<s16>(-m_dy[i]);
			m_bobs[i].x = m_x[i];
			m_bobs[i].y = m_y[i];
		}
		m_bobs.tick(); // avanza la animacion ligada de cada actor
	}

	void render(auto& app) {
		auto s = app.screen();
		s.fill_box(eng::Box {0, 0, kWidth, kHeight}, 0u); // fondo (color 0)
		s.bobs(m_bobs);                                   // capa de BOBs ordenada por `z`
		app.present();
		// READY tras varios frames completos: el primer render puede caer antes de que la captura
		// vea un buffer ya publicado (mismo criterio que la 214).
		if (app.frame() >= 4u) eng::debug::mark_ready(g_eng_run_status, 0u);
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}

private:
	[[nodiscard]] static graphics::Sprite make_sprite(eng::ChipView<eng::BobTag> sheet) {
		graphics::Bob b {};
		b.sheet = sheet;
		b.width = kObjW;
		b.height = kObjH;
		b.planes = kObjPlanes;
		b.frame_count = kFrames;
		b.frame_stride = kFrameStride;
		b.layout = graphics::BobLayout::Planar;
		b.draw = graphics::BobDraw::Opaque; // D = A: sin mascara.
		b.erase = graphics::BobErase::None;
		return graphics::Sprite {b, kSheetBytes, 0u};
	}

	/// Frame `f` = cuadro relleno centrado de lado `side`; `planes_mask` elige que planos se pintan.
	void build_sheet(eng::u8* sheet, eng::u8 planes_mask) {
		for (u32 i = 0u; i < kSheetBytes; ++i) sheet[i] = 0u;
		for (u16 f = 0u; f < kFrames; ++f) {
			const u16 side = (f == 0u) ? 14u : 8u; // frame 0 grande, frame 1 pequeno (animacion)
			const u16 x0 = static_cast<u16>(8u - side / 2u);
			const u16 y0 = static_cast<u16>(8u - side / 2u);
			for (eng::u8 p = 0u; p < kObjPlanes; ++p) {
				if ((planes_mask & (1u << p)) == 0u) continue;
				for (u16 yy = 0u; yy < side; ++yy) {
					for (u16 xx = 0u; xx < side; ++xx) {
						const u16 x = static_cast<u16>(x0 + xx);
						const u16 y = static_cast<u16>(y0 + yy);
						const u32 row = static_cast<u32>(f) * kFrameStride +
								static_cast<u32>(p) * kPlaneBytes +
								static_cast<u32>(y) * kRowBytes;
						sheet[row + (x >> 3u)] =
							static_cast<eng::u8>(sheet[row + (x >> 3u)] |
									     (0x80u >> (x & 7u)));
					}
				}
			}
		}
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	if (!backend.configure_game_memory()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00021502u);
		return 0;
	}
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = kPlanes;
	display.buffers = 2u;              // doble buffer: sin tearing mientras el Blitter dibuja
	display.palette.color[0] = 0x013u; // fondo oscuro
	display.palette.color[1] = 0xf00u; // rojo
	display.palette.color[2] = 0x0f0u; // verde
	display.palette.color[3] = 0xff0u; // amarillo (planos 0+1)

	BobsDemo game {};
	eng::App app {backend, game, backend.memory_manager()};
	if (!app.set_display(display) || !app.start()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00021503u);
		return 0;
	}
	app.run(0xffffu);
	return 0;
}
