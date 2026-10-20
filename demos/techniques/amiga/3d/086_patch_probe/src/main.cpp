// ============================================================================
// Demo 086 - SONDA DEL PARCHEO DE COPPERLIST ACTIVA (replica del emit de la 080)
// ============================================================================
//
// Caso MINIMO y aislado del patron de la 080 que sospecha DT-032: la escena emite su
// copperlist con un callback PROPIO (como `emit_display` de la 080) que captura los
// slots con `s.move_at` + `set_plane_patch`; por frame se parchean las palabras de
// DATOS de BPLxPT/BPLCON1 con punteros que cambian (ventana), a MEDIA PANTALLA.
// NO hay geometria, NI fill, NI clip, NI HUD: solo el parcheo + telemetria.
//
// En `init` se VERIFICA cada slot contra la lista materializada: `word[slot.opcode]`
// debe seguir siendo el registro esperado. Si el Plan reordena al materializar, los
// indices de `move_at` quedarian obsoletos y el parcheo escribiria en palabras
// equivocadas; el contador de mismatches queda en `detail` (bits 24-31) y el numero
// de slots en bits 16-23. En `update`, `detail` = ciclos del parcheo del frame.
//
// Ablaciones (DEMO_FLAGS):
//   -DPATCH_PROBE_MODE=0  (defecto) parchear punteros con valor que cambia (ventana).
//   -DPATCH_PROBE_MODE=2  no parchear (control: debe ir rapido).
//   -DPATCH_PROBE_MODE=4  barrido de FASE: el parcheo cae en una posicion distinta del
//                          campo cada frame (milisegundo a milisegundo del haz), para
//                          buscar instantes venenosos (p. ej. cuando el Copper lee la
//                          cabecera de la lista).
//   -DPATCH_PROBE_MODE=5  SIN parcheo de cobre: por frame, relleno por plano de un
//                          rectangulo con PATRON por fases (damero 1x1, periodo 2), el
//                          camino sospechoso de la 080.
//   -DPATCH_PROBE_MODE=6  como 5 pero SOLIDO (control del relleno sin patron).
//   -DPATCH_PROBE_MODE=7  combo exacto de la 080: relleno de sala con patron (fila 8,
//                          193 filas, 352 px) + relleno de paneles solido (plano 2,
//                          fila 52, 104 filas), ambos por frame.
//   -DPATCH_PROBE_BUFFERS2   escena con 2 buffers de display (la 080 usa 2).
//   -DPATCH_PROBE_DELAY=N    ciclos de retardo antes del parcheo (defecto 300000).
// ============================================================================

#define ENG_SCALAR_RETRO16
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/api/api.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/fill_pattern.hpp>
#include <eng/graphics/polygon_planes.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

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

using eng::s16;
using eng::s32;
using eng::u8;
using eng::u16;
using eng::u32;

constexpr u16 kWidth = 352;
constexpr u16 kHeight = 208;
constexpr u8 kPlanes = 4;
constexpr u16 kRowBytes = kWidth / 8u;
constexpr u32 kPlaneBytes = static_cast<u32>(kRowBytes) * kHeight;

constexpr s16 kRoomBoxY = 8;
constexpr u16 kRoomBoxH = 193u; // filas 8..200 (caja de la 080)
constexpr s16 kPanelBoxY = 52;
constexpr u16 kPanelBoxH = 104u; // filas 52..155 (caja de la 080)

constexpr scene::SceneResources kRes = scene::planar(kWidth, kHeight, kPlanes);
static_assert(scene::valid_scene(kRes, scene::ocs_a500), "086: 320x256 4 planos en A500");

constexpr eng::Palette32 kPalette {{
	0x024, 0x911, 0x191, 0xfff, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

#ifndef PATCH_PROBE_MODE
#define PATCH_PROBE_MODE 0
#endif
#ifndef PATCH_PROBE_DELAY
#define PATCH_PROBE_DELAY 300000u // ~2,1 campos PAL: el parcheo cae a media pantalla
#endif
#if defined(PATCH_PROBE_BUFFERS2)
#define PATCH_PROBE_BUFFERS 2u
#else
#define PATCH_PROBE_BUFFERS 1u
#endif

struct PatchProbe {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		scene::SceneResources res = kRes;
		res.buffers = static_cast<u8>(PATCH_PROBE_BUFFERS);
		const bool mem_ok = backend.configure_memory({
			scene::chip_bytes_for(res),
			8u * 1024u,
			4u * 1024u,
		});
#if PATCH_PROBE_MODE == 5 || PATCH_PROBE_MODE == 6 || PATCH_PROBE_MODE == 7 || PATCH_PROBE_MODE == 8 || PATCH_PROBE_MODE == 9
		// Patron 1-bit de 2 filas por plano (damero e inverso), como la 080: reservado
		// ANTES del compose (mismo orden de arena que la demo: patron, luego escena).
		m_pattern = backend.memory_manager().chip().reserve<eng::MaskTag>(
			4u * static_cast<u32>(kRowBytes), 16);
		if (mem_ok && !m_pattern.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008603u);
			return;
		}
		(void)eng::graphics::fill_dither_plane_cpu(m_pattern.view.first(2u * kRowBytes),
							   kRowBytes, 1u, 2u, 0u);
		(void)eng::graphics::fill_dither_plane_cpu(
			m_pattern.view.subspan(2u * kRowBytes, 2u * kRowBytes), kRowBytes, 1u, 2u, 1u);
#endif
		const bool scene_ok =
			mem_ok && scene::compose(m_scene, backend.memory_manager(), res,
						 scene::ocs_a500, scene::palette(kPalette, 0u, 16u),
						 [this](scene::Scene& sc) { emit_display(sc); });
		if (!mem_ok || !scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008602u);
			return;
		}
		backend.install_raster(m_scene);
		m_scene.takeover(backend);

		// Verificacion de slots contra la lista materializada: la palabra de OPCODE del
		// slot debe seguir siendo el registro esperado. Un mismatch = indices obsoletos
		// (reordenacion al materializar) y el parcheo iria a palabras equivocadas.
		const eng::Span<u16> w = m_scene.plan().active_span();
		for (u8 p = 0u; p < kPlanes; ++p) {
			check_slot(w, m_plane_hi[p], static_cast<u16>(0x00E0u + 4u * p));
			check_slot(w, m_plane_lo[p], static_cast<u16>(0x00E2u + 4u * p));
		}
		check_slot(w, m_bplcon1_slot, 0x0102u);
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		const u32 f = context.frame.frame_index;
		const u32 c0 = eng::debug::prof_clock();
#if PATCH_PROBE_MODE == 5 || PATCH_PROBE_MODE == 6 || PATCH_PROBE_MODE == 7 || PATCH_PROBE_MODE == 8 || PATCH_PROBE_MODE == 9
		// Rellenos por plano replicando los de la 080 (sala en la caja 8..200 y paneles
		// solidos en el plano 2, filas 52..155), sin parcheo de cobre ni geometria.
		const eng::PlaneBytes buf = m_scene.buffer(0u);
		eng::graphics::PlaneFillBuilder<16> walls;
#if PATCH_PROBE_MODE == 9
		// Poligono BASURA real: salida de 10 vertices del clip (fuzz del caso extremo,
		// local a la caja 0..351 x 0..192). Auto-intersectado y con paridad impredecible.
		const eng::s16 wx[10] { 154, 165, 352, 352, 349, 300, 77, 0, 0, 26 };
		const eng::s16 wy[10] { 192, 0, 0, 146, 192, 192, 0, 0, 170, 192 };
		(void)walls.submit(wx, wy, 10u, 3u);
#else
		const eng::s16 wx[4] { 0, static_cast<eng::s16>(kWidth - 1u),
				       static_cast<eng::s16>(kWidth - 1u), 0 };
		const eng::s16 wy[4] { 0, 0, static_cast<eng::s16>(kRoomBoxH - 1u),
				       static_cast<eng::s16>(kRoomBoxH - 1u) };
		(void)walls.submit(wx, wy, 4u, 3u);
#endif
#if PATCH_PROBE_MODE == 5 || PATCH_PROBE_MODE == 7 || PATCH_PROBE_MODE == 8 || PATCH_PROBE_MODE == 9
		const eng::MaskBytes pats[2] {
			m_pattern.view.first(2u * kRowBytes).as_const(),
			m_pattern.view.subspan(2u * kRowBytes, 2u * kRowBytes).as_const()};
		(void)backend.fill_polygons_by_plane_patterns(
			walls.faces().data(), walls.count(),
			buf.subspan(static_cast<u32>(kRoomBoxY) * kRowBytes), kRowBytes, kPlaneBytes,
			2u, kWidth, kRoomBoxH, eng::Span<const eng::MaskBytes>(pats, 2u), 2u);
#else
		(void)backend.fill_polygons_by_plane(
			walls.faces().data(), walls.count(),
			buf.subspan(static_cast<u32>(kRoomBoxY) * kRowBytes), kRowBytes, kPlaneBytes,
			2u, kWidth, kRoomBoxH);
#endif
#if PATCH_PROBE_MODE == 6 || PATCH_PROBE_MODE == 7 || PATCH_PROBE_MODE == 8 || PATCH_PROBE_MODE == 9
		// Paneles: solidos, ventana de 2 planos en el plano 2 (como la 080).
		eng::graphics::PlaneFillBuilder<1> panels;
		const eng::s16 px[4] { 0, static_cast<eng::s16>(kWidth - 1u),
				       static_cast<eng::s16>(kWidth - 1u), 0 };
		const eng::s16 py[4] { 0, 0, static_cast<eng::s16>(kPanelBoxH - 1u),
				       static_cast<eng::s16>(kPanelBoxH - 1u) };
		(void)panels.submit(px, py, 4u, 3u);
		const u32 dest_off =
			2u * kPlaneBytes + static_cast<u32>(kPanelBoxY) * kRowBytes;
		(void)backend.fill_polygons_by_plane(panels.faces().data(), panels.count(),
						     buf.subspan(dest_off), kRowBytes, kPlaneBytes,
						     2u, kWidth, kPanelBoxH);
#endif
#else
		// Coloca el parcheo en una posicion temporal concreta del campo: fija (080-like) o
		// barriendo la fase del haz (MODE 4) para localizar instantes venenosos.
#if PATCH_PROBE_MODE == 4
		const u32 delay = 500u + ((f & 63u) * 2200u); // barre ~0..140k (1 campo) cada 64 frames
#else
		const u32 delay = static_cast<u32>(PATCH_PROBE_DELAY);
#endif
		const u32 t0 = eng::debug::prof_clock();
		while (eng::debug::prof_clock() - t0 < delay) {
		}
#endif
#if PATCH_PROBE_MODE == 0 || PATCH_PROBE_MODE == 1 || PATCH_PROBE_MODE == 3 || PATCH_PROBE_MODE == 4 || PATCH_PROBE_MODE == 8
		eng::Span<u16> w = m_scene.plan().active_span();
		// Ventana: desplaza 2 B por frame dentro 0..15 (coarse), como la 080.
		const u32 coarse = (f & 7u) * 2u;
		for (u8 p = 0u; p < kPlanes; ++p) {
			const u8* pc = m_scene.buffer(0u).data() + static_cast<u32>(p) * kPlaneBytes +
				       coarse;
			const u32 addr = reinterpret_cast<u32>(pc);
			eng::copper::patch_data(w, m_plane_hi[p], static_cast<u16>(addr >> 16u));
			eng::copper::patch_data(w, m_plane_lo[p], static_cast<u16>(addr & 0xffffu));
		}
		const u16 fine = static_cast<u16>((f & 7u) | ((f & 7u) << 4u));
		eng::copper::patch_data(w, m_bplcon1_slot, fine);
#endif
		m_last = eng::debug::prof_clock() - c0;
		g_eng_run_status.detail = (static_cast<u32>(m_bad_slots) << 24u) |
					  (static_cast<u32>(m_checked) << 16u) |
					  (m_last & 0xffffu);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	void check_slot(const eng::Span<u16>& w, eng::copper::MoveSlot slot, u16 expect) {
		if (!slot.valid() || slot.opcode_word() >= w.size()) {
			return;
		}
		++m_checked;
		if (w[slot.opcode_word()] != expect) {
			++m_bad_slots;
		}
	}

	// Emit propio, como `emit_display` de la 080: mismos MOVEs (DMACON, BPLCON0/1/2,
	// modulos, ventana, DDF) + pares BPLxPT con `move_at` y `set_plane_patch`.
	void emit_display(scene::Scene& sc) {
		auto& s = sc.scheduler();
		s.move(eng::copper::Register::DMACON,
		       static_cast<u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
				eng::copper::DmaCopper | eng::copper::DmaBitplane));
		s.move(eng::copper::Register::BPLCON0,
		       scene::bplcon0_for(scene::SceneMode::Standard, kPlanes));
		m_bplcon1_slot = s.move_at(eng::copper::Register::BPLCON1, 0x0000u);
		s.move(eng::copper::Register::BPLCON2, 0x0000u);
		s.move(eng::copper::Register::BPL1MOD, 0x0000u);
		s.move(eng::copper::Register::BPL2MOD, 0x0000u);
		s.move(eng::copper::Register::DIWSTRT, static_cast<u16>(0x2c81u));
		s.move(eng::copper::Register::DIWSTOP, static_cast<u16>(0x2cc1u));
		s.move(eng::copper::Register::DDFSTRT, static_cast<u16>(0x0030u));
		s.move(eng::copper::Register::DDFSTOP, static_cast<u16>(0x00d8u));
		const eng::ChipPlaneView planes = sc.chip_planes();
		for (u8 p = 0u; p < kPlanes; ++p) {
			const eng::Address<eng::MemoryKind::Chip> ip =
				planes.address(static_cast<s32>(p) * kPlaneBytes);
			m_plane_hi[p] = s.move_at(eng::copper::bitplane_pointer_high_register(p),
						  static_cast<u16>(ip.value >> 16u));
			m_plane_lo[p] = s.move_at(eng::copper::bitplane_pointer_low_register(p),
						  static_cast<u16>(ip.value & 0xffffu));
			sc.set_plane_patch(p, scene::Patch32 {m_plane_hi[p], m_plane_lo[p]});
		}
	}

	scene::Scene m_scene {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_pattern {};
	eng::copper::MoveSlot m_plane_hi[kPlanes] {};
	eng::copper::MoveSlot m_plane_lo[kPlanes] {};
	eng::copper::MoveSlot m_bplcon1_slot {};
	u32 m_last = 0u;
	u8 m_checked = 0u;
	u8 m_bad_slots = 0u;
	bool m_ready = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	PatchProbe game {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffffu);
	return 0;
}
