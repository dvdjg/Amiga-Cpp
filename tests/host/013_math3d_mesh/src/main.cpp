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

	// Orden elegido en compilacion por la cualidad del solido.
	static_assert(eng::math3d::mesh_order_traits<eng::math3d::ConvexSolid>::sorts == false &&
			      eng::math3d::mesh_order_traits<eng::math3d::ConcaveMesh>::sorts == true,
		      "convexo no ordena; concavo si");
	static_assert(eng::math3d::mesh_order_traits<eng::math3d::ConvexSolid>::depth_key == false,
		      "convexo no calcula clave de profundidad");
	static_assert(sizeof(eng::math3d::ConvexFace) == 2, "ConvexFace = indice (2 B/cara)");
	{
		using K = eng::math3d::MeshFaceOrder<eng::math3d::ConvexSolid>;
		static_assert(K::kSorts == false, "MeshFaceOrder<ConvexSolid> no ordena");
		eng::math3d::ConvexFace cv[3];
		const eng::u32 nc = K::order(mesh, eng::Span<const Vec3>(world, 6), cam,
					     eng::Span<eng::math3d::ConvexFace>(cv, 3));
		check(nc == 2 && ((cv[0].index == 0 && cv[1].index == 1) ||
					  (cv[0].index == 1 && cv[1].index == 0)),
		      "convexo: 2 caras frontales (culling, sin ordenar)");
		eng::math3d::ConvexFace cc[3];
		const eng::u32 ncc = eng::math3d::mesh_convex_order(
			mesh, eng::Span<const Vec3>(world, 6), cam,
			eng::Span<eng::math3d::ConvexFace>(cc, 3));
		check(ncc == 2, "mesh_convex_order: 2 caras");
	}

	// Misma malla "solapada": el concavo ordena lejos->cerca; el convexo respeta el encuentro.
	{
		const Vec3 ov[6] = {vec3(5, 5, 50), vec3(15, 5, 50), vec3(5, 15, 50), // tri 0 (z 50, cerca)
				    vec3(0, 0, 0),  vec3(10, 0, 0),  vec3(0, 10, 0)}; // tri 1 (z 0, lejos)
		const Face of[2] = {{0, 1, 2}, {3, 4, 5}};
		const MeshView om {eng::Span<const Vec3>(ov, 6), eng::Span<const Face>(of, 2)};
		FaceOrder so[2];
		const eng::u32 nso = eng::math3d::mesh_painter_order(
			om, eng::Span<const Vec3>(ov, 6), cam, eng::Span<FaceOrder>(so, 2));
		check(nso == 2 && so[0].index == 1 && so[1].index == 0,
		      "concavo: pinta lejos->cerca (z 0 antes que 50)");
		ConvexFace sc[2];
		const eng::u32 nsc = eng::math3d::mesh_convex_order(
			om, eng::Span<const Vec3>(ov, 6), cam, eng::Span<ConvexFace>(sc, 2));
		check(nsc == 2 && sc[0].index == 0 && sc[1].index == 1,
		      "convexo: orden de encuentro (sin sort)");
	}

	// Malla n-gon (parches convexos): culling por Newell + pintor por parche.
	{
		using PV = eng::math3d::PolyMeshViewT<eng::coord>;
		const Vec3 qv[8] = {
			vec3(0, 0, 0),	vec3(10, 0, 0),	 vec3(10, 10, 0),  vec3(0, 10, 0),   // quad (z 0)
			vec3(5, 5, 50), vec3(15, 5, 50), vec3(15, 15, 50), vec3(5, 15, 50), // quad (z 50)
		};
		const eng::u16 idx[8] = {0, 1, 2, 3, 4, 5, 6, 7};
		const eng::math3d::FaceSpan fs[2] = {{0, 4}, {4, 4}};
		const PV pv {eng::Span<const Vec3>(qv, 8), eng::Span<const eng::u16>(idx, 8),
			     eng::Span<const eng::math3d::FaceSpan>(fs, 2)};
		check(pv.vertex_count() == 8 && pv.face_count() == 2, "PolyMeshView tamanos");
		check(pv.face_indices(0).size() == 4 && pv.face_indices(0)[2] == 2, "face_indices");
		check(eng::math3d::poly_face_visible(pv, 0, pv.vertices, cam) &&
			      eng::math3d::poly_face_visible(pv, 1, pv.vertices, cam),
		      "quads n-gon visibles (Newell)");
		FaceOrder po[2];
		const eng::u32 np =
			eng::math3d::mesh_patches_order(pv, pv.vertices, cam, eng::Span<FaceOrder>(po, 2));
		check(np == 2 && po[0].index == 0 && po[1].index == 1 && po[0].z == 0 && po[1].z == 50,
		      "parches convexos: pintor lejos->cerca");

		const eng::u16 idxr[8] = {3, 2, 1, 0, 4, 5, 6, 7}; // quad 0 con winding inverso
		const PV pr {eng::Span<const Vec3>(qv, 8), eng::Span<const eng::u16>(idxr, 8),
			     eng::Span<const eng::math3d::FaceSpan>(fs, 2)};
		FaceOrder pr2[2];
		const eng::u32 npr =
			eng::math3d::mesh_patches_order(pr, pr.vertices, cam, eng::Span<FaceOrder>(pr2, 2));
		check(npr == 1 && pr2[0].index == 1, "quad con winding inverso descartado");
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

	// Genericidad: coordenada ancha `Fixed<s32,12>` (su propia especializacion de mesh_traits,
	// con clave de orden `s32`).
	{
		using S32 = eng::math::Fixed<eng::s32, 12>;
		using V32 = eng::math3d::Vec3t<S32>;
		const V32 v32[3] = {eng::math3d::vec3<S32>(0, 0, 0), eng::math3d::vec3<S32>(10, 0, 0),
				    eng::math3d::vec3<S32>(0, 10, 0)};
		const Face f32[1] = {{0, 1, 2}};
		const eng::math3d::MeshViewT<S32> m32 {eng::Span<const V32>(v32, 3),
						       eng::Span<const Face>(f32, 1)};
		V32 w32[3];
		const eng::math::Affine<3, S32, S32> id32 = eng::math::Affine<3, S32, S32>::identity();
		eng::math3d::mesh_transform(m32.vertices, id32, eng::Span<V32>(w32, 3));
		eng::math3d::FaceOrderT<eng::s32> o32[1];
		const eng::u32 n32 = eng::math3d::mesh_painter_order(
			m32, eng::Span<const V32>(w32, 3), eng::math3d::vec3<S32>(0, 0, 100),
			eng::Span<eng::math3d::FaceOrderT<eng::s32>>(o32, 1));
		check(n32 == 1 && w32[1].v[0].v == (10 << 12) && o32[0].z == 0,
		      "mesh3d generico: misma malla con Fixed<s32,12> (clave s32)");
	}

	// Relleno convexo por dos cadenas: mismos spans que el barrido por min/max de referencia.
	{
		auto ref_rows = [](const eng::s32* xsr, const eng::s32* ysr, eng::u32 cnt, auto&& emit) {
			eng::s32 ymin = ysr[0], ymax = ysr[0];
			for (eng::u32 i = 1; i < cnt; ++i) {
				if (ysr[i] < ymin) ymin = ysr[i];
				if (ysr[i] > ymax) ymax = ysr[i];
			}
			for (eng::s32 y = ymin; y <= ymax; ++y) {
				eng::s32 xl = 32767, xr = -32768;
				for (eng::u32 i = 0; i < cnt; ++i) {
					const eng::u32 j = (i + 1u == cnt) ? 0u : i + 1u;
					eng::s32 y0 = ysr[i], y1 = ysr[j], x0 = xsr[i], x1 = xsr[j];
					if (y0 == y1) continue;
					if (y0 > y1) {
						const eng::s32 t = y0; y0 = y1; y1 = t;
						const eng::s32 u = x0; x0 = x1; x1 = u;
					}
					if (y < y0 || y >= y1) continue;
					const eng::s32 x = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
					if (x < xl) xl = x;
					if (x > xr) xr = x;
				}
				if (xl > xr) continue;
				emit(y, xl, xr);
			}
		};
		auto cmp = [&](const eng::s32* xsr, const eng::s32* ysr, eng::u32 cnt, const char* msg) {
			eng::s32 ry[64], rxl[64], rxr[64];
			eng::u32 rn = 0;
			ref_rows(xsr, ysr, cnt, [&](eng::s32 y, eng::s32 xl, eng::s32 xr) {
				if (rn < 64) { ry[rn] = y; rxl[rn] = xl; rxr[rn] = xr; ++rn; }
			});
			eng::s32 cy[64], cxl[64], cxr[64];
			eng::u32 cn = 0;
			eng::math3d::convex_spans(
				eng::Span<const eng::s32>(xsr, cnt), eng::Span<const eng::s32>(ysr, cnt),
				[&](eng::s32 y, eng::s32 xl, eng::s32 xr) {
					if (cn < 64) { cy[cn] = y; cxl[cn] = xl; cxr[cn] = xr; ++cn; }
				});
			bool same = (rn == cn);
			for (eng::u32 i = 0; same && i < rn; ++i) {
				same = ry[i] == cy[i] && rxl[i] == cxl[i] && rxr[i] == cxr[i];
			}
			check(same, msg);
		};
		const eng::s32 sq_x[4] = {0, 10, 10, 0}, sq_y[4] = {0, 0, 10, 10};
		const eng::s32 tr_x[3] = {0, 10, 0}, tr_y[3] = {0, 0, 10};
		const eng::s32 pe_x[5] = {5, 10, 9, 1, 0}, pe_y[5] = {0, 4, 10, 10, 4};
		const eng::s32 di_x[4] = {5, 10, 5, 0}, di_y[4] = {0, 5, 10, 5};
		cmp(sq_x, sq_y, 4, "convex_spans: cuadrado == referencia");
		cmp(tr_x, tr_y, 3, "convex_spans: triangulo == referencia");
		cmp(pe_x, pe_y, 5, "convex_spans: pentagono == referencia");
		cmp(di_x, di_y, 4, "convex_spans: rombo == referencia");
	}

	if (failures == 0) {
		std::printf("OK: math3d mesh (transform + culling + painter) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
