#define ENG_SCALAR_RETRO16  // host: instancia retro (eng::real=q12, coord=q0)
// HOST-053 — tabla dorada de `lib3d::transform_vertices` (la proyección con `div_wide`)
// a ángulo FIJO. Fija los valores proyectados y la bounding-box para que ningún cambio
// en la capa de cálculo (matrices, exponentes, redondeo) los altere en silencio.
//
// Malla mínima: 3 nodos (un triángulo) + 1 arista + 1 cara.
#include <eng/platform/amiga/lib3d.hpp>

#include <cstdio>
#include <cstring>

using namespace eng::lib3d;
using eng::object3d::Object3D;
using eng::object3d::Mesh3D;
using eng::s16;

// Layout empaquetado: Node=14 B (0,14,28), Edge=6 B (42), Face=10+6*2=22 B (48).
static unsigned char g_objdat[80];
static s16 g_vertexGroups[] = {2, 16, 30, 0, 0};
static s16 g_edgeGroups[] = {42, 0, 0};
static s16 g_faceGroups[] = {48, 0, 0};

static void wr16(unsigned off, s16 v) { std::memcpy(g_objdat + off, &v, 2); }
static s16 rd16(unsigned off) { s16 v; std::memcpy(&v, g_objdat + off, 2); return v; }

static void build_mesh() {
	for (unsigned i = 0; i < sizeof(g_objdat); ++i) g_objdat[i] = 0;
	// Nodos en (100,0,0), (0,100,0), (0,0,100): point.x/y/z en el offset i+2/+4/+6.
	wr16(0 + 2, 100); wr16(0 + 4, 0); wr16(0 + 6, 0);
	wr16(14 + 2, 0); wr16(14 + 4, 100); wr16(14 + 6, 0);
	wr16(28 + 2, 0); wr16(28 + 4, 0); wr16(28 + 6, 100);
	// Arista 0 -> nodos (2, 16).
	wr16(42 + 2, 2); wr16(42 + 4, 16);
	// Cara 0: normal (0,0,4096), material 0, count 3, 3 pares (vertice, arista).
	wr16(48 + 0, 0); wr16(48 + 2, 0); wr16(48 + 4, 4096);
	wr16(48 + 8, 3);
	wr16(48 + 10, 2); wr16(48 + 12, 42);
	wr16(48 + 14, 16); wr16(48 + 16, 42);
	wr16(48 + 18, 30); wr16(48 + 20, 42);
}

int main() {
	Object3D obj {};
	Mesh3D mesh {};
	mesh.bytes = eng::Span<eng::u8>(g_objdat);
	mesh.vertexGroups = g_vertexGroups;
	mesh.edgeGroups = g_edgeGroups;
	mesh.faceGroups = g_faceGroups;
	eng::object3d::new_object3d(obj, mesh);

	build_mesh();
	// Ángulo FIJO: es lo que hace reproducible la proyección.
	const s16 angle = 1000;
	obj.rotate = {eng::retro::angle_to_radians(static_cast<eng::u32>(angle)).value,
		      eng::retro::angle_to_radians(static_cast<eng::u32>(angle)).value,
		      eng::retro::angle_to_radians(static_cast<eng::u32>(angle)).value};
	obj.translate = {eng::retro::q0 {0}, eng::retro::q0 {0}, eng::retro::q0 {-4000}};
	eng::object3d::update_object_transformation(obj);
	update_face_visibility(obj);
	update_edge_visibility_convex(obj);
	s16 bbox[4] = {0, 0, 0, 0};
	transform_vertices(obj, 128, 128, bbox);

	int fail = 0;
	const auto ck = [&](bool ok, const char *m) {
		if (!ok) { std::printf("  [FAIL] %s\n", m); ++fail; }
	};

	// Tabla DORADA (malla + angulo 1000 fijos): si la capa de calculo cambia un valor,
	// estos asserts lo detectan. Es un trinquete de regresion, no un juicio de belleza.
	ck(obj.camera.x.v == 3988 && obj.camera.y.v == 291 && obj.camera.z.v == 4, "camara (golden)");
	ck(rd16(0 + 8) == 128 && rd16(0 + 10) == 128 && rd16(0 + 12) == -3901, "nodo0 vertex (golden)");
	ck(rd16(14 + 8) == 128 && rd16(14 + 10) == 134 && rd16(14 + 12) == -3993, "nodo1 vertex (golden)");
	ck(rd16(28 + 8) == 122 && rd16(28 + 10) == 128 && rd16(28 + 12) == -4000, "nodo2 vertex (golden)");
	ck(bbox[0] == 122 && bbox[1] == 128 && bbox[2] == 128 && bbox[3] == 134, "bbox (golden)");

	// Cordura geometrica (que la tabla no sea "dorada basura"):
	for (int k = 0; k < 3; ++k) {
		const unsigned b0 = static_cast<unsigned>(k) * 14u;
		ck(rd16(b0 + 8) >= 0 && rd16(b0 + 8) < 256, "vertex.x dentro del viewport");
		ck(rd16(b0 + 10) >= 0 && rd16(b0 + 10) < 256, "vertex.y dentro del viewport");
		ck(rd16(b0 + 12) < 0, "zp negativo (delante de la camara)");
	}
	ck(bbox[0] <= bbox[1] && bbox[2] <= bbox[3], "bbox ordenada");
	ck(bbox[0] == 122 && bbox[2] == 128, "bbox = extremos de los vertices proyectados");

	if (fail == 0) {
		std::printf("OK: proyeccion lib3d (tabla dorada a angulo fijo) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d\n", fail);
	return 1;
}
