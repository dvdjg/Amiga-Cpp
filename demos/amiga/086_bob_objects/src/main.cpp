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
// El número de BOBs es configurable (`-DK_086_BOBS=n`, 1..16; por defecto 8): la rejilla es
// 4x4 celdas de 80x64 (los recorridos no se salen de su celda, requisito de las políticas
// de borrado) y el presupuesto de intenciones del Plan (320) da `256 + 4n <= 320` -> n<=16.
//
// Build/run:
//   bash ./tools/build/build-demo.sh demos/amiga/086_bob_objects --debug
//   EXTRA_DEFINES="-DK_086_BOBS=16" bash ./tools/build/build-demo.sh demos/amiga/086_bob_objects --debug
//   bash ./tools/run/run-demo.sh demos/amiga/086_bob_objects

#include <eng/core/sinetable.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/scene/actor.hpp>

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

#ifndef K_086_BOBS
#define K_086_BOBS 8
#endif
// Bandas del cielo: 0 = una intención por línea (degradado continuo, caro: ver README).
#ifndef K_086_SKY_BANDS
#define K_086_SKY_BANDS 256
#endif

namespace {

namespace scene = eng::scene;
namespace graphics = eng::graphics;
namespace drivers = eng::graphics::drivers;

// Geometría: 320x256 lowres, 4 planos (16 colores), planos contiguos.
constexpr eng::u16 kWidth = 320;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u16 kBytesPerRow = kWidth / 8u;             // 40
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kHeight;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;
constexpr eng::u16 kFirstLine = 0x2cu;

// Rejilla de BOBs: 4x4 celdas de 80x64; el objeto se mueve solo dentro de su celda.
constexpr eng::u8  kBobCount = K_086_BOBS;
constexpr eng::u16 kSkyBands = K_086_SKY_BANDS;
constexpr eng::u8  kCols = 4u;
constexpr eng::u16 kCellW = 80u;
constexpr eng::u16 kCellH = 64u;

// Hojas: disco 32x32 (2 planos + máscara) y cuadrado 16x16 (1 y 2 planos).
constexpr eng::u16 kDiscW = 32, kDiscH = 32, kDiscPlanes = 2;
constexpr eng::u32 kDiscRow = ((kDiscW / 16u) + 1u) * 2u;   // 6
constexpr eng::u32 kDiscPlane = kDiscH * kDiscRow;          // 192
constexpr eng::u16 kSqW = 16, kSqH = 16;
constexpr eng::u32 kSqRow = ((kSqW / 16u) + 1u) * 2u;       // 4
constexpr eng::u32 kSqPlane = kSqH * kSqRow;                // 64
constexpr eng::u32 kDiscOff = 0;
constexpr eng::u32 kGlowOff = kDiscOff + kDiscPlane * kDiscPlanes;
constexpr eng::u32 kOpaqueOff = kGlowOff + kSqPlane;
constexpr eng::u32 kMaskOff = kOpaqueOff + kSqPlane * 2u;
constexpr eng::u32 kSheetBytes = kMaskOff + kDiscPlane;
constexpr eng::u32 kSaveWords = 64;

constexpr eng::u8 kObjKinds = 3u;
constexpr eng::u8 kObjCopperSteps = 4u;   // pasos de degradado de copper por objeto

// Paleta base: fondo + colores de los tres objetos (COLOR01..). El cielo reescribe
// COLOR00 por línea; cada objeto reescribe COLOR01.. con su degradado.
constexpr drivers::EhbPalette kPalette {{
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

/// Interpolación lineal por nibble (RGB444) entre dos colores.
constexpr eng::u16 lerp444(eng::u16 a, eng::u16 b, eng::u16 num, eng::u16 den) {
	if (den == 0u) {
		return a;
	}
	eng::u16 out = 0u;
	for (eng::u8 shift = 0u; shift < 12u; shift += 4u) {
		const eng::s16 ca = static_cast<eng::s16>((a >> shift) & 0xfu);
		const eng::s16 cb = static_cast<eng::s16>((b >> shift) & 0xfu);
		const eng::s16 v = static_cast<eng::s16>(ca + ((cb - ca) * static_cast<eng::s16>(num)) /
							      static_cast<eng::s16>(den));
		out = static_cast<eng::u16>(out | (static_cast<eng::u16>(v & 0xf) << shift));
	}
	return out;
}

// Gradiente del cielo (256 líneas) y paletas/intenciones por objeto.
// `g_obj_pal[i][s + 1]` es el color del paso `s`: la vista de `colors` debe cubrir el
// índice `first` (COLOR01), así que se pasa `{&pal[s], 2}` — con tamaño 1 el scheduler
// recortaría `count` a 0 y el objeto no escribiría nada.
eng::u16 g_sky[256] {};
eng::u16 g_obj_pal[kBobCount > 0u ? kBobCount : 1u][kObjCopperSteps + 1u] {};
graphics::CopperIntent g_obj_needs[kBobCount > 0u ? kBobCount : 1u][kObjCopperSteps] {};

/// Arcoíris de 4 pasos para el degradado que pide cada objeto (evidente a la vista).
constexpr eng::u16 kObjRainbow[kObjCopperSteps] = {0x00f, 0x0f0, 0xf00, 0xff0};

struct BobObjectsDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({96u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008601u);
			return;
		}
		m_bitmap = backend.memory().chip.allocate_block<eng::PlaneTag>(kBitplaneBytes, 16);
		m_sheet = backend.memory().chip.allocate_block<eng::BobTag>(kSheetBytes, 16);
		m_save = backend.memory().chip.allocate_block<eng::BobTag>(kSaveWords * 2u, 16);
		if (!m_bitmap.valid() || !m_sheet.valid() || !m_save.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008602u);
			return;
		}
		backend.blitter_clear(m_bitmap.view, kPlanes, kBytesPerRow, kPlaneBytes, kWidth, kHeight, true);
		build_sky();
		build_sheet();
		if (!add_actors()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008603u);
			return;
		}
		// El plan reserva su doble buffer de copperlist; `first_line` es el arranque del
		// display, para ordenar las intenciones relativas a él (cruce de 256 líneas).
		if (!m_plan.begin(backend.memory(), {4096u, kFirstLine})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008604u);
			return;
		}
		if (!build_frame()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008605u);
			return;
		}
		m_plan.takeover(backend);
		eng::debug::mark_ready(g_eng_run_status,
				       (static_cast<eng::u32>(kBobCount) << 8u) |
					       static_cast<eng::u32>(m_plan.intent_count() & 0xffu));
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		const eng::u16 t = static_cast<eng::u16>(context.frame.frame_index);

		// Cada objeto se mueve dentro de su celda (los borrados por caja/save-under no
		// deben invadir la caja de otro).
		for (eng::u8 i = 0; i < kBobCount; ++i) {
			scene::Actor* a = m_actors.get(m_ids[i]);
			if (a == nullptr) {
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
		ctx.targets = &m_target;
		ctx.target_count = 1u;
		ctx.clip = graphics::DirtyRect {0, 0, static_cast<eng::s16>(kWidth),
						static_cast<eng::s16>(kHeight)};
		ctx.buffer = 0u;

		eng::u16 emitted = 0;
		for (eng::u8 i = 0; i < kBobCount; ++i) {
			scene::Actor* a = m_actors.get(m_ids[i]);
			if (a == nullptr) {
				continue;
			}
			if (scene::actor_emit(m_blits, *a, ctx) == scene::ActorEmitStatus::Ok) {
				++emitted;
			}
		}
		if (!backend.execute_frame_plan(m_blits)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008606u);
			return;
		}
		if (!build_frame()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008607u);
			return;
		}
		g_eng_run_status.detail = (static_cast<eng::u32>(emitted) << 8u) |
					  static_cast<eng::u32>(kBobCount);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		// Publica la lista del frame (swap de COP1LC) tras VBlank, como manda el contrato.
		m_plan.commit(backend);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Cielo continuo: un valor por línea de raster, interpolado entre las claves.
	void build_sky() {
		const eng::u16 seg = static_cast<eng::u16>(256u / (kSkyKeys - 1u));
		for (eng::u16 l = 0; l < 256u; ++l) {
			eng::u16 k = static_cast<eng::u16>(l / seg);
			if (k >= kSkyKeys - 1u) {
				k = static_cast<eng::u16>(kSkyKeys - 2u);
			}
			g_sky[l] = lerp444(kSky[k], kSky[k + 1u], static_cast<eng::u16>(l % seg), seg);
		}
	}

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
		for (eng::s16 dy = -16; dy < 16; ++dy) {
			for (eng::s16 dx = -16; dx < 16; ++dx) {
				const eng::s16 d2 = static_cast<eng::s16>(dx * dx + dy * dy);
				if (d2 > 14 * 14) {
					continue;
				}
				const eng::u16 x = static_cast<eng::u16>(dx + 16);
				const eng::u16 y = static_cast<eng::u16>(dy + 16);
				put(kDiscOff, kDiscRow, x, y);
				put(kMaskOff, kDiscRow, x, y);
				if (d2 <= 8 * 8) {
					put(kDiscOff + kDiscPlane, kDiscRow, x, y);
				}
			}
		}
		for (eng::u16 y = 0; y < kSqH; ++y) {
			for (eng::u16 x = 0; x < kSqW; ++x) {
				put(kGlowOff, kSqRow, x, y);
				put(kOpaqueOff, kSqRow, x, y);
				put(kOpaqueOff + kSqPlane, kSqRow, x, y);
			}
		}
	}

	bool add_actors() {
		m_actors.reset();
		m_allocator.reset({0u, 60000u, 0u}); // sin sprites: todo va por Blitter
		const eng::u8* const sheet = m_sheet.view.data();
		for (eng::u8 i = 0; i < kBobCount; ++i) {
			const eng::u8 kind = static_cast<eng::u8>(i % kObjKinds);
			scene::ActorDesc d {};
			d.visual.kind = graphics::VisualKind::Bob;
			d.x = 0;
			d.y = 0;
			d.surface = 0u;
			d.z = static_cast<eng::u8>(10u + i);
			d.preferred = scene::Representation::Bob;
			d.layout = graphics::BobLayout::Planar;
			if (kind == 0u) {
				d.visual.pixels = eng::Span<const eng::u16> {
					reinterpret_cast<const eng::u16*>(sheet + kDiscOff),
					static_cast<eng::usize>(kDiscPlane * kDiscPlanes / 2u)};
				d.visual.mask = eng::Span<const eng::u16> {
					reinterpret_cast<const eng::u16*>(sheet + kMaskOff),
					static_cast<eng::usize>(kDiscPlane / 2u)};
				d.visual.w = kDiscW;
				d.visual.h = kDiscH;
				d.visual.bitplanes = kDiscPlanes;
				d.transparency = scene::TransparencyMode::Mask1Bit;
				d.background = scene::BackgroundPolicy::ClearRect;
			} else if (kind == 1u) {
				d.visual.pixels = eng::Span<const eng::u16> {
					reinterpret_cast<const eng::u16*>(sheet + kGlowOff),
					static_cast<eng::usize>(kSqPlane / 2u)};
				d.visual.w = kSqW;
				d.visual.h = kSqH;
				d.visual.bitplanes = 1u;
				d.transparency = scene::TransparencyMode::AdditiveOr;
				d.background = scene::BackgroundPolicy::ClearRect;
			} else {
				d.visual.pixels = eng::Span<const eng::u16> {
					reinterpret_cast<const eng::u16*>(sheet + kOpaqueOff),
					static_cast<eng::usize>(kSqPlane)};
				d.visual.w = kSqW;
				d.visual.h = kSqH;
				d.visual.bitplanes = 2u;
				d.transparency = scene::TransparencyMode::Opaque;
				d.background = scene::BackgroundPolicy::SaveUnder;
				d.save[0] = eng::Span<eng::u16> {
					reinterpret_cast<eng::u16*>(m_save.view.data()), kSaveWords};
				d.save_words_per_row = 2u; // 16 px + palabra de guarda del shift
				d.save_height = kSqH;
			}
			// Necesidades de Copper del objeto: un arcoíris de `kObjCopperSteps` pasos a lo
			// largo de sus filas (relativo a su Y), que `actor_add_copper` pasa al plan con
			// su z. Es VISIBLE: el color del objeto cambia según la fila.
			eng::u16* colors = g_obj_pal[i];
			for (eng::u8 s = 0; s < kObjCopperSteps; ++s) {
				colors[s + 1u] = kObjRainbow[s];
				graphics::CopperIntent& need = g_obj_needs[i][s];
				need = graphics::CopperIntent {};
				need.kind = graphics::CopperIntentKind::PaletteLine;
				need.top = static_cast<eng::u16>(static_cast<eng::u16>(s) * d.visual.h /
								 kObjCopperSteps);
				need.bottom = need.top;
				need.colors = eng::PaletteWords {&colors[s], 2u}; // cubre el índice 1
				need.first = 1u; // COLOR01
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

	/// Compone el copper del frame: parte estática + cielo por línea + necesidades de
	/// Copper de cada objeto (ancladas a su Y y con su prioridad).
	bool build_frame() {
		m_plan.begin_frame();
		m_plan.scheduler().emit_planes_display(0x2c81u, 0x2cc1u, 0x0038u, 0x00d0u, kBytesPerRow,
						       0x4200u, kPlanes, m_bitmap.view, kPlaneBytes);
		m_plan.scheduler().emit_palette(kPalette.color);
		// Cielo: `kSkyBands` intenciones repartidas por el raster (una por banda). Con
		// `K_086_SKY_BANDS=256` es un valor por línea (continuo) y el frame se dispara a
		// ~9 campos: el coste no es el copper sino el bucle (ver F4.6 del roadmap).
		for (eng::u16 b = 0; b < kSkyBands; ++b) {
			const eng::u16 line = static_cast<eng::u16>(b * (256u / kSkyBands));
			graphics::CopperIntent sky {};
			sky.kind = graphics::CopperIntentKind::PaletteLine;
			sky.top = static_cast<eng::u16>(kFirstLine + line);
			sky.bottom = sky.top;
			sky.colors = eng::PaletteWords {&g_sky[line], 1u};
			sky.first = 0u; // COLOR00
			sky.count = 1u;
			m_plan.add(sky);
		}
		// Necesidades de cada objeto, con su (superficie, z).
		for (eng::u8 i = 0; i < kBobCount; ++i) {
			const scene::Actor* a = m_actors.get(m_ids[i]);
			if (a == nullptr) {
				continue;
			}
			const graphics::Frame f = scene::actor_current_frame(*a);
			const graphics::DirtyRect r = scene::actor_screen_rect(*a, f, 0, 0);
			scene::actor_add_copper(m_plan, *a, r.top, kFirstLine);
		}
		m_plan.materialize();
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

	eng::amiga::MinimalBackend backend {};
	BobObjectsDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
