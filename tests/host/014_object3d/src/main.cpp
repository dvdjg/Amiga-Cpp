// Test host de eng::object3d (modelo obj2c + Object3D portado de lib3d).
#include <eng/core/object3d.hpp>

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
	Mesh3D mesh {};
	mesh.vertices = 1;
	mesh.data = data;
	mesh.vertexGroups = data;
	mesh.edgeGroups = data;
	mesh.faceGroups = data;
	mesh.objects = data;

	Object3D obj {};
	new_object3d(obj, mesh);
	check(obj.objdat == data, "new_object3d enlaza objdat");
	check(obj.scale.x == (1 << 12) && obj.scale.y == (1 << 12), "scale inicial 1.0 (4.12)");

	// Offsets de las macros (indice = offset de byte; primer vertice = 2).
	const Point3D* p = point3d(data, 2);
	check(p->x == 111 && p->y == 222 && p->z == 333, "point3d(i) -> point del nodo");
	const Point3D* v = vertex3d(data, 2);
	check(v->x == 0 && v->y == 0 && v->z == 0, "vertex3d(i) -> vertex del nodo");
	check(reinterpret_cast<short*>(node3d(data, 2)) == data, "node3d(i) = objdat + i - 2");

	// Transformacion: identidad + traslacion.
	obj.rotate = {0, 0, 0};
	obj.scale = {1 << 12, 1 << 12, 1 << 12};
	obj.translate = {100, 200, 300};
	update_object_transformation(obj);
	check(obj.objectToWorld.m00 == (1 << 12) && obj.objectToWorld.m11 == (1 << 12), "objectToWorld identidad");
	check(obj.objectToWorld.x == 100 && obj.objectToWorld.y == 200 && obj.objectToWorld.z == 300, "objectToWorld traslacion");
	check(obj.worldToObject.x == -100 && obj.worldToObject.y == -200 && obj.worldToObject.z == -300, "worldToObject traslacion inversa");
	check(obj.camera.x == -100 && obj.camera.y == -200 && obj.camera.z == -300, "camara en espacio objeto");

	if (failures == 0) {
		std::printf("OK: object3d (obj2c + Object3D) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
