#pragma once

/// \file mesh3d.hpp
/// Modelo de **malla** 3D: vista no propietaria de vértices + caras triangulares,
/// transformación por lotes, **back-face culling** y **orden de pintado** (painter's
/// algorithm). Todo con buffers del llamador, sin asignación dinámica ni STL.
///
/// La **coordenada** es un escalar tipado (`Coord = Fixed<s16,0>`, LONGITUD) y el punto
/// es el `Vec<3,Coord>` genérico: mismo tamaño (6 B) que tres `s16`, pero entra en el
/// álgebra de `eng::math` (`transform`, `dot`, `Vec`) y sus productos usan
/// `arith<s16>::mul` → **`muls.w`** en el 68000, nunca `__mulsi3`. Las claves derivadas
/// (signo del producto mixto, suma/mínimo de Z) siguen siendo enteros: al culling y al
/// orden solo les importa signo/orden, no la fracción.
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
/// Convención de coordenada: mallas de juego con componentes en `s16` (el rango de una
/// LONGITUD de 16 bits); las diferencias que entran en el producto mixto se tratan como
/// `s16`, igual que el original.

#include <eng/core/arith.hpp>
#include <eng/core/fixed.hpp>
#include <eng/core/linalg.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::math3d {

/// Coordenada de modelo (LONGITUD): entero con signo como escalar tipado (`1.0 == 1`).
using Coord = eng::math::Fixed<s16, 0>;

/// Punto/vector 3D de la malla. Es el `Vec<3, Coord>` genérico (mismo layout que tres
/// `s16`), así que `eng::math::transform`/`dot`/`Vec` funcionan sin puente.
using Vec3 = eng::math::Vec<3, Coord>;

/// Construye un vértice a partir de literales (`vec3(-48, -48, -48)`).
[[nodiscard]] constexpr Vec3 vec3(int x, int y, int z) {
	return Vec3 {{Coord {static_cast<s16>(x)}, Coord {static_cast<s16>(y)}, Coord {static_cast<s16>(z)}}};
}

/// Coordenada `i` (0..2) como entero crudo: el culling/orden trabajan en enteros.
[[nodiscard]] constexpr s16 coord_at(const Vec3& p, int i) { return p.v[i].v; }

/// Cara triangular: 3 índices sobre el array de vértices.
struct Face {
	u16 a = 0;
	u16 b = 0;
	u16 c = 0;
};

namespace detail {

/// Producto `s16 × s16 -> s32` con la multiplicación nativa del `arith` del CPU
/// (`muls.w` en 68000). Escribir `(s32)a * (s32)b` acabaría en `__mulsi3` (~50+ ciclos).
[[nodiscard]] constexpr s32 mul16(s16 a, s16 b) { return eng::math::arith<s16>::mul(a, b); }

/// Producto `s32 × s16 -> s32` (se conservan los 32 bits bajos, como el `*` directo) sin
/// `__mulsi3`: se parte el operando de 32 bits y se usan dos multiplicaciones de 16 bits
/// nativas. El segundo operando se sign-extiende (de ahí la corrección de `b < 0`); el
/// resultado queda bit a bit igual que `(s32)a * (s32)b` (probado por HOST-013).
[[nodiscard]] constexpr s32 mul32x16(s32 a, s16 b) {
	const u16 a_lo = static_cast<u16>(static_cast<u32>(a) & 0xFFFFu);
	const s16 a_hi = static_cast<s16>(static_cast<u32>(a) >> 16);
	u32 lo_prod = eng::math::arith<s16>::mulu(a_lo, static_cast<u16>(b));
	if (b < 0) lo_prod -= static_cast<u32>(a_lo) << 16;
	const s32 hi_prod = eng::math::arith<s16>::mul(a_hi, b);
	return static_cast<s32>(lo_prod + (static_cast<u32>(hi_prod) << 16));
}

} // namespace detail

/// Producto mixto `(B-A)·[(C-A)×(cam-A)]` en 32 bits (signo = visibilidad de la cara
/// `(A,B,C)` desde `cam`). Back-face culling sin normalizar (solo importa el signo).
/// Los productos 16×16 usan `muls.w`; las diferencias se tratan como `s16` (contrato de
/// coordenada de modelo documentado arriba).
constexpr s32 face_signed_area(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& cam) {
	const s16 ux = static_cast<s16>(b.v[0].v - a.v[0].v);
	const s16 uy = static_cast<s16>(b.v[1].v - a.v[1].v);
	const s16 uz = static_cast<s16>(b.v[2].v - a.v[2].v);
	const s16 vx = static_cast<s16>(c.v[0].v - a.v[0].v);
	const s16 vy = static_cast<s16>(c.v[1].v - a.v[1].v);
	const s16 vz = static_cast<s16>(c.v[2].v - a.v[2].v);
	const s32 nx = detail::mul16(uy, vz) - detail::mul16(uz, vy);
	const s32 ny = detail::mul16(uz, vx) - detail::mul16(ux, vz);
	const s32 nz = detail::mul16(ux, vy) - detail::mul16(uy, vx);
	return detail::mul32x16(nx, static_cast<s16>(cam.v[0].v - a.v[0].v)) +
	       detail::mul32x16(ny, static_cast<s16>(cam.v[1].v - a.v[1].v)) +
	       detail::mul32x16(nz, static_cast<s16>(cam.v[2].v - a.v[2].v));
}

/// ¿Es visible la cara `(a,b,c)` desde `cam`? (`>= 0`, como el origen).
constexpr bool face_visible(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& cam) {
	return face_signed_area(a, b, c, cam) >= 0;
}

/// Clave de orden Z por SUMA de los z de la cara (como `SortFaces`).
constexpr s16 face_z_sum(const Vec3& a, const Vec3& b, const Vec3& c) {
	return static_cast<s16>(a.v[2].v + b.v[2].v + c.v[2].v);
}

/// Clave de orden Z por MÍNIMO de los z de la cara (como `SortFacesMinZ`).
constexpr s16 face_z_min(const Vec3& a, const Vec3& b, const Vec3& c) {
	const s16 ab = a.v[2].v < b.v[2].v ? a.v[2].v : b.v[2].v;
	return ab < c.v[2].v ? ab : c.v[2].v;
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
/// sobre el escalar del afín: sirve para `Affine<3,q12,q0>` (Amiga), `Affine<3,MiniFloat16,..>`
/// o cualquier otro. La coordenada de modelo entra como LONGITUD (`Coord`).
template <int N, typename SR, typename SL>
inline void mesh_transform(Span<const Vec3> in, const eng::math::Affine<N, SR, SL>& m,
			   Span<Vec3> out) {
	const u32 n = static_cast<u32>(in.size() < out.size() ? in.size() : out.size());
	for (u32 i = 0; i < n; ++i) {
		const eng::math::Vec<N, SL> p {{SL {in[i].v[0].v}, SL {in[i].v[1].v}, SL {in[i].v[2].v}}};
		const eng::math::Vec<N, SL> w = eng::math::transform(m, p);
		out[i].v[0].v = static_cast<s16>(w.v[0].v);
		out[i].v[1].v = static_cast<s16>(w.v[1].v);
		out[i].v[2].v = static_cast<s16>(w.v[2].v);
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
inline u32 mesh_painter_order(const MeshView& mesh, Span<const Vec3> verts, const Vec3& cam,
			      Span<FaceOrder> out, bool double_sided = false) {
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
