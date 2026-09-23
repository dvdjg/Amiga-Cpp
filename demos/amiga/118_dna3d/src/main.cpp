// Demo 118 - dna3d (PORTE PARCIAL de demoscene-repo-orig/effects/dna3d/dna3d.c)
//
// Nucleo del efecto: la DOBLE HELICE de ADN generada en runtime con trigonometria
// (`GenCircularDoubleHelix`) y dibujada como "links" (lineas por Blitter) entre los
// vertices consecutivos. El original anade flares (BOBs OR) en cada vertice y un fondo
// `necrocoq` en doble playfield con color por linea; eso queda pendiente
// (ver docs/demos/effects/DNA3D_PORT_PLAN.md, decisiones en §6).
//
// Convencion de angulo: el original usa el indice 0..4095 (`SIN(a)=sintab[a&0xfff]`); aqui
// `sin(turns(idx))`/`cos(turns(idx))` (tabla 4.12 exacta) y `object.rotate = turns(frame*6)`
// (igual que el original), con `phi_offset = frame*24`.
//
// Build/run:
//   bash ./tools/build/build-demo.sh demos/amiga/118_dna3d --debug
#include <eng/core/math/fixed_affine.hpp>
#include <eng/api/api.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga/object3d.hpp>
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

// --- Asset del original (copiado y adaptado al `Mesh3D` del engine) ----------
using eng::object3d::Mesh3D;
#define __data
using u_short = eng::u16;
#include "data/dna.c"
#include "data/dna-pal.c"
#undef __data

namespace {

namespace obj = eng::object3d;
namespace copper = eng::copper;

using eng::s16;
using eng::s32;
using eng::u8;
using eng::u16;
using eng::u32;

constexpr u16 kWidth = 256;
constexpr u16 kHeight = 256;
constexpr u8 kPlanes = 4;
constexpr u16 kBytesPerRow = kWidth / 8; // 32
constexpr u32 kPlaneBytes = static_cast<u32>(kBytesPerRow) * kHeight;
constexpr u8 kRing = static_cast<u8>(kPlanes + 1); // anillo de 5 (como 079)
constexpr u32 kBitmapBytes = kPlaneBytes * kRing;
constexpr u32 kCopperPerList = 512;

// Display del original (MODE_LORES, X(32), Y(0), 256x256), como 079.
constexpr u16 kDiwstrt = 0x2ca1;
constexpr u16 kDiwstop = 0x2ca1;
constexpr u16 kDdfstrt = 0x0048;
constexpr u16 kDdfstop = 0x00c0;
constexpr u16 kBplcon0 = 0x4000; // 4 planos, color
constexpr u16 kBplcon1 = 0x0000;

constexpr int kNPoints = 40; // iteraciones (2 nodos cada una -> 80 vertices)

/// `swap16(a*b)`: alto de 16 bits del producto de dos `s16` (68000 `swap`).
[[nodiscard]] s16 swap_hi(s16 a, s16 b) {
	return static_cast<s16>((static_cast<s32>(a) * static_cast<s32>(b)) >> 16);
}

/// Port de `GenCircularDoubleHelix`: escribe `point` de los 80 nodos del mesh.
void gen_helix(obj::Object3D& object, s16 phi_offset) {
	using eng::retro::cos;
	using eng::retro::sin;
	using eng::retro::turns;
	obj::Node3D* node = object.node(2); // nodo 0 (objdat + 2 - 2)
	const s16 radius = 10240;           // fx12f(2.5)
	u16 alpha = 0;
	for (int i = 0; i < kNPoints; ++i, alpha = static_cast<u16>(alpha + 1638)) {
		const s16 theta = static_cast<s16>(alpha >> 4);
		const s16 phi = static_cast<s16>(alpha / 4 + phi_offset);
		s16 st = sin(turns(static_cast<u16>(theta))).v;
		s16 ct = cos(turns(static_cast<u16>(theta))).v;
		s16 sp = static_cast<s16>(sin(turns(static_cast<u16>(phi))).v >> 1);
		s16 cp = static_cast<s16>(cos(turns(static_cast<u16>(phi))).v >> 1);
		st = static_cast<s16>(st + st);
		ct = static_cast<s16>(ct + ct);
		node[0].point.x = eng::retro::q0 {swap_hi(static_cast<s16>(radius + cp), ct)};
		node[0].point.y = eng::retro::q0 {swap_hi(static_cast<s16>(radius + sp), st)};
		node[0].point.z = eng::retro::q0 {static_cast<s16>(sp >> 3)};
		const s16 t = cp;
		cp = static_cast<s16>(-sp);
		sp = t;
		node[1].point.x = eng::retro::q0 {swap_hi(static_cast<s16>(radius + cp), ct)};
		node[1].point.y = eng::retro::q0 {swap_hi(static_cast<s16>(radius + sp), st)};
		node[1].point.z = eng::retro::q0 {static_cast<s16>(sp >> 3)};
		node += 2;
	}
}

/// Transforma+proyecta los 80 nodos con las macros `MULVERTEX` del original (los puntos
/// generados son 12.4; el `>>4` de `MULVERTEX1` es el del original).
#define MULVERTEX1(D, E0, E1, E2, E) { \
	eng::s16 t0 = static_cast<eng::s16>((E0) + y); \
	eng::s16 t1 = static_cast<eng::s16>((E1) + x); \
	eng::s32 t2 = static_cast<eng::s32>(E2) * z; \
	D = static_cast<eng::s32>(((static_cast<eng::s32>(t0) * t1 + t2 - xy) >> 4) + E); \
}
#define MULVERTEX2(D, E0, E1, E2, E3) { \
	eng::s16 t0 = static_cast<eng::s16>((E0) + y); \
	eng::s16 t1 = static_cast<eng::s16>((E1) + x); \
	eng::s32 t2 = static_cast<eng::s32>(E2) * z; \
	D = static_cast<eng::s32>(eng::retro::normfx(static_cast<eng::s32>(t0) * t1 + t2 - xy)) + (E3); \
}

void transform_all(obj::Object3D& object) {
	eng::math3d::Affine3<>& M = object.objectToWorld;
	eng::s32 m0 = (static_cast<eng::s32>(M.t.x().v) -
		       eng::retro::normfx(static_cast<eng::s32>(M.m.m[0][0].v) * M.m.m[0][1].v))
		      << 8;
	eng::s32 m1 = (static_cast<eng::s32>(M.t.y().v) -
		       eng::retro::normfx(static_cast<eng::s32>(M.m.m[1][0].v) * M.m.m[1][1].v))
		      << 8;
	M.t.z().v = static_cast<eng::s16>(
		M.t.z().v - eng::retro::normfx(static_cast<eng::s32>(M.m.m[2][0].v) * M.m.m[2][1].v));

	obj::Node3D* n = object.node(2);
	for (int k = 0; k < 2 * kNPoints; ++k) {
		eng::s16 x = n[k].point.x.v;
		eng::s16 y = n[k].point.y.v;
		eng::s16 z = n[k].point.z.v;
		eng::s32 xy = static_cast<eng::s32>(x) * y;
		eng::s32 xp, yp, zp;
		MULVERTEX1(xp, M.m.m[0][0].v, M.m.m[0][1].v, M.m.m[0][2].v, m0);
		MULVERTEX1(yp, M.m.m[1][0].v, M.m.m[1][1].v, M.m.m[1][2].v, m1);
		MULVERTEX2(zp, M.m.m[2][0].v, M.m.m[2][1].v, M.m.m[2][2].v, M.t.z().v);
		n[k].vertex.x = eng::retro::q0 {
			static_cast<eng::s16>(eng::math::div_wide(xp, static_cast<eng::s16>(zp)) + kWidth / 2)};
		n[k].vertex.y = eng::retro::q0 {
			static_cast<eng::s16>(eng::math::div_wide(yp, static_cast<eng::s16>(zp)) + kHeight / 2)};
		n[k].vertex.z = eng::retro::q0 {static_cast<eng::s16>(zp)};
	}
}

/// Port de `DrawLinks`: una linea por cara entre sus dos vertices.
void draw_links(obj::Object3D& object, eng::PlaneBytes plane, eng::amiga::AmigaBackend& backend) {
	s16* group = object.faceGroups;
	do {
		s16 f;
		while ((f = *group++)) {
			obj::Face* face = object.face(f);
			s16* vi = reinterpret_cast<s16*>(obj::face_indices(face));
			const s16 v0 = vi[0];
			const s16 v1 = vi[1]; // formato linea: vertices contiguos (sin campo edge)
			backend.blitter_line(plane, kBytesPerRow, object.vertex(v0)->x.v, object.vertex(v0)->y.v,
					     object.vertex(v1)->x.v, object.vertex(v1)->y.v);
		}
	} while (*group);
}

/// Las DOS hebras (backbone): conecta nodos consecutivos de cada strand. Strand A =
/// nodos pares; strand B = impares (`GenCircularDoubleHelix` escribe 2 nodos por paso).
void draw_strands(obj::Object3D& object, eng::PlaneBytes plane, eng::amiga::AmigaBackend& backend) {
	for (int i = 0; i < kNPoints - 1; ++i) {
		const s16 a0 = static_cast<s16>(2 + 14 * (2 * i));
		const s16 a1 = static_cast<s16>(2 + 14 * (2 * i + 2));
		const s16 b0 = static_cast<s16>(2 + 14 * (2 * i + 1));
		const s16 b1 = static_cast<s16>(2 + 14 * (2 * i + 3));
		backend.blitter_line(plane, kBytesPerRow, object.vertex(a0)->x.v, object.vertex(a0)->y.v,
				     object.vertex(a1)->x.v, object.vertex(a1)->y.v);
		backend.blitter_line(plane, kBytesPerRow, object.vertex(b0)->x.v, object.vertex(b0)->y.v,
				     object.vertex(b1)->x.v, object.vertex(b1)->y.v);
	}
}

struct Dna3DDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({128u * 1024u, 4u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011801u);
			return;
		}
		m_bitplane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kBitmapBytes, 16);
		m_copper_block =
			backend.memory().chip.allocate_block<eng::CopperTag>(kRing * kCopperPerList, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011802u);
			return;
		}
		for (u8 a = 0; a < kRing; ++a) {
			if (!build_copper(a)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00011803u);
				return;
			}
		}
		backend.takeover_display(m_copper_ptrs[0]);

		obj::new_object3d(m_object, dna_helix);
		m_object.translate.z = eng::retro::q0 {-4000}; // fx4i(-250), como 079
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(dna_helix.vertices));
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (m_bitplane_block.view.data() == nullptr) {
			return;
		}
		const u32 f = context.frame.frame_index;
		const u8 active = m_active;
		eng::PlaneBytes plane =
			m_bitplane_block.view.subspan(static_cast<u32>(active) * kPlaneBytes, kPlaneBytes);
		backend.blitter_clear(plane, 1, kBytesPerRow, kPlaneBytes, kWidth, kHeight);

		m_object.rotate.x = m_object.rotate.y = m_object.rotate.z =
			eng::retro::turns(static_cast<u16>(f * 6u)); // == original
		obj::update_object_transformation(m_object);
		gen_helix(m_object, static_cast<s16>(f * 24u));
		transform_all(m_object);
		draw_links(m_object, plane, backend);
		draw_strands(m_object, plane, backend);

		backend.install_copper_list(m_copper_ptrs[active]);
		m_active = static_cast<u8>((active + 1u) % kRing);
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Copperlist del plano `active` (anillo de 5, como 079): bit3=`planes[active]`,
	/// bit2=`planes[active-1]`… (rastro de los 4 frames anteriores).
	bool build_copper(u8 active) {
		const eng::Bytes<eng::CopperTag> slice = m_copper_block.view.subspan(
			static_cast<u32>(active) * kCopperPerList, kCopperPerList);
		copper::Scheduler sched { eng::Block<eng::CopperTag> { slice, m_copper_block.kind } };
		sched.emit_planes_display(kDiwstrt, kDiwstop, kDdfstrt, kDdfstop, kBytesPerRow, kBplcon0,
					  kPlanes, m_bitplane_block.view, kPlaneBytes);
		for (u8 n = 0; n < kPlanes; ++n) {
			const u8 idx = static_cast<u8>((active + 2u + n) % kRing);
			sched.move_bitplane_pointer(
				n, m_bitplane_block.view.address(static_cast<eng::s32>(idx) *
								 static_cast<eng::s32>(kPlaneBytes)));
		}
		sched.move(copper::Register::BPLCON1, kBplcon1);
		sched.emit_palette(dna_colors, 0, 16);
		sched.end();
		m_copper_ptrs[active] = sched.data();
		return sched.ok();
	}

	u8 m_active = 0;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	const u16* m_copper_ptrs[kRing] = {nullptr, nullptr, nullptr, nullptr, nullptr};
	eng::object3d::Object3D m_object {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	Dna3DDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
