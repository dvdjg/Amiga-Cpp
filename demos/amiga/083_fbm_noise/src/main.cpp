// Demo 083 - fbm noise (value noise + fbm de eng/core/noise.hpp).
//
// Muestra el ruido procedural como **mapa de altura animado** sobre un display copper
// chunky 288x256 (36x64 bloques de 8x4): la CPU reescribe los COLOR00 de la rejilla y
// el Copper los pinta por bloques (sin bitplanes).
//
// Técnica que enseña: un campo **fbm 2D** (suma de octavas de value noise) genera un
// relieve que no se puede dibujar con formas sencillas. Para que quepa en el presupuesto
// del 68000, el fbm se evalua una vez en una rejilla GRUESA (16x16) y por frame solo se
// muestrea bilinealmente con un offset animado: el coste por frame es un muestreo, no
// miles de evaluaciones de ruido. La paleta codifica agua/arena/verde/roca/nieve.
#include <eng/core/minifloat_math.hpp>
#include <eng/core/noise.hpp>
#include <eng/api/api.hpp>
#include <eng/graphics/composition/copper_chunky.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

// Numero de buffers de display: 1 = sin doble buffer, 2 = doble, 3 = triple.
// Configurable sin tocar codigo: EXTRA_DEFINES="-DK_083_BUFFERS=1".
#ifndef K_083_BUFFERS
#define K_083_BUFFERS 2
#endif
static_assert(K_083_BUFFERS >= 1 && K_083_BUFFERS <= 4, "K_083_BUFFERS fuera de rango");

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

namespace amiga = eng::amiga;
namespace scene = eng::graphics::composition;
namespace comp = eng::graphics::composition;
using eng::u8;
using eng::u16;
using eng::s16;
using eng::s32;
using MF = eng::math::MiniFloat16;

constexpr u8 kCols = 36;
constexpr u8 kRows = 64;
constexpr int kGW = 16; // rejilla gruesa del campo fbm
constexpr int kGH = 16;

/// MiniFloat16 -> entero truncado [0,255] sin float (parte entera del formato). Se usa
/// con el valor ya escalado a [0,256), así que `floor(x)` es el índice de paleta.
u8 mf_to_u8(MF x) {
	const int e = static_cast<int>((x.raw >> 10) & 31) - MF::bias; // x = m·2^(e-10)
	if (e < 0) return 0;
	const int m = 0x400 | (x.raw & eng::math::MiniFloat16::man_mask);
	const int sh = e - 10;
	const int v = (sh >= 0) ? (m << sh) : (m >> (-sh));
	return static_cast<u8>(v > 255 ? 255 : v);
}

struct FbmDemo {
	void init(amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({64u * 1024u, 8u * 1024u, 2u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008301u);
			return;
		}
		comp::CopperChunkyConfig cfg {};
		cfg.cols = kCols;
		cfg.rows = kRows;
		// Escena copper chunky (sin bitplanes); doble buffer por el `copper::Plan`.
		scene::SceneResources res = scene::planar(288u, 256u, 0u);
		res.mode = scene::SceneMode::CopperChunky;
		res.copper_bytes = 12288u;
		if (!scene::compose(m_scene, backend.memory(), res, scene::ocs_a500)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008302u);
			return;
		}
		if (!m_layer.init(cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008303u);
			return;
		}
		build_coarse();
		build_palette();

		// Estructura de la lista UNA vez en AMBOS bloques del `Plan`; por frame solo se
		// parchean los colores (no se re-emite la lista).
		m_scene.begin_build();
		m_layer.emit(m_scene.scheduler());
		m_scene.end_build();
		m_scene.begin_build();
		m_layer.emit(m_scene.scheduler());
		m_scene.end_build();
		fill_colors(m_scene.active_words());
		m_scene.takeover(backend);
		m_init_ok = true;
		eng::debug::mark_ready(g_eng_run_status, 0x0083u);
	}

	void update(amiga::MinimalBackend& backend, eng::GameContext&) {
		if (!m_init_ok) return;
		// Offset de muestreo animado (Q8 de celda gruesa) en ping-pong, sin costura.
		m_ox += m_dx;
		if (m_ox >= (3 << 8)) { m_ox = 3 << 8; m_dx = -1; }
		else if (m_ox <= 0) { m_ox = 0; m_dx = 1; }
		m_oy += m_dy;
		if (m_oy >= (3 << 8)) { m_oy = 3 << 8; m_dy = -1; }
		else if (m_oy <= 0) { m_oy = 0; m_dy = 1; }

		fill_colors(m_scene.inactive_words());
		m_scene.flip_copper();
		m_scene.present(backend);
	}

	void render(amiga::MinimalBackend& backend, eng::GameContext& context) {
		(void)backend;
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	void build_coarse() {
		// fbm 2D de 3 octavas sobre la rejilla gruesa; coordenadas/constantes sin float.
		const MF kStep(0.25f);
		const MF kScale(256.0f);
		for (int j = 0; j < kGH; ++j) {
			const MF fy = eng::math::scalar_traits<MF>::from_int(j) * kStep;
			for (int i = 0; i < kGW; ++i) {
				const MF fx = eng::math::scalar_traits<MF>::from_int(i) * kStep;
				const MF n = eng::math::fbm2(fx, fy, 0x1234u, 3, MF(2.0f), MF(0.5f));
				m_coarse[j * kGW + i] = mf_to_u8(n * kScale);
			}
		}
	}

	void build_palette() {
		// Paradas de color (RGB12) por altura: agua, orilla, verde, roca, nieve.
		struct Stop {
			int at;
			u16 rgb;
		};
		constexpr Stop stops[] = {
			{0, 0x012u},   // azul profundo
			{80, 0x048u},  // azul
			{110, 0x8C0u}, // arena
			{150, 0x280u}, // verde
			{200, 0x630u}, // roca
			{255, 0xFFFu}, // nieve
		};
		for (int v = 0; v < 256; ++v) {
			int k = 0;
			while (k < 4 && v > stops[k + 1].at) ++k;
			const Stop& a = stops[k];
			const Stop& b = stops[k + 1];
			const int span = (b.at - a.at) == 0 ? 1 : (b.at - a.at);
			const int t = ((v - a.at) * 256) / span;
			m_palette[v] = lerp_rgb(a.rgb, b.rgb, t);
		}
	}

	static u16 lerp_rgb(u16 a, u16 b, int t) {
		const int r = (((a >> 8) & 0xF) * (256 - t) + ((b >> 8) & 0xF) * t) >> 8;
		const int g = (((a >> 4) & 0xF) * (256 - t) + ((b >> 4) & 0xF) * t) >> 8;
		const int bl = ((a & 0xF) * (256 - t) + (b & 0xF) * t) >> 8;
		return static_cast<u16>((r << 8) | (g << 4) | bl);
	}

	u8 coarse_at(int gx, int gy) const {
		if (gx < 0) gx = 0;
		if (gx >= kGW) gx = kGW - 1;
		if (gy < 0) gy = 0;
		if (gy >= kGH) gy = kGH - 1;
		return m_coarse[gy * kGW + gx];
	}

	/// Escribe los colores del campo en el bloque `base` (coste: `cols*rows` words).
	void fill_colors(const eng::u16* base) {
		// Escalas de muestreo de la rejilla gruesa a la rejilla de bloques (Q8).
		constexpr int kStepX = (kGW << 8) / kCols;
		constexpr int kStepY = (kGH << 8) / kRows;
		for (u8 y = 0; y < kRows; ++y) {
			u16* p = m_layer.row(base, y);
			if (p == nullptr) {
				return;
			}
			const s32 cyq = static_cast<s32>(y) * kStepY + m_oy;
			const int gy = cyq >> 8;
			const int fy = cyq & 0xFF;
			for (u8 x = 0; x < kCols; ++x) {
				const s32 cxq = static_cast<s32>(x) * kStepX + m_ox;
				const int gx = cxq >> 8;
				const int fx = cxq & 0xFF;
				// Bilineal con índices acotados a la rejilla.
				const int v00 = coarse_at(gx, gy);
				const int v10 = coarse_at(gx + 1, gy);
				const int v01 = coarse_at(gx, gy + 1);
				const int v11 = coarse_at(gx + 1, gy + 1);
				const int a = v00 + (((v10 - v00) * fx) >> 8);
				const int b = v01 + (((v11 - v01) * fx) >> 8);
				const int v = a + (((b - a) * fy) >> 8);
				*p = m_palette[static_cast<u8>(v)];
				p += 2; // cada COLOR00 son [registro, data]
			}
		}
	}

	scene::Scene m_scene {};
	comp::CopperChunkyLayer<kCols, kRows> m_layer {};
	u8 m_coarse[kGW * kGH] {};
	u16 m_palette[256] {};
	s32 m_ox = 0, m_oy = 0;
	s16 m_dx = 1, m_dy = 1;
	bool m_init_ok = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	amiga::MinimalBackend backend {};
	FbmDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffff);

	return 0;
}
