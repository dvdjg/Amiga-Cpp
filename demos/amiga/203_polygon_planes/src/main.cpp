// Demo 203 - Relleno de poligonos compuesto por bitplane.
//
// Motor poligonal 2D/3D minimo que usa el **relleno compuesto por bitplane**
// (`eng/graphics/polygon_planes.hpp` + `MinimalBackend::fill_polygons_by_plane`):
// en vez de rellenar poligono a poligono (un fill por cara), se rellena **por
// plano**: para cada bitplane `p`, el contorno XOR (ONEDOT) de las caras cuyo
// color tiene el bit `p` a 1 y **un solo area fill** (`FILL_XOR`) que cubre todas
// las regiones de ese plano. Las aristas compartidas por caras del mismo bit se
// cancelan (doble cruce even-odd); donde los bits difieren, la arista es frontera
// real y separa dos colores.
//
// Un cubo gira y cada cara se sombrea por profundidad (`shade_of`); las caras
// visibles se acumulan en un `PlaneFillBuilder` (patron `SubmitPoly`/`EndFrame`) y
// al cerrar el frame se vuelcan con 1 fill por plano (4 fills) en vez de 1 por
// cara (hasta 6). Referencia CPU del mismo algoritmo: `fill_polygons_by_plane_cpu`
// (test HOST-217); este es el camino Blitter, verificado en hardware.
//
// Display 256x256x4 (mismos registros que el port de `flatshade-convex`) con doble
// buffer por swap de copperlist (`commit`).
#include <eng/core/mesh3d.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/palette32.hpp>
#include <eng/graphics/pattern_fill.hpp>
#include <eng/graphics/polygon_planes.hpp>
#include <eng/graphics/scene/compose.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/retro/fixed_mesh.hpp>

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
namespace graphics = eng::graphics;

using eng::math3d::Affine3;
using eng::math3d::Vec3;
using eng::math3d::vec3;

constexpr eng::u16 kWidth = 256;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 4;
constexpr eng::u16 kBytesPerRow = kWidth / 8u; // 32
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kHeight;
constexpr eng::u8 kBuffers = 2;

// Registros del display del original de `flatshade-convex` (256x256x4).
constexpr eng::u16 kDiwstrt = 0x2ca1;
constexpr eng::u16 kDiwstop = 0x2ca1;
constexpr eng::u16 kDdfstrt = 0x0048;
constexpr eng::u16 kDdfstop = 0x00c0;
constexpr eng::u16 kBplcon0 = 0x4000; // 4 planos, color
constexpr eng::u16 kBplcon1 = 0x0000;

constexpr scene::SceneResources kRes = scene::planar(kWidth, kHeight, kPlanes);
static_assert(scene::valid_scene(kRes, scene::ocs_a500), "203: 256x256x4 en A500");

/// Fondo (0) y rampa azul->cian para las 6 caras (1..6).
constexpr eng::Palette32 kPalette {{
	0x012, 0x113, 0x225, 0x337, 0x449, 0x55b, 0x66d, 0x88f,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

/// Cubo unidad de radio `kR`: 8 vertices compartidos y 6 caras cuadradas con
/// orientacion CCW vista desde FUERA (el culling usa la normal 3D).
constexpr eng::s16 kR = 56;
constexpr Vec3 kVertices[8] = {
	vec3(-kR, -kR, -kR), vec3(kR, -kR, -kR), vec3(kR, kR, -kR), vec3(-kR, kR, -kR),
	vec3(-kR, -kR, kR), vec3(kR, -kR, kR), vec3(kR, kR, kR), vec3(-kR, kR, kR),
};
/// 6 caras de 4 vertices (CCW desde fuera): +Z, -Z, +X, -X, +Y, -Y.
constexpr eng::u8 kFaces[6][4] = {
	{4, 5, 6, 7},
	{1, 0, 3, 2},
	{5, 1, 2, 6},
	{0, 4, 7, 3},
	{7, 6, 2, 3},
	{0, 1, 5, 4},
};
constexpr eng::u32 kFaceCount = 6;
constexpr eng::u8 kFaceVerts = 4;

constexpr eng::s32 kCX = 128;
constexpr eng::s32 kCY = 120;
constexpr eng::s32 kCamZ = 220;

/// Buffer de vertices proyectados por cara (persistente: el `PlaneFillBuilder`
/// guarda vistas, no copias, asi que los arrays deben seguir vivos al volcar).
eng::s16 g_px[kFaceCount][kFaceVerts];
eng::s16 g_py[kFaceCount][kFaceVerts];

/// Producto vectorial de dos aristas (s32: componentes ~R, productos ~R^2).
inline void cross(const Vec3& a, const Vec3& b, const Vec3& c, eng::s32 out[3]) {
	const eng::s32 ux = b.x().v - a.x().v, uy = b.y().v - a.y().v, uz = b.z().v - a.z().v;
	const eng::s32 vx = c.x().v - a.x().v, vy = c.y().v - a.y().v, vz = c.z().v - a.z().v;
	out[0] = uy * vz - uz * vy;
	out[1] = uz * vx - ux * vz;
	out[2] = ux * vy - uy * vx;
}

/// Visibilidad de una cara convexa por la normal 3D: `dot(normal, cam - v0) > 0`.
bool face_visible(const Vec3* w, const eng::u8* idx) {
	eng::s32 n[3];
	cross(w[idx[0]], w[idx[1]], w[idx[2]], n);
	const eng::s32 vx = -w[idx[0]].x().v;
	const eng::s32 vy = -w[idx[0]].y().v;
	const eng::s32 vz = kCamZ - w[idx[0]].z().v;
	return n[0] * vx + n[1] * vy + n[2] * vz > 0;
}

/// Brillo segun la profundidad de la cara (suma de z de sus 4 vertices): las caras
/// mas cercanas (z alta, hacia la camara) se pintan mas claras. Sin division.
eng::u8 shade_of(eng::s32 zsum) {
	if (zsum > 90) return 6;
	if (zsum > 30) return 5;
	if (zsum > -30) return 4;
	if (zsum > -90) return 3;
	if (zsum > -150) return 2;
	return 1;
}

struct PolygonPlanesDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({96u * 1024u, 4u * 1024u, 4u * 1024u});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020301u);
			return;
		}

		scene::SceneResources res = scene::planar(kWidth, kHeight, kPlanes);
		res.buffers = kBuffers;
		if (!scene::compose(m_scene, backend.memory(), res, scene::ocs_a500,
				    scene::display(kDiwstrt, kDiwstop, kDdfstrt, kDdfstop, kBplcon0),
				    scene::palette(kPalette, 0u, 16u))) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020302u);
			return;
		}
		m_scene.takeover(backend);

		// Patron del suelo: DOS filas de `kBytesPerRow` palabras (tablero 4x2 px
		// desplazado). El relleno multifila (`add_rect_pattern`) emite un blit por fila
		// del patron. Debe vivir en Chip RAM (el Blitter no lee .rodata).
		constexpr eng::u8 kPatRows = 2u;
		m_pattern = backend.memory().chip.allocate_block<eng::PlaneTag>(
			static_cast<eng::u32>(kBytesPerRow) * kPatRows + 16u, 16);
		if (!m_pattern.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020303u);
			return;
		}
		for (eng::u32 r = 0; r < kPatRows; ++r) {
			eng::u16* row = reinterpret_cast<eng::u16*>(m_pattern.view.data()) +
					r * (kBytesPerRow / 2u);
			for (eng::u32 i = 0; i < kBytesPerRow / 2u; ++i) {
				row[i] = (r == 0u) ? 0xf0f0u : 0x0f0fu;
			}
		}

		// Pre-clear de ambos buffers: el relleno limpia cada plano igualmente, pero
		// dejamos el primer frame listo sin depender del estado de la memoria.
		for (eng::u8 b = 0; b < kBuffers; ++b) {
			backend.blitter_clear(m_scene.buffer(b), kPlanes, kBytesPerRow, kPlaneBytes,
					      kWidth, kHeight, true);
		}

		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(kFaceCount));
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_memory_ok) {
			return;
		}
		backend.wait_blitter();

		const eng::u32 f = context.frame.frame_index;
		eng::PlaneBytes planes = m_scene.back();

		// Inclinacion fija (~45 grados en X) + giro en Y y balanceo en Z: el cubo
		// muestra 2-3 caras la mayor parte del tiempo (a diferencia de una rotacion
		// puramente pequena, que se queda casi de frente).
		Affine3<> m = Affine3<>::identity();
		eng::math3d::load_rotate(m.m, eng::retro::turns(static_cast<eng::u16>(f * 9u + 512u)),
					 eng::retro::turns(static_cast<eng::u16>(f * 64u)),
					 eng::retro::turns(static_cast<eng::u16>(f * 5u)));

		Vec3 world[8];
		eng::math3d::mesh_transform(eng::Span<const Vec3>(kVertices, 8), m,
					    eng::Span<Vec3>(world, 8));

		// Acumular las caras visibles (patron SubmitPoly): proyectar + color por
		// profundidad, sin dibujar todavia.
		graphics::PlaneFillBuilder<kFaceCount> fb;
		for (eng::u32 i = 0; i < kFaceCount; ++i) {
			if (!face_visible(world, kFaces[i])) {
				continue;
			}
			eng::s32 zsum = 0;
			for (eng::u8 k = 0; k < kFaceVerts; ++k) {
				const Vec3& v = world[kFaces[i][k]];
				g_px[i][k] = static_cast<eng::s16>(kCX + v.x().v);
				g_py[i][k] = static_cast<eng::s16>(kCY - v.y().v);
				zsum += v.z().v;
			}
			(void)fb.submit(g_px[i], g_py[i], kFaceVerts, shade_of(zsum));
		}

		// EndFrame: 1 contorno XOR + 1 area fill por plano (4 blits de fill en total),
		// en vez de un fill por cara. La limpieza de cada plano va dentro del relleno.
		if (fb.count() != 0u) {
			(void)backend.fill_polygons_by_plane(fb.faces().data(), fb.count(), planes,
							    kBytesPerRow, kPlaneBytes, kPlanes,
							    kWidth, kHeight);
		} else {
			backend.blitter_clear(planes, kPlanes, kBytesPerRow, kPlaneBytes,
					      kWidth, kHeight, false);
		}

		// Suelo texturizado: relleno con patron MULTIFILA (2 filas, tablero) OR-ado en
		// el plano 0 sobre la banda inferior (un blit por fila del patron).
		{
			constexpr eng::u16 kFloorY = 200u;
			constexpr eng::u16 kFloorH = kHeight - kFloorY;
			constexpr eng::u8 kPatRows = 2u;
			graphics::FramePlan plan {};
			if (graphics::add_rect_pattern(
				    plan, graphics::BlitDest(reinterpret_cast<eng::u16*>(planes.data())),
				    kBytesPerRow, 0u, kFloorY, static_cast<eng::u16>(kBytesPerRow / 2u),
				    kFloorH,
				    reinterpret_cast<const eng::u16*>(m_pattern.view.data()),
				    static_cast<eng::u16>(kBytesPerRow / 2u), kBytesPerRow, kPatRows,
				    static_cast<eng::u32>(kBytesPerRow) * kPatRows, 1u)) {
				(void)backend.execute_frame_plan(plan);
			}
		}

		m_scene.commit();
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	bool m_memory_ok = false;
	scene::Scene m_scene {};
	eng::Block<eng::PlaneTag> m_pattern {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	PolygonPlanesDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
