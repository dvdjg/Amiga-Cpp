// ============================================================================
// Demo 085: escena con el copper orquestado por `copper::Plan`.
// ============================================================================
//
// Demuestra PARA QUE SIRVE el plan (docs/engine/architecture/DISPLAY_COMPOSITION.md §5):
// una escena con **varias fuentes de copper** que cambian a ritmos distintos y deben
// intercalarse por scanline sin que el llamador tenga que ordenarlas a mano.
//
//   - **CIELO (Copper sky)**: 16 bandas de COLOR00 con un degradado que cicla, generado
//     por el efecto reutilizable `RasterGradientEffect` (claves + fase). Es una
//     aportacion de la ESCENA: fija, de fondo.
//   - **BOB (software)**: un disco que se mueve por la pantalla y **comunica su
//     necesidad** a la escena: su degradado (COLOR01) debe quedar **anclado a su Y**, asi
//     que aporta 3 intenciones que se mueven cada frame.
//
// Las dos fuentes se anaden al plan en cualquier orden y el plan las **ordena por linea
// relativa al inicio del display** (el listado del Copper envuelve a 256 lineas), las
// materializa en el bloque TRASERO de su doble buffer y `commit()` publica con COP1LC.
// El BOB usa registros (COLOR01/03/05) distintos de los del cielo (COLOR00) para que sus
// cambios no se pisen.
//
// El bitmap va doble-buffered a mano (2 bloques): el BOB se dibuja en el trasero y la
// lista del plan apunta a ese con `move_bitplane_pointer` cada frame.
//
// Build/run:
//   bash ./tools/build/build-demo.sh demos/amiga/085_copper_plan_scene --debug
//   bash ./tools/run/run-demo.sh demos/amiga/085_copper_plan_scene

#include <eng/core/math/sinetable.hpp>
#include <eng/api/api.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/effects/raster_gradient.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/platform/amiga/backend.hpp>

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

namespace amiga = eng::amiga;
namespace copper = eng::copper;
namespace effects = eng::graphics::effects;
namespace graphics = eng::graphics;

// Geometria: 320x256 lowres, 4 planos (16 colores).
constexpr eng::u16 kWidth = 320;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 4;
constexpr eng::u16 kBytesPerRow = kWidth / 8u;             // 40
constexpr eng::u32 kPlaneBytes = kBytesPerRow * kHeight;   // 10240
constexpr eng::u16 kBplcon0 = 0x4200;                      // 4 planos, color
constexpr eng::u16 kFirstLine = 0x2cu;                     // arranque del display (PAL)

// Cielo: 16 bandas de 16 lineas cubren las 256 visibles (con envuelta a 256 lineas).
constexpr eng::u8 kSkyBands = 16;
constexpr eng::u16 kSkyBandHeight = 16;

// BOB: radio y degradado de 3 tramos anclado a su Y.
constexpr eng::s16 kBobR = 22;
constexpr eng::u16 kBobSpan = 2u * kBobR + 1u; // 45 filas
constexpr eng::u16 kBobStride = 8u;            // bytes por fila y plano (45+7 bits)

// Paleta base (antes de las zonas). COLOR00 lo pinta el cielo; el BOB usa 1/3/5.
constexpr eng::u16 kBasePalette[16] = {
	0x000, 0xff0, 0x000, 0xf00, 0x000, 0xfff, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
};

// Degradado del cielo (RGB444): azul profundo -> violeta -> naranja -> rojo.
constexpr eng::u16 kSky[16] = {
	0x003, 0x004, 0x005, 0x006, 0x007, 0x108, 0x20a, 0x30c,
	0x50c, 0x70b, 0x909, 0xb06, 0xd04, 0xf02, 0xf20, 0xf50,
};

// Arcoiris de 32 colores (RGB444) para el interior del BOB: cicla 4 veces sobre el
// disco, de modo que entre scanlines consecutivas el cambio es de un solo paso (suave).
constexpr eng::u16 kRainbow[32] = {
	0x00f, 0x00d, 0x00b, 0x009, 0x008, 0x009, 0x00b, 0x00d,
	0x00f, 0x0df, 0x0ff, 0x0fd, 0x0fb, 0x0f9, 0x0f8, 0x0f9,
	0x0fb, 0x0fd, 0x0ff, 0xfff, 0xffd, 0xffb, 0xff9, 0xff8,
	0xff9, 0xffb, 0xffd, 0xfff, 0xf0f, 0xf0d, 0xf0b, 0xf09,
};

/// Seno entero (-64..64) de 64 pasos para la trayectoria (compile-time, sin float).
constexpr eng::SineTable<64, 64> kSin {};

struct CopperPlanDemo {
	void init(amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({96u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008501u);
			return;
		}
		for (eng::u8 b = 0; b < 2u; ++b) {
			m_bitmaps[b] = backend.memory().chip.allocate_block<eng::PlaneTag>(kPlaneBytes * kPlanes, 16);
			if (!m_bitmaps[b].valid()) {
				eng::debug::mark_failed(g_eng_run_status, 0x00008502u);
				return;
			}
			backend.blitter_clear(m_bitmaps[b].view, kPlanes, kBytesPerRow, kPlaneBytes,
					      kWidth, kHeight, true);
		}
		// El plan reserva su doble buffer de copperlist; `first_line` es el arranque del
		// display para ordenar las intenciones relativas a el (cruce de 256 lineas).
		if (!m_plan.begin(backend.memory(), {4096u, kFirstLine})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008503u);
			return;
		}

		m_bob_x = 160;
		m_bob_y = 128;
		// Cielo: degradado por banda generado por el efecto (claves `kSky`, cíclico).
		m_sky.configure({kFirstLine, kSkyBandHeight, kSkyBands, 0u});
		m_sky.set_cyclic(true);
		m_sky.set_keys(kSky);
		build_shape();         // prerenderiza el disco (8 variantes de offset sub-byte)
		draw_bob(0u);          // primer frame en el bitmap 0
		if (!build_frame(0u)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008504u);
			return;
		}
		m_plan.takeover(backend);
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status,
				       (static_cast<eng::u32>(m_plan.intent_count()) << 8) |
					       static_cast<eng::u32>(m_plan.words() & 0xffu));
	}

	void update(amiga::AmigaBackend& backend, eng::GameContext& context) {
		(void)backend;
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_ready) return;

		// Trayectoria en Lissajous (movimiento continuo en X y en Y).
		const eng::u16 t = static_cast<eng::u16>(context.frame.frame_index);
		m_bob_x = static_cast<eng::s16>(160 + (kSin[static_cast<eng::u8>(t & 63u)] * 120) / 64);
		m_bob_y = static_cast<eng::s16>(128 + (kSin[static_cast<eng::u8>((t * 3u + 16u) & 63u)] * 90) / 64);

		const eng::u8 back = m_back;
		draw_bob(back);
		if (!build_frame(back)) {
			return;
		}
		m_back = static_cast<eng::u8>(back ^ 1u);
	}

	void render(amiga::AmigaBackend& backend, eng::GameContext& context) {
		// Publica la lista del frame (swap de COP1LC) tras VBlank.
		m_plan.commit(backend);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	// --- aportaciones de copper -------------------------------------------------

	/// CIELO: 16 bandas de COLOR00 con el degradado desplazado por la fase. Lo genera el
	/// efecto reutilizable `RasterGradientEffect` (claves `kSky`, cíclico); la demo solo
	/// aporta las intenciones al plan, que las ordena por scanline.
	void add_sky_intents() {
		m_sky.set_phase(m_sky_phase);
		const eng::u16 room = static_cast<eng::u16>(copper::Plan::max_intents - m_n);
		m_n = static_cast<eng::u16>(m_n + m_sky.fill_intents({m_intents + m_n, room}));
	}

	/// BOB: comunica su necesidad a la escena — su degradado (COLOR01) viaja con su Y con
	/// **un cambio por scanline** (arcoiris suave: un paso de la tabla entre lineas
	/// consecutivas), cubriendo el disco entero.
	void add_bob_intents() {
		for (eng::s16 row = -kBobR; row <= kBobR; ++row) {
			const eng::s16 y = static_cast<eng::s16>(m_bob_y + row);
			if (y < 0 || y >= static_cast<eng::s16>(kHeight)) continue;
			const eng::u8 idx = static_cast<eng::u8>(((row + kBobR) * 4 + m_sky_phase) & 31);
			push_intent(static_cast<eng::u16>((kFirstLine + y) & 0xffu), 1u, kRainbow[idx]);
		}
	}

	/// Anade una intencion `PaletteLine` que escribe el registro COLOR `first` con un
	/// color suelto. El color vive en un slot PROPIO (`m_colors[2*i + first]`) y la vista
	/// abarca `first + count`: el scheduler escribe `COLOR[first+i] = colors[first+i]`, asi
	/// que para `first = 1` la vista necesita 2 entradas (color en el indice 1). Usar un
	/// unico scratch compartido haria que TODAS las intenciones leyeran el ultimo color.
	void push_intent(eng::u16 line, eng::u8 first, eng::u16 color) {
		if (m_n >= eng::copper::Plan::max_intents) return;
		const eng::u16 slot = static_cast<eng::u16>(m_n * 2u);
		m_colors[static_cast<eng::u16>(slot + first)] = color;
		graphics::CopperIntent& it = m_intents[m_n++];
		it.kind = graphics::CopperIntentKind::PaletteLine;
		it.top = line;
		it.bottom = line;
		it.hpos = 0;
		it.colors = eng::PaletteWords {&m_colors[slot], 2u};
		it.first = first;
		it.count = 1;
	}

	// --- emision del frame ------------------------------------------------------

	/// Emite el frame con el plan: parte estatica (display apuntando al bitmap TRASERO +
	/// paleta base), las aportaciones (cielo y bob, en cualquier orden) y la cola.
	bool build_frame(eng::u8 back) {
		m_plan.begin_frame();
		copper::Scheduler& s = m_plan.scheduler();
		s.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, kBplcon0, kPlanes,
				      m_bitmaps[back].view, kPlaneBytes);
		s.emit_palette(eng::PaletteWords {kBasePalette, 16u});
		m_n = 0;
		m_sky_phase = static_cast<eng::u8>((m_sky_phase + 1u) & 15u);
		add_bob_intents();   // el BOB primero...
		add_sky_intents();   // ...y el cielo despues: el PLAN los ordena por scanline.
		m_plan.add(m_intents, m_n);
		m_plan.materialize();
		s.wait_line(0xf8);
		s.move(copper::Register::COLOR00, 0x0000);
		return m_plan.end_frame();
	}

	// --- dibujo del BOB por CPU ------------------------------------------------

	/// Borra la caja del disco (bytes, no pixel a pixel): el fondo es indice 0.
	void erase_box(eng::u8* base, eng::s16 cx, eng::s16 cy) {
		const eng::s16 x0 = static_cast<eng::s16>((cx - kBobR - 1) & ~7);
		const eng::s16 x1 = static_cast<eng::s16>(cx + kBobR + 1);
		if (x1 < 0 || x0 >= static_cast<eng::s16>(kWidth)) return;
		const eng::u16 bx0 = static_cast<eng::u16>(x0 < 0 ? 0 : x0) >> 3u;
		const eng::u16 bx1 = static_cast<eng::u16>(x1 >= static_cast<eng::s16>(kWidth)
								   ? kBytesPerRow - 1u : x1 >> 3u);
		const eng::s16 y0 = static_cast<eng::s16>(cy - kBobR - 1);
		const eng::s16 y1 = static_cast<eng::s16>(cy + kBobR + 1);
		for (eng::s16 y = y0; y <= y1; ++y) {
			if (y < 0 || y >= static_cast<eng::s16>(kHeight)) continue;
			for (eng::u8 p = 0; p < 3u; ++p) {
				eng::u8* row = base + static_cast<eng::u32>(p) * kPlaneBytes +
					       static_cast<eng::u32>(y) * kBytesPerRow;
				for (eng::u16 b = bx0; b <= bx1; ++b) row[b] = 0u;
			}
		}
	}

	/// Dibuja el disco del BOB en el bitmap `back` copiando la variante prerenderizada
	/// que toca (offset sub-byte de `x`): el clasico BOB software. La trayectoria esta
	/// acotada para que el disco no salga de pantalla (no hace falta recorte por pixel).
	void draw_bob(eng::u8 back) {
		eng::u8* base = m_bitmaps[back].view.data();
		erase_box(base, m_prev_x[back], m_prev_y[back]);
		const eng::s16 x0 = static_cast<eng::s16>(m_bob_x - kBobR);
		const eng::u16 bx = static_cast<eng::u16>(x0) >> 3u;
		const eng::u8 o = static_cast<eng::u8>(x0 & 7);
		for (eng::u16 row = 0; row < kBobSpan; ++row) {
			const eng::u32 off = (static_cast<eng::u32>(m_bob_y - kBobR) + row) * kBytesPerRow + bx;
			for (eng::u8 p = 0; p < 3u; ++p) {
				eng::u8* dst = base + static_cast<eng::u32>(p) * kPlaneBytes + off;
				const eng::u8* src = m_shape[o][p][row];
				for (eng::u8 b = 0; b < kBobStride; ++b) dst[b] = src[b];
			}
		}
		m_prev_x[back] = m_bob_x;
		m_prev_y[back] = m_bob_y;
	}

	/// Prerenderiza el disco en 8 variantes (una por offset sub-byte de `x`): 3 planos,
	/// 45 filas de 8 bytes. Asi el frame solo copia bytes y mueve el BOB a 50 fps.
	void build_shape() {
		for (eng::u8 o = 0; o < 8u; ++o) {
			for (eng::u16 row = 0; row < kBobSpan; ++row) {
				const eng::s16 dy = static_cast<eng::s16>(row) - kBobR;
				for (eng::s16 dx = -kBobR; dx <= kBobR; ++dx) {
					const eng::s16 d2 = static_cast<eng::s16>(dx * dx + dy * dy);
					if (d2 > kBobR * kBobR) continue;
					eng::u8 idx = 1u; // disco -> COLOR01: el degradado que viaja con su Y
					if (d2 >= (kBobR - 2) * (kBobR - 2)) idx = 5u;   // borde (blanco, fijo)
					// Brillo descentrado (arriba-izquierda), color fijo: deja ver el
					// degradado de COLOR01 en el resto del disco.
					const eng::s16 hx = static_cast<eng::s16>(dx + kBobR / 3);
					const eng::s16 hy = static_cast<eng::s16>(dy + kBobR / 3);
					if (hx * hx + hy * hy <= (kBobR / 4) * (kBobR / 4)) idx = 3u;
					const eng::u16 bit = static_cast<eng::u16>(dx + kBobR + o);
					const eng::u16 byte = bit >> 3u;
					const eng::u8 mask = static_cast<eng::u8>(0x80u >> (bit & 7u));
					for (eng::u8 p = 0; p < 3u; ++p) {
						if (((idx >> p) & 1u) != 0u) {
							m_shape[o][p][row][byte] = static_cast<eng::u8>(
								m_shape[o][p][row][byte] | mask);
						}
					}
				}
			}
		}
	}

	eng::Block<eng::PlaneTag> m_bitmaps[2] {};
	eng::u8 m_shape[8][3][kBobSpan][kBobStride] {};
	copper::Plan m_plan {};
	effects::RasterGradientEffect m_sky {};
	graphics::CopperIntent m_intents[eng::copper::Plan::max_intents] {};
	eng::u16 m_colors[eng::copper::Plan::max_intents * 2u] {};
	eng::u16 m_n = 0;
	eng::u8 m_back = 0;
	eng::u8 m_sky_phase = 0;
	eng::s16 m_bob_x = 0;
	eng::s16 m_bob_y = 0;
	eng::s16 m_prev_x[2] = {0, 0};
	eng::s16 m_prev_y[2] = {0, 0};
	bool m_ready = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	amiga::AmigaBackend backend {};
	CopperPlanDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
