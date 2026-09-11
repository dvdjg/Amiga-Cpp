// Test host de eng::math3d::MeshView (malla + transform + back-face culling + orden painter).
#include <eng/core/mesh3d.hpp>

#include <cstdio>

using namespace eng::math3d;

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

int main() {
	const Vec3 verts[6] = {
		{0, 0, 0}, {10, 0, 0}, {0, 10, 0},    // cara 0 (z min 0)
		{0, 0, 50}, {10, 0, 50}, {0, 10, 50}, // cara 1 (z min 50)
	};
	const Face faces[3] = {
		{0, 1, 2}, // visible (CCW visto desde +z)
		{3, 4, 5}, // visible
		{0, 2, 1}, // culled (sentido inverso)
	};
	const MeshView mesh {eng::Span<const Vec3>(verts, 6), eng::Span<const Face>(faces, 3)};
	check(mesh.vertex_count() == 6 && mesh.face_count() == 3, "MeshView tamanos");

	// Transform identidad -> mismos vertices.
	Vec3 world[6];
	Mat3x3 id {};
	mesh_transform(mesh.vertices, id, eng::Span<Vec3>(world, 6));
	check(world[1].x == 10 && world[5].z == 50, "mesh_transform identidad");

	// Culling + orden painter desde +z (camara mirando -z).
	const Vec3 cam {0, 0, 100};
	FaceOrder order[3];
	const eng::u32 n = mesh_painter_order(mesh, eng::Span<const Vec3>(world, 6), cam,
					      eng::Span<FaceOrder>(order, 3));
	check(n == 2, "2 caras visibles (1 culled)");
	check(order[0].index == 0 && order[1].index == 1, "orden lejos->cerca (z 0 antes que 50)");
	check(order[0].z == 0 && order[1].z == 50, "claves z");

	// Cara con indice fuera de rango se descarta sin fallar.
	{
		const Face bad[1] = {{0, 1, 99}};
		const MeshView mb {eng::Span<const Vec3>(verts, 6), eng::Span<const Face>(bad, 1)};
		FaceOrder o[1];
		check(mesh_painter_order(mb, eng::Span<const Vec3>(verts, 6), cam, eng::Span<FaceOrder>(o, 1)) == 0,
			"cara fuera de rango descartada");
	}

	// Capacidad de salida menor que las caras visibles -> se acota y no desborda.
	{
		FaceOrder o[1];
		const eng::u32 m = mesh_painter_order(mesh, eng::Span<const Vec3>(world, 6), cam,
						      eng::Span<FaceOrder>(o, 1));
		check(m == 1, "capacidad de salida acotada");
	}

	if (failures == 0) {
		std::printf("OK: math3d mesh (transform + culling + painter) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
