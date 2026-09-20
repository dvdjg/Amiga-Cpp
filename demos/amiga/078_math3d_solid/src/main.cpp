#include <eng/retro/fixed_mesh.hpp>
// Demo 078 - Solido 3D relleno (math3d + mesh3d + Blitter).
//
// Extiende la 077: en vez de alambre, pinta las caras RELLENAS en orden
// lejos->cerca (painter's algorithm) con sombreado plano por profundidad, de modo
// que las caras cercanas sobrescriben las lejanas. El borrado del frame anterior
// se delega al **Blitter** (FramePlan + CopyRect desde un buffer en blanco), que
// libera a la CPU del coste dominante (escribir 6 bitplanes a cero).
//
// Cadena validada en hardware (68000, sin soft-float):
//   math3d (rotacion 4.12) -> mesh3d (transform + culling + orden painter)
//   -> rasterizado de triangulos (scanline entero, sin division) -> Blitter clear.
#include <eng/assets/uaf.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/core/mesh3d.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/palette32.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

// Ruta de relleno de caras: 0 = CPU por tramos de byte/word (por defecto,
// verificado), 1 = Blitter-asistido (mascara por CPU + cookie-cut del Blitter a
// los planos de color). Se conservan AMBAS rutinas; elegir con -DK_FILL_BLITTER=1.
//
// NOTA (WIP): la ruta Blitter dibuja pero el resultado aun sale RAYADO (tanto el
// area-fill directo `fill_triangles_blitter` como el cookie-cut
// `blit_fill_from_mask`), sintoma de alineacion/carry de los canales A/C-D del
// Blitter. Por eso el DEFECTO es la CPU (solida y verificada).
#ifndef K_FILL_BLITTER
#define K_FILL_BLITTER 0
#endif

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

// Blob UAF-R de la MALLA, producido por el exportador host
// (`node dist/tools/assets/uaf-pack.js out/assets/uaf/cube.uafr --mesh`, hook
// `src/prebuild.sh`) e incbinado aqui. En runtime se carga con
// `eng::assets::Blob` + `MeshAssetView`, cerrando el ciclo exportador -> runtime
// tambien en 3D (la geometria ya no son constantes de C++). Va a `.text` (solo
// lectura, no necesita Chip RAM).
__asm__(".globl g_cube_uafr\ng_cube_uafr:\n"
	".align 2\n"
	".incbin \"out/assets/uaf/cube.uafr\"\n"
	".globl g_cube_uafr_end\ng_cube_uafr_end:\n");
extern "C" const unsigned char g_cube_uafr[];
extern "C" const unsigned char g_cube_uafr_end[];

namespace {

namespace scene = eng::graphics::composition;
namespace field = eng::field;

constexpr eng::u16 kWidth = 320;
constexpr eng::u16 kHeight = 256;
constexpr eng::u16 kRowBytes = kWidth / 8;
constexpr eng::u8 kPlanes = 6;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kRowBytes) * kHeight;

/// Escena EHB 320x256 (6 planos) sobre `scene::compose`: perfil y presupuesto validados
/// en compilacion.
constexpr scene::SceneResources kRes = scene::planar(kWidth, kHeight, kPlanes);
static_assert(scene::valid_scene(kRes, scene::ocs_a500), "078: EHB 320x256 en A500");

/// Paleta EHB: 0 fondo, 1..7 rampa del solido, 8 marco, 9/10 estrellas.
constexpr eng::Palette32 kPalette {{
	0x012, 0x123, 0x246, 0x358, 0x47a, 0x58c, 0x6ae, 0x8cf,
	0x0ff, 0x046, 0x024, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};


using eng::math3d::Face;
using eng::math3d::MeshView;
using eng::math3d::Vec3;
using eng::math3d::vec3;

constexpr eng::s32 kCX = 160;
constexpr eng::s32 kCY = 126;
constexpr eng::s16 kCamZ = 200;

// Region de borrado por Blitter: debe quedar alineada a word y cubrir el cubo en
// cualquier rotacion (|x|,|y| <= R*sqrt(3), con R=30 -> ~52). 96..224 x 62..190.
constexpr eng::s32 kClearX = 96;
constexpr eng::s32 kClearY = 62;
constexpr eng::s32 kClearW = 128;
constexpr eng::s32 kClearH = 128;
constexpr eng::u16 kClearWords = static_cast<eng::u16>(kClearW / 16); // 8
constexpr eng::u32 kBlankBytes = static_cast<eng::u32>(kClearWords) * 2u * kClearH * kPlanes;

eng::u8 shade_of(eng::s16 zsum) {
	if (zsum > 60) return 7;
	if (zsum > 30) return 6;
	if (zsum > 0)  return 5;
	if (zsum > -30) return 4;
	if (zsum > -60) return 3;
	return 2;
}

/// Auto-test en hardware (igual que HOST-013): con rotacion identidad solo la
/// cara +Z (triangulos 0 y 1) es visible.

struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);

		m_memory_ok = backend.configure_memory({
			96u * 1024u, // Chip: 6 bitplanes EHB + copper + buffer en blanco del Blitter.
			8u * 1024u,
			4u * 1024u,
		});

		m_scene_ok = m_memory_ok &&
			     scene::compose(m_scene, backend.memory(), kRes, scene::ocs_a500,
					    scene::display(scene::kPal320x256, scene::kBplcon0_Ehb),
					    scene::palette(kPalette, 0u, 32u));

		// Buffer en blanco (Chip RAM: el Blitter solo direcciona Chip) para el
		// CopyRect que borra la zona del solido cada frame.
		m_blank_block = backend.memory().chip.allocate_block<eng::PatternTag>(kBlankBytes, 16);
		if (m_blank_block.valid()) {
			eng::u8* b = m_blank_block.view.data();
			for (eng::u32 i = 0; i < kBlankBytes; ++i) b[i] = 0u;
			m_blank = reinterpret_cast<const eng::u16*>(m_blank_block.view.data());
		}
#if K_FILL_BLITTER
		// Plano-mascara 1 bit (Chip RAM) para el relleno por Blitter.
		m_mask_block = backend.memory().chip.allocate_block<eng::MaskTag>(kPlaneBytes, 16);
		if (m_mask_block.valid()) {
			(void)m_mask_pf.bind_raw(m_mask_block.view.data(),
						 static_cast<eng::u32>(m_mask_block.view.size()),
						 kWidth, kHeight, 1u, kPlaneBytes);
		}
#endif

		m_mesh_ok = load_mesh();
#if K_FILL_BLITTER
		const bool mask_ok = m_mask_block.valid();
#else
		const bool mask_ok = true;
#endif
		if (m_memory_ok && m_scene_ok && m_blank_block.valid() && mask_ok && m_mesh_ok) {
			backend.install_raster(m_scene); // Blitter/CPU según las caps del backend
			draw_static();
			m_scene.takeover(backend);
			if (!verify_mesh()) {
				eng::debug::mark_failed(g_eng_run_status, 0x00007801u);
				return;
			}
			eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_scene.words()));
		} else {
			eng::debug::mark_failed(g_eng_run_status, m_mesh_ok ? 0x00000078u : 0x00007803u);
		}
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		(void)backend; // la lista es estatica: `takeover` ya la instalo
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_scene.ok() || !m_blank_block.valid()) {
			return;
		}

		// 1) Borrado por Blitter (CopyRect desde el buffer en blanco). La CPU queda
		//    libre mientras el Blitter vacia los 6 bitplanes de la region.
		eng::graphics::FramePlan plan;
		eng::graphics::BlitJob job {};
		job.source = m_blank;
		job.destination = reinterpret_cast<eng::u16*>(
			m_scene.bitplanes().data() + static_cast<eng::u32>(kClearY) * kRowBytes + kClearX / 8);
		job.words_per_row = kClearWords;
		job.height = static_cast<eng::u16>(kClearH);
		job.source_modulo_bytes = 0; // filas contiguas en la fuente
		job.destination_modulo_bytes = static_cast<eng::s16>(kRowBytes - kClearWords * 2u);
		job.bitplane_count = kPlanes;
		job.source_plane_stride_bytes = static_cast<eng::u32>(kClearWords) * 2u * kClearH;
		job.destination_plane_stride_bytes = kPlaneBytes;
		if (!plan.add_copy_rect(job)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007802u);
			return;
		}
		backend.execute_frame_plan(plan);

		// 2) Rasterizado del solido (cubo = convexo): rotacion + culling, SIN orden painter.
		const eng::u32 f = context.frame.frame_index;
		eng::math3d::Affine3<> m = eng::math3d::Affine3<>::identity();
		eng::math3d::load_rotate(m.m, eng::retro::turns(static_cast<eng::u16>(f * 17u)),
					 eng::retro::turns(static_cast<eng::u16>(f * 11u)),
					 eng::retro::turns(static_cast<eng::u16>(f * 7u)));

		Vec3 world[8];
		eng::math3d::mesh_transform(m_mesh.vertices, m, eng::Span<Vec3>(world, 8));

		eng::math3d::ConvexFace order[12];
		const Vec3 cam = vec3(0, 0, kCamZ);
		const eng::u32 visible = eng::math3d::mesh_convex_order(
			m_mesh, eng::Span<const Vec3>(world, 8), cam,
			eng::Span<eng::math3d::ConvexFace>(order, 12));

#if K_FILL_BLITTER
		field::Surface mc = mask_surface(); // mascara 1 bit (Chip)
#else
		field::Surface c = m_scene.surface();
#endif
		for (eng::u32 i = 0; i < visible; ++i) {
			const Face& fc = m_faces[order[i].index];
			const eng::u8 col = shade_of(eng::math3d::face_z_sum(world[fc.a], world[fc.b], world[fc.c]));
			const eng::s16 ax = static_cast<eng::s16>(kCX + world[fc.a].x().v);
			const eng::s16 ay = static_cast<eng::s16>(kCY - world[fc.a].y().v);
			const eng::s16 bx = static_cast<eng::s16>(kCX + world[fc.b].x().v);
			const eng::s16 by = static_cast<eng::s16>(kCY - world[fc.b].y().v);
			const eng::s16 cx = static_cast<eng::s16>(kCX + world[fc.c].x().v);
			const eng::s16 cy = static_cast<eng::s16>(kCY - world[fc.c].y().v);
#if K_FILL_BLITTER
			// Ruta robusta: la CPU dibuja la mascara (1 plano, por tramos) y el
			// Blitter hace el cookie-cut de la mascara a los planos de color.
			eng::s16 xmin = ax, xmax = ax, ymin = ay, ymax = ay;
			if (bx < xmin) xmin = bx;
			if (bx > xmax) xmax = bx;
			if (cx < xmin) xmin = cx;
			if (cx > xmax) xmax = cx;
			if (by < ymin) ymin = by;
			if (by > ymax) ymax = by;
			if (cy < ymin) ymin = cy;
			if (cy > ymax) ymax = cy;
			mc.fill_rect(xmin, ymin, static_cast<eng::u16>(xmax - xmin + 1),
				     static_cast<eng::u16>(ymax - ymin + 1), 0);
			const eng::s16 tri_x[3] = {ax, bx, cx};
			const eng::s16 tri_y[3] = {ay, by, cy};
			mc.fill_polygon(tri_x, tri_y, 3u, 1u);
			if (!backend.blit_fill_from_mask(
				    m_mask_block.view.as_const(), m_scene.bitplanes(),
				    kPlanes, kRowBytes, kPlaneBytes,
				    xmin, ymin,
				    static_cast<eng::u16>(xmax - xmin + 1),
				    static_cast<eng::u16>(ymax - ymin + 1), col)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00007804u);
				return;
			}
#else
			const eng::s16 tri_x[3] = {ax, bx, cx};
			const eng::s16 tri_y[3] = {ay, by, cy};
			c.fill_polygon(tri_x, tri_y, 3u, col);
#endif
		}

		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Contexto de dibujo de la máscara de 1 plano (solo ruta `K_FILL_BLITTER`).
	field::Surface mask_surface() {
		return field::Surface {m_mask_pf, field::SurfaceRect {0, 0, kWidth, kHeight}};
	}
	/// Carga la malla del blob UAF-R incbinado: `Blob::bind` -> `find(Mesh)` ->
	/// `MeshAssetView` -> copia a buffers del llamador -> `MeshView`. Es el mismo
	/// camino que usaria cualquier asset cocinado (sin parsing pesado en Amiga).
	bool load_mesh() {
		const eng::UafPayload bytes {reinterpret_cast<const eng::u8*>(g_cube_uafr),
		                             static_cast<eng::u32>(g_cube_uafr_end - g_cube_uafr)};
		eng::assets::Blob blob;
		if (!blob.bind(bytes)) {
			return false;
		}
		// `Blob::find` usa el índice por tipo (FlatMap) construido en `bind`.
		const eng::assets::ChunkRef* mesh = blob.find(eng::assets::ChunkType::Mesh);
		if (mesh == nullptr) {
			return false;
		}
		const eng::assets::MeshAssetView mv {blob.data(*mesh)};
		if (!mv.valid() || mv.vertex_count() != 8u || mv.face_count() != 12u) {
			return false;
		}
		for (eng::u32 i = 0; i < 8u; ++i) m_verts[i] = mv.vertex(i);
		for (eng::u32 i = 0; i < 12u; ++i) m_faces[i] = mv.face(i);
		m_mesh = MeshView {eng::Span<const Vec3>(m_verts, 8), eng::Span<const Face>(m_faces, 12)};
		return true;
	}

	/// Auto-test en hardware: con rotacion identidad solo la cara +Z (triangulos 0
	/// y 1) es visible. Si la matematica entera o la lectura UAF-R fallaran, la demo
	/// iria a Failed en vez de Ready.
	bool verify_mesh() const {
		const eng::math3d::Affine3<> id = eng::math3d::Affine3<>::identity();
		Vec3 w[8];
		eng::math3d::mesh_transform(m_mesh.vertices, id, eng::Span<Vec3>(w, 8));
		eng::math3d::FaceOrder order[12];
		const Vec3 cam = vec3(0, 0, kCamZ);
		const eng::u32 n = eng::math3d::mesh_painter_order(
			m_mesh, eng::Span<const Vec3>(w, 8), cam, eng::Span<eng::math3d::FaceOrder>(order, 12));
		return n == 2u && order[0].index == 0u && order[1].index == 1u &&
		       eng::math3d::face_z_sum(w[4], w[5], w[6]) > 0;
	}

	void draw_static() {
		field::Surface c = m_scene.surface();
		for (eng::s32 i = 0; i < 2; ++i) {
			const eng::s32 x0 = 6 + i * 4, y0 = 6 + i * 4;
			const eng::s32 x1 = static_cast<eng::s32>(kWidth) - 7 - i * 4;
			const eng::s32 y1 = static_cast<eng::s32>(kHeight) - 7 - i * 4;
			c.draw_line(x0, y0, x1, y0, 8);
			c.draw_line(x1, y0, x1, y1, 8);
			c.draw_line(x1, y1, x0, y1, 8);
			c.draw_line(x0, y1, x0, y0, 8);
		}
		// Estrellas fuera de la region del solido (que borra el Blitter).
		eng::u16 s = 0xbeefu;
		for (eng::u16 i = 0; i < 120; ++i) {
			s = static_cast<eng::u16>(static_cast<eng::u16>(s * 25173) + 13849);
			const eng::s32 x = 12 + (s & 0xffu);
			s = static_cast<eng::u16>(static_cast<eng::u16>(s * 25173) + 13849);
			const eng::s32 y = 12 + (s & 0xffu);
			if (x >= kClearX - 4 && x <= kClearX + kClearW + 4 &&
			    y >= kClearY - 4 && y <= kClearY + kClearH + 4) {
				continue;
			}
			c.set_pixel(x, y, (i & 1u) ? 9 : 10);
		}
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	Vec3 m_verts[8] {};
	Face m_faces[12] {};
	MeshView m_mesh {};
	bool m_mesh_ok = false;
	eng::Block<eng::PatternTag> m_blank_block {};
	const eng::u16* m_blank = nullptr;
	eng::Block<eng::MaskTag> m_mask_block {};
	field::ContiguousPlayfield m_mask_pf {}; ///< lienzo 1 plano sobre `m_mask_block` (K_FILL_BLITTER)
	scene::Scene m_scene {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	DemoGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
