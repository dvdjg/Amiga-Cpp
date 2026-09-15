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
	check(obj.objectToWorld.m.m[0][0].v == (1 << 12) && obj.objectToWorld.m.m[1][1].v == (1 << 12), "objectToWorld identidad");
	check(obj.objectToWorld.t.x().v == 100 && obj.objectToWorld.t.y().v == 200 && obj.objectToWorld.t.z().v == 300, "objectToWorld traslacion");
	check(obj.worldToObject.t.x().v == -100 && obj.worldToObject.t.y().v == -200 && obj.worldToObject.t.z().v == -300, "worldToObject traslacion inversa");
	check(obj.camera.x == -100 && obj.camera.y == -200 && obj.camera.z == -300, "camara en espacio objeto");

	// Pipeline de visibilidad/luz/proyeccion (port de lib3d). Malla obj2c de 1 cara
	// triangular con 3 vertices; los offsets de grupo son offsets de byte al campo
	// `point` de cada nodo.
	{
		static short md[57] = {
			// nodos A/B/C (7 shorts): flags|pad, ox,oy,oz, x,y,z
			0, 100, 0, 0, 0, 0, 0,
			0, 0, 100, 0, 0, 0, 0,
			0, 0, 0, -100, 0, 0, 0,
			// cara (11 shorts): normal(3), flags|material, count, (vertex,edge)*3
			-4096, 0, 0, 0, 3, 2, 90, 16, 98, 30, 106,
			// vertexGroups
			2, 16, 30, 0, 0,
			// faceGroups (byte 42 = short 21)
			42, 0, 0,
			// edgeGroups
			90, 98, 106, 0, 0,
			// aristas (2 shorts): flags|pad, point[2]
			0, 0, 0, 0,
			0, 0, 0, 0,
			0, 0, 0, 0,
		};
		Mesh3D mm {};
		mm.vertices = 3;
		mm.edges = 3;
		mm.faces = 1;
		mm.data = md;
		mm.vertexGroups = md + 32;
		mm.faceGroups = md + 37;
		mm.edgeGroups = md + 40;
		Object3D o {};
		new_object3d(o, mm);
		o.translate = {0, 0, -1000};
		update_object_transformation(o);
		update_face_visibility(o);
		const Face* fc = face3d(md, 42);
		check(fc->flags >= -1 && fc->flags <= 15, "luz por cara en rango [-1,15]");
		update_edge_visibility_convex(o);
		transform_vertices(o, 160, 128);
		const Point3D* vv = vertex3d(md, 2);
		check(vv->z != 0, "transform_vertices escribe zp del vertice");
	}

	if (failures == 0) {
		std::printf("OK: object3d (obj2c + Object3D) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
