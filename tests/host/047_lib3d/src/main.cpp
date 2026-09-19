#define ENG_SCALAR_RETRO16  // host: instancia retro (eng::real=q12, coord=q0)
// Test host de eng::lib3d (visibilidad de caras/aristas y transform+proyeccion de
// vertices sobre el modelo empaquetado de object3d). Construye una malla minima a
// mano (1 nodo + 1 arista + 1 cara) y comprueba signos/valores calculables.
#include <eng/platform/amiga/lib3d.hpp>

#include <cstdio>
#include <cstring>

using namespace eng::lib3d;

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

// Layout empaquetado (offsets de byte):
//   0..13   Node3D   (flags, pad, point[3], vertex[3])  -> indice i = 2
//   14..19  Edge     (flags, pad, point[2])             -> indice i = 14
//   20..41  Face     (normal[3], flags, material, count, 3x FaceIndex)
static unsigned char g_objdat[64];

static s16 rd16(unsigned off) {
	s16 v;
	std::memcpy(&v, g_objdat + off, 2);
	return v;
}
static void wr16(unsigned off, s16 v) { std::memcpy(g_objdat + off, &v, 2); }
static void wr8(unsigned off, s8 v) { g_objdat[off] = static_cast<unsigned char>(v); }

// Indices de grupo: un solo grupo, terminado por dos 0 (el bucle do/while del
// original: `*group++` hasta 0, y repite mientras el siguiente no sea 0).
static s16 g_vertexGroups[] = {2, 0, 0};
static s16 g_edgeGroups[] = {14, 0, 0};
static s16 g_faceGroups[] = {20, 0, 0};

static void build_mesh(s16 normal_z) {
	for (unsigned i = 0; i < sizeof(g_objdat); ++i) g_objdat[i] = 0;
	// Node0 en offset 0: point y vertex a 0 (punto en el origen).
	// Edge0 en offset 14: apunta a dos veces el nodo (i=2).
	wr16(14 + 2, 2);
	wr16(14 + 4, 2);
	// Face0 en offset 20: normal (0,0,normal_z), count=3, 3 indices (vertice, arista).
	wr16(20 + 0, 0);
	wr16(20 + 2, 0);
	wr16(20 + 4, normal_z);
	wr8(20 + 6, 0);   // flags
	wr8(20 + 7, 0);   // material >= 0
	wr16(20 + 8, 3);  // count
	// offset 10: 3 x FaceIndex {vertex, edge}
	wr16(20 + 10, 2); wr16(20 + 12, 14);
	wr16(20 + 14, 2); wr16(20 + 16, 14);
	wr16(20 + 18, 2); wr16(20 + 20, 14);
}

int main() {
	// hi16: parte alta de 32 bits como s16.
	check(hi16(0x12345678) == 0x1234, "hi16 parte alta");
	check(hi16(0x0001FFFF) == 1, "hi16 con signo bajo");
	check(hi16(-1) == -1, "hi16 de -1");

	// Tabla de luz: 65535/sqrt(x) en los primeros valores (los del original).
	check(kInvSqrt[0] == 0 && kInvSqrt[1] == 65535, "kInvSqrt[0..1]");
	check(kInvSqrt[2] == 46340 && kInvSqrt[4] == 32768, "kInvSqrt[2]/[4]");

	// Objeto: malla minima + camara detras del origen (translate z negativo).
	eng::object3d::Object3D obj {};
	eng::object3d::Mesh3D mesh {};
	mesh.bytes = eng::Span<eng::u8>(g_objdat);
	mesh.vertexGroups = g_vertexGroups;
	mesh.edgeGroups = g_edgeGroups;
	mesh.faceGroups = g_faceGroups;
	mesh.objects = nullptr;
	eng::object3d::new_object3d(obj, mesh);
	obj.rotate = {};
	obj.translate = {eng::retro::q0 {0}, eng::retro::q0 {0}, eng::retro::q0 {-4000}};
	eng::object3d::update_object_transformation(obj);

	// La camara queda en +z (translate.z = -4000): la cara que mira a la camara es
	// la de normal +z, y sale con luz 16 (determinista para esta geometria).
	build_mesh(4096);
	update_face_visibility(obj);
	const s8 lit = static_cast<s8>(g_objdat[20 + 6]);
	check(lit == 16, "cara hacia la camara: visible con luz 16");

	// Cara de espaldas (normal -z) con material >= 0: oculta (-1).
	build_mesh(-4096);
	update_face_visibility(obj);
	check(static_cast<s8>(g_objdat[20 + 6]) == -1, "cara de espaldas: oculta (flags == -1)");

	// Aristas: con la cara visible, los nodos se marcan y la arista recibe el XOR
	// de la luz (partiendo de 0 tras el dibujo, queda = luz de la cara).
	build_mesh(4096);
	update_face_visibility(obj);
	const s8 faceLit = static_cast<s8>(g_objdat[20 + 6]);
	wr16(14, 0); // arista "limpia" como tras dibujarla
	update_edge_visibility_convex(obj);
	check(g_objdat[0] == 1, "nodo marcado por la cara visible");
	check(static_cast<s8>(g_objdat[14]) == faceLit, "arista = XOR de la luz de la cara");

	// Transform+proyeccion: el vertice queda proyectado y la bbox recoge su (x, y).
	// Punto en el origen + camara a 4000 -> centro de pantalla (128,128) y zp=-4000.
	build_mesh(4096);
	update_face_visibility(obj);
	update_edge_visibility_convex(obj);
	s16 bbox[4] = {0, 0, 0, 0};
	transform_vertices(obj, 128, 128, bbox);
	const s16 vx = rd16(0 + 8);
	const s16 vy = rd16(0 + 10);
	const s16 vz = rd16(0 + 12);
	check(vx == 128 && vy == 128, "punto en el origen proyecta al centro (128,128)");
	check(vz != 0, "zp del vertice distinto de 0");
	check(bbox[0] == 128 && bbox[1] == 128 && bbox[2] == 128 && bbox[3] == 128,
	      "bbox del vertice proyectado");

	if (failures == 0) {
		std::printf("OK: lib3d (visibilidad de caras/aristas + transform) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
