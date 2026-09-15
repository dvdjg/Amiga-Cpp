#pragma once

/// \file mesh3d.hpp
/// Modelo de **malla** 3D sobre `math3d` (lib3d): vista no propietaria de
/// vértices + caras triangulares, transformación por lotes, **back-face culling**
/// y **orden de pintado** (painter's algorithm). Todo con buffers del llamador,
/// sin asignación dinámica ni STL (apto para gameplay).
///
/// Patrón de uso:
///
///   Vec3 world[N];                     // scratch del llamador
///   mesh_transform(mesh.vertices, m, world);
///   FaceOrder order[mesh.face_count()];
///   const u32 n = mesh_painter_order(mesh, world, cam, order);
///   for (u32 i = 0; i < n; ++i) {
///       const Face& f = mesh.faces[order[i].index];
///       draw_triangle(world[f.a], world[f.b], world[f.c]);
///   }
///
/// Reutiliza `Face`, `transform`, `face_visible` y `face_z_min` de `math3d`
/// (no duplica); aquí solo vive la composición «malla → caras listas para pintar».

#include <eng/core/linalg.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::math3d {

/// Punto/vector 3D crudo: 3 enteros. Es del modelo de malla (no depende del 4.12 ni del
/// chipset); un backend lo cruza con sus escalares cuando transforma.
struct Vec3 {
	s16 x = 0;
	s16 y = 0;
	s16 z = 0;
};

/// Cara triangular: 3 índices sobre el array de vértices.
struct Face {
	u16 a = 0;
	u16 b = 0;
	u16 c = 0;
};

/// Producto mixto `(B-A)·[(C-A)×(cam-A)]` en 32 bits (signo = visibilidad de la cara
/// `(A,B,C)` desde `cam`). Back-face culling sin normalizar (solo importa el signo).
constexpr s32 face_signed_area(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& cam) {
	const s32 ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
	const s32 vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
	const s32 nx = uy * vz - uz * vy;
	const s32 ny = uz * vx - ux * vz;
	const s32 nz = ux * vy - uy * vx;
	return nx * (cam.x - a.x) + ny * (cam.y - a.y) + nz * (cam.z - a.z);
}

/// ¿Es visible la cara `(a,b,c)` desde `cam`? (`>= 0`, como el origen).
constexpr bool face_visible(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& cam) {
	return face_signed_area(a, b, c, cam) >= 0;
}

/// Clave de orden Z por SUMA de los z de la cara (como `SortFaces`).
constexpr s16 face_z_sum(const Vec3& a, const Vec3& b, const Vec3& c) {
	return static_cast<s16>(a.z + b.z + c.z);
}

/// Clave de orden Z por MÍNIMO de los z de la cara (como `SortFacesMinZ`).
constexpr s16 face_z_min(const Vec3& a, const Vec3& b, const Vec3& c) {
	const s16 ab = a.z < b.z ? a.z : b.z;
	return ab < c.z ? ab : c.z;
}

/// Vista no propietaria de una malla: vértices compartidos (enlazado por índice)
/// y caras que los referencian. No posee memoria; el llamador la mantiene viva.
struct MeshView {
	Span<const Vec3> vertices {};
	Span<const Face> faces {};

	[[nodiscard]] constexpr u32 vertex_count() const { return static_cast<u32>(vertices.size()); }
	[[nodiscard]] constexpr u32 face_count() const { return static_cast<u32>(faces.size()); }
};

/// Transforma los vértices `in` al mundo con un afín `out = M·in + t`. Es **genérico**
/// sobre el escalar del afín: sirve para `Affine<3,q12,q0>` (Amiga), `Affine<3,float,float>`
/// o cualquier otro. Un backend de CPU puede especializar la aritmética por dentro.
template <int N, typename SR, typename SL>
inline void mesh_transform(Span<const Vec3> in, const eng::math::Affine<N, SR, SL>& m, Span<Vec3> out) {
	const u32 n = static_cast<u32>(in.size() < out.size() ? in.size() : out.size());
	for (u32 i = 0; i < n; ++i) {
		const eng::math::Vec<N, SL> p {{SL {in[i].x}, SL {in[i].y}, SL {in[i].z}}};
		const eng::math::Vec<N, SL> w = eng::math::transform(m, p);
		out[i].x = static_cast<s16>(w.v[0].v);
		out[i].y = static_cast<s16>(w.v[1].v);
		out[i].z = static_cast<s16>(w.v[2].v);
	}
}

/// Cara visible + clave de profundidad para el orden de pintado.
struct FaceOrder {
	u16 index = 0; // índice de la cara en `MeshView::faces`
	s16 z = 0;     // clave painter (`face_z_min`); menor = más lejana
};

/// Clasifica por **back-face culling** desde `cam` y ordena las caras visibles de
/// lejos a cerca (pintor). Escribe hasta `out.size()` entradas y devuelve cuántas
/// caras visibles caben. `verts` son los vértices ya transformados (mismo tamaño
/// que `mesh.vertices`).
///
/// Con `double_sided = false` (por defecto) se descartan las caras ocultas
/// (`face_visible`). Con `double_sided = true` se incluyen TODAS las caras válidas
/// (equivalente a `AllFacesDoubleSided`): útil para mallas abiertas como suelos o
/// paredes, donde la cara trasera también debe verse.
///
/// Ordenación por **shell sort** in-place (sin memoria extra): para mallas de
/// juego (decenas/cientos de caras) evita el coste O(n²) de una inserción y se
/// mantiene 100 % aritmética entera. Las caras con índices fuera de rango se
/// descartan silenciosamente (malla corrupta no rompe el frame).
inline u32 mesh_painter_order(const MeshView& mesh, Span<const Vec3> verts,
			      const Vec3& cam, Span<FaceOrder> out,
			      bool double_sided = false) {
	const u32 nv = mesh.vertex_count();
	const u32 cap = static_cast<u32>(out.size());
	u32 n = 0;
	for (u32 i = 0; i < mesh.face_count() && n < cap; ++i) {
		const Face& f = mesh.faces[i];
		if (f.a >= nv || f.b >= nv || f.c >= nv) {
			continue;
		}
		const Vec3& a = verts[f.a];
		const Vec3& b = verts[f.b];
		const Vec3& c = verts[f.c];
		if (double_sided || face_visible(a, b, c, cam)) {
			out[n].index = static_cast<u16>(i);
			out[n].z = face_z_min(a, b, c);
			++n;
		}
	}

	// Shell sort ascendente por z: la cara más lejana (z menor) se pinta primero,
	// de modo que las cercanas la sobrescriban (painter's algorithm).
	for (u32 gap = n / 2u; gap > 0u; gap /= 2u) {
		for (u32 i = gap; i < n; ++i) {
			const FaceOrder tmp = out[i];
			u32 j = i;
			while (j >= gap && out[j - gap].z > tmp.z) {
				out[j] = out[j - gap];
				j -= gap;
			}
			out[j] = tmp;
		}
	}
	return n;
}

} // namespace eng::math3d
