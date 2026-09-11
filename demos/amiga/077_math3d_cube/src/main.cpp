// Demo 077 - Cubo 3D en alambre (math3d + mesh3d) sobre EHB.
//
// Objetivo: validar EN HARDWARE (68000, sin soft-float) la cadena
//   math3d (rotacion 4.12) -> mesh3d (transform + back-face culling + painter)
//   -> primitivas de linea
// dibujando un cubo que gira a 50 fps. Solo se pintan las caras VISIBLES y en
// orden lejos->cerca, que es justo lo que devuelve `mesh_painter_order`.
//
// El "canvas" planar de dibujo vive en la demo (cocina), como en la 030: el
// driver `StaticEhbScene` entrega los bitplanes y aqui se escriben con un
// set_pixel/line minimos. El trazado se hace en `render()` (durante el vblank):
// escribir CPU al Chip RAM con el DMA de bitplanes activo roba ciclos y produce
// scanlines negros (ver 107).
#include <eng/core/math3d.hpp>
#include <eng/core/mesh3d.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/platform/amiga_minimal.hpp>

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

namespace ehb = eng::graphics::drivers;

constexpr eng::u16 kWidth = ehb::StaticEhbScene::width;
constexpr eng::u16 kHeight = ehb::StaticEhbScene::height;
constexpr eng::u16 kRowBytes = ehb::StaticEhbScene::bytes_per_row;
constexpr eng::u8 kPlanes = ehb::StaticEhbScene::plane_count;
constexpr eng::u32 kPlaneBytes = ehb::StaticEhbScene::plane_bytes;

/// Paleta EHB de 32 colores base (los indices 32..63 son su mitad de brillo). El
/// indice 0 es el fondo; 1..7 son la rampa del cubo; 8 el marco; 9/10 las estrellas.
constexpr ehb::EhbPalette kPalette {{
	0x012, 0x111, 0x22a, 0x33c, 0x44e, 0x55f, 0x77f, 0x9bf,
	0x0ff, 0x046, 0x024, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

/// Canvas planar minimo de la demo (escritura directa a los 6 bitplanes EHB).
///
/// Se escribe bit a bit en cada plano: `index & (1<<p)` decide el bit del plano
/// `p`. Es el mismo mapeo que usaria el driver, pero local a la demo para no
/// arrastrar toda la maquinaria de `Playfield` en un test de matematicas.
struct Canvas {
	eng::u8* planes = nullptr;

	void px(eng::s32 x, eng::s32 y, eng::u8 color) {
		if (x < 0 || y < 0 || x >= static_cast<eng::s32>(kWidth) || y >= static_cast<eng::s32>(kHeight)) {
			return;
		}
		const eng::u32 off = static_cast<eng::u32>(y) * kRowBytes + static_cast<eng::u32>(x >> 3);
		const eng::u8 mask = static_cast<eng::u8>(0x80u >> (x & 7));
		for (eng::u8 p = 0; p < kPlanes; ++p) {
			eng::u8* base = planes + static_cast<eng::u32>(p) * kPlaneBytes;
			if (color & (1u << p)) {
				base[off] = static_cast<eng::u8>(base[off] | mask);
			} else {
				base[off] = static_cast<eng::u8>(base[off] & static_cast<eng::u8>(~mask));
			}
		}
	}

	/// Línea oblicua (Bresenham entero, sin divisiones ni floats).
	void line(eng::s32 x0, eng::s32 y0, eng::s32 x1, eng::s32 y1, eng::u8 color) {
		const eng::s32 dx = x1 > x0 ? x1 - x0 : x0 - x1;
		const eng::s32 dy = y1 > y0 ? y1 - y0 : y0 - y1;
		const eng::s32 sx = x0 < x1 ? 1 : -1;
		const eng::s32 sy = y0 < y1 ? 1 : -1;
		eng::s32 err = dx - dy;
		for (;;) {
			px(x0, y0, color);
			if (x0 == x1 && y0 == y1) {
				break;
			}
			const eng::s32 e2 = 2 * err;
			if (e2 > -dy) { err -= dy; x0 += sx; }
			if (e2 < dx)  { err += dx; y0 += sy; }
		}
	}

	/// Borra (a 0 = fondo) una zona alineada a byte. `x` se redondea a la baja y
	/// `x1` al alza para cubrir el borde redondeado sin dejar restos del frame
	/// anterior.
	void clear_rect(eng::s32 x0, eng::s32 y0, eng::s32 x1, eng::s32 y1) {
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > static_cast<eng::s32>(kWidth) - 1) x1 = static_cast<eng::s32>(kWidth) - 1;
		if (y1 > static_cast<eng::s32>(kHeight) - 1) y1 = static_cast<eng::s32>(kHeight) - 1;
		if (x0 > x1 || y0 > y1) return;
		const eng::u32 bx0 = static_cast<eng::u32>(x0) >> 3;
		const eng::u32 bx1 = static_cast<eng::u32>(x1) >> 3;
		const eng::u32 n = bx1 - bx0 + 1u;
		for (eng::u8 p = 0; p < kPlanes; ++p) {
			eng::u8* base = planes + static_cast<eng::u32>(p) * kPlaneBytes;
			for (eng::s32 y = y0; y <= y1; ++y) {
				eng::u8* row = base + static_cast<eng::u32>(y) * kRowBytes + bx0;
				for (eng::u32 i = 0; i < n; ++i) {
					row[i] = 0u;
				}
			}
		}
	}
};

using eng::math3d::Face;
using eng::math3d::MeshView;
using eng::math3d::Vec3;

/// Cubo unidad de radio `kR`, 8 vertices compartidos y 12 triangulos con
/// orientacion CCW vista desde FUERA (necesario para que `face_visible` haga
/// back-face culling correcto: el producto mixto solo es positivo si la cara
/// mira a la camara).
constexpr eng::s16 kR = 48;
constexpr Vec3 kVertices[8] = {
	{-kR, -kR, -kR}, {kR, -kR, -kR}, {kR, kR, -kR}, {-kR, kR, -kR},
	{-kR, -kR,  kR}, {kR, -kR,  kR}, {kR, kR,  kR}, {-kR, kR,  kR},
};
constexpr Face kFaces[12] = {
	{4, 5, 6}, {4, 6, 7}, // +Z (frente)
	{0, 3, 2}, {0, 2, 1}, // -Z (detras)
	{7, 6, 2}, {7, 2, 3}, // +Y (arriba)
	{0, 1, 5}, {0, 5, 4}, // -Y (abajo)
	{1, 2, 6}, {1, 6, 5}, // +X (derecha)
	{0, 4, 7}, {0, 7, 3}, // -X (izquierda)
};

constexpr eng::s32 kCX = 160;
constexpr eng::s32 kCY = 118;
constexpr eng::s16 kCamZ = 200;
constexpr eng::s32 kHalfSpan = 92; // media anchura de la zona a borrar (> kR*sqrt(3))

/// Brillo segun la profundidad de la cara (suma de z de sus 3 vertices): las
/// caras mas cercanas (z alta, hacia la camara) se pintan mas claras. Sin
/// division: umbrales enteros fijos.
eng::u8 shade_of(eng::s16 zsum) {
	if (zsum > 100) return 7;
	if (zsum > 50)  return 6;
	if (zsum > 0)   return 5;
	if (zsum > -50) return 4;
	if (zsum > -100) return 3;
	return 2;
}

struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);

		m_memory_ok = backend.configure_memory({
			70u * 1024u, // Chip: 6 bitplanes EHB (61 KB) + copperlist.
			8u * 1024u,  // Slow.
			4u * 1024u,  // Frame scratch.
		});

		const ehb::StaticEhbSceneConfig scene_config {
			&kPalette,
			nullptr,
			0,
			1024,
		};
		m_scene_ok = m_scene.init(backend.memory(), scene_config);

		if (m_memory_ok && m_scene_ok) {
			draw_static();
			m_scene.takeover(backend);
			eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_scene.copper_words()));
		} else {
			eng::debug::mark_failed(g_eng_run_status, 0x00000077u);
		}
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (m_scene.ok()) {
			m_scene.install(backend);
		}
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_scene.ok()) {
			return;
		}
		Canvas c {m_scene.bitplanes()};
		const eng::u32 f = context.frame.frame_index;

		// Rotacion compuesta Rx(f*3)·Ry(f*2)·Rz(f) en 4.12. Angulos de 12 bits:
		// el seno/coseno vienen de la tabla de `math2d` (enteros, sin libm).
		eng::math3d::Mat3x3 m;
		eng::math3d::load_rotate(m, static_cast<eng::u16>(f * 3u), static_cast<eng::u16>(f * 2u),
					 static_cast<eng::u16>(f));

		Vec3 world[8];
		const MeshView mesh {eng::Span<const Vec3>(kVertices, 8), eng::Span<const Face>(kFaces, 12)};
		eng::math3d::mesh_transform(mesh.vertices, m, eng::Span<Vec3>(world, 8));

		eng::math3d::FaceOrder order[12];
		const Vec3 cam {0, 0, kCamZ};
		const eng::u32 visible = eng::math3d::mesh_painter_order(
			mesh, eng::Span<const Vec3>(world, 8), cam, eng::Span<eng::math3d::FaceOrder>(order, 12));

		c.clear_rect(kCX - kHalfSpan, kCY - kHalfSpan, kCX + kHalfSpan, kCY + kHalfSpan);

		// Proyeccion ortografica (sin division): x a la derecha, y hacia arriba.
		for (eng::u32 i = 0; i < visible; ++i) {
			const Face& fc = kFaces[order[i].index];
			const eng::u8 col = shade_of(eng::math3d::face_z_sum(world[fc.a], world[fc.b], world[fc.c]));
			const eng::s32 ax = kCX + world[fc.a].x, ay = kCY - world[fc.a].y;
			const eng::s32 bx = kCX + world[fc.b].x, by = kCY - world[fc.b].y;
			const eng::s32 dx = kCX + world[fc.c].x, dy = kCY - world[fc.c].y;
			c.line(ax, ay, bx, by, col);
			c.line(bx, by, dx, dy, col);
			c.line(dx, dy, ax, ay, col);
		}

		m_scene.install(backend);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Marco y estrellas estaticas: solo se pintan una vez. El borrado por frame
	/// toca unicamente la zona central del cubo, asi que el marco no se pierde.
	void draw_static() {
		Canvas c {m_scene.bitplanes()};

		// Doble marco.
		for (eng::s32 i = 0; i < 2; ++i) {
			const eng::s32 x0 = 6 + i * 4, y0 = 6 + i * 4;
			const eng::s32 x1 = static_cast<eng::s32>(kWidth) - 7 - i * 4;
			const eng::s32 y1 = static_cast<eng::s32>(kHeight) - 7 - i * 4;
			c.line(x0, y0, x1, y0, 8);
			c.line(x1, y0, x1, y1, 8);
			c.line(x1, y1, x0, y1, 8);
			c.line(x0, y1, x0, y0, 8);
		}

		// Estrellas (LCG de 16 bits) fuera de la zona del cubo. Sin modulo: se
		// recorta con mascaras de potencia de dos y se desplaza, para no arrastrar
		// divisiones software de 32 bits (el runtime no trae libgcc).
		eng::u16 s = 0xace1u;
		for (eng::u16 i = 0; i < 160; ++i) {
			s = static_cast<eng::u16>(static_cast<eng::u16>(s * 25173) + 13849);
			const eng::s32 x = 12 + (s & 0xffu);
			s = static_cast<eng::u16>(static_cast<eng::u16>(s * 25173) + 13849);
			const eng::s32 y = 12 + (s & 0xffu);
			if (x > kCX - kHalfSpan - 4 && x < kCX + kHalfSpan + 4 &&
			    y > kCY - kHalfSpan - 4 && y < kCY + kHalfSpan + 4) {
				continue; // no ensuciar la zona que se borra cada frame
			}
			c.px(x, y, (i & 1u) ? 9 : 10);
		}
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	ehb::StaticEhbScene m_scene {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	DemoGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffff);

	return 0;
}
