#define ENG_SCALAR_RETRO16  // host: instancia retro (eng::real=q12, coord=q0)
// Test host de eng::object3d (modelo obj2c + Object3D portado de lib3d).
#include <eng/platform/amiga/object3d.hpp>
#include <eng/platform/amiga/object3d_poly.hpp>
#include <eng/platform/amiga/lib3d.hpp>

#include <cstdio>

using namespace eng::object3d;

// La `pilka` real (icosaedro truncado, asset de la demo 116) para el adaptador n-gon.
#include "../../../../../../demos/techniques/amiga/effects/116_flatshade_convex/src/data/pilka.c"

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
	static short empty_group[1] = {0};
	const eng::Span<eng::u8> bytes {reinterpret_cast<eng::u8*>(data), sizeof(data)};
	Mesh3D mesh {};
	mesh.vertices = 1;
	mesh.bytes = bytes;
	mesh.vertexGroups = empty_group;
	mesh.edgeGroups = empty_group;
	mesh.faceGroups = empty_group;
	mesh.objects = nullptr;

	Object3D obj {};
	new_object3d(obj, mesh);
	check(obj.objdat == bytes.data() && obj.objdat_size == bytes.size(), "new_object3d enlaza objdat");
	check(obj.scale.x.v == (1 << 12) && obj.scale.y.v == (1 << 12), "scale inicial 1.0 (4.12)");

	// Validacion del descriptor: blob no vacio, grupos dentro de rango y sin trampa.
	check(mesh_validate(mesh), "mesh_validate: malla valida");
	check(new_object3d_checked(obj, mesh), "new_object3d_checked: malla valida -> true");
	{
		Mesh3D no_blob = mesh;
		no_blob.bytes = {};
		check(!mesh_validate(no_blob), "mesh_validate: sin blob -> false");
		check(!new_object3d_checked(obj, no_blob), "new_object3d_checked: sin blob -> false");
		static short bad_group[2] = {1000, 0};
		Mesh3D out_of_range = mesh;
		out_of_range.vertexGroups = bad_group;
		check(!mesh_validate(out_of_range), "mesh_validate: grupo fuera de rango -> false");
		static short odd_group[2] = {3, 0};
		Mesh3D misaligned = mesh;
		misaligned.vertexGroups = odd_group;
		check(!mesh_validate(misaligned), "mesh_validate: offset impar -> false");
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

	// Adaptador obj2c -> math3d::PolyMeshView (n-gon) + orden por parches convexos.
	{
		static short quad[41] = {
			0, 0, 0, 0, 0, 0, 0,	   // nodo 0 (off 0; point @2)
			0, 100, 0, 0, 0, 0, 0,	   // nodo 1 (off 14; point @16)
			0, 100, 100, 0, 0, 0, 0,   // nodo 2 (off 28; point @30)
			0, 0, 100, 0, 0, 0, 0,	   // nodo 3 (off 42; point @44)
			0, 0, 4096, 0, 4,	   // cara @56: normal (0,0,1), flags/mat, count=4
			2, 0, 16, 0, 30, 0, 44, 0, // FaceIndex: vertex-offset, edge-offset
		};
		static short qv[6] = {2, 16, 30, 44, 0, 0};
		static short qe[2] = {0, 0};
		static short qf[3] = {56, 0, 0};
		Mesh3D quad_mesh {};
		quad_mesh.vertices = 4;
		quad_mesh.faces = 1;
		quad_mesh.materials = 1;
		quad_mesh.bytes = {reinterpret_cast<eng::u8*>(quad), sizeof(quad)};
		quad_mesh.vertexGroups = {qv, 6};
		quad_mesh.edgeGroups = {qe, 2};
		quad_mesh.faceGroups = {qf, 3};
		check(mesh_validate(quad_mesh), "adaptador: mesh_validate del quad");

		Object3D qobj {};
		new_object3d(qobj, quad_mesh);

		eng::math3d::Vec3 qverts[8];
		s16 qoffsets[8];
		eng::u16 qindices[16];
		eng::math3d::FaceSpan qfaces[4];
		eng::math3d::Vec3 qnorm[4];
		eng::math3d::PolyMeshView qview {};
		const PolyMeshCounts qc = build_poly_mesh(
			qobj, eng::Span<eng::math3d::Vec3>(qverts, 8), eng::Span<s16>(qoffsets, 8),
			eng::Span<eng::u16>(qindices, 16),
			eng::Span<eng::math3d::FaceSpan>(qfaces, 4),
			eng::Span<eng::math3d::Vec3>(qnorm, 4), qview);
		check(qc.vertices == 4 && qc.faces == 1 && qc.indices == 4, "adaptador: conteos");
		check(qview.face_indices(0).size() == 4 && qview.face_indices(0)[2] == 2,
		      "adaptador: FaceIndex offset -> indice");
		check(qview.vertices[1].v[0].v == 100 && qview.vertices[3].v[1].v == 100,
		      "adaptador: vertices de modelo (q0)");

		eng::math3d::FaceOrder qorder[4];
		const eng::u32 qn = eng::math3d::mesh_patches_order(
			qview, qview.vertices, eng::math3d::vec3<eng::coord>(50, 50, 200),
			eng::Span<eng::math3d::FaceOrder>(qorder, 4));
		check(qn == 1 && qorder[0].index == 0,
		      "adaptador: mesh_patches_order (1 parche visible)");
	}

	// Pilka real (obj2c): el adaptador da 32 parches n-gon; triangular serian 116.
	{
		Object3D po {};
		new_object3d(po, pilka);
		static eng::math3d::Vec3 pverts[64];
		static s16 poff[64];
		static eng::u16 pidx[256];
		static eng::math3d::FaceSpan pface[64];
		static eng::math3d::Vec3 pnorm[64];
		eng::math3d::PolyMeshView pview {};
		const PolyMeshCounts c = build_poly_mesh(
			po, eng::Span<eng::math3d::Vec3>(pverts, 64), eng::Span<s16>(poff, 64),
			eng::Span<eng::u16>(pidx, 256),
			eng::Span<eng::math3d::FaceSpan>(pface, 64),
			eng::Span<eng::math3d::Vec3>(pnorm, 64), pview);
		check(c.vertices == 60 && c.faces == 32, "pilka: 60 vertices / 32 caras");
		eng::u32 tris = 0;
		for (eng::u32 i = 0; i < c.faces; ++i) {
			tris += static_cast<eng::u32>(pview.faces[i].count) - 2u;
		}
		check(c.indices == 180 && tris == 116, "pilka: 180 indices = 116 triangulos");
		static eng::math3d::FaceOrder porder[64];
		const eng::u32 vis = eng::math3d::mesh_patches_order(
			pview, pview.vertices, eng::math3d::vec3<eng::coord>(0, 0, 6000),
			eng::Span<eng::math3d::FaceOrder>(porder, 64));
		check(vis > 0 && vis < 32, "pilka: culling parcial de parches");
	}

	// El culling n-gon de math3d coincide con lib3d sobre la pilka (misma cara visible).
	{
		Object3D po {};
		new_object3d(po, pilka);
		po.rotate = {};
		po.scale = {eng::retro::q12 {1 << 12}, eng::retro::q12 {1 << 12}, eng::retro::q12 {1 << 12}};
		po.translate = {eng::retro::q0 {0}, eng::retro::q0 {0}, eng::retro::q0 {-6000}};
		update_object_transformation(po);
		eng::lib3d::update_face_visibility(po);
		eng::u32 lib3d_visible = 0;
		const s16* fg = po.faceGroups;
		if (fg != nullptr) {
			do {
				s16 off;
				while ((off = *fg++) != 0) {
					if (po.face(off)->flags >= 0) ++lib3d_visible;
				}
			} while (*fg != 0);
		}

		static eng::math3d::Vec3 mv[64];
		static s16 moff[64];
		static eng::u16 midx[256];
		static eng::math3d::FaceSpan mface[64];
		static eng::math3d::Vec3 mnorm[64];
		eng::math3d::PolyMeshView mview {};
		build_poly_mesh(po, eng::Span<eng::math3d::Vec3>(mv, 64), eng::Span<s16>(moff, 64),
				eng::Span<eng::u16>(midx, 256),
				eng::Span<eng::math3d::FaceSpan>(mface, 64),
				eng::Span<eng::math3d::Vec3>(mnorm, 64), mview);
		const eng::math3d::Vec3 mcam {{po.camera.x, po.camera.y, po.camera.z}};
		static eng::math3d::FaceOrder morder[64];
		const eng::u32 m3d_visible = eng::math3d::mesh_patches_order(
			mview, mview.vertices, mcam, eng::Span<eng::math3d::FaceOrder>(morder, 64));
		check(lib3d_visible == m3d_visible,
		      "pilka: math3d n-gon (Newell) == lib3d (caras visibles)");
		// El cull por normal almacenada (apto 68000) coincide EXACTO con lib3d.
		static eng::math3d::FaceOrder morder_lit[64];
		const eng::u32 lit_visible = eng::math3d::mesh_patches_order_lit(
			mview, mview.vertices, mcam, eng::Span<eng::math3d::FaceOrder>(morder_lit, 64));
		check(lit_visible == lib3d_visible,
		      "pilka: math3d n-gon (normal almacenada) == lib3d (caras visibles)");
	}

	if (failures == 0) {
		std::printf("OK: object3d (obj2c + Object3D) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
