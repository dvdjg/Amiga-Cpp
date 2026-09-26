// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/086_bob_objects --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/086_bob_objects
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/086_bob_objects --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/086_bob_objects

// ============================================================================
// Demo 086: objetos de bitmap (BOB) por Blitter + copper de escena orquestado.
// ============================================================================
//
// Gate visual del camino de **BOB** del sistema de objetos
// (`docs/engine/architecture/OBJECT_SYSTEM.md`) y de cómo se gestiona, A NIVEL DE ESCENA,
// el copper que necesita cada objeto:
//
//   - el **cielo** es un degradado **continuo por línea de raster** (256 intenciones
//     `PaletteLine`), aportado por la escena;
//   - cada **BOB declara sus necesidades de Copper** ancladas a su Y (un degradado de su
//     color a lo largo de sus filas) y `actor_add_copper` las pasa al `copper::Plan` con su
//     prioridad `(superficie, z)`. El objeto no escribe registros: describe.
//
// Los BOB se dibujan y borran con el Blitter vía la capa de actores (`actor_emit` +
// `bob.hpp` + `FramePlan`), con tres políticas: cookie-cut con máscara + borrado por caja,
// OR aditivo + borrado por caja, y opaco + save-under. Todos con desplazamiento fino.
//
// **Coste y configuración.** El cielo (256 intenciones) es **constante**: se construye una
// vez y, con `K_086_STATIC_COPPER=1` (por defecto), no se re-emite la lista cada frame, así
// la demo corre a **~50 fps (1 campo/frame)**. Las necesidades de Copper **por objeto**
// (dinámicas, ancladas a la Y) se activan con `K_086_STATIC_COPPER=0`, que exige
// re-materializar el plan cada frame y baja el framerate (referencia visual, no 50 fps).
//
// El número de BOBs es configurable (`-DK_086_BOBS=n`, 1..16; por defecto 3): la rejilla es
// 4x4 celdas de 80x64 (los recorridos no se salen de su celda, requisito de las políticas
// de borrado) y el presupuesto de intenciones del Plan (320) da `256 + 4n <= 320` -> n<=16.
//
// Build/run:
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/086_bob_objects --debug
//   EXTRA_DEFINES="-DK_086_BOBS=8 -DK_086_STATIC_COPPER=0" bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/086_bob_objects --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/086_bob_objects

#include <eng/api/api.hpp>          // fachada: escena, actores, dibujo, paleta, run_status
#include <eng/api/game.hpp>         // App/Screen + servicios (Blitter/copper)
#include <eng/core/math/sinetable.hpp>
#include <eng/core/util/color.hpp>
#include <eng/debug/prof.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

// El bloque de profiling (`eng::debug::g_eng_prof`) lo define el engine como
// `inline volatile` en prof.hpp: no hay que definirlo aquí.

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

#ifndef K_086_BOBS
#define K_086_BOBS 3
#endif
// Bandas del cielo: 0 = una intención por línea (degradado continuo, caro: ver README).
#ifndef K_086_SKY_BANDS
#define K_086_SKY_BANDS 256
#endif
// 1 = desactivar la parte DINAMICA del copper (cielo + necesidades de objeto +
// materialize) para aislar su coste. Solo diagnostico.
#ifndef K_086_STATIC_COPPER
#define K_086_STATIC_COPPER 1
#endif

namespace {

namespace scene = eng::scene;
namespace graphics = eng::graphics;

/// Secciones medidas con `eng::debug::g_eng_prof` (ver tools/debug/profile.mjs).
enum {
	kProfActors = 0, // colocacion + actor_emit (jobs al plan de blits)
	kProfBlits = 1,  // execute_frame_plan (Blitter, con sus esperas)
	kProfCopper = 2, // build_frame completo (el Plan)
	kProfStatic = 3, // build_frame: begin_frame + display + paleta
	kProfSky = 4,    // build_frame: bucle de intenciones del cielo
	kProfObjCopper = 5, // build_frame: necesidades de copper de los objetos
	kProfMaterialize = 6, // build_frame: ordenar y emitir
	kProfCalib = 10,      // bucle conocido: ciclos por iteracion (calibracion de la CPU)
	/// Ciclo COMPLETO del bucle del engine (update+wait_vblank+render) medido dentro de
	/// `render`: la diferencia con la suma de secciones de `update` es el tiempo de
	/// `wait_vblank` (frames perdidos incluidos). Con el bucle de polling, si `update` no
	/// cabe en un frame, el `wait_vblank` llega despues del VBlank y pierde frames enteros.
	kProfLoop = 11,
	kProfCount = 13,
};

// Geometría: 320x256 lowres, 4 planos (16 colores), planos contiguos.
constexpr eng::u16 kWidth = 320;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u16 kBytesPerRow = kWidth / 8u;             // 40
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kHeight;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;
constexpr eng::u16 kFirstLine = 0x2cu;

// Rejilla de BOBs: 4 columnas x 2 filas; el objeto se mueve solo dentro de su celda.
constexpr eng::u8  kBobCount = K_086_BOBS;
constexpr eng::u16 kSkyBands = K_086_SKY_BANDS;
constexpr eng::u8  kCols = 4u;
constexpr eng::u16 kCellW = 80u;
constexpr eng::u16 kCellH = 128u;

// Objeto: disco 32x32 de 3 planos (indices 1..4 -> COLOR01..COLOR04) + mascara. Cada
// OBJETO usa su PROPIO indice de color, para que sus intenciones de copper no colisionen
// con las de otro objeto aunque compartan lineas de raster (el Plan, si colisionan,
// resolveria por prioridad y se veria el color del ganador en los dos).
constexpr eng::u16 kObjW = 32, kObjH = 32, kObjPlanes = 3;
constexpr eng::u8  kObjVariants = 4u;                     // indices 1..4
constexpr eng::u32 kObjRow = ((kObjW / 16u) + 1u) * 2u;   // 6 (base + guarda)
constexpr eng::u32 kObjPlane = kObjH * kObjRow;           // 192
constexpr eng::u32 kObjData = kObjPlane * kObjPlanes;     // 576
constexpr eng::u32 kObjMask = kObjPlane;                  // 192 (1 plano)
constexpr eng::u32 kObjStride = kObjData + kObjMask;      // 768
constexpr eng::u32 kSheetBytes = kObjStride * kObjVariants;
constexpr eng::u32 kSaveWords = kObjPlanes * 3u * kObjH;  // 288: 3 palabras x 32 filas x 3 planos

constexpr eng::u8 kObjPolicies = 3u;
constexpr eng::u8 kObjCopperSteps = 4u;   // pasos de degradado de copper por objeto

// Paleta base: fondo + colores de los tres objetos (COLOR01..). El cielo reescribe
// COLOR00 por línea; cada objeto reescribe COLOR01.. con su degradado.
constexpr eng::Palette32 kPalette {{
	0x013, 0xf00, 0x0f0, 0xff0, 0x333, 0x333, 0x333, 0x333,
	0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

// Claves del cielo (RGB444) para interpolar un valor por línea.
constexpr eng::u8 kSkyKeys = 8u;
constexpr eng::u16 kSky[kSkyKeys] = {
	0x003, 0x005, 0x108, 0x50c, 0xb06, 0xf20, 0xf50, 0x203,
};

constexpr eng::SineTable<64, 64> kSin {};

/// Sumidero del bucle de calibracion (evita que el compilador lo elimine).
volatile eng::u32 g_calib_sink = 0;

/// Relleno del gradiente del cielo: un valor RGB444 por linea interpolando las
/// `kSkyKeys` claves. Resuelto en compilacion (`constexpr`): antes se recalculaba en cada
/// frame (~166k ciclos/frame medidos, seccion `sky`) aunque el degradado es constante.
/// Usa `eng::util::lerp444` (misma interpolacion por nibble, probada en HOST-094).
constexpr eng::u16 make_sky_entry(eng::u16 l) {
	const eng::u16 seg = static_cast<eng::u16>(256u / (kSkyKeys - 1u));
	eng::u16 k = static_cast<eng::u16>(l / seg);
	if (k >= kSkyKeys - 1u) {
		k = static_cast<eng::u16>(kSkyKeys - 2u);
	}
	return eng::util::lerp444(kSky[k], kSky[k + 1u], static_cast<eng::u16>(l % seg), seg);
}

struct SkyGradient {
	eng::u16 v[256] {};
	constexpr SkyGradient() {
		for (eng::u16 l = 0; l < 256u; ++l) {
			v[l] = make_sky_entry(l);
		}
	}
};
constexpr SkyGradient kSkyTable {};

/// Intenciones del cielo, resueltas en compilacion: `K_086_SKY_BANDS` intenciones
/// `PaletteLine` (una por banda; con 256, una por linea). Antes se construia cada
/// intencion en el bucle del frame y se llamaba `m_plan.add(sky)` una a una (~166k
/// ciclos/frame medidos, seccion `sky`); ahora solo se copian en bloque.
struct SkyIntents {
	static constexpr eng::u16 max_bands = 256u;
	graphics::CopperIntent v[max_bands] {};
	constexpr SkyIntents() {
		for (eng::u16 b = 0; b < kSkyBands; ++b) {
			const eng::u16 line = static_cast<eng::u16>(b * (256u / kSkyBands));
			graphics::CopperIntent it {};
			it.kind = graphics::CopperIntentKind::PaletteLine;
			it.top = static_cast<eng::u16>(kFirstLine + line);
			it.bottom = it.top;
			it.first = 0u; // COLOR00
			it.count = 1u;
			it.colors = eng::PaletteWords {&kSkyTable.v[line], 1u};
			v[b] = it;
		}
	}
};
constexpr SkyIntents kSkyIntents {};

// Gradiente del cielo (256 líneas) y paletas/intenciones por objeto.
// `g_obj_pal[i][s + 1]` es el color del paso `s`: la vista de `colors` debe cubrir el
// índice `first` (COLOR01), así que se pasa `{&pal[s], 2}` — con tamaño 1 el scheduler
// recortaría `count` a 0 y el objeto no escribiría nada.
eng::u16 g_obj_pal[kBobCount > 0u ? kBobCount : 1u][kObjCopperSteps][kObjVariants + 1u] {};
graphics::CopperIntent g_obj_needs[kBobCount > 0u ? kBobCount : 1u][kObjCopperSteps] {};

/// Arcoíris de 4 pasos para el degradado que pide cada objeto (evidente a la vista).
constexpr eng::u16 kObjRainbow[kObjCopperSteps] = {0x00f, 0x0f0, 0xf00, 0xff0};

struct BobObjectsDemo {
	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);
		ENG_PROF_INIT(kProfCount);
		m_bitmap = app.device().memory().chip.template allocate_block<eng::PlaneTag>(kBitplaneBytes, 16);
		m_sheet = app.device().memory().chip.template allocate_block<eng::BobTag>(kSheetBytes, 16);
		m_save = app.device().memory().chip.template allocate_block<eng::BobTag>(kSaveWords * 2u, 16);
		if (!m_bitmap.valid() || !m_sheet.valid() || !m_save.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008602u);
			return;
		}
		app.device().blitter_clear(m_bitmap.view, kPlanes, kBytesPerRow, kPlaneBytes, kWidth, kHeight, true);
		build_sheet();
		if (!add_actors()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008603u);
			return;
		}
		// El plan reserva su doble buffer de copperlist; `first_line` es el arranque del
		// display, para ordenar las intenciones relativas a él (cruce de 256 líneas).
		if (!m_plan.begin(app.device().memory(), {4096u, kFirstLine})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008604u);
			return;
		}
		if (!build_frame()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008605u);
			return;
		}
		app.device().takeover_copper(m_plan);
		eng::debug::mark_ready(g_eng_run_status,
				       (static_cast<eng::u32>(kBobCount) << 8u) |
					       static_cast<eng::u32>(m_plan.intent_count() & 0xffu));
		ENG_PROF_BEGIN(kProfLoop);
	}

	void update(auto& app) {
		eng::debug::mark_frame(g_eng_run_status, app.frame());
		// CALIBRACION: bucle de coste conocido. `profile.mjs` dara ciclos/frame con 1.0
		// llamadas/frame; dividiendo entre 1000 salen los ciclos por iteracion y, con las
		// ~4 instrucciones del bucle, la velocidad efectiva del CPU en este contexto (con
		// el DMA de bitplanes activo). Es lo que permite interpretar las demas secciones.
		ENG_PROF_BEGIN(kProfCalib);
		{
			eng::u32 acc = 0;
			for (eng::u32 i = 0; i < 1000u; ++i) {
				acc = static_cast<eng::u32>(acc + i);
			}
			g_calib_sink = acc;
		}
		ENG_PROF_END(kProfCalib);
		ENG_PROF_FRAME();
		ENG_PROF_BEGIN(kProfActors);
		const eng::u16 t = static_cast<eng::u16>(app.frame());

		// Cada objeto se mueve dentro de su celda (los borrados por caja/save-under no
		// deben invadir la caja de otro).
		for (eng::u8 i = 0; i < kBobCount; ++i) {
			auto a = m_actors.get(m_ids[i]);
			if (!a.valid()) {
				continue;
			}
			const eng::u16 col = static_cast<eng::u16>(i % kCols);
			const eng::u16 row = static_cast<eng::u16>(i / kCols);
			const eng::s16 cx = static_cast<eng::s16>(col * kCellW + kCellW / 2u);
			const eng::s16 cy = static_cast<eng::s16>(row * kCellH + kCellH / 2u);
			const eng::u8 ph = static_cast<eng::u8>((i * 7u + t) & 63u);
			a->desc.x = static_cast<eng::s16>(cx + kSin[ph] * 18 / 64);
			a->desc.y = static_cast<eng::s16>(cy + kSin[static_cast<eng::u8>((ph + 21u) & 63u)] * 12 / 64);
		}

		m_blits.clear();
		m_blits.set_blit_budget_limits({8192u, 16384u, 32u, 64u});
		scene::ActorEmitContext ctx {};
		ctx.targets = {&m_target, 1u};
		ctx.clip = graphics::DirtyRect {0, 0, static_cast<eng::s16>(kWidth),
						static_cast<eng::s16>(kHeight)};
		ctx.buffer = 0u;

		eng::u16 emitted = 0;
		for (eng::u8 i = 0; i < kBobCount; ++i) {
			auto a = m_actors.get(m_ids[i]);
			if (!a.valid()) {
				continue;
			}
			if (scene::actor_emit(m_blits, *a, ctx) == scene::ActorEmitStatus::Ok) {
				++emitted;
			}
		}
		ENG_PROF_END(kProfActors);
		ENG_PROF_BEGIN(kProfBlits);
		if (!app.device().execute_frame_plan(m_blits)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008606u);
			return;
		}
		ENG_PROF_END(kProfBlits);
		ENG_PROF_BEGIN(kProfCopper);
#if K_086_STATIC_COPPER == 0
		if (!build_frame()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008607u);
			return;
		}
#endif
		ENG_PROF_END(kProfCopper);
		// detail = nº de intenciones del plan (16 bits altos) | BOBs emitidos (8 bits
		// medios) | BOBs configurados (8 bits bajos). El conteo de intenciones permite
		// interpretar `emit` (ciclos por intención) sin sondas extra.
		g_eng_run_status.detail = (static_cast<eng::u32>(m_plan.intent_count() & 0xffffu) << 16u) |
					  (static_cast<eng::u32>(emitted) << 8u) |
					  static_cast<eng::u32>(kBobCount);
	}

	void render(auto& app) {
		// Publica la lista del frame (swap de COP1LC) tras VBlank, como manda el contrato.
		app.device().commit_copper(m_plan);
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
		// `loop` mide el ciclo completo (update+wait_vblank+render) entre dos renders: la
		// diferencia con la suma de secciones de `update` es el tiempo de `wait_vblank`.
		ENG_PROF_END(kProfLoop);
		ENG_PROF_BEGIN(kProfLoop);
	}

private:
	void build_sheet() {
		eng::u8* sheet = m_sheet.view.data();
		for (eng::u32 i = 0; i < kSheetBytes; ++i) {
			sheet[i] = 0u;
		}
		const auto put = [&](eng::u32 plane_off, eng::u32 row_bytes, eng::u16 x, eng::u16 y) {
			const eng::u32 row = plane_off + static_cast<eng::u32>(y) * row_bytes;
			sheet[row + (x >> 3u)] = static_cast<eng::u8>(sheet[row + (x >> 3u)] |
								      (0x80u >> (x & 7u)));
		};
		for (eng::u8 v = 0; v < kObjVariants; ++v) {
			const eng::u8 index = static_cast<eng::u8>(v + 1u); // COLOR01..COLOR04
			const eng::u32 base = static_cast<eng::u32>(v) * kObjStride;
			for (eng::s16 dy = -16; dy < 16; ++dy) {
				for (eng::s16 dx = -16; dx < 16; ++dx) {
					const eng::s16 d2 = static_cast<eng::s16>(dx * dx + dy * dy);
					if (d2 > 14 * 14) {
						continue;
					}
					const eng::u16 x = static_cast<eng::u16>(dx + 16);
					const eng::u16 y = static_cast<eng::u16>(dy + 16);
					put(base + kObjData, kObjRow, x, y); // máscara (cookie-cut)
					for (eng::u8 p = 0; p < kObjPlanes; ++p) {
						if (((index >> p) & 1u) != 0u) {
							put(base + static_cast<eng::u32>(p) * kObjPlane, kObjRow, x, y);
						}
					}
				}
			}
		}
	}

	bool add_actors() {
		m_actors.reset();
		m_allocator.reset({0u, 60000u, 0u}); // sin sprites: todo va por Blitter
		const eng::u8* const sheet = m_sheet.view.data();
		for (eng::u8 i = 0; i < kBobCount; ++i) {
			const eng::u8 policy = static_cast<eng::u8>(i % kObjPolicies);
			const eng::u8 variant = static_cast<eng::u8>(i % kObjVariants);
			const eng::u8 index = static_cast<eng::u8>(variant + 1u); // registro COLOR
			const eng::u32 base = static_cast<eng::u32>(variant) * kObjStride;
			scene::ActorDesc d {};
			d.visual.kind = graphics::VisualKind::Bob;
			d.visual.pixels = eng::Span<const eng::u16> {
				reinterpret_cast<const eng::u16*>(sheet + base),
				static_cast<eng::usize>(kObjData / 2u)};
			d.visual.mask = eng::Span<const eng::u16> {
				reinterpret_cast<const eng::u16*>(sheet + base + kObjData),
				static_cast<eng::usize>(kObjMask / 2u)};
			d.visual.w = kObjW;
			d.visual.h = kObjH;
			d.visual.bitplanes = kObjPlanes;
			d.x = 0;
			d.y = 0;
			d.surface = 0u;
			d.z = static_cast<eng::u8>(10u + i);
			d.preferred = scene::Representation::Bob;
			d.layout = graphics::BobLayout::Planar;
			if (policy == 0u) {
				d.transparency = scene::TransparencyMode::Mask1Bit;
				d.background = scene::BackgroundPolicy::ClearRect;
			} else if (policy == 1u) {
				d.transparency = scene::TransparencyMode::AdditiveOr;
				d.background = scene::BackgroundPolicy::ClearRect;
			} else {
				d.transparency = scene::TransparencyMode::Opaque;
				d.background = scene::BackgroundPolicy::SaveUnder;
				d.save[0] = eng::Span<eng::u16> {
					reinterpret_cast<eng::u16*>(m_save.view.data()), kSaveWords};
				d.save_words_per_row = 3u; // 32 px -> 2 palabras + guarda del shift
				d.save_height = kObjH;
			}
			// Necesidades de Copper del objeto: un arcoíris de `kObjCopperSteps` pasos a lo
			// largo de sus filas, en SU registro (`COLOR0<index>`), relativo a su Y. Así no
			// colisiona con otro objeto ni aunque compartan líneas de raster.
			for (eng::u8 s = 0; s < kObjCopperSteps; ++s) {
				eng::u16* colors = g_obj_pal[i][s];
				colors[index] = kObjRainbow[s];
				graphics::CopperIntent& need = g_obj_needs[i][s];
				need = graphics::CopperIntent {};
				need.kind = graphics::CopperIntentKind::PaletteLine;
				need.top = static_cast<eng::u16>(static_cast<eng::u16>(s) * d.visual.h /
								 kObjCopperSteps);
				need.bottom = need.top;
				need.colors = eng::PaletteWords {colors, static_cast<eng::u8>(index + 1u)};
				need.first = index; // COLOR0<index>
				need.count = 1u;
			}
			d.copper = eng::Span<const graphics::CopperIntent> {g_obj_needs[i], kObjCopperSteps};
			m_ids[i] = m_actors.add(d, m_allocator);
			if (!m_ids[i].valid()) {
				return false;
			}
		}
		m_target.base = m_bitmap.view.data();
		m_target.row_bytes = kBytesPerRow;
		m_target.plane_bytes = kPlaneBytes;
		m_target.planes = kPlanes;
		m_target.layout = graphics::BobLayout::Planar;
		return true;
	}

	/// Compone el copper del frame. El **cielo** es constante: se construye (y materializa)
	/// siempre, y en modo `K_086_STATIC_COPPER=1` solo se llama **una vez** (en `init`), de
	/// modo que el frame no re-emite la lista. Las **necesidades de Copper por objeto**
	/// (ancladas a su Y, dinámicas) solo entran con `K_086_STATIC_COPPER == 0`.
	bool build_frame() {
		ENG_PROF_BEGIN(kProfStatic);
		m_plan.begin_frame();
		m_plan.scheduler().emit_planes_display(0x2c81u, 0x2cc1u, 0x0038u, 0x00d0u, kBytesPerRow,
						       0x4200u, kPlanes, m_bitmap.view, kPlaneBytes);
		m_plan.scheduler().emit_palette(kPalette.color);
		ENG_PROF_END(kProfStatic);
		// Cielo: `kSkyBands` intenciones repartidas por el raster (una por banda). Con
		// `K_086_SKY_BANDS=256` es un valor por línea (continuo). Las intenciones son
		// invariantes: van en `kSkyIntents` (constexpr) y se copian en un solo `add`.
		ENG_PROF_BEGIN(kProfSky);
		m_plan.add(kSkyIntents.v, kSkyBands);
		ENG_PROF_END(kProfSky);
#if K_086_STATIC_COPPER == 0
		// Necesidades de cada objeto, con su (superficie, z).
		ENG_PROF_BEGIN(kProfObjCopper);
		for (eng::u8 i = 0; i < kBobCount; ++i) {
			const auto a = m_actors.get(m_ids[i]);
			if (!a.valid()) {
				continue;
			}
			const graphics::Frame f = scene::actor_current_frame(*a);
			const graphics::DirtyRect r = scene::actor_screen_rect(*a, f, 0, 0);
			scene::actor_add_copper(m_plan, *a, r.top, kFirstLine);
		}
		ENG_PROF_END(kProfObjCopper);
#endif
		ENG_PROF_BEGIN(kProfMaterialize);
		m_plan.materialize();
		ENG_PROF_END(kProfMaterialize);
		return m_plan.end_frame();
	}

	eng::Block<eng::PlaneTag> m_bitmap {};
	eng::Block<eng::BobTag> m_sheet {};
	eng::Block<eng::BobTag> m_save {};
	graphics::FramePlan m_blits {};
	graphics::BobTarget m_target {};
	eng::copper::Plan m_plan {};
	scene::ActorStore<16> m_actors {};
	scene::RepresentationAllocator m_allocator {};
	scene::ActorId m_ids[kBobCount > 0u ? kBobCount : 1u] {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	if (!backend.configure_memory({96u * 1024u, 8u * 1024u, 4u * 1024u})) {
		eng::debug::mark_failed(g_eng_run_status, 0x00008601u);
		return 0;
	}
	BobObjectsDemo game {};
	eng::App app {backend, game};
	app.run(0xffffu);

	return 0;
}
