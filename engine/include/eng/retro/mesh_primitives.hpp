#pragma once

/// \file mesh_primitives.hpp
/// **Primitivas 3D generadas en compilación** para demos y juegos sobre el engine:
/// caja, prisma regular, tubo abierto, cono/pirámide, esfera UV, cápsula y sólidos
/// platónicos (tetraedro, cubo, octaedro, dodecaedro, icosaedro).
///
/// Todo es `constexpr`: el llamador declara su `MeshData` de capacidad fija (estilo del
/// engine: buffers del llamador, sin heap) y la malla queda materializada como datos en
/// el binario (cero coste de runtime). Las mismas funciones se pueden invocar en un
/// programa host para volcar un `.cpp` con las tablas (`tools/`), de modo que ambos
/// caminos salen de la misma fuente.
///
/// Formato: `math3d::PolyMeshViewT<S>` — vértices compartidos + índices concatenados por
/// cara (`FaceSpan`) + normales opcionales. Es la forma amiga del raster Amiga (caras
/// n-gon convexas; una cara con `count == 3` es un triángulo).
///
/// Convenio de winding: caras **CCW vistas desde FUERA** del sólido (el culling del
/// engine, `face_visible`/`mesh_patches_order_lit`, espera esa orientación para descartar
/// las traseras). Los sólidos son cerrados salvo el tubo (abierto, sin tapas).
///
/// Trigonometría: tabla `kSinQ12` de `fixed_trig.hpp` (índice 0..4095 = vuelta), de modo
/// que la generación es puro entero (nada de `float`).

#include <eng/core/math/scalar.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/data/mesh3d.hpp>
#include <eng/retro/fixed_trig.hpp>

namespace eng::retro {

/// Sólido platónico (para `fill_platonic`).
enum class Platonic : u8 {
	Tetrahedron = 4,
	Cube = 6,
	Octahedron = 8,
	Dodecahedron = 12,
	Icosahedron = 20,
};

/// **Malla generada** con capacidad fija: vértices, índices por cara y spans de cara.
/// El llamador la declara `constexpr` y la usa como `const`: `m.view()` da la vista del
/// renderer (`mesh_patches_order_lit`/`mesh_render_poly_filled`/`fill_polygons_by_plane`).
template <class S, u16 MaxV, u16 MaxI, u16 MaxF>
struct MeshData {
	/// Escalar de los vértices; los generadores lo leen como defecto de plantilla.
	using scalar = S;

	eng::math3d::Vec3t<S> verts[MaxV] {};
	u16 indices[MaxI] {};
	eng::math3d::FaceSpan faces[MaxF] {};
	u16 nv = 0;
	u16 ni = 0;
	u16 nf = 0;

	/// Añade un vértice; `false` si la capacidad está llena.
	constexpr bool add_vertex(eng::math3d::Vec3t<S> p) {
		if (nv >= MaxV) {
			return false;
		}
		verts[nv++] = p;
		return true;
	}

	/// Añade una cara con `n` índices; `false` si no cabe.
	constexpr bool add_face(const u16* ids, u16 n) {
		if (nf >= MaxF || static_cast<u32>(ni) + n > MaxI) {
			return false;
		}
		faces[nf++] = eng::math3d::FaceSpan {ni, n};
		for (u16 i = 0; i < n; ++i) {
			indices[ni++] = ids[i];
		}
		return true;
	}

	/// Vista de solo lectura de la malla construida (vértices, índices, caras).
	[[nodiscard]] constexpr eng::math3d::PolyMeshViewT<S> view() const {
		return eng::math3d::PolyMeshViewT<S> {
			eng::Span<const eng::math3d::Vec3t<S>>(verts, nv),
			eng::Span<const u16>(indices, ni),
			eng::Span<const eng::math3d::FaceSpan>(faces, nf),
			{}};
	}
};

/// `v = from_int` para las tres componentes (azúcar de los generadores).
template <class S>
[[nodiscard]] constexpr eng::math3d::Vec3t<S> p3s(int x, int y, int z) {
	return eng::math3d::vec3<S>(x, y, z);
}

namespace detail {

/// `(cos, sin)` en Q12 (4096 = 1.0) del paso `i` de `steps` sobre la vuelta completa.
constexpr void circle_q12(u16 i, u16 steps, s16& c, s16& s) {
	const u32 idx = (static_cast<u32>(i) * 4096u) / steps;
	c = kSinQ12[(idx + 1024u) & 4095u];
	s = kSinQ12[idx & 4095u];
}

/// `(cos, sin)` en Q12 del ángulo polar `ang` (unidades de `kSinQ12`: 4096 = vuelta,
/// 2048 = media vuelta); `cos` = seno desplazado un cuarto de vuelta.
constexpr void polar_q12(u16 ang, s16& c, s16& s) {
	c = kSinQ12[(ang + 1024u) & 4095u];
	s = kSinQ12[ang & 4095u];
}

/// `radio * q12` redondeado (constexpr: sin libcalls en el host).
constexpr int scale_q12(int radius, s16 q) {
	return static_cast<int>((static_cast<s32>(radius) * q + 2048) >> 12);
}

/// Invierte in-place el anillo `ids` si el triángulo que forman sus **tres primeros**
/// vértices no mira hacia fuera del origen: es la misma convención que el culling del
/// engine (`face_visible`, el render decide con los tres primeros puntos de la cara). En
/// los quads no planos de esfera/cápsula la normal del anillo de Newell puede diferir de
/// la del primer triángulo, y el render usa la del triángulo.
template <class S>
constexpr void outward_ring(u16* ids, u16 n, const eng::math3d::Vec3t<S>* v) {
	using T = eng::math::scalar_traits<S>;
	const auto at = [&](u16 i, int k) { return T::to_int(v[ids[i]].v[k]); };
	const int ux = at(1u, 0) - at(0u, 0);
	const int uy = at(1u, 1) - at(0u, 1);
	const int uz = at(1u, 2) - at(0u, 2);
	const int wx = at(2u, 0) - at(0u, 0);
	const int wy = at(2u, 1) - at(0u, 1);
	const int wz = at(2u, 2) - at(0u, 2);
	const int nx = uy * wz - uz * wy;
	const int ny = uz * wx - ux * wz;
	const int nz = ux * wy - uy * wx;
	int sx = 0;
	int sy = 0;
	int sz = 0;
	for (u16 i = 0; i < n; ++i) {
		sx += at(i, 0);
		sy += at(i, 1);
		sz += at(i, 2);
	}
	if (nx * sx + ny * sy + nz * sz < 0) {
		for (u16 i = 0; i < n / 2u; ++i) {
			const u16 t = ids[i];
			ids[i] = ids[n - 1u - i];
			ids[n - 1u - i] = t;
		}
	}
}

} // namespace detail

/// **Caja** centrada en el origen con semiejes `half` (x, y, z): 8 vértices, 6 quads.
template <class Mesh, class S = typename Mesh::scalar>
[[nodiscard]] constexpr bool fill_box(Mesh& m, eng::math3d::Vec3t<S> half) {
	using T = eng::math::scalar_traits<S>;
	const S hx = half.v[0];
	const S hy = half.v[1];
	const S hz = half.v[2];
	const S nx = T::from_int(-T::to_int(hx));
	const S ny = T::from_int(-T::to_int(hy));
	const S nz = T::from_int(-T::to_int(hz));
	// Vértices 0..3 = cara -Z (y luego +Z), CCW visto desde fuera.
	const eng::math3d::Vec3t<S> v[8] = {
		{nx, ny, nz}, {hx, ny, nz}, {hx, hy, nz}, {nx, hy, nz},
		{nx, ny, hz}, {hx, ny, hz}, {hx, hy, hz}, {nx, hy, hz},
	};
	for (u16 i = 0; i < 8u; ++i) {
		if (!m.add_vertex(v[i])) {
			return false;
		}
	}
	const u16 f[6][4] = {
		{4, 5, 6, 7}, // +Z
		{1, 0, 3, 2}, // -Z
		{5, 1, 2, 6}, // +X
		{0, 4, 7, 3}, // -X
		{7, 6, 2, 3}, // +Y
		{0, 1, 5, 4}, // -Y
	};
	for (u16 i = 0; i < 6u; ++i) {
		if (!m.add_face(f[i], 4u)) {
			return false;
		}
	}
	return true;
}

/// **Prisma regular** de `sides` lados, radio y altura; eje Y centrado en el origen
/// (base en -h/2, tapa en +h/2): `sides` quads laterales + 2 tapas n-gon.
template <class Mesh, class S = typename Mesh::scalar>
[[nodiscard]] constexpr bool fill_prism(Mesh& m, u16 sides, s16 radius, s16 height) {
	if (sides < 3u || radius <= 0 || height <= 0) {
		return false;
	}
	// Winding hacia fuera del origen (normales de Newell de cada cara).
	const auto add_face = [&](u16* ids, u16 n) {
		detail::outward_ring<S>(ids, n, m.verts);
		return m.add_face(ids, n);
	};
	const int hh = height / 2;
	for (u16 i = 0; i < sides; ++i) {
		s16 c {}, s {};
		detail::circle_q12(i, sides, c, s);
		const S x = eng::math::scalar_traits<S>::from_int(detail::scale_q12(radius, c));
		const S z = eng::math::scalar_traits<S>::from_int(detail::scale_q12(radius, s));
		const S yb = eng::math::scalar_traits<S>::from_int(-hh);
		const S yt = eng::math::scalar_traits<S>::from_int(hh);
		if (!m.add_vertex(eng::math3d::Vec3t<S> {x, yb, z})) { // i = base
			return false;
		}
		if (!m.add_vertex(eng::math3d::Vec3t<S> {x, yt, z})) { // sides + i = tapa
			return false;
		}
	}
	for (u16 i = 0; i < sides; ++i) {
		const u16 j = static_cast<u16>((i + 1u) % sides);
		u16 quad[4] = {static_cast<u16>(2u * i), static_cast<u16>(2u * j),
				     static_cast<u16>(2u * j + 1u), static_cast<u16>(2u * i + 1u)};
		if (!add_face(quad, 4u)) {
			return false;
		}
	}
	// Tapas: recorrido inverso en la base para dejar la normal hacia fuera.
	u16 cap[32];
	if (sides > 32u) {
		return false;
	}
	for (u16 i = 0; i < sides; ++i) {
		cap[i] = static_cast<u16>(2u * i); // base, CCW vista desde -Y
	}
	if (!add_face(cap, sides)) {
		return false;
	}
	for (u16 i = 0; i < sides; ++i) {
		cap[i] = static_cast<u16>(2u * (sides - 1u - i) + 1u); // tapa, CCW desde +Y
	}
	return add_face(cap, sides);
}

/// **Tubo abierto** (sin tapas): análogo al prisma, para túneles/interiores.
template <class Mesh, class S = typename Mesh::scalar>
[[nodiscard]] constexpr bool fill_tube(Mesh& m, u16 sides, s16 radius, s16 height) {
	if (sides < 3u || radius <= 0 || height <= 0) {
		return false;
	}
	// Winding hacia fuera del origen (normales de Newell de cada cara).
	const auto add_face = [&](u16* ids, u16 n) {
		detail::outward_ring<S>(ids, n, m.verts);
		return m.add_face(ids, n);
	};
	const int hh = height / 2;
	for (u16 i = 0; i < sides; ++i) {
		s16 c {}, s {};
		detail::circle_q12(i, sides, c, s);
		const S x = eng::math::scalar_traits<S>::from_int(detail::scale_q12(radius, c));
		const S z = eng::math::scalar_traits<S>::from_int(detail::scale_q12(radius, s));
		const S yb = eng::math::scalar_traits<S>::from_int(-hh);
		const S yt = eng::math::scalar_traits<S>::from_int(hh);
		if (!m.add_vertex(eng::math3d::Vec3t<S> {x, yb, z})) {
			return false;
		}
		if (!m.add_vertex(eng::math3d::Vec3t<S> {x, yt, z})) {
			return false;
		}
	}
	for (u16 i = 0; i < sides; ++i) {
		const u16 j = static_cast<u16>((i + 1u) % sides);
		u16 quad[4] = {static_cast<u16>(2u * i), static_cast<u16>(2u * j),
				     static_cast<u16>(2u * j + 1u), static_cast<u16>(2u * i + 1u)};
		if (!add_face(quad, 4u)) {
			return false;
		}
	}
	return true;
}

/// **Cono/pirámide**: base regular de `sides` lados en -h/2 y ápice en +h/2.
template <class Mesh, class S = typename Mesh::scalar>
[[nodiscard]] constexpr bool fill_cone(Mesh& m, u16 sides, s16 radius, s16 height) {
	if (sides < 3u || radius <= 0 || height <= 0) {
		return false;
	}
	// Winding hacia fuera del origen (normales de Newell de cada cara).
	const auto add_face = [&](u16* ids, u16 n) {
		detail::outward_ring<S>(ids, n, m.verts);
		return m.add_face(ids, n);
	};
	const int hh = height / 2;
	for (u16 i = 0; i < sides; ++i) {
		s16 c {}, s {};
		detail::circle_q12(i, sides, c, s);
		const S x = eng::math::scalar_traits<S>::from_int(detail::scale_q12(radius, c));
		const S z = eng::math::scalar_traits<S>::from_int(detail::scale_q12(radius, s));
		if (!m.add_vertex(eng::math3d::Vec3t<S> {x, eng::math::scalar_traits<S>::from_int(-hh),
							 z})) {
			return false;
		}
	}
	const u16 apex = sides;
	if (!m.add_vertex(eng::math3d::Vec3t<S> {eng::math::scalar_traits<S>::from_int(0),
						 eng::math::scalar_traits<S>::from_int(hh),
						 eng::math::scalar_traits<S>::from_int(0)})) {
		return false;
	}
	for (u16 i = 0; i < sides; ++i) {
		const u16 j = static_cast<u16>((i + 1u) % sides);
		u16 tri[3] = {i, j, apex};
		if (!add_face(tri, 3u)) {
			return false;
		}
	}
	u16 cap[32];
	if (sides > 32u) {
		return false;
	}
	for (u16 i = 0; i < sides; ++i) {
		cap[i] = i; // base, CCW desde -Y
	}
	return add_face(cap, sides);
}

/// **Esfera UV**: `sectors` meridianos y `stacks` franjas. Polos con triángulos; el resto,
/// quads. Caras CCW vistas desde fuera.
template <class Mesh, class S = typename Mesh::scalar>
[[nodiscard]] constexpr bool fill_sphere(Mesh& m, u16 sectors, u16 stacks, s16 radius) {
	if (sectors < 3u || stacks < 2u || radius <= 0 || sectors > 16u || stacks > 8u) {
		return false;
	}
	// Winding hacia fuera del origen (normales de Newell de cada cara).
	const auto add_face = [&](u16* ids, u16 n) {
		detail::outward_ring<S>(ids, n, m.verts);
		return m.add_face(ids, n);
	};
	const u16 rings = static_cast<u16>(stacks - 1u); // anillos intermedios (sin polos)
	// Polos: 0 = +Y, 1 = -Y; luego (rings * sectors) vértices intermedios.
	if (!m.add_vertex(eng::math3d::Vec3t<S> {eng::math::scalar_traits<S>::from_int(0),
						 eng::math::scalar_traits<S>::from_int(radius),
						 eng::math::scalar_traits<S>::from_int(0)})) {
		return false;
	}
	if (!m.add_vertex(eng::math3d::Vec3t<S> {eng::math::scalar_traits<S>::from_int(0),
						 eng::math::scalar_traits<S>::from_int(-radius),
						 eng::math::scalar_traits<S>::from_int(0)})) {
		return false;
	}
	for (u16 r = 1; r <= rings; ++r) {
		// Ángulo polar desde +Y: `stacks` cubre el cuarto de vuelta del hemisferio (1024).
		const u16 polar = static_cast<u16>((static_cast<u32>(r) * 2048u) / stacks);
		s16 cp {}, sp {};
		detail::polar_q12(polar, cp, sp);
		const int y = detail::scale_q12(radius, cp);
		const int rad = detail::scale_q12(radius, sp);
		for (u16 i = 0; i < sectors; ++i) {
			s16 c {}, s {};
			detail::circle_q12(i, sectors, c, s);
			const S x = eng::math::scalar_traits<S>::from_int(detail::scale_q12(rad, c));
			const S z = eng::math::scalar_traits<S>::from_int(detail::scale_q12(rad, s));
			if (!m.add_vertex(eng::math3d::Vec3t<S> {
				    x, eng::math::scalar_traits<S>::from_int(y), z})) {
				return false;
			}
		}
	}
	const auto ring_v = [&](u16 r, u16 i) -> u16 { // r 0..rings-1, i 0..sectors-1
		return static_cast<u16>(2u + r * sectors + (i % sectors));
	};
	// Casquetes (triángulos) contra los polos.
	for (u16 i = 0; i < sectors; ++i) {
		u16 tri_n[3] = {0u, ring_v(0u, static_cast<u16>(i + 1u)), ring_v(0u, i)};
		if (!add_face(tri_n, 3u)) {
			return false;
		}
		u16 tri_s[3] = {1u, ring_v(static_cast<u16>(rings - 1u), i),
				      ring_v(static_cast<u16>(rings - 1u),
					     static_cast<u16>(i + 1u))};
		if (!add_face(tri_s, 3u)) {
			return false;
		}
	}
	// Quads intermedios.
	for (u16 r = 0; r + 1u < rings; ++r) {
		for (u16 i = 0; i < sectors; ++i) {
			u16 quad[4] = {ring_v(r, i), ring_v(r, static_cast<u16>(i + 1u)),
					     ring_v(static_cast<u16>(r + 1u),
						    static_cast<u16>(i + 1u)),
					     ring_v(static_cast<u16>(r + 1u), i)};
			if (!add_face(quad, 4u)) {
				return false;
			}
		}
	}
	return true;
}

/// **Cápsula**: cilindro de radio `radius` y altura `cyl_height` con casquetes
/// hemisféricos de `cap_stacks` franjas (mismo `sectors`). Eje Y.
template <class Mesh, class S = typename Mesh::scalar>
[[nodiscard]] constexpr bool fill_capsule(Mesh& m, u16 sectors, u16 cap_stacks, s16 radius,
					  s16 cyl_height) {
	if (sectors < 3u || cap_stacks < 2u || radius <= 0 || cyl_height < 0 || sectors > 16u ||
	    cap_stacks > 8u) {
		return false;
	}
	const int hh = cyl_height / 2;
	// Winding hacia fuera del origen (normales de Newell de cada cara).
	const auto add_face = [&](u16* ids, u16 n) {
		detail::outward_ring<S>(ids, n, m.verts);
		return m.add_face(ids, n);
	};
	// Vértices: anillos del hemisferio superior (de +hh hacia arriba), anillo del cilindro
	// en ±hh, y hemisferio inferior. El primer y último anillo del hemisferio son los polos.
	u16 poles[2] = {0xffffu, 0xffffu};
	u16 ring_top = 0;    // primer anillo del hemisferio superior
	u16 ring_mid_top = 0; // anillo del cilindro en +hh
	u16 ring_mid_bot = 0; // anillo del cilindro en -hh
	u16 ring_bot = 0;     // primer anillo del hemisferio inferior
	// Polo superior.
	if (!m.add_vertex(eng::math3d::Vec3t<S> {eng::math::scalar_traits<S>::from_int(0),
						 eng::math::scalar_traits<S>::from_int(hh + radius),
						 eng::math::scalar_traits<S>::from_int(0)})) {
		return false;
	}
	// Anillos superiores: i = 1..cap_stacks-1 (el 0 es el polo).
	for (u16 i = 1; i <= static_cast<u16>(cap_stacks - 1u); ++i) {
		if (i == 1u) {
			ring_top = m.nv;
		}
		// Ángulo polar del casquete superior (0 = polo): 1024 = cuarto de vuelta.
		const u16 polar = static_cast<u16>((static_cast<u32>(i) * 1024u) / cap_stacks);
		s16 cp {}, sp {};
		detail::polar_q12(polar, cp, sp);
		const int y = hh + detail::scale_q12(radius, cp);
		const int rad = detail::scale_q12(radius, sp);
		for (u16 k = 0; k < sectors; ++k) {
			s16 c {}, s {};
			detail::circle_q12(k, sectors, c, s);
			if (!m.add_vertex(eng::math3d::Vec3t<S> {
				    eng::math::scalar_traits<S>::from_int(detail::scale_q12(rad, c)),
				    eng::math::scalar_traits<S>::from_int(y),
				    eng::math::scalar_traits<S>::from_int(detail::scale_q12(rad, s))})) {
				return false;
			}
		}
	}
	// Anillo del cilindro en +hh y en -hh.
	ring_mid_top = m.nv;
	for (u16 k = 0; k < sectors; ++k) {
		s16 c {}, s {};
		detail::circle_q12(k, sectors, c, s);
		if (!m.add_vertex(eng::math3d::Vec3t<S> {
			    eng::math::scalar_traits<S>::from_int(detail::scale_q12(radius, c)),
			    eng::math::scalar_traits<S>::from_int(hh),
			    eng::math::scalar_traits<S>::from_int(detail::scale_q12(radius, s))})) {
			return false;
		}
	}
	ring_mid_bot = m.nv;
	for (u16 k = 0; k < sectors; ++k) {
		s16 c {}, s {};
		detail::circle_q12(k, sectors, c, s);
		if (!m.add_vertex(eng::math3d::Vec3t<S> {
			    eng::math::scalar_traits<S>::from_int(detail::scale_q12(radius, c)),
			    eng::math::scalar_traits<S>::from_int(-hh),
			    eng::math::scalar_traits<S>::from_int(detail::scale_q12(radius, s))})) {
			return false;
		}
	}
	// Hemisferio inferior (anillos de -hh hacia abajo) + polo inferior.
	ring_bot = m.nv;
	for (u16 i = 1; i <= static_cast<u16>(cap_stacks - 1u); ++i) {
		// Ángulo polar del casquete inferior (0 = polo inferior).
		const u16 polar = static_cast<u16>((static_cast<u32>(i) * 1024u) / cap_stacks);
		s16 cp {}, sp {};
		detail::polar_q12(polar, cp, sp);
		// Hacia abajo: el anillo 0 es el más próximo al polo (-hh-radio·cos) y el último
		// queda a la altura del ecuador (-hh), igual que el casquete superior.
		const int y = -hh - detail::scale_q12(radius, cp);
		const int rad = detail::scale_q12(radius, sp);
		for (u16 k = 0; k < sectors; ++k) {
			s16 c {}, s {};
			detail::circle_q12(k, sectors, c, s);
			if (!m.add_vertex(eng::math3d::Vec3t<S> {
				    eng::math::scalar_traits<S>::from_int(detail::scale_q12(rad, c)),
				    eng::math::scalar_traits<S>::from_int(y),
				    eng::math::scalar_traits<S>::from_int(detail::scale_q12(rad, s))})) {
				return false;
			}
		}
	}
	poles[1] = m.nv;
	if (!m.add_vertex(eng::math3d::Vec3t<S> {eng::math::scalar_traits<S>::from_int(0),
						 eng::math::scalar_traits<S>::from_int(-hh - radius),
						 eng::math::scalar_traits<S>::from_int(0)})) {
		return false;
	}
	// Caras: casquete superior (polo -> ring_top, CCW desde fuera), quads del cilindro,
	// casquete inferior (ring_bot -> polo).
	const auto rv = [&](u16 base, u16 r, u16 i) -> u16 {
		return static_cast<u16>(base + r * sectors + (i % sectors));
	};
	for (u16 i = 0; i < sectors; ++i) {
		u16 t[3] = {0u, rv(ring_top, 0u, static_cast<u16>(i + 1u)),
				  rv(ring_top, 0u, i)};
		if (!add_face(t, 3u)) {
			return false;
		}
	}
	const u16 top_rings = static_cast<u16>(cap_stacks - 2u);
	for (u16 r = 0; r < top_rings; ++r) {
		for (u16 i = 0; i < sectors; ++i) {
			u16 q[4] = {rv(ring_top, r, i), rv(ring_top, r, static_cast<u16>(i + 1u)),
					  rv(ring_top, static_cast<u16>(r + 1u),
					     static_cast<u16>(i + 1u)),
					  rv(ring_top, static_cast<u16>(r + 1u), i)};
			if (!add_face(q, 4u)) {
				return false;
			}
		}
	}
	// Empalme del anillo del ecuador del casquete (índice `top_rings`) con el borde
	// superior del cilindro.
	for (u16 i = 0; i < sectors; ++i) {
		u16 q[4] = {rv(ring_top, top_rings, i),
				  rv(ring_top, top_rings, static_cast<u16>(i + 1u)),
				  static_cast<u16>(ring_mid_top + ((i + 1u) % sectors)),
				  static_cast<u16>(ring_mid_top + i)};
		if (!add_face(q, 4u)) {
			return false;
		}
	}
	for (u16 i = 0; i < sectors; ++i) {
		u16 q[4] = {static_cast<u16>(ring_mid_top + i),
				  static_cast<u16>(ring_mid_top + ((i + 1u) % sectors)),
				  static_cast<u16>(ring_mid_bot + ((i + 1u) % sectors)),
				  static_cast<u16>(ring_mid_bot + i)};
		if (!add_face(q, 4u)) {
			return false;
		}
	}
	for (u16 i = 0; i < sectors; ++i) {
		u16 t[3] = {poles[1], rv(ring_bot, 0u, i), rv(ring_bot, 0u, static_cast<u16>(i + 1u))};
		if (!add_face(t, 3u)) {
			return false;
		}
	}
	for (u16 r = 0; r < top_rings; ++r) {
		for (u16 i = 0; i < sectors; ++i) {
			u16 q[4] = {rv(ring_bot, r, i), rv(ring_bot, r, static_cast<u16>(i + 1u)),
					  rv(ring_bot, static_cast<u16>(r + 1u),
					     static_cast<u16>(i + 1u)),
					  rv(ring_bot, static_cast<u16>(r + 1u), i)};
			if (!add_face(q, 4u)) {
				return false;
			}
		}
	}
	// Empalme del anillo del ecuador del casquete inferior (índice `top_rings`) con el
	// borde inferior del cilindro.
	for (u16 i = 0; i < sectors; ++i) {
		u16 q[4] = {rv(ring_bot, top_rings, i),
				  rv(ring_bot, top_rings, static_cast<u16>(i + 1u)),
				  static_cast<u16>(ring_mid_bot + ((i + 1u) % sectors)),
				  static_cast<u16>(ring_mid_bot + i)};
		if (!add_face(q, 4u)) {
			return false;
		}
	}
	return true;
}

/// **Sólido platónico** (`scale` = semiarista): tetraedro, cubo, octaedro, dodecaedro e
/// icosaedro, con caras n-gon convexas y CCW vistas desde fuera. Las tablas de vértices y
/// caras son las clásicas con φ aproximada por cocientes enteros (26/16 y 10/16), así que
/// en el icosaedro/dodecaedro la arista no es exactamente equidistante: por eso las caras
/// NO se derivan por comparación de distancias (imposible con enteros), sino de tablas
/// combinatorias; el dodecaedro se construye como **dual del icosaedro** (vértices =
/// centroides de las caras del icosaedro; caras = las 5 caras incidentes en cada vértice,
/// ordenadas por adyacencia de arista). El winding se corrige siempre con la normal de
/// Newell y el centro del sólido (origen), de modo que la orientación de las tablas no
/// puede dejar caras invertidas.
template <class Mesh, class S = typename Mesh::scalar>
[[nodiscard]] constexpr bool fill_platonic(Mesh& m, Platonic kind, s16 scale) {
	if (scale <= 0) {
		return false;
	}
	const int s = scale;
	const int g = (s * 26) / 16; // φ·s (26/16 ≈ 1.625; el icosaedro/dodecaedro quedan aproximados)
	struct P3i {
		int x, y, z;
	};
	if (kind == Platonic::Cube) {
		return fill_box(m, eng::math3d::vec3<S>(s, s, s));
	}
	// Icosaedro estándar (índice: (−1,t,0),(1,t,0),(−1,−t,0),(1,−t,0),(0,−1,t),(0,1,t),
	// (0,−1,−t),(0,1,−t),(t,0,−1),(t,0,1),(−t,0,−1),(−t,0,1)); tabla de 20 triángulos
	// usual para este orden.
	const P3i ico[12] = {
		{-s, g, 0}, {s, g, 0},   {-s, -g, 0}, {s, -g, 0},
		{0, -s, g}, {0, s, g},   {0, -s, -g}, {0, s, -g},
		{g, 0, -s}, {g, 0, s},   {-g, 0, -s}, {-g, 0, s},
	};
	constexpr u16 kIcoFaces[20][3] = {
		{0, 11, 5}, {0, 5, 1},  {0, 1, 7},  {0, 7, 10}, {0, 10, 11},
		{1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
		{3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
		{4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1},
	};
	// Normal del triángulo de los tres primeros vértices de la cara (misma convención que
	// el culling del engine); corrige el orden para que mire hacia fuera del origen (el
	// sólido está centrado: se contrasta con la suma de sus vértices, proporcional a su
	// centroide).
	const auto fix_winding = [](u16* ids, u16 n, const P3i* vv) {
		const int ux = vv[ids[1]].x - vv[ids[0]].x;
		const int uy = vv[ids[1]].y - vv[ids[0]].y;
		const int uz = vv[ids[1]].z - vv[ids[0]].z;
		const int wx = vv[ids[2]].x - vv[ids[0]].x;
		const int wy = vv[ids[2]].y - vv[ids[0]].y;
		const int wz = vv[ids[2]].z - vv[ids[0]].z;
		const int nx = uy * wz - uz * wy;
		const int ny = uz * wx - ux * wz;
		const int nz = ux * wy - uy * wx;
		int sx = 0;
		int sy = 0;
		int sz = 0;
		for (u16 i = 0; i < n; ++i) {
			sx += vv[ids[i]].x;
			sy += vv[ids[i]].y;
			sz += vv[ids[i]].z;
		}
		if (nx * sx + ny * sy + nz * sz < 0) {
			for (u16 i = 0; i < n / 2u; ++i) {
				const u16 t = ids[i];
				ids[i] = ids[n - 1u - i];
				ids[n - 1u - i] = t;
			}
		}
	};
	/// Añade un anillo corrigiendo el winding antes de insertarlo.
	const auto add_ring = [&](u16* ids, u16 n, const P3i* vv) -> bool {
		fix_winding(ids, n, vv);
		return m.add_face(ids, n);
	};
	if (kind == Platonic::Dodecahedron) {
		// Dual del icosaedro: vértice i del dodecaedro = suma (centroide exacto sin
		// dividir) de los 3 vértices de la cara i del icosaedro.
		P3i dv[20];
		for (u16 i = 0; i < 20u; ++i) {
			const P3i& a = ico[kIcoFaces[i][0]];
			const P3i& b = ico[kIcoFaces[i][1]];
			const P3i& c = ico[kIcoFaces[i][2]];
			dv[i] = {a.x + b.x + c.x, a.y + b.y + c.y, a.z + b.z + c.z};
		}
		for (u16 i = 0; i < 20u; ++i) {
			if (!m.add_vertex(eng::math3d::vec3<S>(dv[i].x, dv[i].y, dv[i].z))) {
				return false;
			}
		}
		const auto face_has = [&](u16 f, u16 k) {
			return kIcoFaces[f][0] == k || kIcoFaces[f][1] == k || kIcoFaces[f][2] == k;
		};
		// ¿Las caras f1 y f2 comparten una arista por el vértice k? (otro vértice común).
		const auto shares_other = [&](u16 k, u16 f1, u16 f2) {
			for (u16 i = 0; i < 3u; ++i) {
				const u16 a = kIcoFaces[f1][i];
				if (a == k) {
					continue;
				}
				for (u16 j = 0; j < 3u; ++j) {
					if (kIcoFaces[f2][j] == a) {
						return true;
					}
				}
			}
			return false;
		};
		for (u16 k = 0; k < 12u; ++k) { // cada vértice del icosaedro -> una cara pentagonal
			u16 inc[5];
			u16 n = 0;
			for (u16 f = 0; f < 20u; ++f) {
				if (face_has(f, k)) {
					if (n >= 5u) {
						return false;
					}
					inc[n++] = f;
				}
			}
			if (n != 5u) {
				return false;
			}
			u16 ring[5];
			bool used[5] = {true, false, false, false, false};
			ring[0] = inc[0];
			for (u16 step = 1u; step < 5u; ++step) {
				u16 nxt = 5u;
				for (u16 c = 1u; c < 5u; ++c) {
					if (!used[c] && shares_other(k, ring[step - 1u], inc[c])) {
						nxt = c;
						break;
					}
				}
				if (nxt == 5u) {
					return false;
				}
				used[nxt] = true;
				ring[step] = inc[nxt];
			}
			if (!add_ring(ring, 5u, dv)) {
				return false;
			}
		}
		return true;
	}
	// Tetraedro: 4 vértices, las 4 caras son todos los triples; octaedro: tabla clásica;
	// icosaedro: tabla de arriba.
	P3i v[12] {};
	u16 tri_list[20][3] = {};
	u16 tn = 0;
	if (kind == Platonic::Tetrahedron) {
		v[0] = {s, s, s};
		v[1] = {s, -s, -s};
		v[2] = {-s, s, -s};
		v[3] = {-s, -s, s};
		tn = 4;
		for (u16 i = 0; i < 4u; ++i) {
			u16 m2 = 0;
			for (u16 j = 0; j < 4u; ++j) {
				if (j != i) {
					tri_list[i][m2++] = j;
				}
			}
		}
	} else if (kind == Platonic::Octahedron) {
		v[0] = {s, 0, 0};
		v[1] = {-s, 0, 0};
		v[2] = {0, s, 0};
		v[3] = {0, -s, 0};
		v[4] = {0, 0, s};
		v[5] = {0, 0, -s};
		tn = 8;
		constexpr u16 kOctaFaces[8][3] = {
			{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4},
			{2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5},
		};
		for (u16 i = 0; i < 8u; ++i) {
			tri_list[i][0] = kOctaFaces[i][0];
			tri_list[i][1] = kOctaFaces[i][1];
			tri_list[i][2] = kOctaFaces[i][2];
		}
	} else if (kind == Platonic::Icosahedron) {
		for (u16 i = 0; i < 12u; ++i) {
			v[i] = ico[i];
		}
		tn = 20;
		for (u16 i = 0; i < 20u; ++i) {
			tri_list[i][0] = kIcoFaces[i][0];
			tri_list[i][1] = kIcoFaces[i][1];
			tri_list[i][2] = kIcoFaces[i][2];
		}
	} else {
		return false; // enum fuera de rango
	}
	const u16 vn = kind == Platonic::Tetrahedron ? 4u
		       : kind == Platonic::Octahedron ? 6u
						      : 12u;
	for (u16 i = 0; i < vn; ++i) {
		if (!m.add_vertex(eng::math3d::vec3<S>(v[i].x, v[i].y, v[i].z))) {
			return false;
		}
	}
	for (u16 i = 0; i < tn; ++i) {
		if (!add_ring(tri_list[i], 3u, v)) {
			return false;
		}
	}
	return true;
}

} // namespace eng::retro
