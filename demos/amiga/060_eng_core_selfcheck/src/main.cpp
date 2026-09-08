// ============================================================================
// Demo 060: self-check de eng::core (isqrt, sort, crc32, random) en hardware.
// ============================================================================
//
// Valida en WinUAE los ports de libmisc/libc que cubre el test host HOST-000,
// dibujando el resultado en BITPLANES REALES visibles en la ventana del Amiga
// (no solo overlay del depurador).
//
// Display: `StaticEhbScene` (320x256, 6 planos EHB) — el mismo driver probado
// de las demos 030/050. El texto se rasteriza por CPU con la fuente `Font8`
// (LATIN-1) en el formato filas bit0=izquierda, coherente con el raster host.
//
// Para juegos, el texto debe ir por la API de contexto de dispositivo:
// `Surface::draw_text`/`draw_text_literal` (ver demo 201). Esta demo es un
// self-check de algoritmos, por eso escribe bitplanes directamente.
//
// Fases (las mismas que HOST-000) ejecutadas en init, resultado en
// `g_eng_run_status.detail`:
//
//   0x060100FF  todas las fases OK (self-check completo)
//   0x06000201  isqrt fallo  ·  0x06000202  crc32 fallo
//   0x06000203  random fallo  ·  0x06000204  sort fallo
//
// Build (Windows nativo):
//   bash tools/build/build-demo.sh demos/amiga/060_eng_core_selfcheck --clean
//   <Node de Windows> dist/tools/run/run-demo.js demos\amiga\060_eng_core_selfcheck --warp

#include <eng/core/crc32.hpp>
#include <eng/core/isqrt.hpp>
#include <eng/core/random.hpp>
#include <eng/core/sort.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/utf8.hpp>
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

constexpr eng::u16 kScreenW = drivers::StaticEhbScene::width;
constexpr eng::u16 kScreenH = drivers::StaticEhbScene::height;
constexpr eng::u16 kBytesPerRow = drivers::StaticEhbScene::bytes_per_row;
constexpr eng::u8 kPlanes = drivers::StaticEhbScene::plane_count;
constexpr eng::u32 kPlaneBytes = drivers::StaticEhbScene::plane_bytes;

// Índices EHB (0..63): fondo azul 1, texto blanco 31, amarillo 30, rojo 26,
// cian 20 (pie). Con EHB el 6.º plano suma 32 (half-brite); los índices aquí
// son de la paleta base 0..31.
constexpr eng::u8 kBgIndex = 1;
constexpr eng::u8 kTextWhite = 31;
constexpr eng::u8 kTextYellow = 30;
constexpr eng::u8 kTextFail = 26;

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
			0x000, 0x06a, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
			0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
			0x000, 0x000, 0x000, 0x000, 0x0aa, 0x000, 0xf00, 0x000,
			0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0xff0, 0xfff,
		};
		const drivers::StaticEhbSceneConfig scene_config {
			&palette, nullptr, 0, 1024,
		};

		m_scene_ok = m_scene.init(backend.memory(), scene_config);
		if (!m_memory_ok || !m_scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000050u);
			return;
		}

		// Ejecuta las fases y dibuja el resultado en los bitplanes.
		check_isqrt();
		check_crc32();
		check_random();
		check_sort();

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
		draw_text(planes, 16, 240, "optica: eng::core ports", 20);

		// Toma el control del display una sola vez (apaga interrupciones/DMA
		// del sistema y arranca la primera copperlist alineada al VBL). La
		// pantalla es estatica, asi que no hay swaps por frame.
		backend.takeover_display(m_scene.copper_words_ptr());

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
		// Instalar la copperlist UNA sola vez (en init). Reinstalarla en cada
		// frame hace COPJMP1 (reinicio del Copper) y puede producir un glitch
		// esporádico de 1 frame (banda azul vertical) si el reinicio cae a
		// media pantalla. La pantalla es estática, así que una instalación
		// basta. (Ver docs/debugging/README o CONTINUATION_CONTEXT sobre el
		// reinicio del Copper en render.)
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
/// Rasteriza un code point (UTF-8 ya decodificado) en los bitplanes EHB.
    /// `planes` es la base de `StaticEhbScene`, que usa layout SEPARATE: cada
    /// plano p vive a `planes + p*kPlaneBytes` y dentro del plano el byte es
    /// `y*kBytesPerRow + x/8` (BPLMOD=0 en el display). El glifo usa filas
    /// bit0=izquierda (Font8); el píxel (x+k,y+r) marca el bit 0x80>>((x+k)&7).
    void draw_text(eng::u8* planes, eng::s32 x, eng::s32 y, const char* text, eng::u8 color) {
        const eng::u8* p = reinterpret_cast<const eng::u8*>(text);
        for (;;) {
            const eng::u32 cp = eng::utf8::decode(p);
            if (cp == 0u) {
                break; // NUL (fin de cadena) o byte inválido/fuera de LATIN-1
            }
            if (cp >= 32u) {
                for (eng::u8 row = 0; row < eng::Font8::kRows; ++row) {
                    const eng::u8 glyph = eng::Font8::row(static_cast<eng::u16>(cp), row);
                    if (glyph == 0u) {
                        continue;
                    }
                    const eng::u16 py = static_cast<eng::u16>(y + row);
                    for (eng::u8 k = 0; k < 8u; ++k) {
                        if ((glyph & (1u << k)) == 0u) {
                            continue;
                        }
                        const eng::u16 px = static_cast<eng::u16>(x + k);
                        const eng::u32 byte = static_cast<eng::u32>(py) * kBytesPerRow + (px / 8u);
                        const eng::u8 bit = static_cast<eng::u8>(0x80u >> (px & 7u));
                        for (eng::u8 pl = 0; pl < kPlanes; ++pl) {
                            if (color & (1u << pl)) {
                                planes[static_cast<eng::u32>(pl) * kPlaneBytes + byte] |= bit;
                            }
                        }
                    }
                }
            }
            x += 8;
        }
    }

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