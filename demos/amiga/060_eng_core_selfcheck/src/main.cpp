// ============================================================================
// Demo 060: self-check de eng::core (isqrt, sort, crc32, random) en hardware.
// ============================================================================
//
// Valida en WinUAE los ports de libmisc/libc que cubre el test host HOST-000,
// dibujando el resultado en BITPLANES REALES (no solo overlay del depurador):
// la demo es visible en la ventana del Amiga normal (WinUAE, Coppenheimer,
// hardware real), porque el texto se rasteriza con una fuente 8x8 en la
// superficie EHB.
//
// Fases (las mismas que HOST-000) ejecutadas en init, resultado publicado en
// `g_eng_run_status.detail`:
//
//   0x060100FF  todas las fases OK (self-check completo)
//   0x06000201  isqrt fallo  ·  0x06000202  crc32 fallo
//   0x06000203  random fallo  ·  0x06000204  sort fallo
//
// En pantalla (bitplanes EHB): titulo, cuatro lineas con OK/FAIL a color, y un
// cartel SELF-CHECK: ALL PHASES OK / FAILED.
//
// Build/run/analyze:
//   tools/build/build-demo.sh demos/amiga/060_eng_core_selfcheck --clean
//   tools/run/run-demo.sh       demos/amiga/060_eng_core_selfcheck
//   tools/analyze/analyze-demo.sh demos/amiga/060_eng_core_selfcheck

#include <eng/core/crc32.hpp>
#include <eng/core/isqrt.hpp>
#include <eng/core/random.hpp>
#include <eng/core/sort.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/graphics/font8.hpp>
#include <eng/platform/amiga_minimal.hpp>

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

namespace drivers = eng::graphics::drivers;

constexpr eng::u16 kScreenW = drivers::StaticEhbScene::width;      // 320
constexpr eng::u16 kScreenH = drivers::StaticEhbScene::height;     // 256
constexpr eng::u16 kBytesPerRow = drivers::StaticEhbScene::bytes_per_row; // 40
constexpr eng::u8 kPlanes = drivers::StaticEhbScene::plane_count;  // 6
constexpr eng::u32 kPlaneBytes = drivers::StaticEhbScene::plane_bytes;

// Indices EHB (0..63). El 6.º plano suma 32 (half-brite). Usamos base 0..31 y
// texto en 31 (blanco) / 30 (amarillo), fondo 1 (azul).
constexpr eng::u8 kBgIndex = 1;
constexpr eng::u8 kTextWhite = 31;
constexpr eng::u8 kTextYellow = 30;
constexpr eng::u8 kTextFail = 26; // rojo

// Resultado del self-check.
struct SelfCheck {
	bool isqrt_ok = false;
	bool crc32_ok = false;
	bool random_ok = false;
	bool sort_ok = false;

	bool ok() const { return isqrt_ok && crc32_ok && random_ok && sort_ok; }
	eng::u32 detail() const {
		if (ok()) return 0x060100FFu;
		if (!isqrt_ok) return 0x06000201u;
		if (!crc32_ok) return 0x06000202u;
		if (!random_ok) return 0x06000203u;
		return 0x06000204u;
	}
};

SelfCheck g_check;

// --- Fase 1: isqrt ----------------------------------------------------------
void check_isqrt() {
	const struct { eng::u32 n; eng::u32 expect; } cases[] = {
		{ 0u, 0u }, { 1u, 1u }, { 4u, 2u }, { 9u, 2u }, { 16u, 4u },
		{ 100u, 10u }, { 32768u, 181u }, { 65535u, 253u }, { 65536u, 256u },
		{ 100000u, 313u }, { 1000000u, 999u }, { 2147483647u, 45977u },
	};
	g_check.isqrt_ok = true;
	for (const auto& c : cases) {
		if (eng::isqrt(c.n) != c.expect) {
			g_check.isqrt_ok = false;
			return;
		}
	}
}

// --- Fase 2: crc32 ----------------------------------------------------------
void check_crc32() {
	const eng::u8 b1[] = { '1','2','3','4','5','6','7','8','9' };
	const eng::u8 b2[] = { 0x00 };
	const eng::u8 b3[] = { 0x00, 0x00, 0x00, 0x00 };
	const eng::u8 b4[] = { 'a','b','c','d','e','f','g','h','i','j','k','l','m',
	                       'n','o','p','q','r','s','t','u','v','w','x','y','z' };
	const eng::u8 b5[] = { 0x41 };
	const struct { const eng::u8* data; eng::u32 len; eng::u32 expect; } cases[] = {
		{ b1, sizeof(b1), 0xCBF43926u },
		{ b2, sizeof(b2), 0xD202EF8Du },
		{ b3, sizeof(b3), 0x2144DF1Cu },
		{ b4, sizeof(b4), 0x4C2750BDu },
		{ b5, sizeof(b5), 0xD3D99E8Bu },
	};
	g_check.crc32_ok = true;
	for (const auto& c : cases) {
		if (eng::crc32(c.data, c.len) != c.expect) {
			g_check.crc32_ok = false;
			return;
		}
	}
}

// --- Fase 3: random ---------------------------------------------------------
void check_random() {
	eng::Xoroshiro64pp rng{1u, 0u};
	const eng::u32 expect[] = {
		0x48020a01u, 0x81662931u, 0xcd2b5253u, 0xd3e6cbe6u, 0xcd5af43du,
		0x860aa4bau, 0xb7bea7fbu, 0x63dcaff3u, 0x762d74c9u, 0x3e7d7e8fu,
	};
	g_check.random_ok = true;
	for (const eng::u32 e : expect) {
		if (rng.next() != e) {
			g_check.random_ok = false;
			return;
		}
	}
}

// --- Fase 4: sort -----------------------------------------------------------
void check_sort() {
	eng::s32 a[] = { 9, 4, 7, 1, 5, 3, 8, 2, 6, 0, 10, -3 };
	const eng::s32 n = static_cast<eng::s32>(sizeof(a) / sizeof(a[0]));
	eng::quick_sort(eng::Span<eng::s32>{a}, [](eng::s32 x, eng::s32 y) {
		return x <= y;
	});
	g_check.sort_ok = true;
	for (eng::s32 i = 1; i < n; ++i) {
		if (a[i - 1] > a[i]) {
			g_check.sort_ok = false;
			return;
		}
	}
	if (a[0] != -3 || a[n - 1] != 10) {
		g_check.sort_ok = false;
	}
}

// --- Rasterizacion de texto en bitplanes EHB -------------------------------
// `color_index` es el indice EHB 0..63. La fuente 8x8 esta en formato FILAS
// (byte r = fila r, bit k = pixel en la columna k desde la izquierda). Para
// cada fila del glifo, recorremos los 8 bits y encendemos el pixel (x+k, y+r),
// poniendo el bit correspondiente en los planos del color.
void draw_text(eng::u8* planes, eng::u16 x, eng::u16 y, const char* text, eng::u8 color_index) {
	while (*text) {
		const char ch = *text++;
		if (ch >= 32) {
			for (eng::u8 row = 0; row < eng::Font8::kRows; ++row) {
				const eng::u8 glyph_row = eng::Font8::row(static_cast<eng::u16>(ch), row);
				if (glyph_row == 0) {
					continue;
				}
				const eng::u16 py = static_cast<eng::u16>(y + row);
				for (eng::u8 k = 0; k < 8u; ++k) {
					if ((glyph_row & (1u << k)) == 0) {
						continue;
					}
					const eng::u16 px = static_cast<eng::u16>(x + k);
					const eng::u32 base = static_cast<eng::u32>(py) * kBytesPerRow + (px / 8u);
					const eng::u8 bit = static_cast<eng::u8>(0x80u >> (px & 7u));
					for (eng::u8 plane = 0; plane < kPlanes; ++plane) {
						if (color_index & (1u << plane)) {
							planes[static_cast<eng::u32>(plane) * kPlaneBytes + base] |= bit;
						}
					}
				}
			}
		}
		x = static_cast<eng::u16>(x + 8);
	}
}

const char* ok_fail(bool ok) {
	return ok ? "OK" : "FAIL";
}

struct CoreSelfcheckDemo {
	static consteval int language_level_marker() { return 23; }

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		static_assert(language_level_marker() == 23);

		m_memory_ok = backend.configure_memory({
			80u * 1024u, // Chip: bitplanes EHB + copperlist
			8u * 1024u,  // Slow: metadatos
			4u * 1024u,  // Frame scratch
		});

		const drivers::EhbPalette palette {
			// 0 negro, 1 azul fondo, 26 rojo, 30 amarillo, 31 blanco.
			0x000, 0x088, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
			0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
			0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
			0x000, 0x000, 0xf00, 0x000, 0x000, 0x000, 0xff0, 0xfff,
		};
		const drivers::StaticEhbSceneConfig scene_config {
			&palette, nullptr, 0, 1024,
		};

		m_scene_ok = m_scene.init(backend.memory(), scene_config);
		if (!m_memory_ok || !m_scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000050u);
			return;
		}

		// Ejecuta las cuatro fases.
		check_isqrt();
		check_crc32();
		check_random();
		check_sort();

		// Dibuja en bitplanes reales (indices EHB).
		eng::u8* planes = m_scene.bitplanes();
		draw_text(planes, 16, 16, "Demo 060 - eng::core self-check", kTextWhite);
		draw_text(planes, 16, 40, "isqrt  : ", kTextWhite);
		draw_text(planes, 100, 40, ok_fail(g_check.isqrt_ok),
		          g_check.isqrt_ok ? kTextYellow : kTextFail);
		draw_text(planes, 16, 56, "crc32  : ", kTextWhite);
		draw_text(planes, 100, 56, ok_fail(g_check.crc32_ok),
		          g_check.crc32_ok ? kTextYellow : kTextFail);
		draw_text(planes, 16, 72, "random : ", kTextWhite);
		draw_text(planes, 100, 72, ok_fail(g_check.random_ok),
		          g_check.random_ok ? kTextYellow : kTextFail);
		draw_text(planes, 16, 88, "sort   : ", kTextWhite);
		draw_text(planes, 100, 88, ok_fail(g_check.sort_ok),
		          g_check.sort_ok ? kTextYellow : kTextFail);
		draw_text(planes, 16, 120,
		          g_check.ok() ? "SELF-CHECK: ALL PHASES OK" : "SELF-CHECK: FAILED",
		          g_check.ok() ? kTextYellow : kTextFail);
		draw_text(planes, 16, 240, "eng::core ports from libmisc/libc", 0x1a);

		// Publica el resultado.
		if (g_check.ok()) {
			eng::debug::mark_ready(g_eng_run_status, g_check.detail());
		} else {
			eng::debug::mark_failed(g_eng_run_status, g_check.detail());
		}
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		(void)backend;
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		// La superficie EHB se muestra desde init (0-bit no, 6 planos). El
		// contenido ya está escrito; solo hay que asegurar la copperlist. "commit"
		// del engine ocurre en render (ver AGENTS: update->wait_vblank->render).
		m_scene.install(backend);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	drivers::StaticEhbScene m_scene {};
	bool m_memory_ok = false;
	bool m_scene_ok = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	CoreSelfcheckDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}