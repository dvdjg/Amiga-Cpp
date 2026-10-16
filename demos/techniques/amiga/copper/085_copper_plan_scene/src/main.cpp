// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/copper/085_copper_plan_scene --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/copper/085_copper_plan_scene --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/copper/085_copper_plan_scene --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/copper/085_copper_plan_scene --keep-running

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
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/copper/085_copper_plan_scene --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/copper/085_copper_plan_scene

#include <eng/core/math/sinetable.hpp>
#include <eng/api/api.hpp>
#include <eng/debug/prof.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/effects/raster_gradient.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/setup/coherence.hpp>

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

// Geometria: 320x256 lowres, 3 planos (8 colores: fondo + disco con 1/3/5). El disco y el
// cielo solo necesitan 3 planos, asi que BPU=3 es la configuracion semantica correcta (el
// 4.º plano era contenido ajeno; v. VALIDATION.md y la deuda 085 abierta, que es de layout
// de memoria en runtime: la copperlist cae dentro del rango del bitmap que el display lee).
constexpr eng::u16 kWidth = 320;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 3;
constexpr eng::u16 kBytesPerRow = kWidth / 8u;             // 40
constexpr eng::u32 kPlaneBytes = kBytesPerRow * kHeight;   // 10240
constexpr eng::u16 kBplcon0 = 0x3200;                      // 3 planos, color
constexpr eng::u16 kFirstLine = 0x2cu;                     // arranque del display (PAL)

// Cielo: 16 bandas de 16 lineas cubren las 256 visibles (con envuelta a 256 lineas).
constexpr eng::u8 kSkyBands = 16;
constexpr eng::u16 kSkyBandHeight = 16;

// BOB: radio y degradado de 3 tramos anclado a su Y.
constexpr eng::s16 kBobR = 22;
constexpr eng::u16 kBobSpan = 2u * kBobR + 1u; // 45 filas
constexpr eng::u16 kBobStride = 8u;            // bytes por fila y plano (45+7 bits)
constexpr eng::u16 kBobWords = 4u;             // palabras por fila (64 bits ≥ 45 + 15 de shift)
// Amplitud de la trayectoria en Y: 128 ± 90 → el disco puede cubrir las líneas de
// pantalla [16, 240]. Las intenciones del BOB se emiten FIJAS para ese rango completo
// (setup) y por frame solo se parchea su palabra de DATO.
constexpr eng::u16 kBobLineMin = 16u;
constexpr eng::u16 kBobLineMax = 240u;

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
		ENG_PROF_INIT(3); // secciones 0-2: dibujo del disco / parcheo de slots / flip+commit
		// 112K chip: 2 bitmaps (60K) + doble copperlist (8K) + variantes del disco (17K) + resto.
		if (!backend.configure_memory({112u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008501u);
			return;
		}
		for (eng::u8 b = 0; b < 2u; ++b) {
			m_bitmaps[b] = backend.memory_manager().chip().reserve<eng::PlaneTag>(kPlaneBytes * kPlanes, 16);
			if (!m_bitmaps[b].valid()) {
				eng::debug::mark_failed(g_eng_run_status, 0x00008502u);
				return;
			}
			backend.blitter_clear(m_bitmaps[b].view, kPlanes, kBytesPerRow, kPlaneBytes,
					      kWidth, kHeight, true);
		}
		// El plan reserva su doble buffer de copperlist; `first_line` es el arranque del
		// display para ordenar las intenciones relativas a el (cruce de 256 lineas).
		if (!m_plan.begin(backend.memory_manager(), {4096u, kFirstLine})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008503u);
			return;
		}

		m_bob_x = 160;
		m_bob_y = 128;
		// Fuentes del Blitter en Chip: variantes del disco (16 × 3 planos × 45 filas ×
		// 8 bytes) y la fila de ceros del borrado (con módulo -8 se repite por fila).
		m_shape = backend.memory_manager().chip().reserve<eng::BobTag>(
			16u * 3u * kBobSpan * kBobStride, 16u);
		m_zero = backend.memory_manager().chip().reserve<eng::BobTag>(16u, 16u);
		if (!m_shape.valid() || !m_zero.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008506u);
			return;
		}
		for (eng::u8 i = 0; i < 16u; ++i) m_zero.view.data()[i] = 0u;

		// Validación del layout (capa 2 de `COHERENCIA_DE_SETUP.md`): si dos reservas de arena
		// se solapan, una lee memoria de la otra (el defecto abierto de 085: la copperlist
		// ejecutada cae dentro del rango del bitmap). `validate_disjoint` lo convierte en
		// fallo de setup con nombres, en vez de basura silenciosa en pantalla. Código:
		// 0x85<<8 | índice del par culpable.
		{
			eng::setup::RegionTable<8> regions;
			(void)regions.add("bitmap0", reinterpret_cast<eng::uintptr>(m_bitmaps[0].view.data()),
					  kPlaneBytes * kPlanes);
			(void)regions.add("bitmap1", reinterpret_cast<eng::uintptr>(m_bitmaps[1].view.data()),
					  kPlaneBytes * kPlanes);
			(void)regions.add("copper_a",
					  reinterpret_cast<eng::uintptr>(m_plan.active_view().data()),
					  static_cast<eng::u32>(m_plan.active_view().size()));
			(void)regions.add("copper_b",
					  reinterpret_cast<eng::uintptr>(m_plan.inactive_view().data()),
					  static_cast<eng::u32>(m_plan.inactive_view().size()));
			(void)regions.add("shape", reinterpret_cast<eng::uintptr>(m_shape.view.data()),
					  m_shape.view.size());
			(void)regions.add("zero", reinterpret_cast<eng::uintptr>(m_zero.view.data()),
					  m_zero.view.size());
			const eng::setup::CheckResult layout = regions.validate_disjoint();
			if (!layout.ok()) {
				eng::debug::mark_failed(g_eng_run_status,
							static_cast<eng::u32>(0x00008500u | (layout.index & 0xffu)));
				return;
			}
		}
		// Cielo: degradado por banda generado por el efecto (claves `kSky`, cíclico).
		m_sky.configure({kFirstLine, kSkyBandHeight, kSkyBands, 0u});
		m_sky.set_cyclic(true);
		m_sky.set_keys(kSky);
		build_shape();         // prerenderiza el disco (16 variantes de offset de palabra)
		draw_bob(backend, 0u); // primer frame en el bitmap 0
		// Lista ESTÁTICA en cada bloque del doble buffer (una por bitmap: solo cambian los
		// punteros BPL del display). Después del setup el frame solo parchea datos.
		if (!build_static_list(0u)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008504u);
			return;
		}
		m_sky.bind_slots(m_plan); // palabras de DATO de las 16 bandas del cielo
		bind_bob_slots();         // palabras de DATO de las líneas del disco (reg COLOR01)
		if (!build_static_list(1u)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008505u);
			return;
		}
		m_plan.flip(); // vuelve a dejar ACTIVO el bloque del bitmap 0 (el que se muestra)
		m_plan.takeover(backend);
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status,
				       (static_cast<eng::u32>(m_plan.intent_count()) << 8) |
					       static_cast<eng::u32>(m_plan.words() & 0xffu));
	}

	void update(amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_ready) return;
		ENG_PROF_FRAME();

		// Trayectoria en Lissajous (movimiento continuo en X y en Y).
		const eng::u16 t = static_cast<eng::u16>(context.frame.frame_index);
		m_bob_x = static_cast<eng::s16>(160 + (kSin[static_cast<eng::u8>(t & 63u)] * 120) / 64);
		m_bob_y = static_cast<eng::s16>(128 + (kSin[static_cast<eng::u8>((t * 3u + 16u) & 63u)] * 90) / 64);

		// `back` = el bitmap que se mostrará al publicar este frame (el bloque inactivo del
		// plan es el suyo: se parchea ahí y el `flip` del render lo hace activo).
		const eng::u8 back = static_cast<eng::u8>(m_back ^ 1u);
		ENG_PROF_BEGIN(0); // sección 0: dos blits del disco (borrado + dibujo) en el bitmap trasero
		draw_bob(backend, back);
		ENG_PROF_END(0);
		ENG_PROF_BEGIN(1); // sección 1: parcheo de slots (cielo + BOB) en el bloque inactivo
		m_sky_phase = static_cast<eng::u8>((m_sky_phase + 1u) & 15u);
		patch_frame();
		ENG_PROF_END(1);
		m_back = back;
	}

	void render(amiga::AmigaBackend& backend, eng::GameContext& context) {
		// Publica la lista parcheada (swap de COP1LC) tras VBlank.
		ENG_PROF_BEGIN(2); // sección 2: flip + commit (swap de COP1LC)
		m_plan.flip();
		m_plan.commit(backend);
		ENG_PROF_END(2);
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

	/// BOB: intents `PaletteLine` de COLOR01 para TODAS las líneas que el disco puede ocupar
	/// (posiciones FIJAS, emitidas solo en el setup). Por frame el valor del disco viaja como
	/// **parche de la palabra de DATO** (dentro del disco → arcoíris; fuera → color base), así
	/// el `materialize`/`sort` se paga una vez y no en el bucle.
	void add_bob_intents() {
		for (eng::u16 line = kBobLineMin; line <= kBobLineMax; ++line) {
			push_intent(static_cast<eng::u16>((kFirstLine + line) & 0xffu), 1u, kBasePalette[1]);
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

	/// Emite la lista COMPLETA (estática) en el bloque inactivo del plan: display apuntando al
	/// bitmap `b`, paleta base, intents de cielo y de BOB (posiciones fijas) y cola. Se ejecuta
	/// solo en el setup (una vez por bitmap); a partir de ahí el frame solo parchea palabras de
	/// DATO de los slots (`patch_frame`), sin re-emitir ni re-ordenar.
	bool build_static_list(eng::u8 b) {
		m_plan.begin_frame();
		copper::Scheduler& s = m_plan.scheduler();
		s.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, kBplcon0, kPlanes,
				      m_bitmaps[b].mem_view_chip(), kPlaneBytes);
		s.emit_palette(eng::PaletteWords {kBasePalette, 16u});
		m_n = 0;
		add_bob_intents(); // el BOB primero...
		add_sky_intents(); // ...y el cielo despues: el PLAN los ordena por scanline.
		m_plan.add(m_intents, m_n);
		m_plan.materialize();
		s.wait_line(0xf8);
		s.move(copper::Register::COLOR00, 0x0000);
		return m_plan.end_frame();
	}

	/// Casa las palabras de DATO del BOB con sus líneas de pantalla (los slots del plan),
	/// en un array DENSO indexado por `linea - kBobLineMin`. Idéntico en ambos bloques: la
	/// secuencia de emisión del setup es la misma.
	void bind_bob_slots() {
		for (eng::u16 j = 0u; j < m_plan.slot_count(); ++j) {
			if (m_plan.slot_reg(j) != 1u) continue; // reg 1 = COLOR01 (disco)
			const eng::u16 line = static_cast<eng::u16>(
				(static_cast<eng::u16>(m_plan.slot_line(j)) - kFirstLine) & 0xffu);
			m_bob_slot_word[line - kBobLineMin] = m_plan.slot_word(j);
		}
	}

	/// Parcheo por frame (bloque INACTIVO, antes del `flip`): cielo (fase del efecto) y BOB
	/// (arcoíris dentro del disco, color base fuera). Solo escribe palabras de DATO, y del
	/// BOB únicamente las líneas que cambian (rango actual ∪ anterior: fuera de ahí ya
	/// tienen el color base).
	void patch_frame() {
		eng::u16* const words = m_plan.inactive_words();
		m_sky.set_phase(m_sky_phase);
		m_sky.patch_words(words);
		const eng::s16 lo = static_cast<eng::s16>(m_bob_y - kBobR);
		const eng::s16 hi = static_cast<eng::s16>(m_bob_y + kBobR);
		eng::s16 u0 = lo;
		eng::s16 u1 = hi;
		if (m_prev_hi >= m_prev_lo) { // el frame anterior dejó un rango válido
			if (m_prev_lo < u0) u0 = m_prev_lo;
			if (m_prev_hi > u1) u1 = m_prev_hi;
		}
		for (eng::s16 line = u0; line <= u1; ++line) {
			eng::u16 color = kBasePalette[1];
			if (line >= lo && line <= hi) {
				const eng::s16 row = static_cast<eng::s16>(line - m_bob_y);
				const eng::u8 idx = static_cast<eng::u8>(((row + kBobR) * 4 + m_sky_phase) & 31);
				color = kRainbow[idx];
			}
			words[m_bob_slot_word[static_cast<eng::u16>(line) - kBobLineMin].data_word()] = color;
		}
		m_prev_lo = lo;
		m_prev_hi = hi;
	}

	// --- dibujo del BOB por Blitter --------------------------------------------

	/// Envía UNA caja del disco al Blitter: copia `kBobWords` palabras × `kBobSpan` filas ×
	/// 3 planos con módulos/pasos del caso (variante pre-shifteada o fila de ceros). El
	/// destino va alineado a palabra (`x0 >> 4`): las 16 variantes prerenderizadas evitan el
	/// shift del Blitter y no tocan bits vecinos.
	bool blit_box(amiga::AmigaBackend& backend, eng::u8* base, eng::s16 x0, eng::s16 y0,
		      const eng::u16* src, eng::s16 src_mod, eng::u32 src_plane_stride, bool wait) {
		// La trayectoria está acotada (x ∈ [40, 280], y ∈ [38, 218]): la caja siempre cae
		// dentro de pantalla. Guarda defensiva: una caja fuera (p. ej. un `prev` sin
		// inicializar) no se envía — el puntero envuelto escribiría memoria ajena.
		const eng::u16 word_x = static_cast<eng::u16>(x0) >> 4u;
		if (x0 < 0 || static_cast<eng::s16>(y0 - kBobR) < 0 ||
		    word_x + kBobWords > kBytesPerRow / 2u) {
			return false;
		}
		eng::graphics::BlitJob job {};
		job.kind = eng::graphics::BlitJobKind::CopyRect;
		job.source = eng::graphics::BlitPtr::from_storage(src);
		job.destination = eng::graphics::BlitPtr::from_storage(
			reinterpret_cast<eng::u16*>(base +
				static_cast<eng::u32>(y0 - kBobR) * kBytesPerRow) + word_x);
		job.words_per_row = kBobWords;
		job.height = kBobSpan;
		job.source_modulo_bytes = src_mod;
		job.destination_modulo_bytes = eng::graphics::mod16(
			static_cast<eng::s32>(kBytesPerRow) - static_cast<eng::s32>(kBobWords) * 2);
		job.bitplane_count = 3u;
		job.source_plane_stride_bytes = src_plane_stride;
		job.destination_plane_stride_bytes = kPlaneBytes;
		return backend.blitter_submit(job, wait);
	}

	/// Mueve el disco en el bitmap `back` con DOS blits: borra la caja anterior (copia desde
	/// la fila de ceros, repetida con módulo `-8`) y copia la variante pre-shifteada en la
	/// posición nueva. El CPU solo programa registros; la copia la hace el Blitter.
	void draw_bob(amiga::AmigaBackend& backend, eng::u8 back) {
		eng::u8* const base = m_bitmaps[back].view.data();
		const eng::u16* const zero = reinterpret_cast<const eng::u16*>(m_zero.view.data());
		const eng::s16 px0 = static_cast<eng::s16>(m_prev_x[back] - kBobR);
		(void)blit_box(backend, base, px0, m_prev_y[back], zero,
			       static_cast<eng::s16>(-(kBobWords * 2)), 0u, true);
		const eng::s16 x0 = static_cast<eng::s16>(m_bob_x - kBobR);
		const eng::u8 v = static_cast<eng::u8>(x0 & 15);
		const eng::u16* const shape = reinterpret_cast<const eng::u16*>(
			m_shape.view.data() +
			static_cast<eng::u32>(v) * (3u * kBobSpan * kBobStride));
		(void)blit_box(backend, base, x0, m_bob_y, shape, 0,
			       3u * kBobSpan * kBobStride, true);
		m_prev_x[back] = m_bob_x;
		m_prev_y[back] = m_bob_y;
	}

	/// Prerenderiza el disco en 16 variantes (offset de PALABRA de `x`, 0..15): 3 planos,
	/// 45 filas de 8 bytes (64 bits = 45 + 15 de margen). Así el Blitter copia palabras ya
	/// alineadas (sin shift) y el disco entra en cualquier x.
	void build_shape() {
		eng::u8* const base = m_shape.view.data();
		for (eng::u8 v = 0; v < 16u; ++v) {
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
					const eng::u16 bit = static_cast<eng::u16>(dx + kBobR + v);
					const eng::u16 byte = bit >> 3u;
					const eng::u8 mask = static_cast<eng::u8>(0x80u >> (bit & 7u));
					for (eng::u8 p = 0; p < 3u; ++p) {
						if (((idx >> p) & 1u) == 0u) continue;
						eng::u8* const px =
							base + (static_cast<eng::u32>(v) * 3u + p) *
								       kBobSpan * kBobStride +
							static_cast<eng::u32>(row) * kBobStride + byte;
						*px = static_cast<eng::u8>(*px | mask);
					}
				}
			}
		}
	}

	eng::Block<eng::PlaneTag> m_bitmaps[2] {};
	eng::Block<eng::BobTag> m_shape {}; ///< 16 variantes pre-shifteadas del disco (fuente del Blitter)
	eng::Block<eng::BobTag> m_zero {};  ///< fila de ceros (fuente del borrado por Blitter)
	copper::Plan m_plan {};
	effects::RasterGradientEffect m_sky {};
	graphics::CopperIntent m_intents[eng::copper::Plan::max_intents] {};
	eng::u16 m_colors[eng::copper::Plan::max_intents * 2u] {};
	eng::u16 m_n = 0;
	// Slots del BOB (palabras de DATO de COLOR01), casados tras el materialize del setup:
	// array DENSO indexado por línea (kBobLineMax - kBobLineMin + 1 líneas posibles).
	static constexpr eng::u16 kMaxBobSlots = kBobLineMax - kBobLineMin + 1u;
	eng::copper::MoveSlot m_bob_slot_word[kMaxBobSlots] {};
	// Rango de líneas del disco parcheado en el frame anterior (unión con el actual).
	eng::s16 m_prev_lo = 1;
	eng::s16 m_prev_hi = 0;
	eng::u8 m_back = 0;
	eng::u8 m_sky_phase = 0;
	eng::s16 m_bob_x = 0;
	eng::s16 m_bob_y = 0;
	eng::s16 m_prev_x[2] = {160, 160}; // = posición inicial del disco (la caja del primer borrado cae dentro)
	eng::s16 m_prev_y[2] = {128, 128};
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
