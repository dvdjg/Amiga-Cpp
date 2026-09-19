#define ENG_SCALAR_RETRO16  // host: instancia retro (eng::real=q12, coord=q0)
#include <eng/retro/fixed_mesh.hpp>
// Test host de eng::math3d::MeshView (malla + transform + back-face culling + orden painter).
#include <eng/platform/amiga/gfx3d.hpp>
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
		vec3(0, 0, 0), vec3(10, 0, 0), vec3(0, 10, 0),    // cara 0 (z min 0)
		vec3(0, 0, 50), vec3(10, 0, 50), vec3(0, 10, 50), // cara 1 (z min 50)
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
	Affine3<> id = Affine3<>::identity();
	mesh_transform(mesh.vertices, id, eng::Span<Vec3>(world, 6));
	check(world[1].x().v == 10 && world[5].z().v == 50, "mesh_transform identidad");

	// Culling + orden painter desde +z (camara mirando -z).
	const Vec3 cam = vec3(0, 0, 100);
	FaceOrder order[3];
	const eng::u32 n = mesh_painter_order(mesh, eng::Span<const Vec3>(world, 6), cam,
					      eng::Span<FaceOrder>(order, 3));
	check(n == 2, "2 caras visibles (1 culled)");
	check(order[0].index == 0 && order[1].index == 1, "orden lejos->cerca (z 0 antes que 50)");
	check(order[0].z == 0 && order[1].z == 50, "claves z");

	// Doble cara (`AllFacesDoubleSided`): se incluyen TODAS las caras, sin culling.
	{
		FaceOrder all[3];
		const eng::u32 m = mesh_painter_order(mesh, eng::Span<const Vec3>(world, 6), cam,
						      eng::Span<FaceOrder>(all, 3), true);
		check(m == 3, "doble cara incluye las 3 caras");
	}

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

	// Genericidad: la MISMA malla instanciada con coordenada `float` (sin tocar el engine).
	{
		using Vf = eng::math3d::Vec3t<float>;
		const Vf vf[3] = {eng::math3d::vec3<float>(0, 0, 0), eng::math3d::vec3<float>(10, 0, 0),
				  eng::math3d::vec3<float>(0, 10, 0)};
		const Face ff[1] = {{0, 1, 2}};
		const eng::math3d::MeshViewT<float> meshf {eng::Span<const Vf>(vf, 3),
							   eng::Span<const Face>(ff, 1)};
		Vf wf[3];
		const eng::math::Affine<3, float, float> idf =
			eng::math::Affine<3, float, float>::identity();
		eng::math3d::mesh_transform(meshf.vertices, idf, eng::Span<Vf>(wf, 3));
		eng::math3d::FaceOrder of[1];
		const eng::u32 nf = eng::math3d::mesh_painter_order(
			meshf, eng::Span<const Vf>(wf, 3), eng::math3d::vec3<float>(0, 0, 100),
			eng::Span<eng::math3d::FaceOrder>(of, 1));
		check(nf == 1 && wf[1].v[0] == 10.0f, "mesh3d generico: misma malla con float");
	}

	// Genericidad: coordenada `double` (default generico del mesh_traits).
	{
		using Vd = eng::math3d::Vec3t<double>;
		const Vd vd[3] = {eng::math3d::vec3<double>(0, 0, 0), eng::math3d::vec3<double>(10, 0, 0),
				  eng::math3d::vec3<double>(0, 10, 0)};
		const Face fd[1] = {{0, 1, 2}};
		const eng::math3d::MeshViewT<double> md {eng::Span<const Vd>(vd, 3),
							 eng::Span<const Face>(fd, 1)};
		Vd wd[3];
		const eng::math::Affine<3, double, double> idd =
			eng::math::Affine<3, double, double>::identity();
		eng::math3d::mesh_transform(md.vertices, idd, eng::Span<Vd>(wd, 3));
		eng::math3d::FaceOrder od[1];
		const eng::u32 nd = eng::math3d::mesh_painter_order(
			md, eng::Span<const Vd>(wd, 3), eng::math3d::vec3<double>(0, 0, 100),
			eng::Span<eng::math3d::FaceOrder>(od, 1));
		check(nd == 1 && wd[1].v[0] == 10.0, "mesh3d generico: misma malla con double");
	}
	// Genericidad: coordenada `Fixed<s16,8>` (Fixed no-q0 por el camino generico, host).
	// Nota: `Fixed<s32,E>` NO lo cubre el default (el producto mixto ensancharia a un
	// `Repr` no definido); necesitaria su propia especializacion de `mesh_traits`.
	{
		using S8 = eng::math::Fixed<eng::s16, 8>;
		using V8 = eng::math3d::Vec3t<S8>;
		const V8 v8[3] = {eng::math3d::vec3<S8>(0, 0, 0), eng::math3d::vec3<S8>(10, 0, 0),
				  eng::math3d::vec3<S8>(0, 10, 0)};
		const Face f8[1] = {{0, 1, 2}};
		const eng::math3d::MeshViewT<S8> m8 {eng::Span<const V8>(v8, 3),
						     eng::Span<const Face>(f8, 1)};
		V8 w8[3];
		const eng::math::Affine<3, S8, S8> id8 = eng::math::Affine<3, S8, S8>::identity();
		eng::math3d::mesh_transform(m8.vertices, id8, eng::Span<V8>(w8, 3));
		eng::math3d::FaceOrder o8[1];
		const eng::u32 n8 = eng::math3d::mesh_painter_order(
			m8, eng::Span<const V8>(w8, 3), eng::math3d::vec3<S8>(0, 0, 100),
			eng::Span<eng::math3d::FaceOrder>(o8, 1));
		check(n8 == 1 && w8[1].v[0].v == (10 << 8),
		      "mesh3d generico: misma malla con Fixed<s16,8>");
	}

	if (failures == 0) {
		std::printf("OK: math3d mesh (transform + culling + painter) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
