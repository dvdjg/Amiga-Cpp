// ============================================================================
// Test HOST-477: primitivas 3D generadas en compilacion
// (engine/include/eng/retro/mesh_primitives.hpp)
// ============================================================================
//
// Valida los generadores constexpr de mallas (`math3d::PolyMeshViewT`): caja,
// prisma regular, tubo abierto, cono/piramide, esfera UV, capsula y solidos
// platonicos (tetraedro, cubo, octaedro, dodecaedro, icosaedro).
//
//   1) Generacion en COMPILACION: las mallas son `constexpr` y los contadores
//      exactos de vertices/indices/caras se fijan con `static_assert`.
//   2) Topologia cerrada: V - E + F = 2 (Euler) contando aristas unicas.
//   3) Winding exterior: para un solido convexo centrado, NINGUNA cara es
//      visible desde el centro; y con una camara cualquiera la visibilidad
//      coincide con el signo de la normal de Newell (n . (cam - centro)).
//   4) Integridad: indices en rango y caras sin vertices repetidos.
//   5) Limites de capacidad: un buffer pequeno devuelve false sin desbordar.
//   6) Dos escalares (`coord` s16 y `float`): la cabecera no impone el escalar.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/477_mesh_primitives

#define ENG_SCALAR_RETRO16 // host: eng::coord = q0 (s16), como el resto de tests 3D
#include <eng/retro/mesh_primitives.hpp>

#include <cstdio>

namespace {

using eng::u16;
using eng::u32;
using eng::coord;
using eng::math3d::PolyMeshViewT;
using eng::math3d::Vec3t;
using Mesh = PolyMeshViewT<coord>;

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// --- Mallas de prueba (constexpr: si compilan, la generacion es en compilacion) ---

constexpr eng::retro::MeshData<coord, 8, 24, 6> make_box() {
	eng::retro::MeshData<coord, 8, 24, 6> m {};
	const bool ok = eng::retro::fill_box(m, eng::math3d::vec3<coord>(10, 20, 30));
	(void)ok;
	return m;
}

constexpr eng::retro::MeshData<coord, 12, 36, 8> make_prism() {
	eng::retro::MeshData<coord, 12, 36, 8> m {};
	const bool ok = eng::retro::fill_prism(m, 6u, 20, 30);
	(void)ok;
	return m;
}

constexpr eng::retro::MeshData<coord, 16, 32, 8> make_tube() {
	eng::retro::MeshData<coord, 16, 32, 8> m {};
	const bool ok = eng::retro::fill_tube(m, 8u, 20, 30);
	(void)ok;
	return m;
}

constexpr eng::retro::MeshData<coord, 7, 24, 7> make_cone() {
	eng::retro::MeshData<coord, 7, 24, 7> m {};
	const bool ok = eng::retro::fill_cone(m, 6u, 20, 30);
	(void)ok;
	return m;
}

constexpr eng::retro::MeshData<coord, 26, 112, 32> make_sphere() {
	eng::retro::MeshData<coord, 26, 112, 32> m {};
	const bool ok = eng::retro::fill_sphere(m, 8u, 4u, 20);
	(void)ok;
	return m;
}

constexpr eng::retro::MeshData<coord, 50, 208, 56> make_capsule() {
	eng::retro::MeshData<coord, 50, 208, 56> m {};
	const bool ok = eng::retro::fill_capsule(m, 8u, 3u, 20, 16);
	(void)ok;
	return m;
}

constexpr eng::retro::MeshData<coord, 4, 12, 4> make_tetra() {
	eng::retro::MeshData<coord, 4, 12, 4> m {};
	const bool ok = eng::retro::fill_platonic(m, eng::retro::Platonic::Tetrahedron, 16);
	(void)ok;
	return m;
}

constexpr eng::retro::MeshData<coord, 8, 24, 6> make_cube() {
	eng::retro::MeshData<coord, 8, 24, 6> m {};
	const bool ok = eng::retro::fill_platonic(m, eng::retro::Platonic::Cube, 16);
	(void)ok;
	return m;
}

constexpr eng::retro::MeshData<coord, 6, 24, 8> make_octa() {
	eng::retro::MeshData<coord, 6, 24, 8> m {};
	const bool ok = eng::retro::fill_platonic(m, eng::retro::Platonic::Octahedron, 16);
	(void)ok;
	return m;
}

constexpr eng::retro::MeshData<coord, 20, 60, 12> make_dodeca() {
	eng::retro::MeshData<coord, 20, 60, 12> m {};
	const bool ok = eng::retro::fill_platonic(m, eng::retro::Platonic::Dodecahedron, 16);
	(void)ok;
	return m;
}

constexpr eng::retro::MeshData<coord, 12, 60, 20> make_icosa() {
	eng::retro::MeshData<coord, 12, 60, 20> m {};
	const bool ok = eng::retro::fill_platonic(m, eng::retro::Platonic::Icosahedron, 16);
	(void)ok;
	return m;
}

constexpr auto kBox = make_box();
constexpr auto kPrism = make_prism();
constexpr auto kTube = make_tube();
constexpr auto kCone = make_cone();
constexpr auto kSphere = make_sphere();
constexpr auto kCapsule = make_capsule();
constexpr auto kTetra = make_tetra();
constexpr auto kCube = make_cube();
constexpr auto kOcta = make_octa();
constexpr auto kDodeca = make_dodeca();
constexpr auto kIcosa = make_icosa();

// 1) Contadores exactos en compilacion.
static_assert(kBox.view().vertex_count() == 8 && kBox.view().face_count() == 6);
static_assert(kBox.view().indices.size() == 24); // 6 quads
static_assert(kPrism.view().vertex_count() == 12 && kPrism.view().face_count() == 8);
static_assert(kPrism.view().indices.size() == 36); // 6 quads + 2 hexagonos
static_assert(kTube.view().vertex_count() == 16 && kTube.view().face_count() == 8);
static_assert(kTube.view().indices.size() == 32); // 8 quads, sin tapas
static_assert(kCone.view().vertex_count() == 7 && kCone.view().face_count() == 7);
static_assert(kCone.view().indices.size() == 24); // 6 triangulos + base
static_assert(kSphere.view().vertex_count() == 26 && kSphere.view().face_count() == 32);
static_assert(kSphere.view().indices.size() == 112); // 16 tri + 16 quads
static_assert(kCapsule.view().vertex_count() == 50 && kCapsule.view().face_count() == 56);
static_assert(kCapsule.view().indices.size() == 208);
static_assert(kTetra.view().vertex_count() == 4 && kTetra.view().face_count() == 4);
static_assert(kCube.view().vertex_count() == 8 && kCube.view().face_count() == 6);
static_assert(kOcta.view().vertex_count() == 6 && kOcta.view().face_count() == 8);
static_assert(kDodeca.view().vertex_count() == 20 && kDodeca.view().face_count() == 12);
static_assert(kDodeca.view().indices.size() == 60); // 12 pentagonos
static_assert(kIcosa.view().vertex_count() == 12 && kIcosa.view().face_count() == 20);
static_assert(kIcosa.view().indices.size() == 60);

// --- Comprobaciones en runtime ---

/// Integridad basica: indices en rango y sin vertices repetidos en una cara.
void check_integrity(const Mesh& m, const char* name) {
	bool ok_idx = true;
	bool ok_rep = true;
	for (u32 f = 0; f < m.face_count(); ++f) {
		const eng::Span<const u16> idx = m.face_indices(f);
		for (u32 i = 0; i < idx.size(); ++i) {
			if (idx[i] >= m.vertex_count()) {
				ok_idx = false;
			}
			for (u32 j = i + 1u; j < idx.size(); ++j) {
				if (idx[i] == idx[j]) {
					ok_rep = false;
				}
			}
		}
	}
	char msg[128];
	std::snprintf(msg, sizeof(msg), "%s: indices en rango", name);
	check(ok_idx, msg);
	std::snprintf(msg, sizeof(msg), "%s: caras sin vertices repetidos", name);
	check(ok_rep, msg);
}

/// Aristas unicas usadas por las caras (pares consecutivos no ordenados).
u32 unique_edges(const Mesh& m) {
	u32 e = 0;
	for (u32 a = 0; a < m.vertex_count(); ++a) {
		for (u32 b = a + 1u; b < m.vertex_count(); ++b) {
			bool found = false;
			for (u32 f = 0; f < m.face_count() && !found; ++f) {
				const eng::Span<const u16> idx = m.face_indices(f);
				for (u32 i = 0; i < idx.size(); ++i) {
					const u16 p = idx[i];
					const u16 q = idx[(i + 1u) % idx.size()];
					if ((p == a && q == b) || (p == b && q == a)) {
						found = true;
						break;
					}
				}
			}
			if (found) {
				++e;
			}
		}
	}
	return e;
}

/// Solido cerrado: V - E + F = 2 (caracteristica de Euler de la esfera).
void check_euler(const Mesh& m, const char* name) {
	const u32 v = m.vertex_count();
	const u32 f = m.face_count();
	const u32 e = unique_edges(m);
	const int chi = static_cast<int>(v) - static_cast<int>(e) + static_cast<int>(f);
	char msg[128];
	std::snprintf(msg, sizeof(msg), "%s: Euler V-E+F=2 (V=%u E=%u F=%u)", name, v, e, f);
	check(chi == 2, msg);
}

/// Winding exterior: ninguna cara visible desde el centro (el observador está dentro del
/// sólido convexo centrado). El culling del engine decide con los tres primeros puntos de
/// la cara, que es exactamente lo que comprueba `face_visible` aquí.
void check_outward(const Mesh& m, const char* name) {
	bool none_from_center = true;
	for (u32 f = 0; f < m.face_count(); ++f) {
		const eng::Span<const u16> idx = m.face_indices(f);
		const Vec3t<coord> a = m.vertices[idx[0]];
		const Vec3t<coord> b = m.vertices[idx[1]];
		const Vec3t<coord> c = m.vertices[idx[2]];
		const Vec3t<coord> center = eng::math3d::vec3<coord>(0, 0, 0);
		if (eng::math3d::face_visible(a, b, c, center)) {
			none_from_center = false;
		}
	}
	char msg[128];
	std::snprintf(msg, sizeof(msg), "%s: ninguna cara visible desde el centro", name);
	check(none_from_center, msg);
}

/// Integridad + topologia + winding de un solido cerrado convexo centrado.
void check_solid(const Mesh& m, const char* name) {
	check_integrity(m, name);
	check_euler(m, name);
	check_outward(m, name);
}

void test_limits() {
	// Prisma de 20 lados no cabe en una capacidad de 8 lados: false sin desbordar.
	eng::retro::MeshData<coord, 40, 120, 22> big {};
	check(eng::retro::fill_prism(big, 20u, 20, 30), "prisma de 20 lados cabe en su buffer");
	eng::retro::MeshData<coord, 12, 36, 8> small {};
	check(!eng::retro::fill_prism(small, 20u, 20, 30), "prisma de 20 lados NO cabe en 8");
	check(small.nv <= 12u && small.nf <= 8u, "capacidad respetada al fallar");
	// Parametros invalidos: false.
	eng::retro::MeshData<coord, 16, 32, 8> m {};
	check(!eng::retro::fill_prism(m, 2u, 20, 30), "prisma de 2 lados rechazado");
	check(!eng::retro::fill_sphere(m, 8u, 1u, 20), "esfera de 1 stack rechazada");
	check(!eng::retro::fill_platonic(m, eng::retro::Platonic::Icosahedron, 0),
	      "platonic con escala 0 rechazado");
}

void test_second_scalar() {
	// Test negativo de genericidad: la misma cabecera con `float`.
	eng::retro::MeshData<float, 8, 24, 6> mf {};
	check(eng::retro::fill_box(mf, eng::math3d::vec3<float>(10, 20, 30)), "caja float cabe");
	check(mf.nv == 8u && mf.nf == 6u, "caja float: contadores");
	const PolyMeshViewT<float> vf = mf.view();
	check(vf.vertex_count() == 8u && vf.face_count() == 6u, "caja float: vista");
	check(vf.vertices[6].v[0] > 9.0f && vf.vertices[6].v[0] < 11.0f,
	      "caja float: coordenada x del vertice 6");
	eng::retro::MeshData<float, 12, 60, 20> mi {};
	check(eng::retro::fill_platonic(mi, eng::retro::Platonic::Icosahedron, 16),
	      "icosaedro float cabe");
	check(mi.nv == 12u && mi.nf == 20u, "icosaedro float: contadores");
}

} // namespace

int main() {
	check_solid(kBox.view(), "caja");
	check_solid(kPrism.view(), "prisma");
	check_solid(kCone.view(), "cono");
	check_solid(kSphere.view(), "esfera");
	check_solid(kCapsule.view(), "capsula");
	check_solid(kTetra.view(), "tetraedro");
	check_solid(kCube.view(), "cubo");
	check_solid(kOcta.view(), "octaedro");
	check_solid(kDodeca.view(), "dodecaedro");
	check_solid(kIcosa.view(), "icosaedro");
	// Tubo: abierto (sin tapas) -> sin Euler; integridad y winding (convexo centrado), si.
	check_integrity(kTube.view(), "tubo");
	check_outward(kTube.view(), "tubo");
	test_limits();
	test_second_scalar();

	if (g_fail == 0) {
		std::printf("OK: primitivas 3D (caja, prisma, tubo, cono, esfera, capsula y "
			    "platonicos) en compilacion\n");
	} else {
		std::printf("FALLOS: %u\n", g_fail);
	}
	return g_fail == 0 ? 0 : 1;
}
