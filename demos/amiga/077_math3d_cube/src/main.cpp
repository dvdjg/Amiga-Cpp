#include <eng/retro/fixed_mesh.hpp>
// Demo 077 - Cubo 3D en alambre (math3d + mesh3d) sobre EHB.
//
// Objetivo: validar EN HARDWARE (68000, sin soft-float) la cadena
//   math3d (rotacion 4.12) -> mesh3d (transform + back-face culling + painter)
//   -> primitivas de linea
// dibujando un cubo que gira a 50 fps. Solo se pintan las caras VISIBLES y en
// orden lejos->cerca, que es justo lo que devuelve `mesh_painter_order`.
//
// El dibujo va por `scene.surface()` (un `field::Surface` sobre los planos contiguos):
// la demo pide lineas/pixeles y el engine enruta al layout, sin que la app vea planos
// ni punteros. El trazado se hace en `render()` (durante el vblank): escribir CPU al
// Chip RAM con el DMA de bitplanes activo roba ciclos y produce scanlines negros (ver 107).
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/core/mesh3d.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/palette32.hpp>
#include <eng/graphics/scene/compose.hpp>
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

namespace scene = eng::graphics::scene;
namespace field = eng::field;
namespace graphics = eng::graphics;

constexpr eng::u16 kWidth = 320;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 6;

/// Escena EHB 320x256 (6 planos) sobre `scene::compose`: perfil y presupuesto validados
/// en compilacion.
constexpr scene::SceneResources kRes = scene::planar(kWidth, kHeight, kPlanes);
static_assert(scene::valid_scene(kRes, scene::ocs_a500), "077: EHB 320x256 en A500");

/// Paleta EHB de 32 colores base (los indices 32..63 son su mitad de brillo). El
/// indice 0 es el fondo; 1..7 son la rampa del cubo; 8 el marco; 9/10 las estrellas.
constexpr eng::Palette32 kPalette {{
	0x012, 0x111, 0x22a, 0x33c, 0x44e, 0x55f, 0x77f, 0x9bf,
	0x0ff, 0x046, 0x024, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

using eng::math3d::Face;
using eng::math3d::MeshView;
using eng::math3d::Vec3;
using eng::math3d::vec3;

/// Cubo unidad de radio `kR`, 8 vertices compartidos y 12 triangulos con
/// orientacion CCW vista desde FUERA (necesario para que `face_visible` haga
/// back-face culling correcto: el producto mixto solo es positivo si la cara
/// mira a la camara).
constexpr eng::s16 kR = 48;
constexpr Vec3 kVertices[8] = {
	vec3(-kR, -kR, -kR), vec3(kR, -kR, -kR), vec3(kR, kR, -kR), vec3(-kR, kR, -kR),
	vec3(-kR, -kR, kR), vec3(kR, -kR, kR), vec3(kR, kR, kR), vec3(-kR, kR, kR),
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

/// Aristas REALES del cubo (pares de vertices). El mesh esta formado por
/// triangulos, y cada cuadro tiene una diagonal de triangulacion que NO es una
/// arista del cubo: filtrandola se obtiene un alambre limpio.
constexpr eng::u16 kEdges[12][2] = {
	{0, 1}, {1, 2}, {2, 3}, {3, 0}, // cara -Z
	{4, 5}, {5, 6}, {6, 7}, {7, 4}, // cara +Z
	{0, 4}, {1, 5}, {2, 6}, {3, 7}, // aristas verticales
};

constexpr bool is_cube_edge(eng::u16 a, eng::u16 b) {
	for (const auto& e : kEdges) {
		if ((e[0] == a && e[1] == b) || (e[0] == b && e[1] == a)) {
			return true;
		}
	}
	return false;
}

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

/// Dibuja una arista de una cara SI es una de las 12 aristas del cubo (descarta
/// las diagonales de triangulacion).
void draw_edge(field::Surface& c, const Vec3* w, eng::u16 p, eng::u16 q, eng::u8 col) {
	if (!is_cube_edge(p, q)) {
		return;
	}
	c.draw_line(kCX + w[p].x().v, kCY - w[p].y().v, kCX + w[q].x().v, kCY - w[q].y().v, col);
}

/// Auto-test EN HARDWARE (misma comprobacion que HOST-013, sobre el 68000): con
/// la rotacion identidad, la camara en +Z debe ver UNICAMENTE la cara +Z, que son
/// los triangulos 0 y 1 del mesh. Si la matematica entera fallara en m68k (por
/// ejemplo por overflow de 16 bits en `normfx`), el conteo o el orden cambiarian
/// y la demo iria a Failed en vez de Ready.
bool verify_mesh() {
	const eng::math3d::Affine3<> id = eng::math3d::Affine3<>::identity();
	Vec3 w[8];
	const MeshView mesh {eng::Span<const Vec3>(kVertices, 8), eng::Span<const Face>(kFaces, 12)};
	eng::math3d::mesh_transform(mesh.vertices, id, eng::Span<Vec3>(w, 8));

	eng::math3d::FaceOrder order[12];
	const Vec3 cam = vec3(0, 0, kCamZ);
	const eng::u32 n = eng::math3d::mesh_painter_order(
		mesh, eng::Span<const Vec3>(w, 8), cam, eng::Span<eng::math3d::FaceOrder>(order, 12));

	return n == 2u && order[0].index == 0u && order[1].index == 1u &&
	       eng::math3d::face_z_sum(w[4], w[5], w[6]) > 0;
}

struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);

		m_memory_ok = backend.configure_memory({
			70u * 1024u, // Chip: 6 bitplanes EHB (61 KB) + copperlist.
			8u * 1024u,  // Slow.
			4u * 1024u,  // Frame scratch.
		});

		m_scene_ok = m_memory_ok &&
			     scene::compose(m_scene, backend.memory(), kRes, scene::ocs_a500,
					    scene::display(scene::kPal320x256, scene::kBplcon0_Ehb),
					    scene::palette(kPalette, 0u, 32u));

		if (m_memory_ok && m_scene_ok) {
			backend.install_raster(m_scene); // Blitter/CPU según las caps del backend
			draw_static();
			// Validacion de la **linea por Blitter** (seam): dibuja un triangulo fijo con
			// el Blitter (FramePlan) y lo ejecuta; evidencia de la ruta BlitJobKind::Line.
			{
				graphics::FramePlan plan {};
				// `DrawTarget` agrupa Surface + Rasterizer + plan: las lineas se encolan
				// en el mismo plan sin pasar el `&plan` en cada llamada.
				field::DrawTarget dt = m_scene.draw_target(&plan);
				(void)dt.line(20, 20, 60, 20, 8u);
				(void)dt.line(60, 20, 40, 50, 8u);
				(void)dt.line(40, 50, 20, 20, 8u);
				// Triangulo EOR (ONEDOT) en el MISMO plan: el backend fija los comunes
				// de la racha EOR una vez (`blitter_lines_eor_begin`).
				(void)dt.line(260, 20, 300, 20, 8u, field::RasterOp::Xor);
				(void)dt.line(300, 20, 280, 50, 8u, field::RasterOp::Xor);
				(void)dt.line(280, 50, 260, 20, 8u, field::RasterOp::Xor);
				if (!backend.execute_frame_plan(plan)) {
					eng::debug::mark_failed(g_eng_run_status, 0x00007702u);
					return;
				}
			}
			// Self-test de **colision pixel-perfect por Blitter** (`blitter_collide`):
			// dos mascaras de 16x8x1; con el bit (0,0) en ambas hay colision, con bits
			// distintos no. Valida la ruta de hardware (antes NO VERIFICADA).
			{
				constexpr eng::u16 kCw = 16, kCh = 8;
				constexpr eng::u16 kCRow = kCw / 8u;      // 2 bytes/fila
				constexpr eng::u32 kCPlane = kCRow * kCh; // 16 bytes/plano
				auto ca = backend.memory().chip.allocate_block<eng::PlaneTag>(kCPlane + 16u, 16);
				auto cb = backend.memory().chip.allocate_block<eng::PlaneTag>(kCPlane + 16u, 16);
				auto cs = backend.memory().chip.allocate_block<eng::PlaneTag>(kCPlane + 16u, 16);
				if (!ca.valid() || !cb.valid() || !cs.valid()) {
					eng::debug::mark_failed(g_eng_run_status, 0x00007703u);
					return;
				}
				for (eng::u32 i = 0; i < kCPlane; ++i) {
					ca.view.data()[i] = 0u;
					cb.view.data()[i] = 0u;
					cs.view.data()[i] = 0u;
				}
				ca.view.data()[0] = 0x80u; // pixel (0,0)
				cb.view.data()[0] = 0x80u; // mismo pixel -> colision
				if (!backend.blitter_collide(ca.view, cb.view, cs.view, 1u, kCRow, kCPlane, 1u, 1u)) {
					eng::debug::mark_failed(g_eng_run_status, 0x00007704u);
					return;
				}
				cb.view.data()[0] = 0x40u; // pixel distinto -> sin colision
				if (backend.blitter_collide(ca.view, cb.view, cs.view, 1u, kCRow, kCPlane, 1u, 1u)) {
					eng::debug::mark_failed(g_eng_run_status, 0x00007705u);
					return;
				}
			}
			// Self-test del **relleno compuesto por bitplane** (`fill_polygons_by_plane`):
			// dos triangulos DISJUNTOS con el MISMO color (1). El plano 0 debe quedar
			// lleno en AMBOS (valida que un solo area fill barre varias regiones) y el
			// plano 1 vacio. Valida la ruta Blitter (antes NO VERIFICADA).
			{
				constexpr eng::u16 kPw = 32, kPh = 16, kPlanes = 2;
				constexpr eng::u16 kPRow = kPw / 8u;      // 4 bytes/fila
				constexpr eng::u32 kPPlane = kPRow * kPh; // 64 bytes/plano
				auto pm = backend.memory().chip.allocate_block<eng::PlaneTag>(
					kPPlane * kPlanes + 16u, 16);
				if (!pm.valid()) {
					eng::debug::mark_failed(g_eng_run_status, 0x00007706u);
					return;
				}
				for (eng::u32 i = 0; i < kPPlane * kPlanes; ++i) {
					pm.view.data()[i] = 0u;
				}
				static const eng::s16 ax[3] = {2, 13, 2};
				static const eng::s16 ay[3] = {2, 2, 9};
				static const eng::s16 bx[3] = {18, 29, 29};
				static const eng::s16 by[3] = {2, 2, 9};
				const eng::graphics::PlanePolygon faces[2] = {
					{ax, ay, 3u, 1u},
					{bx, by, 3u, 1u},
				};
				if (!backend.fill_polygons_by_plane(faces, 2u, pm.view, kPRow, kPPlane,
								    kPlanes, kPw, kPh)) {
					eng::debug::mark_failed(g_eng_run_status, 0x00007707u);
					return;
				}
				const eng::u8* p0 = pm.view.data();
				const eng::u8* p1 = pm.view.data() + kPPlane;
				auto on = [](const eng::u8* pl, eng::u16 row, eng::u16 x, eng::u16 y) {
					return (pl[static_cast<eng::u32>(y) * row + (x >> 3)] &
						(0x80u >> (x & 7u))) != 0u;
				};
				bool fill_ok = on(p0, kPRow, 5, 3) && on(p0, kPRow, 26, 3);
				for (eng::u32 i = 0; i < kPPlane; ++i) {
					if (p1[i] != 0u) {
						fill_ok = false;
					}
				}
				if (!fill_ok) {
					eng::debug::mark_failed(g_eng_run_status, 0x00007708u);
					return;
				}
			}
			m_scene.takeover(backend);
			if (!verify_mesh()) {
				eng::debug::mark_failed(g_eng_run_status, 0x00007701u);
				return;
			}
			eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_scene.words()));
		} else {
			eng::debug::mark_failed(g_eng_run_status, 0x00000077u);
		}
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		(void)backend; // la lista es estatica: `takeover` ya la instalo
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_scene.ok()) {
			return;
		}
		(void)backend;
		field::Surface c = m_scene.surface();
		const eng::u32 f = context.frame.frame_index;

		// Rotacion compuesta Rx(f*17)·Ry(f*11)·Rz(f*7) en 4.12. Los angulos son de
		// 12 bits (4096 = vuelta completa), asi que el cubo da una vuelta cada
		// ~240 frames (~5 s a 50 fps): lo bastante rapido para "tumbar" el cubo y
		// ver 3 caras, lo bastante lento para leer la geometria.
		eng::math3d::Affine3<> m = eng::math3d::Affine3<>::identity();
		eng::math3d::load_rotate(m.m, eng::retro::turns(static_cast<eng::u16>(f * 17u)),
					 eng::retro::turns(static_cast<eng::u16>(f * 11u)),
					 eng::retro::turns(static_cast<eng::u16>(f * 7u)));

		Vec3 world[8];
		const MeshView mesh {eng::Span<const Vec3>(kVertices, 8), eng::Span<const Face>(kFaces, 12)};
		eng::math3d::mesh_transform(mesh.vertices, m, eng::Span<Vec3>(world, 8));

		eng::math3d::FaceOrder order[12];
	const Vec3 cam = vec3(0, 0, kCamZ);
		const eng::u32 visible = eng::math3d::mesh_painter_order(
			mesh, eng::Span<const Vec3>(world, 8), cam, eng::Span<eng::math3d::FaceOrder>(order, 12));

		c.fill_rect(kCX - kHalfSpan, kCY - kHalfSpan,
			    static_cast<eng::u16>(2 * kHalfSpan + 1),
			    static_cast<eng::u16>(2 * kHalfSpan + 1), 0);

		// Proyeccion ortografica (sin division): x a la derecha, y hacia arriba.
		for (eng::u32 i = 0; i < visible; ++i) {
			const Face& fc = kFaces[order[i].index];
			// Sombreado POR ARISTA (segun la profundidad de sus dos extremos): da
			// varios niveles en una misma captura (validacion determinista).
			draw_edge(c, world, fc.a, fc.b,
				  shade_of(static_cast<eng::s16>(world[fc.a].z().v + world[fc.b].z().v)));
			draw_edge(c, world, fc.b, fc.c,
				  shade_of(static_cast<eng::s16>(world[fc.b].z().v + world[fc.c].z().v)));
			draw_edge(c, world, fc.c, fc.a,
				  shade_of(static_cast<eng::s16>(world[fc.c].z().v + world[fc.a].z().v)));
		}

		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Marco y estrellas estaticas: solo se pintan una vez. El borrado por frame
	/// toca unicamente la zona central del cubo, asi que el marco no se pierde.
	void draw_static() {
		field::Surface c = m_scene.surface();

		// Doble marco.
		for (eng::s32 i = 0; i < 2; ++i) {
			const eng::s32 x0 = 6 + i * 4, y0 = 6 + i * 4;
			const eng::s32 x1 = static_cast<eng::s32>(kWidth) - 7 - i * 4;
			const eng::s32 y1 = static_cast<eng::s32>(kHeight) - 7 - i * 4;
			c.draw_line(x0, y0, x1, y0, 8);
			c.draw_line(x1, y0, x1, y1, 8);
			c.draw_line(x1, y1, x0, y1, 8);
			c.draw_line(x0, y1, x0, y0, 8);
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
			c.set_pixel(x, y, (i & 1u) ? 9 : 10);
		}
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
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
