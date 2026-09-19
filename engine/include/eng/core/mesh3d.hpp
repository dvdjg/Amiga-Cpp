#pragma once

/// \file mesh3d.hpp
/// Modelo de **malla** 3D **genérico sobre el escalar**: vista no propietaria de vértices +
/// caras triangulares, transformación por lotes, **back-face culling** y **orden de pintado**
/// (painter's algorithm). Todo con buffers del llamador, sin asignación dinámica ni STL.
///
/// El escalar de coordenada `S` es un parámetro: sirve `float`/`double`/enteros o cualquier
/// `Fixed`/escalar del engine. La aritmética concreta del culling/orden se aporta por el punto
/// de extensión **`mesh_traits<S>`** (el default usa la aritmética del propio escalar; la
/// variante optimizada para `Fixed<s16,0>` —`muls.w`— vive en `eng/retro/fixed_mesh.hpp`).
/// La instancia por defecto de los alias (`Vec3`, `MeshView`) es `eng::coord`.
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

#include <eng/core/arith.hpp>
#include <eng/core/fixed.hpp>
#include <eng/core/linalg.hpp>
#include <eng/core/scalar.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::math3d {

/// Coordenada de malla por defecto: el escalar de coordenada central (`eng::coord`).
using Coord = eng::coord;

/// Punto/vector 3D genérico sobre el escalar `S`.
template <class S>
using Vec3t = eng::math::Vec<3, S>;

/// Instancia por defecto (escalar `Coord`).
using Vec3 = Vec3t<Coord>;

/// Construye un vértice desde literales con el escalar `S`.
template <class S = Coord>
[[nodiscard]] constexpr Vec3t<S> vec3(int x, int y, int z) {
	using T = eng::math::scalar_traits<S>;
	return Vec3t<S> {{T::from_int(x), T::from_int(y), T::from_int(z)}};
}

/// Coordenada `i` (0..2) como entero crudo: el culling/orden trabajan en enteros.
template <class S>
[[nodiscard]] constexpr s16 coord_at(const Vec3t<S>& p, int i) {
	return static_cast<s16>(eng::math::scalar_traits<S>::to_int(p.v[i]));
}

/// Cara triangular: 3 índices sobre el array de vértices (dominio).
struct Face {
	u16 a = 0;
	u16 b = 0;
	u16 c = 0;
};

/// Rasgos de malla **por escalar**: culling y claves de orden. El default usa la aritmética
/// del propio escalar `S` (vale para `float`/`double`/enteros). Un escalar con camino
/// optimizado lo especializa (p. ej. `Fixed<s16,0>` en `eng/retro/fixed_mesh.hpp`), de modo
/// que este header genérico no conoce ninguna representación concreta.
template <class S>
struct mesh_traits {
	using scalar = S;

	/// Producto mixto `(B-A)·[(C-A)×(cam-A)]` (signo = visibilidad; sin normalizar).
	[[nodiscard]] static constexpr s32 face_signed_area(const Vec3t<S>& a, const Vec3t<S>& b,
							    const Vec3t<S>& c, const Vec3t<S>& cam) {
		const S ux = b.v[0] - a.v[0];
		const S uy = b.v[1] - a.v[1];
		const S uz = b.v[2] - a.v[2];
		const S vx = c.v[0] - a.v[0];
		const S vy = c.v[1] - a.v[1];
		const S vz = c.v[2] - a.v[2];
		const auto nx = uy * vz - uz * vy;
		const auto ny = uz * vx - ux * vz;
		const auto nz = ux * vy - uy * vx;
		const auto d = nx * (cam.v[0] - a.v[0]) + ny * (cam.v[1] - a.v[1]) +
			       nz * (cam.v[2] - a.v[2]);
		const auto zero = decltype(d) {};
		return d < zero ? -1 : (d > zero ? 1 : 0);
	}

	/// Clave de orden Z por SUMA de los z de la cara.
	[[nodiscard]] static constexpr s16 z_sum(const Vec3t<S>& a, const Vec3t<S>& b,
						 const Vec3t<S>& c) {
		return static_cast<s16>(
			eng::math::scalar_traits<S>::to_int(a.v[2] + b.v[2] + c.v[2]));
	}

	/// Clave de orden Z por MÍNIMO de los z de la cara.
	[[nodiscard]] static constexpr s16 z_min(const Vec3t<S>& a, const Vec3t<S>& b,
						 const Vec3t<S>& c) {
		const S ab = a.v[2] < b.v[2] ? a.v[2] : b.v[2];
		return static_cast<s16>(eng::math::scalar_traits<S>::to_int(ab < c.v[2] ? ab : c.v[2]));
	}
};

/// Producto mixto `(B-A)·[(C-A)×(cam-A)]` (signo = visibilidad de la cara `(A,B,C)`).
template <class S>
[[nodiscard]] constexpr s32 face_signed_area(const Vec3t<S>& a, const Vec3t<S>& b, const Vec3t<S>& c,
					     const Vec3t<S>& cam) {
	return mesh_traits<S>::face_signed_area(a, b, c, cam);
}

/// ¿Es visible la cara `(a,b,c)` desde `cam`? (`>= 0`, como el origen).
template <class S>
[[nodiscard]] constexpr bool face_visible(const Vec3t<S>& a, const Vec3t<S>& b, const Vec3t<S>& c,
					  const Vec3t<S>& cam) {
	return mesh_traits<S>::face_signed_area(a, b, c, cam) >= 0;
}

/// Clave de orden Z por SUMA de los z de la cara (como `SortFaces`).
template <class S>
[[nodiscard]] constexpr s16 face_z_sum(const Vec3t<S>& a, const Vec3t<S>& b, const Vec3t<S>& c) {
	return mesh_traits<S>::z_sum(a, b, c);
}

/// Clave de orden Z por MÍNIMO de los z de la cara (como `SortFacesMinZ`).
template <class S>
[[nodiscard]] constexpr s16 face_z_min(const Vec3t<S>& a, const Vec3t<S>& b, const Vec3t<S>& c) {
	return mesh_traits<S>::z_min(a, b, c);
}

/// Vista no propietaria de una malla: vértices compartidos (enlazado por índice) y caras.
template <class S = Coord>
struct MeshViewT {
	Span<const Vec3t<S>> vertices {};
	Span<const Face> faces {};

	[[nodiscard]] constexpr u32 vertex_count() const { return static_cast<u32>(vertices.size()); }
	[[nodiscard]] constexpr u32 face_count() const { return static_cast<u32>(faces.size()); }
};

/// Instancia por defecto (escalar `Coord`).
using MeshView = MeshViewT<Coord>;

/// Transforma los vértices `in` al mundo con un afín `out = M·in + t`. Genérico sobre el
/// escalar del vértice (`S`) y el del afín (`SR`/`SL`); convierte por entero crudo
/// (`to_int`/`from_int`), coste cero cuando `S == SL`.
template <int N, typename SR, typename SL, class S = Coord>
inline void mesh_transform(Span<const Vec3t<S>> in, const eng::math::Affine<N, SR, SL>& m,
			   Span<Vec3t<S>> out) {
	using TS = eng::math::scalar_traits<S>;
	using TL = eng::math::scalar_traits<SL>;
	const u32 n = static_cast<u32>(in.size() < out.size() ? in.size() : out.size());
	for (u32 i = 0; i < n; ++i) {
		const eng::math::Vec<N, SL> p {{TL::from_int(TS::to_int(in[i].v[0])),
						TL::from_int(TS::to_int(in[i].v[1])),
						TL::from_int(TS::to_int(in[i].v[2]))}};
		const eng::math::Vec<N, SL> w = eng::math::transform(m, p);
		out[i].v[0] = TS::from_int(TL::to_int(w.v[0]));
		out[i].v[1] = TS::from_int(TL::to_int(w.v[1]));
		out[i].v[2] = TS::from_int(TL::to_int(w.v[2]));
	}
}

/// Cara visible + clave de profundidad para el orden de pintado.
struct FaceOrder {
	u16 index = 0; // índice de la cara en `MeshView::faces`
	s16 z = 0;     // clave painter (`face_z_min`); menor = más lejana
};

/// Clasifica por **back-face culling** desde `cam` y ordena las caras visibles de lejos a
/// cerca (pintor). Escribe hasta `out.size()` entradas y devuelve cuántas caben. Ordenación
/// por **shell sort** in-place (sin memoria extra); las caras con índices fuera de rango se
/// descartan.
template <class S = Coord>
inline u32 mesh_painter_order(const MeshViewT<S>& mesh, Span<const Vec3t<S>> verts,
			      const Vec3t<S>& cam, Span<FaceOrder> out, bool double_sided = false) {
	const u32 nv = mesh.vertex_count();
	const u32 cap = static_cast<u32>(out.size());
	u32 n = 0;
	for (u32 i = 0; i < mesh.face_count() && n < cap; ++i) {
		const Face& f = mesh.faces[i];
		if (f.a >= nv || f.b >= nv || f.c >= nv) {
			continue;
		}
		const Vec3t<S>& a = verts[f.a];
		const Vec3t<S>& b = verts[f.b];
		const Vec3t<S>& c = verts[f.c];
		if (double_sided || face_visible<S>(a, b, c, cam)) {
			out[n].index = static_cast<u16>(i);
			out[n].z = face_z_min<S>(a, b, c);
			++n;
		}
	}

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
