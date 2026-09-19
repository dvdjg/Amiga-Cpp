#define ENG_SCALAR_RETRO16  // host: instancia retro (eng::real=q12, coord=q0)
// Test host de eng::object3d (modelo obj2c + Object3D portado de lib3d).
#include <eng/platform/amiga/object3d.hpp>

#include <cstdio>

using namespace eng::object3d;

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

int main() {
	// Mesh minimo: 1 vertice empaquetado [flags, ox, oy, oz, x, y, z].
	static short data[7] = {0, 111, 222, 333, 0, 0, 0};
	const eng::Span<eng::u8> bytes {reinterpret_cast<eng::u8*>(data), sizeof(data)};
	Mesh3D mesh {};
	mesh.vertices = 1;
	mesh.bytes = bytes;
	mesh.vertexGroups = data;
	mesh.edgeGroups = data;
	mesh.faceGroups = data;
	mesh.objects = data;

	Object3D obj {};
	new_object3d(obj, mesh);
	check(obj.objdat == bytes.data() && obj.objdat_size == bytes.size(), "new_object3d enlaza objdat");
	check(obj.scale.x.v == (1 << 12) && obj.scale.y.v == (1 << 12), "scale inicial 1.0 (4.12)");

	// Validacion del descriptor: blob no vacio y grupos dentro de rango.
	check(mesh_validate(mesh), "mesh_validate: malla valida");
	{
		Mesh3D no_blob = mesh;
		no_blob.bytes = {};
		check(!mesh_validate(no_blob), "mesh_validate: sin blob -> false");
		static short bad_group[2] = {1000, 0};
		Mesh3D out_of_range = mesh;
		out_of_range.vertexGroups = bad_group;
		check(!mesh_validate(out_of_range), "mesh_validate: grupo fuera de rango -> false");
	}

	// Offsets de las macros (indice = offset de byte; primer vertice = 2).
	const Point3D* p = point3d(bytes, 2);
	check(p->x.v == 111 && p->y.v == 222 && p->z.v == 333, "point3d(i) -> point del nodo");
	const Point3D* v = vertex3d(bytes, 2);
	check(v->x.v == 0 && v->y.v == 0 && v->z.v == 0, "vertex3d(i) -> vertex del nodo");
	check(reinterpret_cast<short*>(node3d(bytes, 2)) == data, "node3d(i) = objdat + i - 2");

	// Transformacion: identidad + traslacion.
	obj.rotate = {};
	obj.scale = {eng::retro::q12 {1 << 12}, eng::retro::q12 {1 << 12}, eng::retro::q12 {1 << 12}};
	obj.translate = {eng::retro::q0 {100}, eng::retro::q0 {200}, eng::retro::q0 {300}};
	update_object_transformation(obj);
	check(obj.objectToWorld.m.m[0][0].v == (1 << 12) && obj.objectToWorld.m.m[1][1].v == (1 << 12), "objectToWorld identidad");
	check(obj.objectToWorld.t.x().v == 100 && obj.objectToWorld.t.y().v == 200 && obj.objectToWorld.t.z().v == 300, "objectToWorld traslacion");
	check(obj.worldToObject.t.x().v == -100 && obj.worldToObject.t.y().v == -200 && obj.worldToObject.t.z().v == -300, "worldToObject traslacion inversa");
	check(obj.camera.x.v == -100 && obj.camera.y.v == -200 && obj.camera.z.v == -300,
	      "camara en espacio objeto");

	if (failures == 0) {
		std::printf("OK: object3d (obj2c + Object3D) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
