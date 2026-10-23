// ============================================================================
// Test HOST-488: fachada 3D de juego (G0) y política numérica de cámara.
// ============================================================================
//
// Cubre `eng/api/3d.hpp`:
//   - ciclo de vida de la escena retenida (handles generacionales, capacidad,
//     posición/visibilidad, cámara validada);
//   - render por `render_solid` con painter entre nodos, recorte al viewport y
//     omisión de modelos fuera de rango o demasiado grandes;
//   - sonda de política numérica: `eng::coord` (q0) representa focal/half_w de un
//     viewport de 320 px en 16 bits; Q8.8 y Q4.12 no (from_int desborda).
//
// Alcance: host puro, relleno CPU por `Surface` (no Blitter), sin rotación,
// jerarquía ni materiales de asset. La validación on-target y la visión quedan
// para las demos de G2/G3.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/488_scene3d_api

#include <eng/api/3d.hpp>
#include <eng/core/math/fixed.hpp>
#include <eng/memory/mem_bank.hpp>
#include <eng/retro/mesh_primitives.hpp>

#include <cstdio>
#include <vector>

using namespace eng;
using namespace eng::playfield;
using namespace eng::scene3d;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// Playfield de prueba: buffer en RAM del host y captura de los polígonos que
/// `Surface` delega (el mismo borde que en Amiga usa el sink del Blitter).
struct MockPlayfield : Playfield {
	struct Poly {
		s16 xs[16] {};
		s16 ys[16] {};
		u8 n = 0;
		u8 color = 0;
	};

	std::vector<u8> mem;
	std::vector<Poly> polys;

	void init(u16 w, u16 h, u8 p) {
		m_width = w;
		m_height = h;
		m_planes = p;
		m_bytes_per_row = static_cast<u16>((w / 8u) & ~1u);
		m_total_bytes = static_cast<u32>(m_bytes_per_row) * m_planes * m_height;
		mem.assign(m_total_bytes, 0u);
		MemBank<MemoryKind::Chip> bank {};
		bank.configure(mem.data(), static_cast<u32>(mem.size()), 2u);
		m_frontbuffer = bank.reserve<PlaneTag>(m_total_bytes, 2u).address();
		m_initialized = true;
	}

	[[nodiscard]] u32 planeline_for(s32 wy) const override {
		return static_cast<u32>(wy) * m_planes;
	}
	[[nodiscard]] u32 byte_for(s32 wx) const override {
		return static_cast<u32>(wx) / 8u & ~1u;
	}
	[[nodiscard]] bool in_bounds(s32 wx, s32 wy) const override {
		return wx >= 0 && wy >= 0 && static_cast<u32>(wx) < m_width &&
		       static_cast<u32>(wy) < m_height;
	}
	[[nodiscard]] PlayfieldHardwareView hardware_view() const override { return {}; }
	bool add_world_bitmap(graphics::FramePlan&, Span<const u16>, s32, s32, u16, u16, u16,
			      u32, u8, u8, bool, RasterOp) override {
		return false;
	}
	bool add_world_bitmap_masked(graphics::FramePlan&, Span<const u16>, Span<const u16>, s32,
				     s32, u16, u16, u16, u32, u8, u8) override {
		return false;
	}
	bool fill_polygon(Span<const s16> xs, Span<const s16> ys, u8 color) override {
		if (xs.size() != ys.size() || xs.size() < 3u || xs.size() > 16u) {
			return false;
		}
		Poly p {};
		p.n = static_cast<u8>(xs.size());
		p.color = color;
		for (u8 i = 0u; i < p.n; ++i) {
			p.xs[i] = xs[i];
			p.ys[i] = ys[i];
		}
		polys.push_back(p);
		return true;
	}
};

using Q0 = eng::math::Fixed<s16, 0>;
using Q8 = eng::math::Fixed<s16, 8>;
using Q12 = eng::math::Fixed<s16, 12>;
using T0 = eng::math::scalar_traits<Q0>;
using TC = eng::math::scalar_traits<eng::coord>;

/// `from_int` de un escalar que no puede representar `value` en su rango. Se lee
/// de un `volatile` para forzar la rama de runtime (la de `if consteval` no
/// ejecuta la comprobación de rango).
template <class S>
[[nodiscard]] int from_int_wrapped(int value) {
	volatile int runtime_value = value;
	return eng::math::scalar_traits<S>::to_int(
		eng::math::scalar_traits<S>::from_int(runtime_value));
}

void test_numeric_policy() {
	check(T0::to_int(T0::from_int(340)) == 340, "q0 representa focal 340");
	check(T0::to_int(T0::from_int(160)) == 160, "q0 representa half_w 160");
	check(T0::to_int(T0::from_int(64)) == 64, "q0 representa near 64");

	check(from_int_wrapped<Q8>(160) != 160, "Q8.8 no representa half_w 160");
	check(from_int_wrapped<Q12>(160) != 160, "Q4.12 no representa half_w 160");

	graphics::Camera3D<Q0> cam {};
	cam.focal = T0::from_int(340);
	cam.cx = 160;
	cam.cy = 100;
	cam.near_z = T0::from_int(64);
	cam.half_w = T0::from_int(160);
	cam.half_h = T0::from_int(100);

	graphics::CamVertex<Q0> v {T0::from_int(0), T0::from_int(0), T0::from_int(340)};
	s16 sx = -1;
	s16 sy = -1;
	check(graphics::project_camera_vertex(v, cam, sx, sy), "centro proyecta");
	check(sx == 160 && sy == 100, "centro -> (160,100)");

	v.x = T0::from_int(160);
	check(graphics::project_camera_vertex(v, cam, sx, sy) && sx == 320,
	      "borde derecho -> 320");

	v.x = T0::from_int(30);
	v.z = T0::from_int(64);
	check(graphics::project_camera_vertex(v, cam, sx, sy) && sx == 319,
	      "near -> 319");

	v.z = T0::zero();
	check(!graphics::project_camera_vertex(v, cam, sx, sy), "z=0 se rechaza");

	const graphics::CamVertex<Q0> in[3] = {
		{T0::from_int(0), T0::from_int(0), T0::from_int(32)},
		{T0::from_int(10), T0::from_int(10), T0::from_int(100)},
		{T0::from_int(-10), T0::from_int(10), T0::from_int(100)},
	};
	graphics::CamVertex<Q0> out[16] {};
	const u8 m = graphics::clip_camera_polygon(in, 3u, cam, out, 16u);
	check(m >= 3u, "clip near conserva el triangulo");
	bool z_ok = true;
	for (u8 i = 0u; i < m; ++i) {
		if (out[i].z < cam.near_z) {
			z_ok = false;
		}
	}
	check(z_ok, "clip near: todo z >= near");
}

void test_scene_lifecycle() {
	scene3d::Scene<4, 8> scene;
	check(scene.live_count() == 0u, "escena nace vacia");
	check(scene.node_capacity() == 4u && scene.vertex_capacity() == 8u,
	      "capacidades congeladas");

	const auto id = scene.spawn({.model = scene3d::ModelHandle {0},
				     .position = math3d::vec3(1, 2, 3)});
	check(id.valid(), "spawn valido");
	check(scene.live_count() == 1u, "live_count sube");
	check(scene.node(id) != nullptr, "node() devuelve el nodo");
	check(scene.node(id)->position.v[2] == 3, "posicion inicial");
	check(scene.set_position(id, math3d::vec3(4, 5, 6)), "set_position");
	check(scene.node(id)->position.v[0] == 4, "posicion actualizada");
	check(scene.set_visible(id, false), "set_visible");
	check(!scene.node(id)->visible, "visibilidad actualizada");
	check(scene.remove(id), "remove valido");
	check(scene.node(id) == nullptr, "handle invalidado");
	check(scene.live_count() == 0u, "live_count baja");
	check(!scene.remove(id), "remove de handle viejo devuelve false");

	const auto a = scene.spawn({.model = {0}});
	const auto b = scene.spawn({.model = {0}});
	const auto c = scene.spawn({.model = {0}});
	const auto d = scene.spawn({.model = {0}});
	check(a.valid() && b.valid() && c.valid() && d.valid(), "cuatro nodos caben");
	const auto e = scene.spawn({.model = {0}});
	check(!e.valid(), "el quinto nodo no cabe");
	check(scene.remove(c), "liberar un nodo");
	const auto f = scene.spawn({.model = {0}});
	check(f.valid(), "reutiliza el hueco");
	check(f != c, "el handle reciclado cambia de generacion");
	scene.clear();
	check(scene.live_count() == 0u, "clear vacia");
	check(scene.node(f) == nullptr, "clear invalida los handles");

	const Camera defaults = scene.camera();
	check(eng::math::scalar_traits<eng::coord>::to_int(defaults.focal) == 340,
	      "camara por defecto focal 340");
	Camera bad = defaults;
	bad.focal = TC::zero();
	check(!scene.set_camera(bad), "camara con focal 0 se rechaza");
	check(scene.camera().focal == defaults.focal, "camara valida no se modifica");
	Camera good = defaults;
	good.near_z = TC::from_int(32);
	check(scene.set_camera(good), "camara valida se acepta");
	check(scene.camera().near_z == TC::from_int(32), "camara actualizada");
}

/// Payload `Model3D` v1 de un cubo con 2 materiales (6 caras de 4 vértices).
[[nodiscard]] std::vector<u8> build_box_model_payload() {
	constexpr u16 nv = 8u;
	constexpr u16 nf = 6u;
	constexpr u16 ni = 24u;
	constexpr u16 nm = 2u;
	constexpr u32 header = assets::Model3DAssetView::kHeaderSize;
	constexpr u32 v_off = header;
	constexpr u32 i_off = v_off + nv * 6u;
	constexpr u32 f_off = i_off + ni * 2u;
	constexpr u32 m_off = f_off + nf * 6u;
	constexpr u32 total = m_off + nm * 4u;

	std::vector<u8> p(total, 0u);
	write_be16(p.data(), assets::Model3DAssetView::kVersion);
	write_be16(p.data() + 2u, 0u);
	write_be16(p.data() + 4u, nv);
	write_be16(p.data() + 6u, nf);
	write_be16(p.data() + 8u, ni);
	write_be16(p.data() + 10u, nm);
	write_be32(p.data() + 12u, v_off);
	write_be32(p.data() + 16u, i_off);
	write_be32(p.data() + 20u, f_off);
	write_be32(p.data() + 24u, m_off);

	const s16 verts[nv][3] = {
		{-20, -20, -20}, {20, -20, -20}, {20, 20, -20}, {-20, 20, -20},
		{-20, -20, 20},  {20, -20, 20},  {20, 20, 20},  {-20, 20, 20},
	};
	for (u16 i = 0u; i < nv; ++i) {
		write_be16(p.data() + v_off + i * 6u, static_cast<u16>(verts[i][0]));
		write_be16(p.data() + v_off + i * 6u + 2u, static_cast<u16>(verts[i][1]));
		write_be16(p.data() + v_off + i * 6u + 4u, static_cast<u16>(verts[i][2]));
	}

	const u16 faces[nf][4] = {
		{4, 5, 6, 7}, {0, 3, 2, 1}, {7, 6, 2, 3},
		{0, 1, 5, 4}, {1, 2, 6, 5}, {0, 4, 7, 3},
	};
	for (u16 f = 0u; f < nf; ++f) {
		for (u16 k = 0u; k < 4u; ++k) {
			write_be16(p.data() + i_off + (f * 4u + k) * 2u, faces[f][k]);
		}
		write_be16(p.data() + f_off + f * 6u, static_cast<u16>(f * 4u));
		write_be16(p.data() + f_off + f * 6u + 2u, 4u);
		write_be16(p.data() + f_off + f * 6u + 4u, static_cast<u16>(f < 3u ? 0u : 1u));
	}

	p.data()[m_off + 0u] = 3u; // color
	p.data()[m_off + 1u] = 0u; // pattern
	write_be16(p.data() + m_off + 2u, 0u); // flags
	p.data()[m_off + 4u] = 5u; // color
	p.data()[m_off + 5u] = 1u; // pattern
	write_be16(p.data() + m_off + 6u, 0u); // flags
	return p;
}

void test_model_asset() {
	using SceneT = scene3d::Scene<4, 8>;
	const std::vector<u8> payload = build_box_model_payload();

	MockPlayfield pf;
	pf.init(320, 200, 4);
	Surface surf(pf, SurfaceRect {0, 0, 320, 200});

	Camera cam {};
	cam.focal = TC::from_int(340);
	cam.cx = 160;
	cam.cy = 100;
	cam.near_z = TC::from_int(64);
	cam.half_w = TC::from_int(160);
	cam.half_h = TC::from_int(100);

	const assets::Model3DAssetView asset {
		UafPayload {payload.data(), static_cast<u32>(payload.size())}};
	check(asset.valid(), "Model3D valido");
	check(asset.vertex_count() == 8u && asset.face_count() == 6u, "conteos Model3D");
	check(asset.material_count() == 2u, "materiales Model3D");
	const assets::ModelBounds bounds = asset.bounds();
	check(bounds.min.v[0] == -20 && bounds.max.v[0] == 20, "bounds X");
	check(bounds.min.v[2] == -20 && bounds.max.v[2] == 20, "bounds Z");

	// UAF-R real: BlobWriter + Model3D; el loader valida el chunk.
	std::vector<u8> container(8u + 8u + payload.size() + 4u, 0u);
	assets::BlobWriter writer {Span<u8>(container.data(), container.size())};
	check(writer.begin(), "BlobWriter begin");
	check(writer.add_chunk(assets::ChunkType::Model3D, 1u,
			       UafPayload {payload.data(), static_cast<u32>(payload.size())}),
	      "BlobWriter Model3D");
	const UafPayload blob_bytes = writer.finish();
	assets::Blob blob;
	check(blob.bind(blob_bytes), "Blob bind");
	const assets::ChunkRef* chunk = blob.find(assets::ChunkType::Model3D);
	check(chunk != nullptr, "chunk Model3D encontrado");
	check(chunk != nullptr && assets::Model3DAssetView {blob.data(*chunk)}.valid(),
	      "chunk Model3D valido");

	scene3d::ModelTable<2, 8, 24, 6, 2> table;
	const scene3d::ModelHandle handle = table.add(asset);
	check(handle.valid(), "ModelTable add");
	check(table.size() == 1u, "ModelTable size");
	check(table.mesh(handle) != nullptr && table.mesh(handle)->vertex_count() == 8u,
	      "ModelTable malla");
	check(table.mesh(handle) != nullptr && table.mesh(handle)->face_count() == 6u,
	      "ModelTable caras");
	check(table.color(handle, 0u) == 3u, "material color cara 0");
	check(table.color(handle, 3u) == 5u, "material color cara 3");
	check(table.pattern(handle, 3u) == 1u, "material patron cara 3");
	check(table.material_index(handle, 3u) == 1u, "indice de material cara 3");
	check(table.bounds(handle).max.v[1] == 20, "bounds del modelo en tabla");

	scene3d::Scene<4, 8> scene;
	check(scene.set_camera(cam), "camara de escena con modelo");
	const auto node = scene.spawn({.model = handle, .position = math3d::vec3(0, 0, 120)});
	check(node.valid(), "nodo con modelo de tabla");
	const u32 faces = scene.render(table, surf, [&](SceneT::NodeId, u32 face) {
		return table.color(handle, face);
	});
	check(faces > 0u, "render con ModelTable");
	check(!pf.polys.empty(), "ModelTable llega a Surface");

	// Rechazos: asset truncado, índice fuera de rango, cara no convexa por conteo,
	// capacidad de tabla y desbordamiento de MaxVertices.
	{
		const assets::Model3DAssetView bad {
			UafPayload {payload.data(), 10u}};
		check(!bad.valid(), "payload truncado se rechaza");
		check(!table.add(bad).valid(), "tabla rechaza payload truncado");
	}
	{
		std::vector<u8> bad = payload;
		const u32 i_off = assets::Model3DAssetView::kHeaderSize + 8u * 6u;
		write_be16(bad.data() + i_off, 99u);
		const assets::Model3DAssetView view {
			UafPayload {bad.data(), static_cast<u32>(bad.size())}};
		check(!view.valid(), "indice fuera de rango se rechaza");
	}
	{
		std::vector<u8> bad = payload;
		const u32 f_off = assets::Model3DAssetView::kHeaderSize + 8u * 6u + 24u * 2u;
		write_be16(bad.data() + f_off + 2u, 2u);
		const assets::Model3DAssetView view {
			UafPayload {bad.data(), static_cast<u32>(bad.size())}};
		check(!view.valid(), "cara de 2 vertices se rechaza");
	}
	{
		scene3d::ModelTable<1, 8, 24, 6, 2> one;
		check(one.add(asset).valid(), "tabla de 1 acepta el primero");
		check(!one.add(asset).valid(), "tabla de 1 rechaza el segundo");
	}
	{
		scene3d::ModelTable<1, 4, 24, 6, 2> small;
		check(!small.add(asset).valid(), "tabla rechaza malla mayor que MaxVertices");
	}
}

void test_scene_render() {
	MockPlayfield pf;
	pf.init(320, 200, 4);
	Surface surf(pf, SurfaceRect {0, 0, 320, 200});

	using SceneT = scene3d::Scene<4, 8>;
	SceneT scene;
	Camera cam {};
	cam.focal = TC::from_int(340);
	cam.cx = 160;
	cam.cy = 100;
	cam.near_z = TC::from_int(64);
	cam.half_w = TC::from_int(160);
	cam.half_h = TC::from_int(100);
	check(scene.set_camera(cam), "camara de render");

	retro::MeshData<eng::coord, 8, 24, 6> cube;
	check(retro::fill_box(cube, math3d::vec3(20, 20, 20)), "cubo de prueba");
	const scene3d::Mesh meshes[1] = {cube.view()};
	const Span<const scene3d::Mesh> table(meshes, 1);

	const auto far = scene.spawn({.model = {0}, .position = math3d::vec3(0, 0, 220)});
	const auto near = scene.spawn({.model = {0}, .position = math3d::vec3(0, 0, 120)});
	check(far.valid() && near.valid(), "dos cubos en escena");

	const auto color = [&](SceneT::NodeId id, u32) -> u8 {
		const scene3d::Node3D* node = scene.node(id);
		return (node != nullptr && node->position.v[2] > 170) ? u8 {2} : u8 {3};
	};

	const u32 faces = scene.render(table, surf, color);
	check(faces > 0u, "render pinta caras");
	check(!pf.polys.empty(), "Surface recibe poligonos");

	bool inside = true;
	for (const auto& p : pf.polys) {
		for (u8 i = 0u; i < p.n; ++i) {
			if (p.xs[i] < 0 || p.xs[i] > 319 || p.ys[i] < 0 || p.ys[i] > 199) {
				inside = false;
			}
		}
	}
	check(inside, "todas las coordenadas caen en el viewport");

	bool seen_far = false;
	bool seen_near = false;
	bool far_after_near = false;
	for (const auto& p : pf.polys) {
		if (p.color == 2u) {
			seen_far = true;
			if (seen_near) {
				far_after_near = true;
			}
		}
		if (p.color == 3u) {
			seen_near = true;
		}
	}
	check(seen_far && seen_near, "painter pinta los dos cubos");
	check(!far_after_near, "painter pinta de lejos a cerca");

	check(scene.set_visible(far, false), "ocultar cubo lejano");
	pf.polys.clear();
	check(scene.render(table, surf, color) > 0u, "oculto: queda el cercano");
	for (const auto& p : pf.polys) {
		check(p.color == 3u, "el nodo oculto no emite caras");
	}

	check(scene.set_visible(far, true), "mostrar cubo lejano");
	check(scene.set_position(far, math3d::vec3(0, 0, 20)), "mover cubo tras near");
	pf.polys.clear();
	check(scene.render(table, surf, color) > 0u, "el cubo cercano sigue visible");
	bool far_drawn = false;
	for (const auto& p : pf.polys) {
		if (p.color == 2u) {
			far_drawn = true;
		}
	}
	check(!far_drawn, "cubo detras del near no emite caras");

	SceneT bad_model_scene;
	check(bad_model_scene.set_camera(cam), "camara de escena de modelo invalido");
	check(bad_model_scene.spawn({.model = {9}, .position = math3d::vec3(0, 0, 120)})
		      .valid(),
	      "nodo con modelo fuera de rango");
	check(bad_model_scene.render(table, surf, [](SceneT::NodeId, u32) { return u8 {1}; }) ==
		      0u,
	      "modelo fuera de rango se omite");

	scene3d::Scene<4, 4> tiny_scene;
	check(tiny_scene.set_camera(cam), "camara de escena con MaxVertices=4");
	check(tiny_scene.spawn({.model = {0}, .position = math3d::vec3(0, 0, 120)}).valid(),
	      "nodo en escena pequena");
	check(tiny_scene.render(table, surf, [](SceneT::NodeId, u32) { return u8 {1}; }) == 0u,
	      "malla mayor que MaxVertices se omite");

	SceneT empty_scene;
	check(empty_scene.set_camera(cam), "camara de escena vacia");
	check(empty_scene.render(table, surf, [](SceneT::NodeId, u32) { return u8 {1}; }) == 0u,
	      "escena vacia no pinta");
	check(scene.render(Span<const scene3d::Mesh> {}, surf, color) == 0u,
	      "tabla de modelos vacia no pinta");
}

} // namespace

int main() {
	test_numeric_policy();
	test_scene_lifecycle();
	test_model_asset();
	test_scene_render();

	if (g_fail == 0) {
		std::printf("OK: HOST-488 fachada 3D (G0) y modelo cocinado (G1) validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones.\n", g_fail);
	return 1;
}
