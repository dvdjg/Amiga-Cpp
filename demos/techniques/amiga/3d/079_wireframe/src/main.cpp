// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/079_wireframe --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/079_wireframe --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/079_wireframe --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/079_wireframe --keep-running

// Demo 079 - wireframe (PORTE 1:1 de demoscene-repo-orig/effects/wireframe/wireframe.c)
//
// Dibuja el objeto `pilka` (malla `obj2c`) en ALAMBRE con la LINEA POR BLITTER, tal
// cual la demo original. El modelo de objeto (formato `obj2c` + `Object3D`) se ha
// portado fiel a `lib3d` en `eng/platform/amiga/object3d.hpp`; la matematica 4.12 viene de
// `lib2d`/`math3d` (HOST-010/011), y la secuencia de registros de la linea vive en
// `AmigaBackend::blitter_line` (identica a `DrawObject`).
//
// Display y doble buffer: ventana 256x256x4 con los registros del original
// (`SetupPlayfield(MODE_LORES,4,X(32),Y(0),256,256)` + `SetupBitplaneFetch`) y doble
// buffer con swap de copperlist por frame (el original rota 5 planos y parchea
// BPLxPT; aqui se usan 2 buffers de 4 planos, equivalente sin tearing).
#include <eng/platform/amiga/object3d.hpp>
#include <eng/api/api.hpp>
#include <eng/graphics/copper/scheduler.hpp>
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
namespace field = eng::field;
namespace graphics = eng::graphics;

// Geometria del original (256x256, 4 planos).
constexpr eng::u16 kWidth = 256;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 4;
constexpr eng::u16 kBytesPerRow = kWidth / 8; // 32
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kHeight;
constexpr eng::u8 kRing = static_cast<eng::u8>(kPlanes + 1); // anillo de 5 (DEPTH+1)
constexpr eng::u32 kBitmapBytes = kPlaneBytes * kRing;
constexpr eng::u32 kCopperPerList = 512;

// Registros del display del original (SetupMode/BitplaneFetch/DisplayWindow para
// MODE_LORES, X(32), Y(0), 256x256). Recalculado de SetupBitplaneFetchImpl.c:
// xs = (0x81+32)<<2 - 4 = 640; fetchstart=64,prefetch=64,w=1024; ddfstrt=(640&-64)-64=576;
// ddfstop=576+1024-64=1536; BPLCON1=(xs&15)*0x11=0. DIW: xs=161, xe=(161+256)&0xff=161
// (ventana de 256 por wrap), ys=0x2c, ye=(0x2c+256)&0xff=0x2c.
constexpr eng::u16 kDiwstrt = 0x2ca1;
constexpr eng::u16 kDiwstop = 0x2ca1;
constexpr eng::u16 kDdfstrt = 0x0048;
constexpr eng::u16 kDdfstop = 0x00c0;
constexpr eng::u16 kBplcon0 = 0x4000;    // 4 planos, color
constexpr eng::u16 kBplcon1 = 0x0000;    // fine scroll 0 (calculado)

// --- Recorrido del object model (PORTADO VERBATIM de wireframe.c) ------------

void update_face_visibility_fast(obj::Object3D& object) {
	const eng::s16 cx = object.camera.x.v;
	const eng::s16 cy = object.camera.y.v;
	const eng::s16 cz = object.camera.z.v;
	eng::s16* group = object.faceGroups;
	eng::s16 f;
	do {
		while ((f = *group++)) {
			eng::s16 px, py, pz;
			{
				obj::Face* face = object.face(f);
				const eng::s16 i = obj::face_indices(face)[0].vertex;
				const obj::Point3D* p = object.point(i);
				px = static_cast<eng::s16>(cx - p->x.v);
				py = static_cast<eng::s16>(cy - p->y.v);
				pz = static_cast<eng::s16>(cz - p->z.v);
			}
			{
				obj::Face* face = object.face(f);
				const eng::s32 v = static_cast<eng::s32>(face->normal[0].v) * px +
						   static_cast<eng::s32>(face->normal[1].v) * py +
						   static_cast<eng::s32>(face->normal[2].v) * pz;
				face->flags = static_cast<eng::s8>(v >= 0 ? 0 : -1);
			}
		}
	} while (*group);
}

void update_edge_visibility(obj::Object3D& object) {
	const eng::s16 s = 1;
	eng::s16* group = object.faceGroups;
	eng::s16 f;
	do {
		while ((f = *group++)) {
			obj::Face* face = object.face(f);
			if (face->flags >= 0) {
				eng::s16* index = reinterpret_cast<eng::s16*>(obj::face_indices(face));
				eng::s16 vertices = static_cast<eng::s16>(face->count - 3);
				eng::s16 i;
				i = *index++; object.node(i)->flags = static_cast<eng::s8>(s);
				i = *index++; object.edge(i)->flags = static_cast<eng::s8>(s);
				i = *index++; object.node(i)->flags = static_cast<eng::s8>(s);
				i = *index++; object.edge(i)->flags = static_cast<eng::s8>(s);
				do {
					i = *index++; object.node(i)->flags = static_cast<eng::s8>(s);
					i = *index++; object.edge(i)->flags = static_cast<eng::s8>(s);
				} while (--vertices != -1);
			}
		}
	} while (*group);
}

// Identidad empaquetada del original: por fila, `(m_i0+y)·(m_i1+x) − x·y` da los tres
// terminos con dos multiplicaciones (los coeficientes de los dos primeros van sumados a
// y/x). `D` recibe la coordenada; `E` es el precalculo de fila (o la traslacion en la 2).
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

void transform_vertices(obj::Object3D& object) {
	eng::math3d::Affine3<>& M = object.objectToWorld;
	eng::s16* group = object.vertexGroups;

	eng::s32 m0 = (static_cast<eng::s32>(M.t.x().v) -
		       eng::retro::normfx(static_cast<eng::s32>(M.m.m[0][0].v) * M.m.m[0][1].v))
		      << 8;
	eng::s32 m1 = (static_cast<eng::s32>(M.t.y().v) -
		       eng::retro::normfx(static_cast<eng::s32>(M.m.m[1][0].v) * M.m.m[1][1].v))
		      << 8;
	// OJO: modifica la matriz de camara (como el original).
	M.t.z().v = static_cast<eng::s16>(
		M.t.z().v - eng::retro::normfx(static_cast<eng::s32>(M.m.m[2][0].v) * M.m.m[2][1].v));

	do {
		eng::s16 i;
		while ((i = *group++)) {
			obj::Node3D* node = object.node(i);
			if (node->flags) {
				eng::s16* pt = reinterpret_cast<eng::s16*>(node);
				eng::s16 x, y, z, zp;
				eng::s32 xy, xp, yp;

				*pt++ = 0; // limpia flags
				x = *pt++;
				y = *pt++;
				z = *pt++;
				xy = static_cast<eng::s32>(x) * y;

				MULVERTEX1(xp, M.m.m[0][0].v, M.m.m[0][1].v, M.m.m[0][2].v, m0);
				MULVERTEX1(yp, M.m.m[1][0].v, M.m.m[1][1].v, M.m.m[1][2].v, m1);
				MULVERTEX2(zp, M.m.m[2][0].v, M.m.m[2][1].v, M.m.m[2][2].v, M.t.z().v);

				*pt++ = static_cast<eng::s16>(eng::math::div_wide(xp, zp) + kWidth / 2);
				*pt++ = static_cast<eng::s16>(eng::math::div_wide(yp, zp) + kHeight / 2);
				*pt++ = zp;
			}
		}
	} while (*group);
}

void draw_object(obj::Object3D& object, field::Surface& surf, graphics::FramePlan& plan,
		 eng::u8 color) {
	eng::s16* group = object.edgeGroups;

	do {
		eng::s16 i;
		while ((i = *group++)) {
			obj::Edge* edge = object.edge(i);
			eng::s16 x0, y0, x1, y1;

			if (edge->flags == 0) {
				continue;
			}
			const eng::s16 e0 = edge->point[0];
			const eng::s16 e1 = edge->point[1];
			edge->flags = 0; // limpia visibilidad

			x0 = object.vertex(e0)->x.v;
			y0 = object.vertex(e0)->y.v;
			x1 = object.vertex(e1)->x.v;
			y1 = object.vertex(e1)->y.v;

			// Línea por el seam (`Surface::draw_line` con plan → Blitter; ver RASTER.md).
			(void)surf.draw_line(x0, y0, x1, y1, color, &plan);
		}
	} while (*group);
}

struct WireframeDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({96u * 1024u, 4u * 1024u, 4u * 1024u});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007901u);
			return;
		}

		m_bitplane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kBitmapBytes, 16);
		m_copper_block = backend.memory().chip.allocate_block<eng::CopperTag>(kRing * kCopperPerList, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007902u);
			return;
		}
		// Lienzo contiguo sobre el anillo de planos, con rasterizador Blitter: las líneas
		// se dibujan por el seam (`Surface::draw_line` + `FramePlan`).
		(void)m_lines_pf.bind_raw(m_bitplane_block.view.data(),
					  static_cast<eng::u32>(m_bitplane_block.view.size()),
					  kWidth, kHeight, kRing, kPlaneBytes);
		m_lines_pf.set_rasterizer(&field::kBlitterRaster);

		for (eng::u8 a = 0; a < kRing; ++a) {
			if (!build_copper(a)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00007903u);
				return;
			}
		}
		backend.takeover_display(m_copper_ptrs[0]);

		obj::new_object3d(m_object, pilka);
		// fx4i(-250) = -250 * 16 = -4000 (4.12).
		m_object.translate.z = eng::retro::q0 {-4000};

		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(pilka.vertices));
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (m_bitplane_block.view.data() == nullptr) {
			return;
		}

		// Anillo de 5 planos (como el original): limpia y dibuja SOLO el plano
		// `active`; la copperlist lo muestra como bit 3 y deja el rastro de los 3
		// frames anteriores en los bits 2..0 (colores 8/4/2/1).
		const eng::u8 active = m_active;
		eng::PlaneBytes plane = m_bitplane_block.view.subspan(static_cast<eng::u32>(active) * kPlaneBytes, kPlaneBytes);

		backend.blitter_clear(plane, 1, kBytesPerRow, kPlaneBytes, kWidth, kHeight);

		m_object.rotate.x = m_object.rotate.y = m_object.rotate.z =
			eng::retro::turns(static_cast<eng::u16>(context.frame.frame_index * 8u));

		obj::update_object_transformation(m_object);
		update_face_visibility_fast(m_object);
		update_edge_visibility(m_object);
		transform_vertices(m_object);
		// Líneas por el seam: color = bit del plano activo (el Copper lo muestra como bit 3).
		field::Surface surf {m_lines_pf, field::SurfaceRect {0, 0, kWidth, kHeight}};
		graphics::FramePlan plan {};
		draw_object(m_object, surf, plan, static_cast<eng::u8>(1u << active));
		if (!backend.execute_frame_plan(plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007904u);
			return;
		}

		backend.install_copper_list(m_copper_ptrs[active]);
		m_active = static_cast<eng::u8>((active + 1u) % kRing);
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Construye la copperlist para el plano `active`: bit3=planes[active],
	/// bit2=planes[active-1], bit1=planes[active-2], bit0=planes[active-3] (mod 5),
	/// como el parcheo de `BPLxPT` del original.
	bool build_copper(eng::u8 active) {
		// Anillo de copperlists: cada tramo del bloque es una lista independiente.
		const eng::Bytes<eng::CopperTag> slice = m_copper_block.view.subspan(
			static_cast<eng::u32>(active) * kCopperPerList, kCopperPerList);
		copper::Scheduler sched { eng::Block<eng::CopperTag> { slice, m_copper_block.kind } };
		sched.emit_planes_display(kDiwstrt, kDiwstop, kDdfstrt, kDdfstop, kBytesPerRow, kBplcon0,
					  kPlanes, m_bitplane_block.view, kPlaneBytes);
		for (eng::u8 n = 0; n < kPlanes; ++n) {
			const eng::u8 idx = static_cast<eng::u8>((active + 2u + n) % kRing);
			sched.move_bitplane_pointer(n, m_bitplane_block.view.address(static_cast<eng::s32>(idx) * static_cast<eng::s32>(kPlaneBytes)));
		}
		sched.move(copper::Register::BPLCON1, kBplcon1);
		sched.emit_palette(wireframe_colors, 0, 16);
		sched.end();
		m_copper_ptrs[active] = sched.data();
		return sched.ok();
	}

	bool m_memory_ok = false;
	eng::u8 m_active = 0;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	field::ContiguousPlayfield m_lines_pf {}; ///< lienzo contiguo para las líneas (seam)
	eng::Block<eng::CopperTag> m_copper_block {};
	const eng::u16* m_copper_ptrs[kRing] = {nullptr, nullptr, nullptr, nullptr, nullptr};
	eng::object3d::Object3D m_object {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	WireframeDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
