// Test host de `eng::graphics::mesh_render_poly_filled` (malla n-gon sobre `Surface`):
// valida el pipeline de alto nivel (transform -> culling/orden por normal almacenada ->
// proyección -> `Surface::fill_polygon`) con un cubo de caras cuadradas, a un ángulo fijo.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/graphics/211_mesh_poly_render

#include <cstdio>
#include <vector>

#include <eng/graphics/mesh_renderer.hpp>

using namespace eng;
using namespace eng::field;
using namespace eng::math3d;

// Playfield mínimo de prueba (buffer del host, layout interleaved), como HOST-045.
struct MockPlayfield : Playfield {
	std::vector<u8> mem;

	void init(u16 w, u16 h, u8 p) {
		m_width = w;
		m_height = h;
		m_planes = p;
		m_bytes_per_row = static_cast<u16>((w / 8) & ~1u);
		m_total_bytes = static_cast<u32>(m_bytes_per_row) * m_planes * m_height;
		mem.assign(m_total_bytes, 0);
		m_frontbuffer = eng::Address<eng::MemoryKind::Chip>::from_storage(mem.data());
		m_initialized = true;
	}
	u32 planeline_for(s32 wy) const override { return static_cast<u32>(wy) * m_planes; }
	u32 byte_for(s32 wx) const override { return (static_cast<u32>(wx) / 8u) & ~1u; }
	bool in_bounds(s32 wx, s32 wy) const override {
		return wx >= 0 && wy >= 0 && static_cast<u32>(wx) < m_width && static_cast<u32>(wy) < m_height;
	}
	PlayfieldHardwareView hardware_view() const override { return {}; }
	bool add_world_bitmap(graphics::FramePlan&, Span<const u16>, s32, s32, u16, u16, u16, u32, u8, u8, bool, RasterOp) override {
		return false;
	}
	bool add_world_bitmap_masked(graphics::FramePlan&, Span<const u16>, Span<const u16>, s32, s32, u16, u16, u16, u32, u8, u8) override {
		return false;
	}

	/// Color (0..15) del píxel leyendo los 4 planos.
	u8 color_at(s32 x, s32 y) const {
		const u32 row = m_bytes_per_row;
		const u32 base = static_cast<u32>(planeline_for(y)) * row;
		const u32 byte = byte_for(x);
		const u16 mask = static_cast<u16>(0x8000u >> (x & 15));
		u8 c = 0;
		for (u8 p = 0; p < m_planes; ++p) {
			const u16 v = *reinterpret_cast<const u16*>(mem.data() + base + static_cast<u32>(p) * row + byte);
			if ((v & mask) != 0u) c |= static_cast<u8>(1u << p);
		}
		return c;
	}
};

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

int main() {
	// Cubo de caras CUADRADAS (n-gon): 8 vértices ±32, 6 caras de 4 índices.
	const Vec3 verts[8] = {
		vec3(-32, -32, -32), vec3(32, -32, -32), vec3(32, 32, -32), vec3(-32, 32, -32),
		vec3(-32, -32, 32), vec3(32, -32, 32), vec3(32, 32, 32), vec3(-32, 32, 32),
	};
	// Índices concatenados por cara (24) + spans (6 caras de 4).
	const u16 idx[24] = {
		0, 3, 2, 1, // -z
		4, 5, 6, 7, // +z
		0, 1, 5, 4, // -y
		3, 7, 6, 2, // +y
		0, 4, 7, 3, // -x
		1, 2, 6, 5, // +x
	};
	const FaceSpan faces[6] = {{0, 4}, {4, 4}, {8, 4}, {12, 4}, {16, 4}, {20, 4}};
	// Normales EXTERIORES (solo importa el signo para el culling).
	const Vec3 normals[6] = {
		vec3(0, 0, -1), vec3(0, 0, 1), vec3(0, -1, 0), vec3(0, 1, 0), vec3(-1, 0, 0), vec3(1, 0, 0),
	};

	PolyMeshView mesh {};
	mesh.vertices = Span<const Vec3>(verts, 8u);
	mesh.indices = Span<const u16>(idx, 24u);
	mesh.faces = Span<const FaceSpan>(faces, 6u);
	mesh.normals = Span<const Vec3>(normals, 6u);

	MockPlayfield pf;
	pf.init(256, 256, 4);
	Surface surf(pf, SurfaceRect {0, 0, 256, 256});

	// Modelo: traslación z+400 (identity en la parte lineal); cámara fuera de eje para
	// que se vean tres caras del cubo.
	using Model = eng::math::Affine<3, Coord, Coord>;
	Model model = Model::identity();
	model.t = vec3(0, 0, 400);
	const Vec3 camera = vec3(-200, -200, 0);

	Vec3 world[8] {};
	FaceOrder order[6] {};
	s16 sx[8] {};
	s16 sy[8] {};
	// color_of(face) = face+1 (1..6), para distinguir la cara pintada.
	const u32 drawn = graphics::mesh_render_poly_filled(
		mesh, model, camera, 1024, 128, 128,
		world, order, sx, sy, surf,
		[](u16 face) { return static_cast<u8>(face + 1u); });

	check(drawn == 3u, "se pintan 3 caras visibles de un cubo fuera de eje");

	// El centro de la proyección cae dentro de alguna cara visible -> coloreado.
	check(pf.color_at(128, 128) != 0u, "el centro del cubo queda pintado");
	// Fuera del cubo (esquina) no se pinta nada.
	check(pf.color_at(5, 5) == 0u, "la esquina está vacía");
	check(pf.color_at(250, 250) == 0u, "la esquina opuesta está vacía");

	// Culling: con la normal almacenada, las caras que miran al lado opuesto no se pintan.
	// (Contamos colores distintos pintados: las 3 caras visibles usan índices distintos.)
	u8 seen[7] = {0, 0, 0, 0, 0, 0, 0};
	for (s32 y = 0; y < 256; ++y) {
		for (s32 x = 0; x < 256; ++x) {
			const u8 c = pf.color_at(x, y);
			if (c >= 1u && c <= 6u) seen[c] = 1u;
		}
	}
	u32 colors = 0;
	for (u8 c = 1u; c <= 6u; ++c) colors += seen[c];
	check(colors == 3u, "aparecen exactamente 3 colores de cara (3 caras visibles)");

	if (failures == 0) {
		std::printf("OK: mesh_render_poly_filled (cubo n-gon: transform, culling, proyeccion y relleno por cara).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
