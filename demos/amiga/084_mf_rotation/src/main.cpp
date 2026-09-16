// Demo 084 - Cubo 3D con rotacion en MiniFloat16 (minifloat_math) sobre EHB.
//
// Objetivo: validar EN HARDWARE (68000, sin soft-float) la cadena
//   minifloat_math (sin/cos, exp, sqrt)  ->  Mat<3, MiniFloat16>  ->
//   eng/retro/minifloat_fixed (transform de coordenadas q0)  ->  Bresenham
// dibujando un cubo alambre que gira a 50 fps. La demo hace un **self-test de los
// trascendentes** en init (sin(pi/2), exp(0), sqrt(4)): si fallara en m68k la demo
// iria a Failed en vez de Ready.
#include <eng/core/minifloat_math.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/platform/amiga_minimal.hpp>
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

namespace ehb = eng::graphics::drivers;
namespace em = eng::math;
using MF = em::MiniFloat16;
using V3 = em::Vec<3, eng::retro::q0>;

constexpr eng::u16 kWidth = ehb::StaticEhbScene::width;
constexpr eng::u16 kHeight = ehb::StaticEhbScene::height;
constexpr eng::u16 kRowBytes = ehb::StaticEhbScene::bytes_per_row;
constexpr eng::u8 kPlanes = ehb::StaticEhbScene::plane_count;
constexpr eng::u32 kPlaneBytes = ehb::StaticEhbScene::plane_bytes;

/// Fondo azul oscuro; 1..7 rampa del cubo (azul -> cian -> amarillo -> blanco, por
/// profundidad); 8 marco; 9/10 estrellas.
constexpr ehb::EhbPalette kPalette {{
	0x013, 0x024, 0x02F, 0x05F, 0x0AF, 0x0FF, 0xFF6, 0xFFF,
	0x112, 0x024, 0x011, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

/// Canvas planar minimo (escritura directa a los 6 bitplanes EHB), igual que la 077.
struct Canvas {
	eng::u8* planes = nullptr;

	void px(eng::s32 x, eng::s32 y, eng::u8 color) {
		if (x < 0 || y < 0 || x >= static_cast<eng::s32>(kWidth) || y >= static_cast<eng::s32>(kHeight)) {
			return;
		}
		const eng::u32 off = static_cast<eng::u32>(y) * kRowBytes + static_cast<eng::u32>(x >> 3);
		const eng::u8 mask = static_cast<eng::u8>(0x80u >> (x & 7));
		const eng::u8 np = (color < 8u) ? 3u : kPlanes;
		for (eng::u8 p = 0; p < np; ++p) {
			eng::u8* base = planes + static_cast<eng::u32>(p) * kPlaneBytes;
			if (color & (1u << p)) {
				base[off] = static_cast<eng::u8>(base[off] | mask);
			} else {
				base[off] = static_cast<eng::u8>(base[off] & static_cast<eng::u8>(~mask));
			}
		}
	}

	void line(eng::s32 x0, eng::s32 y0, eng::s32 x1, eng::s32 y1, eng::u8 color) {
		const eng::s32 dx = x1 > x0 ? x1 - x0 : x0 - x1;
		const eng::s32 dy = y1 > y0 ? y1 - y0 : y0 - y1;
		const eng::s32 sx = x0 < x1 ? 1 : -1;
		const eng::s32 sy = y0 < y1 ? 1 : -1;
		eng::s32 err = dx - dy;
		for (;;) {
			px(x0, y0, color);
			if (x0 == x1 && y0 == y1) break;
			const eng::s32 e2 = 2 * err;
			if (e2 > -dy) { err -= dy; x0 += sx; }
			if (e2 < dx)  { err += dx; y0 += sy; }
		}
	}

	void clear_rect(eng::s32 x0, eng::s32 y0, eng::s32 x1, eng::s32 y1) {
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > static_cast<eng::s32>(kWidth) - 1) x1 = static_cast<eng::s32>(kWidth) - 1;
		if (y1 > static_cast<eng::s32>(kHeight) - 1) y1 = static_cast<eng::s32>(kHeight) - 1;
		if (x0 > x1 || y0 > y1) return;
		const eng::u32 bx0 = static_cast<eng::u32>(x0) >> 3;
		const eng::u32 bx1 = static_cast<eng::u32>(x1) >> 3;
		for (eng::u8 p = 0; p < kPlanes; ++p) {
			eng::u8* base = planes + static_cast<eng::u32>(p) * kPlaneBytes;
			for (eng::s32 y = y0; y <= y1; ++y) {
				eng::u8* row = base + static_cast<eng::u32>(y) * kRowBytes;
				eng::u32 i = bx0;
				for (; i <= bx1 && (i & 3u) != 0u; ++i) row[i] = 0u;
				eng::u32* lp = reinterpret_cast<eng::u32*>(row + i);
				for (; i + 4u <= bx1 + 1u; i += 4u) *lp++ = 0u;
				for (; i <= bx1; ++i) row[i] = 0u;
			}
		}
	}
};

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

/// Pliega un angulo `MF` (radianes) a `[-2pi, 2pi]`, el dominio fiable de `sin/cos`.
MF wrap_2pi(MF a) {
	const MF two_pi(6.2831853f);
	while (a > two_pi) a = a - two_pi;
	while (a < -two_pi) a = a + two_pi;
	return a;
}

/// Brillo por profundidad (media de z de los dos extremos), 7 colores.
eng::u8 shade_of(eng::s16 z0, eng::s16 z1) {
	const eng::s16 z = static_cast<eng::s16>(z0 + z1);
	if (z > 80) return 7;
	if (z > 40) return 6;
	if (z > 0)  return 5;
	if (z > -40) return 4;
	if (z > -80) return 3;
	return 2;
}

struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({70u * 1024u, 8u * 1024u, 4u * 1024u});
		const ehb::StaticEhbSceneConfig scene_config {&kPalette, nullptr, 0, 1024};
		m_scene_ok = m_scene.init(backend.memory(), scene_config);
		if (!(m_memory_ok && m_scene_ok)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008401u);
			return;
		}
		draw_static();
		{
			Canvas c {m_scene.bitplanes().data()};
			compute_projection();
			draw_cube(c); // un frame ya pintado antes de tomar el display
		}
		m_scene.takeover(backend);
		// Self-test de los trascendentes EN HARDWARE.
		if (!verify_math()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008402u);
			return;
		}
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_scene.copper_words()));
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		compute_projection(); // matematica FUERA del vblank
		if (m_scene.ok()) m_scene.install(backend);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_scene.ok()) return;
		Canvas c {m_scene.bitplanes().data()};
		draw_cube(c); // solo traza (rapido) durante el vblank
		m_scene.install(backend);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Rota el cubo un paso y proyecta los 8 vertices a coordenadas de pantalla (q0);
	/// se hace en `update` para que `render` (vblank) solo tenga que trazar lineas.
	void compute_projection() {
		m_ax = wrap_2pi(m_ax + MF(0.021f));
		m_ay = wrap_2pi(m_ay + MF(0.013f));
		m_az = wrap_2pi(m_az + MF(0.007f));
		const em::Mat<3, MF> m = rot_z(m_az) * rot_y(m_ay) * rot_x(m_ax);
		for (int i = 0; i < 8; ++i) {
			const auto r = eng::retro::transform(m, kVerts[i]);
			m_scr[i][0] = static_cast<eng::s16>(kCX + r.v[0].v);
			m_scr[i][1] = static_cast<eng::s16>(kCY - r.v[1].v);
			m_wz[i] = r.v[2].v;
		}
	}

	void draw_cube(Canvas& c) {
		c.clear_rect(kCX - kHalfSpan, kCY - kHalfSpan, kCX + kHalfSpan, kCY + kHalfSpan);
		for (const auto& e : kEdges) {
			c.line(m_scr[e[0]][0], m_scr[e[0]][1], m_scr[e[1]][0], m_scr[e[1]][1],
			       shade_of(m_wz[e[0]], m_wz[e[1]]));
		}
	}

	static em::Mat<3, MF> rot_x(MF a) {
		const MF c = em::cos(a), s = em::sin(a);
		return em::Mat<3, MF> {{{MF::one(), MF::zero(), MF::zero()},
					{MF::zero(), c, -s},
					{MF::zero(), s, c}}};
	}
	static em::Mat<3, MF> rot_y(MF a) {
		const MF c = em::cos(a), s = em::sin(a);
		return em::Mat<3, MF> {{{c, MF::zero(), s},
					{MF::zero(), MF::one(), MF::zero()},
					{-s, MF::zero(), c}}};
	}
	static em::Mat<3, MF> rot_z(MF a) {
		const MF c = em::cos(a), s = em::sin(a);
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
		Canvas c {m_scene.bitplanes().data()};
		for (eng::s32 i = 0; i < 2; ++i) {
			const eng::s32 x0 = 6 + i * 4, y0 = 6 + i * 4;
			const eng::s32 x1 = static_cast<eng::s32>(kWidth) - 7 - i * 4;
			const eng::s32 y1 = static_cast<eng::s32>(kHeight) - 7 - i * 4;
			c.line(x0, y0, x1, y0, 8);
			c.line(x1, y0, x1, y1, 8);
			c.line(x1, y1, x0, y1, 8);
			c.line(x0, y1, x0, y0, 8);
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
			c.px(x, y, (i & 1u) ? 9 : 10);
		}
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	ehb::StaticEhbScene m_scene {};
	MF m_ax {0.0f}, m_ay {0.0f}, m_az {0.0f};
	eng::s16 m_scr[8][2] {};
	eng::s16 m_wz[8] {};
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
