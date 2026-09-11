// Demo 079 - wireframe (PORTE 1:1 de demoscene-repo-orig/effects/wireframe/wireframe.c)
//
// Dibuja el objeto `pilka` (malla `obj2c`) en ALAMBRE con la LINEA POR BLITTER, tal
// cual la demo original. El modelo de objeto (formato `obj2c` + `Object3D`) se ha
// portado fiel a `lib3d` en `eng/core/object3d.hpp`; la matematica 4.12 viene de
// `math2d`/`math3d` (ya validada por HOST-010/011), y la secuencia de registros de
// la linea vive en `MinimalBackend::blitter_line` (identica a `DrawObject`).
//
// ADAPTACIONES (version B, para disipar dudas mas tarde):
//   - Display: usamos la ventana 320x256x4 ya conocida del engine (el original era
//     256x256x4 con BPLCON1=0xCC). El OBJETO se centra igual (el efecto proyecta a
//     WIDTH/2, asi que con WIDTH=320 queda centrado); cambia el encuadre, no la
//     geometria.
//   - Sin doble buffer: el original rota 5 planos y parchea BPLxPT por frame; aqui
//     se limpia y dibuja sobre los mismos 4 planos (misma imagen, puede rasgar).
#include <eng/core/object3d.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/arena.hpp>
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

// --- Datos del efecto (copiados TAL CUAL del demoscene) ----------------------
using eng::object3d::Mesh3D;
#define __data
using u_short = eng::u16;
#include "data/pilka.c"
#include "data/wireframe-pal.c"
#undef __data

namespace {

namespace obj = eng::object3d;
namespace copper = eng::copper;

constexpr eng::u16 kWidth = 320;          // ver nota (version B)
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 4;
constexpr eng::u16 kBytesPerRow = kWidth / 8; // 40
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kHeight;
constexpr eng::u32 kBitmapBytes = kPlaneBytes * kPlanes;

// --- Recorrido del object model (PORTADO VERBATIM de wireframe.c) ------------

void update_face_visibility_fast(obj::Object3D& object) {
	const eng::s16 cx = object.camera.x;
	const eng::s16 cy = object.camera.y;
	const eng::s16 cz = object.camera.z;
	void* objdat = object.objdat;
	eng::s16* group = object.faceGroups;
	eng::s16 f;
	do {
		while ((f = *group++)) {
			eng::s16 px, py, pz;
			{
				obj::Face* face = obj::face3d(objdat, f);
				const eng::s16 i = obj::face_indices(face)[0].vertex;
				const obj::Point3D* p = obj::point3d(objdat, i);
				px = static_cast<eng::s16>(cx - p->x);
				py = static_cast<eng::s16>(cy - p->y);
				pz = static_cast<eng::s16>(cz - p->z);
			}
			{
				obj::Face* face = obj::face3d(objdat, f);
				eng::s16* fn = face->normal;
				const eng::s32 v = static_cast<eng::s32>(fn[0]) * px +
						   static_cast<eng::s32>(fn[1]) * py +
						   static_cast<eng::s32>(fn[2]) * pz;
				face->flags = static_cast<eng::s8>(v >= 0 ? 0 : -1);
			}
		}
	} while (*group);
}

void update_edge_visibility(obj::Object3D& object) {
	const eng::s16 s = 1;
	void* objdat = object.objdat;
	eng::s16* group = object.faceGroups;
	eng::s16 f;
	do {
		while ((f = *group++)) {
			obj::Face* face = obj::face3d(objdat, f);
			if (face->flags >= 0) {
				eng::s16* index = reinterpret_cast<eng::s16*>(obj::face_indices(face));
				eng::s16 vertices = static_cast<eng::s16>(face->count - 3);
				eng::s16 i;
				i = *index++; obj::node3d(objdat, i)->flags = static_cast<eng::s8>(s);
				i = *index++; obj::edge3d(objdat, i)->flags = static_cast<eng::s8>(s);
				i = *index++; obj::node3d(objdat, i)->flags = static_cast<eng::s8>(s);
				i = *index++; obj::edge3d(objdat, i)->flags = static_cast<eng::s8>(s);
				do {
					i = *index++; obj::node3d(objdat, i)->flags = static_cast<eng::s8>(s);
					i = *index++; obj::edge3d(objdat, i)->flags = static_cast<eng::s8>(s);
				} while (--vertices != -1);
			}
		}
	} while (*group);
}

#define MULVERTEX1(D, E) { \
	eng::s16 t0 = static_cast<eng::s16>((*v++) + y); \
	eng::s16 t1 = static_cast<eng::s16>((*v++) + x); \
	eng::s32 t2 = static_cast<eng::s32>(*v++) * z; \
	v++; \
	D = static_cast<eng::s32>(((static_cast<eng::s32>(t0) * t1 + t2 - xy) >> 4) + E); \
}

#define MULVERTEX2(D) { \
	eng::s16 t0 = static_cast<eng::s16>((*v++) + y); \
	eng::s16 t1 = static_cast<eng::s16>((*v++) + x); \
	eng::s32 t2 = static_cast<eng::s32>(*v++) * z; \
	eng::s16 t3 = *v++; \
	D = static_cast<eng::s32>(eng::math2d::normfx(static_cast<eng::s32>(t0) * t1 + t2 - xy)) + t3; \
}

void transform_vertices(obj::Object3D& object) {
	eng::math3d::Mat3x3& M = object.objectToWorld;
	void* objdat = object.objdat;
	eng::s16* group = object.vertexGroups;

	eng::s32 m0 = (static_cast<eng::s32>(M.x) - eng::math2d::normfx(static_cast<eng::s32>(M.m00) * M.m01)) << 8;
	eng::s32 m1 = (static_cast<eng::s32>(M.y) - eng::math2d::normfx(static_cast<eng::s32>(M.m10) * M.m11)) << 8;
	// OJO: modifica la matriz de camara (como el original).
	M.z = static_cast<eng::s16>(M.z - eng::math2d::normfx(static_cast<eng::s32>(M.m20) * M.m21));

	do {
		eng::s16 i;
		while ((i = *group++)) {
			obj::Node3D* node = obj::node3d(objdat, i);
			if (node->flags) {
				eng::s16* pt = reinterpret_cast<eng::s16*>(node);
				eng::s16* v = reinterpret_cast<eng::s16*>(&M);
				eng::s16 x, y, z, zp;
				eng::s32 xy, xp, yp;

				*pt++ = 0; // limpia flags
				x = *pt++;
				y = *pt++;
				z = *pt++;
				xy = static_cast<eng::s32>(x) * y;

				MULVERTEX1(xp, m0);
				MULVERTEX1(yp, m1);
				MULVERTEX2(zp);

				*pt++ = static_cast<eng::s16>(eng::math2d::div16(xp, zp) + kWidth / 2);
				*pt++ = static_cast<eng::s16>(eng::math2d::div16(yp, zp) + kHeight / 2);
				*pt++ = zp;
			}
		}
	} while (*group);
}

void draw_object(obj::Object3D& object, eng::u8* bplpt, eng::amiga::MinimalBackend& backend) {
	void* objdat = object.objdat;
	eng::s16* group = object.edgeGroups;

	do {
		eng::s16 i;
		while ((i = *group++)) {
			obj::Edge* edge = obj::edge3d(objdat, i);
			eng::s16 x0, y0, x1, y1;

			if (edge->flags == 0) {
				continue;
			}
			const eng::s16 e0 = edge->point[0];
			const eng::s16 e1 = edge->point[1];
			edge->flags = 0; // limpia visibilidad

			x0 = obj::vertex3d(objdat, e0)->x;
			y0 = obj::vertex3d(objdat, e0)->y;
			x1 = obj::vertex3d(objdat, e1)->x;
			y1 = obj::vertex3d(objdat, e1)->y;

			backend.blitter_line(bplpt, kBytesPerRow, x0, y0, x1, y1);
		}
	} while (*group);
}

struct WireframeDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({48u * 1024u, 4u * 1024u, 4u * 1024u});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007901u);
			return;
		}

		m_bitplane_block = backend.memory().chip.allocate(kBitmapBytes, 16);
		m_copper_block = backend.memory().chip.allocate(1024, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007902u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007903u);
			return;
		}
		backend.takeover_display(m_copper_ptr);

		obj::new_object3d(m_object, pilka);
		// fx4i(-250) = -250 * 16 = -4000 (4.12).
		m_object.translate.z = static_cast<eng::s16>(-4000);

		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(pilka.vertices));
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_bitplanes) {
			return;
		}
		backend.blitter_clear(m_bitplanes, kPlanes, kBytesPerRow, kPlaneBytes, kWidth, kHeight);

		m_object.rotate.x = m_object.rotate.y = m_object.rotate.z =
			static_cast<eng::s16>(context.frame.frame_index * 8u);

		obj::update_object_transformation(m_object);
		update_face_visibility_fast(m_object);
		update_edge_visibility(m_object);
		transform_vertices(m_object);
		draw_object(m_object, m_bitplanes, backend);

		if (m_copper_ptr != nullptr) {
			backend.install_copper_list(m_copper_ptr);
		}
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	bool build_copper() {
		copper::Scheduler sched {m_copper_block};
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4000, kPlanes,
					  m_bitplanes, kPlaneBytes);
		sched.emit_palette(wireframe_colors, 0, 16);
		sched.end();
		m_copper_ok = sched.ok();
		m_copper_words = sched.words_used();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	bool m_memory_ok = false;
	bool m_copper_ok = false;
	eng::u32 m_copper_words = 0;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
	eng::object3d::Object3D m_object {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	WireframeDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffff);

	return 0;
}
