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
/// ```text
///   vistas de malla                    ordenador (cualidad de compilacion)          salida
///   ───────────────                    ────────────────────────────────────         ──────
///   MeshViewT<S>   (triangulos) ─┐     ┌─ ConvexSolid    : cull, SIN sort  (ConvexFace, 2 B)
///                                ├────►│  MeshFaceOrder<Kind>  (+ mesh_traits<S>)
///   PolyMeshViewT<S> (n-gon) ────┘     ├─ ConcaveMesh    : pintor por triangulo
///                                      └─ ConvexPatches  : pintor por parche n-gon (n-gon convexo)
///
///   mesh_transform(M, verts) ─► world ─► order(...) ─► { index,[z] } ─► relleno (convex_spans/blitter)
/// ```
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

// `mesh3d` es genérico sobre el escalar (Vec3t<S>/MeshViewT<S>); NO incluye el escalar concreto.
// El alias por defecto `Coord = eng::coord` viene de `scalar.hpp` (cabecera de selección de escalar
// por target, exenta). Ver AGENTS §1.10.
#include <eng/core/math/arith.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/data/polygon.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

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

/// Cara de longitud variable **n-gon**: rango `[first, first+count)` sobre un array plano de
/// índices de vértice. Un triángulo es el caso `count == 3`. Es la forma amiga del raster
/// Amiga (rellena polígonos convexos, no triángulos): evita triangular una cara de 5-6 lados.
struct FaceSpan {
	u16 first = 0;
	u16 count = 0;
};

/// Rasgos de malla **por escalar**: culling y claves de orden. El default usa la aritmética
/// del propio escalar `S` (vale para `float`/`double`/enteros). Un escalar con camino
/// optimizado lo especializa (p. ej. `Fixed<s16,0>` en `eng/retro/fixed_mesh.hpp`), de modo
/// que este header genérico no conoce ninguna representación concreta.
template <class S>
struct mesh_traits {
	using scalar = S;
	/// Tipo de la clave de orden Z (profundidad); por defecto `s16`.
	using key = s16;

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
	[[nodiscard]] static constexpr key z_sum(const Vec3t<S>& a, const Vec3t<S>& b,
						 const Vec3t<S>& c) {
		return static_cast<key>(
			eng::math::scalar_traits<S>::to_int(a.v[2] + b.v[2] + c.v[2]));
	}

	/// Clave de orden Z por MÍNIMO de los z de la cara.
	[[nodiscard]] static constexpr key z_min(const Vec3t<S>& a, const Vec3t<S>& b,
						 const Vec3t<S>& c) {
		const S ab = a.v[2] < b.v[2] ? a.v[2] : b.v[2];
		return static_cast<key>(eng::math::scalar_traits<S>::to_int(ab < c.v[2] ? ab : c.v[2]));
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
[[nodiscard]] constexpr typename mesh_traits<S>::key face_z_sum(const Vec3t<S>& a,
								const Vec3t<S>& b,
								const Vec3t<S>& c) {
	return mesh_traits<S>::z_sum(a, b, c);
}

/// Clave de orden Z por MÍNIMO de los z de la cara (como `SortFacesMinZ`).
template <class S>
[[nodiscard]] constexpr typename mesh_traits<S>::key face_z_min(const Vec3t<S>& a,
								const Vec3t<S>& b,
								const Vec3t<S>& c) {
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

/// Vista no propietaria de una malla con caras **n-gon**: vértices compartidos + índices
/// concatenados + caras (rangos). Un triángulo es el caso `count == 3`. Es la forma amiga del
/// raster Amiga (rellena polígonos convexos): evita triangular caras de 5-6 lados.
template <class S = Coord>
struct PolyMeshViewT {
	Span<const Vec3t<S>> vertices {};
	Span<const u16> indices {}; // índices de vértice, concatenados por cara
	Span<const FaceSpan> faces {};
	/// Normales por cara **opcionales** (vacío = se calculan por Newell). Se guardan con el
	/// escalar del vértice aunque la normal «real» sea un ratio: para el culling solo importa
	/// el **signo** de `n·(cam-p0)`, invariante a un reescalado del vector. Así el cull con
	/// normal almacenada es `q0·q0` (sin 64 bits), apto para el 68000.
	Span<const Vec3t<S>> normals {};

	[[nodiscard]] constexpr u32 vertex_count() const { return static_cast<u32>(vertices.size()); }
	[[nodiscard]] constexpr u32 face_count() const { return static_cast<u32>(faces.size()); }
	/// Índices de vértice de la cara `f` (sin comprobar rango).
	[[nodiscard]] constexpr Span<const u16> face_indices(u32 f) const {
		const FaceSpan s = faces[f];
		return Span<const u16>(indices.data() + s.first, s.count);
	}
};

/// Instancia por defecto (escalar `Coord`).
using PolyMeshView = PolyMeshViewT<Coord>;

/// Visibilidad de una cara n-gon planar `f` desde `cam`: signo de la normal de **Newell**
/// (válida también para caras no convexas) contra la vista `cam - p0`.
///
/// Nota de rango: el producto acumula en el exponente ancho del escalar (para `Fixed<s16,0>`
/// usa 64 bits en el último producto), así que **no** es apto para el camino retro optimizado
/// (que trunca a 32 bits): para una cámara lejana el signo puede invertirse. Una
/// especialización retro correcta necesitaría la normal **normalizada** por cara (como guarda
/// `obj2c` y usa `lib3d`), no la arista cruda.
template <class S>
[[nodiscard]] inline s32 poly_face_signed_area(const PolyMeshViewT<S>& mesh, u32 f,
					       Span<const Vec3t<S>> verts, const Vec3t<S>& cam) {
	using W = decltype(verts[0].v[0] * verts[0].v[0]);
	const FaceSpan s = mesh.faces[f];
	W nx {};
	W ny {};
	W nz {};
	for (u32 k = 0; k < s.count; ++k) {
		const Vec3t<S>& p = verts[mesh.indices[s.first + k]];
		const Vec3t<S>& q = verts[mesh.indices[s.first + (k + 1u) % s.count]];
		nx = nx + (p.v[1] - q.v[1]) * (p.v[2] + q.v[2]);
		ny = ny + (p.v[2] - q.v[2]) * (p.v[0] + q.v[0]);
		nz = nz + (p.v[0] - q.v[0]) * (p.v[1] + q.v[1]);
	}
	const Vec3t<S>& p0 = verts[mesh.indices[s.first]];
	const auto d = nx * (cam.v[0] - p0.v[0]) + ny * (cam.v[1] - p0.v[1]) +
		       nz * (cam.v[2] - p0.v[2]);
	const auto zero = decltype(d) {};
	return d < zero ? -1 : (d > zero ? 1 : 0);
}

/// ¿Es visible la cara n-gon `f` desde `cam`?
template <class S>
[[nodiscard]] inline bool poly_face_visible(const PolyMeshViewT<S>& mesh, u32 f,
					    Span<const Vec3t<S>> verts, const Vec3t<S>& cam) {
	return poly_face_signed_area<S>(mesh, f, verts, cam) >= 0;
}

/// Visibilidad usando la **normal almacenada** por cara (`mesh.normals[f]`): `signo(n·(cam-p0))`.
/// Es la ruta **apta para 68000** (`n` y `cam-p0` son del mismo tipo → producto s32, sin
/// 64 bits), equivalente al culling de `lib3d`. Requiere `mesh.normals` no vacío.
template <class S>
[[nodiscard]] inline s32 poly_face_signed_area_normal(const PolyMeshViewT<S>& mesh, u32 f,
						      Span<const Vec3t<S>> verts,
						      const Vec3t<S>& cam) {
	const Vec3t<S>& n = mesh.normals[f];
	const Vec3t<S>& p0 = verts[mesh.indices[mesh.faces[f].first]];
	const auto d = n.v[0] * (cam.v[0] - p0.v[0]) + n.v[1] * (cam.v[1] - p0.v[1]) +
		       n.v[2] * (cam.v[2] - p0.v[2]);
	const auto zero = decltype(d) {};
	return d < zero ? -1 : (d > zero ? 1 : 0);
}

/// ¿Es visible la cara `f` por su normal almacenada?
template <class S>
[[nodiscard]] inline bool poly_face_visible_normal(const PolyMeshViewT<S>& mesh, u32 f,
						   Span<const Vec3t<S>> verts,
						   const Vec3t<S>& cam) {
	return poly_face_signed_area_normal<S>(mesh, f, verts, cam) >= 0;
}

/// Clave de orden Z por MÍNIMO de los z de la cara n-gon `f`.
template <class S>
[[nodiscard]] inline typename mesh_traits<S>::key poly_face_z_min(const PolyMeshViewT<S>& mesh,
								  u32 f,
								  Span<const Vec3t<S>> verts) {
	const FaceSpan s = mesh.faces[f];
	s32 m = static_cast<s32>(
		eng::math::scalar_traits<S>::to_int(verts[mesh.indices[s.first]].v[2]));
	for (u32 k = 1; k < s.count; ++k) {
		const s32 z = static_cast<s32>(
			eng::math::scalar_traits<S>::to_int(verts[mesh.indices[s.first + k]].v[2]));
		if (z < m) {
			m = z;
		}
	}
	return static_cast<typename mesh_traits<S>::key>(m);
}

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

/// Cara visible + clave de profundidad para el orden de pintado. La clave `Key` la fija
/// `mesh_traits<S>::key` (por defecto `s16`; `s32` para escalares anchos).
template <class Key = s16>
struct FaceOrderT {
	u16 index = 0; // índice de la cara en `MeshView::faces`
	Key z = 0;     // clave painter (`face_z_min`); menor = más lejana
};

/// Instancia por defecto (clave `s16`).
using FaceOrder = FaceOrderT<s16>;

/// Cara visible **sin clave de profundidad**: el caso convexo no ordena, así que no guarda
/// `z` (lista de 2 B/cara en vez de 4 B).
struct ConvexFace {
	u16 index = 0; // índice de la cara en `MeshView::faces`
};

/// **Cualidad de compilación** que selecciona el algoritmo de orden de caras.
/// - `ConvexSolid`: sólido convexo. Tras descartar las caras traseras, las visibles
///   particionan la silueta y **no se solapan** en proyección: basta el culling, sin clave
///   de profundidad ni ordenación.
/// - `ConcaveMesh`: malla general de triángulos. Necesita el **orden de pintor** (lejos→cerca).
/// - `ConvexPatches`: malla cóncava descompuesta en **parches convexos** (n-gon): se ordena por
///   parche (pintor) y cada parche se rellena por el camino convexo. Reutiliza el mecanismo:
///   sólo cambia la vista (n-gon) y la intención.
struct ConvexSolid {};
struct ConcaveMesh {};
struct ConvexPatches {};
/// Como `ConvexPatches` pero con la **normal almacenada** por cara (obj2c/lib3d): cull sin
/// 64 bits, apto para el 68000. Requiere `PolyMeshViewT::normals`.
struct ConvexPatchesLit {};

/// Qué necesita el algoritmo para cada cualidad (punto de extensión). El primario es el caso
/// general (seguro: clave + sort); `ConvexSolid` lo especializa. Añadir una cualidad =
/// especializar aquí (y, si cambia el tipo de la lista, `mesh_order_item`).
template <class Kind>
struct mesh_order_traits {
	static constexpr bool depth_key = true;
	static constexpr bool sorts = true;
	/// Culling por la normal **almacenada** (en vez de Newell): evita el producto de 64 bits.
	static constexpr bool cull_normal = false;
};
template <>
struct mesh_order_traits<ConvexSolid> {
	static constexpr bool depth_key = false;
	static constexpr bool sorts = false;
	static constexpr bool cull_normal = false;
};
template <>
struct mesh_order_traits<ConvexPatchesLit> {
	static constexpr bool depth_key = true;
	static constexpr bool sorts = true;
	static constexpr bool cull_normal = true;
};

/// Tipo de la lista de salida por cualidad: el convexo no guarda clave; el resto (general) sí.
template <class Kind, class S>
struct mesh_order_item {
	using type = FaceOrderT<typename mesh_traits<S>::key>;
};
template <class S>
struct mesh_order_item<ConvexSolid, S> {
	using type = ConvexFace;
};
template <class Kind, class S>
using mesh_order_item_t = typename mesh_order_item<Kind, S>::type;

namespace detail {

/// Shell sort in-place por clave `z` (ascendente) de items con campo `.z`.
template <class Item>
inline void shell_sort_by_z(Span<Item> out, u32 n) {
	for (u32 gap = n / 2u; gap > 0u; gap /= 2u) {
		for (u32 i = gap; i < n; ++i) {
			const Item tmp = out[i];
			u32 j = i;
			while (j >= gap && out[j - gap].z > tmp.z) {
				out[j] = out[j - gap];
				j -= gap;
			}
			out[j] = tmp;
		}
	}
}

} // namespace detail

/// **Ordenador de caras**, elegido en compilación por la cualidad `Kind`. Comparte el bucle
/// de culling; `if constexpr` genera una versión por caso (convexo: sin clave ni sort;
/// cóncavo: pintor). No rasteriza: sólo clasifica las caras visibles.
template <class Kind, class S = Coord>
class MeshFaceOrder {
public:
	using item_type = mesh_order_item_t<Kind, S>;
	static constexpr bool kSorts = mesh_order_traits<Kind>::sorts;

	/// Clasifica por **back-face culling** desde `cam` y, si la cualidad lo pide, ordena de
	/// lejos a cerca (shell sort in-place, sin memoria extra). Escribe hasta `out.size()`
	/// entradas, devuelve cuántas caben y descarta caras con índice fuera de rango.
	static u32 order(const MeshViewT<S>& mesh, Span<const Vec3t<S>> verts,
			 const Vec3t<S>& cam, Span<item_type> out, bool double_sided = false) {
		using Tr = mesh_order_traits<Kind>;
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
				if constexpr (Tr::depth_key) {
					out[n].z = face_z_min<S>(a, b, c);
				}
				++n;
			}
		}
		if constexpr (Tr::sorts) {
			detail::shell_sort_by_z(out, n);
		}
		return n;
	}

	/// Igual que `order` pero sobre una malla de caras **n-gon** (`PolyMeshViewT`): culling por
	/// la normal de Newell y clave por el mínimo z de la cara. Un triángulo es `count == 3`.
	static u32 order(const PolyMeshViewT<S>& mesh, Span<const Vec3t<S>> verts,
			 const Vec3t<S>& cam, Span<item_type> out, bool double_sided = false) {
		using Tr = mesh_order_traits<Kind>;
		const u32 nv = mesh.vertex_count();
		const u32 ni = static_cast<u32>(mesh.indices.size());
		const u32 cap = static_cast<u32>(out.size());
		u32 n = 0;
		for (u32 i = 0; i < mesh.face_count() && n < cap; ++i) {
			const FaceSpan s = mesh.faces[i];
			if (s.count < 3u || static_cast<u32>(s.first) + s.count > ni) {
				continue;
			}
			bool ok = true;
			for (u32 k = 0; k < s.count; ++k) {
				if (mesh.indices[s.first + k] >= nv) {
					ok = false;
					break;
				}
			}
			if (!ok) {
				continue;
			}
			bool vis = double_sided;
			if (!vis) {
				if constexpr (Tr::cull_normal) {
					vis = poly_face_visible_normal<S>(mesh, i, verts, cam);
				} else {
					vis = poly_face_visible<S>(mesh, i, verts, cam);
				}
			}
			if (vis) {
				out[n].index = static_cast<u16>(i);
				if constexpr (Tr::depth_key) {
					out[n].z = poly_face_z_min<S>(mesh, i, verts);
				}
				++n;
			}
		}
		if constexpr (Tr::sorts) {
			detail::shell_sort_by_z(out, n);
		}
		return n;
	}
};

/// Orden de **pintor** para una malla general (cóncava): culling + sort lejos→cerca.
template <class S = Coord>
inline u32 mesh_painter_order(const MeshViewT<S>& mesh, Span<const Vec3t<S>> verts,
			      const Vec3t<S>& cam,
			      Span<FaceOrderT<typename mesh_traits<S>::key>> out,
			      bool double_sided = false) {
	return MeshFaceOrder<ConcaveMesh, S>::order(mesh, verts, cam, out, double_sided);
}

/// Orden para un **sólido convexo**: sólo culling (las caras visibles no se solapan, así que
/// el orden es indiferente).
template <class S = Coord>
inline u32 mesh_convex_order(const MeshViewT<S>& mesh, Span<const Vec3t<S>> verts,
			     const Vec3t<S>& cam, Span<ConvexFace> out, bool double_sided = false) {
	return MeshFaceOrder<ConvexSolid, S>::order(mesh, verts, cam, out, double_sided);
}

/// Orden de **parches convexos** (n-gon) de una malla cóncava: culling + pintor por parche.
template <class S = Coord>
inline u32 mesh_patches_order(const PolyMeshViewT<S>& mesh, Span<const Vec3t<S>> verts,
			      const Vec3t<S>& cam,
			      Span<FaceOrderT<typename mesh_traits<S>::key>> out,
			      bool double_sided = false) {
	return MeshFaceOrder<ConvexPatches, S>::order(mesh, verts, cam, out, double_sided);
}

/// Igual que `mesh_patches_order` pero con la **normal almacenada** por cara
/// (`ConvexPatchesLit`): cull sin producto de 64 bits, apto para el 68000. Requiere
/// `mesh.normals` (p. ej. del adaptador `obj2c`).
template <class S = Coord>
inline u32 mesh_patches_order_lit(const PolyMeshViewT<S>& mesh, Span<const Vec3t<S>> verts,
				  const Vec3t<S>& cam,
				  Span<FaceOrderT<typename mesh_traits<S>::key>> out,
				  bool double_sided = false) {
	return MeshFaceOrder<ConvexPatchesLit, S>::order(mesh, verts, cam, out, double_sided);
}

} // namespace eng::math3d
