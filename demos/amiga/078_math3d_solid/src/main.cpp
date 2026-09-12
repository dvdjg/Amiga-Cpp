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
#include <eng/core/math3d.hpp>
#include <eng/core/mesh3d.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
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

namespace ehb = eng::graphics::drivers;

constexpr eng::u16 kWidth = ehb::StaticEhbScene::width;
constexpr eng::u16 kHeight = ehb::StaticEhbScene::height;
constexpr eng::u16 kRowBytes = ehb::StaticEhbScene::bytes_per_row;
constexpr eng::u8 kPlanes = ehb::StaticEhbScene::plane_count;
constexpr eng::u32 kPlaneBytes = ehb::StaticEhbScene::plane_bytes;

/// Paleta EHB: 0 fondo, 1..7 rampa del solido, 8 marco, 9/10 estrellas.
constexpr ehb::EhbPalette kPalette {{
	0x012, 0x123, 0x246, 0x358, 0x47a, 0x58c, 0x6ae, 0x8cf,
	0x0ff, 0x046, 0x024, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

/// Canvas planar minimo (demo). Colores < 8 usan solo planos 0..2 (la zona del
/// solido se borra por Blitter a 0 antes de pintarla).
struct Canvas {
	eng::u8* planes = nullptr;
	eng::u8 plane_count = kPlanes;

	void px(eng::s32 x, eng::s32 y, eng::u8 color) {
		if (x < 0 || y < 0 || x >= static_cast<eng::s32>(kWidth) || y >= static_cast<eng::s32>(kHeight)) {
			return;
		}
		const eng::u32 off = static_cast<eng::u32>(y) * kRowBytes + static_cast<eng::u32>(x >> 3);
		const eng::u8 mask = static_cast<eng::u8>(0x80u >> (x & 7));
		// Colores < 8 (rampa) usan hasta 3 planos; el resto, todos los del canvas.
		eng::u8 np = plane_count;
		if (color < 8u && np > 3u) np = 3u;
		for (eng::u8 p = 0; p < np; ++p) {
			eng::u8* base = planes + static_cast<eng::u32>(p) * kPlaneBytes;
			if (color & (1u << p)) {
				base[off] = static_cast<eng::u8>(base[off] | mask);
			} else {
				base[off] = static_cast<eng::u8>(base[off] & static_cast<eng::u8>(~mask));
			}
		}
	}

	void line(eng::s32 x0, eng::s32 y0, eng::s32 x1, eng::s32 y1, eng::u8 color) {
		const eng::s32 dx = x1 > x0 ? x1 - x0 : x0 - x1;
		const eng::s32 dy = y1 > y0 ? y1 - y0 : y0 - y1;
		const eng::s32 sx = x0 < x1 ? 1 : -1;
		const eng::s32 sy = y0 < y1 ? 1 : -1;
		eng::s32 err = dx - dy;
		for (;;) {
			px(x0, y0, color);
			if (x0 == x1 && y0 == y1) break;
			const eng::s32 e2 = 2 * err;
			if (e2 > -dy) { err -= dy; x0 += sx; }
			if (e2 < dx)  { err += dx; y0 += sy; }
		}
	}

	/// Rellena una fila [xl, xr] de un color PLANO escribiendo bytes/words
	/// completos, no píxel a píxel. Es LA diferencia de rendimiento del relleno:
	/// un tramo de 120 px pasa de ~120 RMW por plano a ~4 escrituras por plano.
	/// Solo toca los planos 0..2 (la rampa del sólido usa índices < 8).
	void fill_span(eng::s32 xl, eng::s32 xr, eng::s32 y, eng::u8 color) {
		if (y < 0 || y >= static_cast<eng::s32>(kHeight)) return;
		if (xl > xr) { const eng::s32 t = xl; xl = xr; xr = t; }
		if (xl < 0) xl = 0;
		if (xr > static_cast<eng::s32>(kWidth) - 1) xr = static_cast<eng::s32>(kWidth) - 1;
		if (xl > xr) return;

		const eng::u32 b0 = static_cast<eng::u32>(xl) >> 3;
		const eng::u32 b1 = static_cast<eng::u32>(xr) >> 3;
		const eng::u8 first_mask = static_cast<eng::u8>(0xffu >> (xl & 7));
		const eng::u8 last_mask = static_cast<eng::u8>(0xffu << (7 - (xr & 7)));

		for (eng::u8 p = 0; p < plane_count; ++p) {
			eng::u8* row = planes + static_cast<eng::u32>(p) * kPlaneBytes +
				       static_cast<eng::u32>(y) * kRowBytes;
			const eng::u8 v = (color & (1u << p)) ? 0xffu : 0x00u;
			if (b0 == b1) {
				const eng::u8 m = static_cast<eng::u8>(first_mask & last_mask);
				row[b0] = v ? static_cast<eng::u8>(row[b0] | m)
					    : static_cast<eng::u8>(row[b0] & static_cast<eng::u8>(~m));
				continue;
			}
			row[b0] = v ? static_cast<eng::u8>(row[b0] | first_mask)
				    : static_cast<eng::u8>(row[b0] & static_cast<eng::u8>(~first_mask));
			row[b1] = v ? static_cast<eng::u8>(row[b1] | last_mask)
				    : static_cast<eng::u8>(row[b1] & static_cast<eng::u8>(~last_mask));
			// Bytes completos b0+1 .. b1-1, con words en el tramo alineado.
			eng::u32 i = b0 + 1u;
			const eng::u32 end = b1;
			for (; i < end && (i & 1u) != 0u; ++i) row[i] = v;
			eng::u16* wp = reinterpret_cast<eng::u16*>(row + i);
			const eng::u16 vw = static_cast<eng::u16>((static_cast<eng::u16>(v) << 8) | v);
			const eng::u32 words = (end - i) / 2u;
			for (eng::u32 w = 0; w < words; ++w) wp[w] = vw;
			i += words * 2u;
			for (; i < end; ++i) row[i] = v;
		}
	}

	/// Borra (a 0) el bbox alineado a byte de los planos del canvas (CPU). Se usa
	/// para limpiar la mascara antes de pintar cada cara.
	void clear_rect(eng::s32 x0, eng::s32 y0, eng::s32 x1, eng::s32 y1) {
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > static_cast<eng::s32>(kWidth) - 1) x1 = static_cast<eng::s32>(kWidth) - 1;
		if (y1 > static_cast<eng::s32>(kHeight) - 1) y1 = static_cast<eng::s32>(kHeight) - 1;
		if (x0 > x1 || y0 > y1) return;
		const eng::u32 b0 = static_cast<eng::u32>(x0) >> 3;
		const eng::u32 b1 = static_cast<eng::u32>(x1) >> 3;
		for (eng::u8 p = 0; p < plane_count; ++p) {
			eng::u8* base = planes + static_cast<eng::u32>(p) * kPlaneBytes;
			for (eng::s32 y = y0; y <= y1; ++y) {
				eng::u8* row = base + static_cast<eng::u32>(y) * kRowBytes;
				for (eng::u32 i = b0; i <= b1; ++i) row[i] = 0u;
			}
		}
	}
};

using eng::math3d::Face;
using eng::math3d::MeshView;
using eng::math3d::Vec3;

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

/// DDA entero y sin divisiones: recorre la x de una arista al avanzar la y.
struct Dda {
	eng::s32 x = 0;
	eng::s32 adx = 0;
	eng::s32 dy = 0;
	eng::s32 sx = 1;
	eng::s32 e = 0;

	void init(eng::s32 xs, eng::s32 ys, eng::s32 xe, eng::s32 ye) {
		dy = ye - ys;
		if (dy < 0) dy = -dy;
		const eng::s32 dx = xe - xs;
		adx = dx < 0 ? -dx : dx;
		sx = dx < 0 ? -1 : 1;
		x = xs;
		e = 0;
	}
	void step() {
		e += adx;
		while (e >= dy && dy > 0) {
			e -= dy;
			x += sx;
		}
	}
};

/// Rellena un triangulo por scanline (dos tramos: arriba->medio, medio->abajo),
/// con DDAs enteros (sin divisiones ni floats). Ruta CPU; se conserva aunque
/// K_FILL_BLITTER use el Blitter.
[[maybe_unused]] void fill_tri(Canvas& c, eng::s32 x0, eng::s32 y0, eng::s32 x1, eng::s32 y1,
	      eng::s32 x2, eng::s32 y2, eng::u8 color) {
	eng::s32 xa = x0, ya = y0, xb = x1, yb = y1, xc = x2, yc = y2;
	if (ya > yb) { const eng::s32 t = xa; xa = xb; xb = t; const eng::s32 u = ya; ya = yb; yb = u; }
	if (ya > yc) { const eng::s32 t = xa; xa = xc; xc = t; const eng::s32 u = ya; ya = yc; yc = u; }
	if (yb > yc) { const eng::s32 t = xb; xb = xc; xc = t; const eng::s32 u = yb; yb = yc; yc = u; }
	if (ya == yc) return;

	Dda ac, ab, bc;
	ac.init(xa, ya, xc, yc);
	ab.init(xa, ya, xb, yb);
	bc.init(xb, yb, xc, yc);

	for (eng::s32 y = ya; y < yb; ++y) {
		c.fill_span(ab.x, ac.x, y, color);
		ab.step();
		ac.step();
	}
	for (eng::s32 y = yb; y < yc; ++y) {
		c.fill_span(bc.x, ac.x, y, color);
		bc.step();
		ac.step();
	}
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

		const ehb::StaticEhbSceneConfig scene_config {&kPalette, nullptr, 0, 1024};
		m_scene_ok = m_scene.init(backend.memory(), scene_config);

		// Buffer en blanco (Chip RAM: el Blitter solo direcciona Chip) para el
		// CopyRect que borra la zona del solido cada frame.
		m_blank_block = backend.memory().chip.allocate(kBlankBytes, 16);
		if (m_blank_block.valid()) {
			eng::u8* b = static_cast<eng::u8*>(m_blank_block.data);
			for (eng::u32 i = 0; i < kBlankBytes; ++i) b[i] = 0u;
			m_blank = static_cast<const eng::u16*>(m_blank_block.data);
		}
#if K_FILL_BLITTER
		// Plano-mascara 1 bit (Chip RAM) para el relleno por Blitter.
		m_mask_block = backend.memory().chip.allocate(kPlaneBytes, 16);
		if (m_mask_block.valid()) {
			m_mask = static_cast<eng::u8*>(m_mask_block.data);
		}
#endif

		m_mesh_ok = load_mesh();
#if K_FILL_BLITTER
		const bool mask_ok = m_mask_block.valid();
#else
		const bool mask_ok = true;
#endif
		if (m_memory_ok && m_scene_ok && m_blank_block.valid() && mask_ok && m_mesh_ok) {
			draw_static();
			m_scene.takeover(backend);
			if (!verify_mesh()) {
				eng::debug::mark_failed(g_eng_run_status, 0x00007801u);
				return;
			}
			eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_scene.copper_words()));
		} else {
			eng::debug::mark_failed(g_eng_run_status, m_mesh_ok ? 0x00000078u : 0x00007803u);
		}
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (m_scene.ok()) {
			m_scene.install(backend);
		}
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
			m_scene.bitplanes() + static_cast<eng::u32>(kClearY) * kRowBytes + kClearX / 8);
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

		// 2) Rasterizado del solido: rotacion + culling + orden painter + relleno.
		const eng::u32 f = context.frame.frame_index;
		eng::math3d::Mat3x3 m;
		eng::math3d::load_rotate(m, static_cast<eng::u16>(f * 17u), static_cast<eng::u16>(f * 11u),
					 static_cast<eng::u16>(f * 7u));

		Vec3 world[8];
		eng::math3d::mesh_transform(m_mesh.vertices, m, eng::Span<Vec3>(world, 8));

		eng::math3d::FaceOrder order[12];
		const Vec3 cam {0, 0, kCamZ};
		const eng::u32 visible = eng::math3d::mesh_painter_order(
			m_mesh, eng::Span<const Vec3>(world, 8), cam, eng::Span<eng::math3d::FaceOrder>(order, 12));

#if K_FILL_BLITTER
		Canvas mc {m_mask, 1}; // mascara 1 bit (Chip)
#else
		Canvas c {m_scene.bitplanes()};
#endif
		for (eng::u32 i = 0; i < visible; ++i) {
			const Face& fc = m_faces[order[i].index];
			const eng::u8 col = shade_of(eng::math3d::face_z_sum(world[fc.a], world[fc.b], world[fc.c]));
			const eng::s16 ax = static_cast<eng::s16>(kCX + world[fc.a].x);
			const eng::s16 ay = static_cast<eng::s16>(kCY - world[fc.a].y);
			const eng::s16 bx = static_cast<eng::s16>(kCX + world[fc.b].x);
			const eng::s16 by = static_cast<eng::s16>(kCY - world[fc.b].y);
			const eng::s16 cx = static_cast<eng::s16>(kCX + world[fc.c].x);
			const eng::s16 cy = static_cast<eng::s16>(kCY - world[fc.c].y);
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
			mc.clear_rect(xmin, ymin, xmax, ymax);
			fill_tri(mc, ax, ay, bx, by, cx, cy, 1u);
			if (!backend.blit_fill_from_mask(m_mask, m_scene.bitplanes(), kPlanes, kRowBytes,
							 kPlaneBytes, xmin, ymin,
							 static_cast<eng::u16>(xmax - xmin + 1),
							 static_cast<eng::u16>(ymax - ymin + 1), col)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00007804u);
				return;
			}
#else
			fill_tri(c, ax, ay, bx, by, cx, cy, col);
#endif
		}

		m_scene.install(backend);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Carga la malla del blob UAF-R incbinado: `Blob::bind` -> `find(Mesh)` ->
	/// `MeshAssetView` -> copia a buffers del llamador -> `MeshView`. Es el mismo
	/// camino que usaria cualquier asset cocinado (sin parsing pesado en Amiga).
	bool load_mesh() {
		const eng::Span<const eng::u8> bytes(reinterpret_cast<const eng::u8*>(g_cube_uafr),
						     static_cast<eng::u32>(g_cube_uafr_end - g_cube_uafr));
		eng::assets::Blob blob;
		if (!blob.bind(bytes)) {
			return false;
		}
		eng::u32 index = 0;
		bool found = false;
		for (eng::u32 i = 0; i < blob.chunk_count(); ++i) {
			if (blob.chunk(i).type == eng::assets::ChunkType::Mesh) {
				index = i;
				found = true;
				break;
			}
		}
		if (!found) {
			return false;
		}
		const eng::assets::MeshAssetView mv {blob.data(index)};
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
		eng::math3d::Mat3x3 id;
		eng::math3d::load_identity(id);
		Vec3 w[8];
		eng::math3d::mesh_transform(m_mesh.vertices, id, eng::Span<Vec3>(w, 8));
		eng::math3d::FaceOrder order[12];
		const Vec3 cam {0, 0, kCamZ};
		const eng::u32 n = eng::math3d::mesh_painter_order(
			m_mesh, eng::Span<const Vec3>(w, 8), cam, eng::Span<eng::math3d::FaceOrder>(order, 12));
		return n == 2u && order[0].index == 0u && order[1].index == 1u &&
		       eng::math3d::face_z_sum(w[4], w[5], w[6]) > 0;
	}

	void draw_static() {
		Canvas c {m_scene.bitplanes()};
		for (eng::s32 i = 0; i < 2; ++i) {
			const eng::s32 x0 = 6 + i * 4, y0 = 6 + i * 4;
			const eng::s32 x1 = static_cast<eng::s32>(kWidth) - 7 - i * 4;
			const eng::s32 y1 = static_cast<eng::s32>(kHeight) - 7 - i * 4;
			c.line(x0, y0, x1, y0, 8);
			c.line(x1, y0, x1, y1, 8);
			c.line(x1, y1, x0, y1, 8);
			c.line(x0, y1, x0, y0, 8);
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
			c.px(x, y, (i & 1u) ? 9 : 10);
		}
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	Vec3 m_verts[8] {};
	Face m_faces[12] {};
	MeshView m_mesh {};
	bool m_mesh_ok = false;
	eng::MemoryBlock m_blank_block {};
	const eng::u16* m_blank = nullptr;
	eng::MemoryBlock m_mask_block {};
	eng::u8* m_mask = nullptr;
	ehb::StaticEhbScene m_scene {};
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
