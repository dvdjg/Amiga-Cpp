// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/084_mf_rotation --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/084_mf_rotation --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/084_mf_rotation --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/084_mf_rotation --keep-running

// Demo 084 - Cubo 3D con rotacion en MiniFloat16 (minifloat_math) sobre EHB.
//
// Objetivo: validar EN HARDWARE (68000, sin soft-float) la cadena
//   minifloat_math (sin/cos, exp, sqrt)  ->  Mat<3, MiniFloat16>  ->
//   eng/retro/minifloat_fixed (transform de coordenadas q0)  ->  Bresenham
// dibujando un cubo alambre que gira a 50 fps. La demo hace un **self-test de los
// trascendentes** en init (sin(pi/2), exp(0), sqrt(4)): si fallara en m68k la demo
// iria a Failed en vez de Ready. Publica en el periferico de depuracion los ciclos
// EMULADOS del calculo por frame (counter 0 = total, 1 = matriz).
#include <eng/core/math/minifloat_math.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/api/api.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/retro/minifloat_fixed.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

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
namespace em = eng::math;
using MF = em::MiniFloat16;
using V3 = em::Vec<3, eng::retro::q0>;
using Periph = eng::debug::DebugPeripheral;

constexpr eng::u16 kWidth = 320;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 6;

/// Escena EHB 320x256 (6 planos) sobre `scene::compose`: el perfil y el presupuesto se
/// validan en compilacion.
constexpr scene::SceneResources kRes = scene::planar(kWidth, kHeight, kPlanes);
static_assert(scene::valid_scene(kRes, scene::ocs_a500), "084: EHB 320x256 en A500");

/// Fondo azul oscuro; 1..7 rampa del cubo (azul -> cian -> dorado -> blanco, por
/// profundidad); 8 marco; 9/10 estrellas.
constexpr eng::Palette32 kPalette {{
	0x013, 0x024, 0x02F, 0x05F, 0x0AF, 0xFF4, 0xFF9, 0xFFF,
	0x112, 0x024, 0x011, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

constexpr eng::s16 kR = 54;
const V3 kVerts[8] = {
	{{eng::retro::q0 {-kR}, eng::retro::q0 {-kR}, eng::retro::q0 {-kR}}},
	{{eng::retro::q0 {kR}, eng::retro::q0 {-kR}, eng::retro::q0 {-kR}}},
	{{eng::retro::q0 {kR}, eng::retro::q0 {kR}, eng::retro::q0 {-kR}}},
	{{eng::retro::q0 {-kR}, eng::retro::q0 {kR}, eng::retro::q0 {-kR}}},
	{{eng::retro::q0 {-kR}, eng::retro::q0 {-kR}, eng::retro::q0 {kR}}},
	{{eng::retro::q0 {kR}, eng::retro::q0 {-kR}, eng::retro::q0 {kR}}},
	{{eng::retro::q0 {kR}, eng::retro::q0 {kR}, eng::retro::q0 {kR}}},
	{{eng::retro::q0 {-kR}, eng::retro::q0 {kR}, eng::retro::q0 {kR}}},
};
constexpr eng::u16 kEdges[12][2] = {
	{0, 1}, {1, 2}, {2, 3}, {3, 0},
	{4, 5}, {5, 6}, {6, 7}, {7, 4},
	{0, 4}, {1, 5}, {2, 6}, {3, 7},
};

constexpr eng::s32 kCX = 160;
constexpr eng::s32 kCY = 118;
constexpr eng::s32 kHalfSpan = 84;

/// Brillo por profundidad (media de z de los dos extremos), 7 colores.
eng::u8 shade_of(eng::s16 z0, eng::s16 z1) {
	const eng::s16 z = static_cast<eng::s16>(z0 + z1);
	if (z > 60) return 7;
	if (z > 30) return 6;
	if (z > 0)  return 5;
	if (z > -30) return 4;
	if (z > -60) return 3;
	return 2;
}

struct DemoGame {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		Periph::counter_name(0, reinterpret_cast<eng::u32>("mf_calc_cycles"));
		Periph::counter_name(1, reinterpret_cast<eng::u32>("mf_matrix_cycles"));
		m_memory_ok = backend.configure_memory({70u * 1024u, 8u * 1024u, 4u * 1024u});
		m_scene_ok = m_memory_ok &&
			     scene::compose(m_scene, backend.memory(), kRes, scene::ocs_a500,
					    scene::display(scene::kPal320x256, scene::kBplcon0_Ehb),
					    scene::palette(kPalette, 0u, 32u));
		if (!m_scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008401u);
			return;
		}
		backend.install_raster(m_scene); // Blitter/CPU según las caps del backend
		draw_static();
		{
			field::Surface c = m_scene.surface();
			compute_projection();
			draw_cube(c); // un frame ya pintado antes de tomar el display
		}
		m_scene.takeover(backend);
		// Self-test de los trascendentes EN HARDWARE.
		if (!verify_math()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008402u);
			return;
		}
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_scene.words()));
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		compute_projection(); // matematica FUERA del vblank
		(void)backend;        // la lista es estatica: `takeover` ya la instalo
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_scene.ok()) return;
		(void)backend;
		field::Surface c = m_scene.surface();
		draw_cube(c); // solo traza (rapido) durante el vblank
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Rota el cubo un paso y proyecta los 8 vertices a coordenadas de pantalla (q0);
	/// se hace en `update` para que `render` (vblank) solo tenga que trazar lineas.
	void compute_projection() {
		// Telemetria: ciclos EMULADOS (periferico de depuracion) del calculo MF por
		// frame. counter 1 = matriz (6 sin/cos + 2 Mat*Mat); counter 0 = total.
		const eng::u32 ta = Periph::cycle_counter();
		m_ax = em::wrap_angle(m_ax + MF(0.021f));
		m_ay = em::wrap_angle(m_ay + MF(0.013f));
		m_az = em::wrap_angle(m_az + MF(0.007f));
		const em::Mat<3, MF> m = rot_z(m_az) * rot_y(m_ay) * rot_x(m_ax);
		const eng::u32 tb = Periph::cycle_counter();
		const eng::retro::RatioMat<3> mq = eng::retro::prepare_ratio(m); // convertir 1x
		for (int i = 0; i < 8; ++i) {
			const auto r = eng::retro::transform(mq, kVerts[i]);
			m_scr[i][0] = static_cast<eng::s16>(kCX + r.v[0].v);
			m_scr[i][1] = static_cast<eng::s16>(kCY - r.v[1].v);
			m_wz[i] = r.v[2].v;
		}
		const eng::u32 tc = Periph::cycle_counter();
		Periph::counter_value(0, tc - ta);
		Periph::counter_value(1, tb - ta);
	}

	void draw_cube(field::Surface& c) {
		// Borra SOLO las aristas del frame anterior (no un rectangulo): el display nunca
		// queda vacio a mitad de frame (un `clear_rect` hacia que la captura cogiera el
		// hueco) y se ahorra escribir toda la zona.
		if (m_have_prev) {
			for (const auto& e : kEdges) {
				c.draw_line(m_prev[e[0]][0], m_prev[e[0]][1], m_prev[e[1]][0], m_prev[e[1]][1], 0);
			}
		}
		for (const auto& e : kEdges) {
			c.draw_line(m_scr[e[0]][0], m_scr[e[0]][1], m_scr[e[1]][0], m_scr[e[1]][1],
			       shade_of(m_wz[e[0]], m_wz[e[1]]));
		}
		for (int i = 0; i < 8; ++i) {
			m_prev[i][0] = m_scr[i][0];
			m_prev[i][1] = m_scr[i][1];
		}
		m_have_prev = true;
	}

	static em::Mat<3, MF> rot_x(MF a) {
		MF s, c;
		em::sincos(a, s, c); // una sola reduccion para seno y coseno
		return em::Mat<3, MF> {{{MF::one(), MF::zero(), MF::zero()},
					{MF::zero(), c, -s},
					{MF::zero(), s, c}}};
	}
	static em::Mat<3, MF> rot_y(MF a) {
		MF s, c;
		em::sincos(a, s, c);
		return em::Mat<3, MF> {{{c, MF::zero(), s},
					{MF::zero(), MF::one(), MF::zero()},
					{-s, MF::zero(), c}}};
	}
	static em::Mat<3, MF> rot_z(MF a) {
		MF s, c;
		em::sincos(a, s, c);
		return em::Mat<3, MF> {{{c, -s, MF::zero()},
					{s, c, MF::zero()},
					{MF::zero(), MF::zero(), MF::one()}}};
	}

	/// Comprueba en hardware los trascendentes clave (si fallan, no hay Ready).
	static bool verify_math() {
		const float s = static_cast<float>(em::sin(MF(1.5707963f)));
		const float e = static_cast<float>(em::exp(MF::zero()));
		const float q = static_cast<float>(em::sqrt(MF(4.0f)));
		return s > 0.997f && s < 1.003f && e > 0.999f && e < 1.001f && q > 1.998f && q < 2.002f;
	}

	void draw_static() {
		field::Surface c = m_scene.surface();
		for (eng::s32 i = 0; i < 2; ++i) {
			const eng::s32 x0 = 6 + i * 4, y0 = 6 + i * 4;
			const eng::s32 x1 = static_cast<eng::s32>(kWidth) - 7 - i * 4;
			const eng::s32 y1 = static_cast<eng::s32>(kHeight) - 7 - i * 4;
			c.draw_line(x0, y0, x1, y0, 8);
			c.draw_line(x1, y0, x1, y1, 8);
			c.draw_line(x1, y1, x0, y1, 8);
			c.draw_line(x0, y1, x0, y0, 8);
		}
		eng::u16 s = 0xace1u;
		for (eng::u16 i = 0; i < 140; ++i) {
			s = static_cast<eng::u16>(static_cast<eng::u16>(s * 25173) + 13849);
			const eng::s32 x = 12 + (s & 0xffu);
			s = static_cast<eng::u16>(static_cast<eng::u16>(s * 25173) + 13849);
			const eng::s32 y = 12 + (s & 0xffu);
			if (x > kCX - kHalfSpan - 4 && x < kCX + kHalfSpan + 4 &&
			    y > kCY - kHalfSpan - 4 && y < kCY + kHalfSpan + 4) {
				continue;
			}
			c.set_pixel(x, y, (i & 1u) ? 9 : 10);
		}
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	scene::Scene m_scene {};
	MF m_ax {0.0f}, m_ay {0.0f}, m_az {0.0f};
	eng::s16 m_scr[8][2] {};
	eng::s16 m_wz[8] {};
	eng::s16 m_prev[8][2] {};
	bool m_have_prev = false;
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
