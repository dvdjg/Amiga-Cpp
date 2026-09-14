// Test host de `eng::graphics::mesh_renderer` (malla 3D -> Surface):
// proyección en perspectiva, back-face culling (mesh_painter_order) y relleno de
// las caras visibles vía `Surface::fill_polygon`. `Playfield` de prueba en RAM.
#include <eng/graphics/mesh_renderer.hpp>

#include <cstdio>
#include <vector>

using namespace eng;
using namespace eng::field;
using namespace eng::graphics;

struct MockPlayfield : Playfield {
	std::vector<u8> mem;
	void init(u16 w, u16 h, u8 p) {
		m_width = w;
		m_height = h;
		m_planes = p;
		m_bytes_per_row = static_cast<u16>((w / 8) & ~1u);
		m_total_bytes = static_cast<u32>(m_bytes_per_row) * m_planes * m_height;
		mem.assign(m_total_bytes, 0);
		m_frontbuffer = mem.data();
		m_initialized = true;
	}
	u32 planeline_for(s32 wy) const override { return static_cast<u32>(wy) * m_planes; }
	u32 byte_for(s32 wx) const override { return (static_cast<u32>(wx) / 8u) & ~1u; }
	bool in_bounds(s32 wx, s32 wy) const override {
		return wx >= 0 && wy >= 0 && static_cast<u32>(wx) < m_width && static_cast<u32>(wy) < m_height;
	}
	PlayfieldHardwareView hardware_view() const override { return {}; }
	bool add_world_bitmap(graphics::FramePlan&, Span<const u16>, s32, s32, u16, u16, u16, u32, u8) override { return false; }
	bool add_world_bitmap_masked(graphics::FramePlan&, Span<const u16>, Span<const u16>, s32, s32, u16, u16, u16, u32, u8) override { return false; }
	bool px(s32 x, s32 y) const {
		const u32 row = m_bytes_per_row;
		const u32 base = static_cast<u32>(planeline_for(y)) * row;
		const u16 v = *reinterpret_cast<const u16*>(mem.data() + base + byte_for(x));
		return (v & static_cast<u16>(0x8000u >> (x & 15))) != 0u;
	}
};

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) { std::printf("  [FAIL] %s\n", msg); ++failures; }
}

int main() {
	// 1) Proyeccion perspectiva: (x*focal/z) + centro.
	{
		s16 sx = 0, sy = 0;
		project_perspective(math3d::Vec3 {256, 512, 1024}, 256, 0, 0, sx, sy);
		check(sx == 64 && sy == 128, "project_perspective escalado");
		project_perspective(math3d::Vec3 {100, -50, 1000}, 256, 32, 32, sx, sy);
		// div16(100*256,1000)=25 ; div16(-50*256,1000)=-12 (truncado a 0)
		check(sx == 57, "project_perspective + centro x");
		check(sy == 20, "project_perspective + centro y");
	}

	// Malla: cuadrado en z=1024 (dos triangulos), ±96 -> ±24 px proyectado.
	math3d::Vec3 verts[4] = {{-96, -96, 1024}, {96, -96, 1024}, {96, 96, 1024}, {-96, 96, 1024}};
	math3d::Face faces[2] = {{0, 1, 2}, {0, 2, 3}};
	const math3d::MeshView mesh {Span<const math3d::Vec3>(verts, 4), Span<const math3d::Face>(faces, 2)};
	const math3d::Mat3x3 model {}; // identidad

	// 2) Cara mirando a la camara ({0,0,2048}) -> 2 caras visibles rellenadas.
	{
		MockPlayfield pf;
		pf.init(64, 64, 4);
		Surface surf(pf, SurfaceRect {0, 0, 64, 64});
		math3d::Vec3 world[4];
		math3d::FaceOrder order[2];
		s16 sx[4], sy[4];
		const auto color = [](u16) -> u8 { return 1; };
		const u32 drawn = mesh_render_filled(mesh, model, math3d::Vec3 {0, 0, 2048}, 256, 32, 32,
						     world, order, sx, sy, surf, color, false);
		check(drawn == 2, "2 caras visibles pintadas");
		check(surf.valid(), "surface valida");
		// El cuadrado proyectado va de 8 a 56 (32±24).
		check(pf.px(32, 32), "centro relleno");
		check(pf.px(12, 12), "esquina interior rellena");
		check(!pf.px(4, 4), "fuera (arriba-izq) sin pintar");
		check(!pf.px(60, 60), "fuera (abajo-der) sin pintar");
	}

	// 3) Back-face culling: la misma cara desde el otro lado ({0,0,0}) no se pinta.
	{
		MockPlayfield pf;
		pf.init(64, 64, 4);
		Surface surf(pf, SurfaceRect {0, 0, 64, 64});
		math3d::Vec3 world[4];
		math3d::FaceOrder order[2];
		s16 sx[4], sy[4];
		const auto color = [](u16) -> u8 { return 1; };
		const u32 drawn = mesh_render_filled(mesh, model, math3d::Vec3 {0, 0, 0}, 256, 32, 32,
						     world, order, sx, sy, surf, color, false);
		check(drawn == 0, "culling: 0 caras desde detras");
		check(!pf.px(32, 32), "culling: centro sin pintar");
	}

	// 4) `double_sided` pinta ambas caras aunque miren al otro lado.
	{
		MockPlayfield pf;
		pf.init(64, 64, 4);
		Surface surf(pf, SurfaceRect {0, 0, 64, 64});
		math3d::Vec3 world[4];
		math3d::FaceOrder order[2];
		s16 sx[4], sy[4];
		const auto color = [](u16) -> u8 { return 1; };
		const u32 drawn = mesh_render_filled(mesh, model, math3d::Vec3 {0, 0, 0}, 256, 32, 32,
						     world, order, sx, sy, surf, color, true);
		check(drawn == 2, "double_sided: 2 caras");
		check(pf.px(32, 32), "double_sided: centro pintado");
	}

	// 5) Ruta alambre: dibuja las aristas (contorno), no el interior.
	{
		MockPlayfield pf;
		pf.init(64, 64, 4);
		Surface surf(pf, SurfaceRect {0, 0, 64, 64});
		math3d::Vec3 world[4];
		math3d::FaceOrder order[2];
		s16 sx[4], sy[4];
		const u32 n = mesh_render_wire(mesh, model, math3d::Vec3 {0, 0, 2048}, 256, 32, 32,
					       world, order, sx, sy, surf, 1, false);
		check(n == 2, "wire: 2 caras procesadas");
		check(pf.px(8, 32), "wire: arista izquierda pintada");
		check(!pf.px(20, 40), "wire: interior vacio (solo contorno)");
	}

	if (failures == 0) {
		std::printf("OK: mesh_renderer (malla 3D -> Surface: proyeccion + culling + relleno) validado.\n");
		return 0;
	}	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
