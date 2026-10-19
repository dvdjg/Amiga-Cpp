#pragma once

/// \file render3d.hpp
/// **Renderer 3D de dominio** (paquete 1 de `3D_GAME_ARCHITECTURE.md`): cámara con
/// convenio único, **clipping en espacio de cámara** (near + cuatro planos laterales)
/// ANTES de dividir por `z`, proyección acotada al viewport, culling por área y
/// relleno de caras convexas con `playfield::Surface::fill_polygon`. Sobre un playfield
/// Amiga con `PolygonFillSink` instalado (`eng::amiga::PolygonFillService`), cada cara
/// se rellena por **Blitter**; sin sink, el mismo camino cae al relleno CPU (host y tests).
///
/// ```text
///   malla (S) ─ model ─► cámara (S) ─► clip cámara (near+laterales) ─► proyectar ─►
///     culling por ÁREA de pantalla ─► Surface::fill_polygon (clip 2D HOST-479) ─► Blitter
/// ```
///
/// **Escalar `S`**: todo el pipeline 3D (cámara, vértices, planos) es genérico sobre el
/// escalar de coordenadas, como el resto del engine (`scalar_traits`/`mul_div`). En la
/// máquina se instancia con `eng::coord` (`Fixed<s16,0>`): las coordenadas de mundo son
/// enteras (píxeles a profundidad 1) y `focal` convierte mundo a píxeles. NUNCA se baja a
/// `s16` crudo dentro de la cabecera: la aritmética pasa por el escalar y por `mul_div`
/// (que preserva el producto ancho antes de dividir). Los únicos `s16` que quedan son las
/// **coordenadas de pantalla** (`sx`/`sy`), por contrato de `playfield::Surface`.
///
/// Contrato (robustez):
///   - Espacio de CÁMARA: `+x` derecha, `+y` abajo, `+z` delante, cámara en el origen.
///     Los vértices se saturan a ±16384 (en unidades del escalar) al recortar: con eso
///     las distancias a los planos caben en el producto ancho del escalar y la división
///     por `z` nunca ve `z <= 0`.
///   - `z >= near_z` y `|x|·focal <= z·half_w`, `|y|·focal <= z·half_h` tras el clip; la
///     proyección queda a ±1 px del viewport (el redondeo lo absorbe el clip 2D de
///     `Surface`, convención de bordes de HOST-479). Ningún vértice fuera del bitmap
///     llega al Blitter (los contornos fuera corrompen memoria: contrato del backend).
///   - Culling por **área firmada** del polígono ya proyectado (winding CCW = visible):
///     seguro en `s32` con coordenadas de pantalla, sin normales ni 64 bits, y respeta
///     el near clipping (el recorte preserva la orientación). Caras degeneradas (área 0)
///     no se emiten.
///   - Painter SIN z-buffer: interior de cada sólido convexo no se solapa; entre sólidos
///     el llamador los pinta de lejos a cerca (`solid_depth_key`). Objetos que se
///     INTERSECAN pueden ordenarse mal (limitación inherente, documentada).
///   - Caras convexas de hasta 8 vértices (tras 5 recortes: ≤ 13; buffers de 16).
///     Un n-gon mayor debe particionarse en el contenido (convexo).
///   - Sin heap: todos los buffers son del llamador.
///
/// Convenio de colores: `color_of(face_index)` devuelve el índice de paleta; la librería
/// no conoce paletas ni sombreado (la demo decide el sombreado plano por cara).

#include <eng/core/data/mesh3d.hpp>
#include <eng/core/math/arith.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/field/surface.hpp>

namespace eng::graphics {

/// Cámara 3D del renderer. `focal`, `near_z` y `half_w/half_h` viven en el escalar de
/// coordenadas `S` (en la máquina, `eng::coord` = enteros de mundo); `cx/cy` son el
/// centro del viewport en **píxeles** (contrato de `playfield::Surface`).
template <class S = eng::coord>
struct Camera3D {
	using scalar = S;
	S focal = eng::math::scalar_traits<S>::from_int(340);
	s16 cx = 160;
	s16 cy = 100;
	S near_z = eng::math::scalar_traits<S>::from_int(64);
	S half_w = eng::math::scalar_traits<S>::from_int(160);
	S half_h = eng::math::scalar_traits<S>::from_int(100);
};

/// Vértice en espacio de cámara, en el escalar de coordenadas `S`.
template <class S = eng::coord>
struct CamVertex {
	S x {};
	S y {};
	S z {};
};

namespace detail3d {

/// Distancia firmada a un plano `a·x + b·y + c·z + d` (≥ 0 = lado visible).
/// El producto es el **interno** del escalar (ancho, sin normalizar) y `d` se lleva a la
/// misma escala multiplicándolo por uno: así la comparación es exacta y no satura antes
/// de tiempo (el patrón de `mesh_traits::face_signed_area`).
template <class S>
struct Plane {
	S a {};
	S b {};
	S c {};
	S d {};

	[[nodiscard]] constexpr auto dist(const CamVertex<S>& v) const {
		const S one = eng::math::scalar_traits<S>::one();
		return eng::math::scalar_traits<S>::inner(a, v.x) +
		       eng::math::scalar_traits<S>::inner(b, v.y) +
		       eng::math::scalar_traits<S>::inner(c, v.z) +
		       eng::math::scalar_traits<S>::inner(d, one);
	}
};

/// Saturación de coordenadas de cámara al contrato del clip (±16384 unidades).
template <class S>
[[nodiscard]] constexpr S saturate_coord(S v) {
	const S lim = eng::math::scalar_traits<S>::from_int(16384);
	const S nlim = -lim;
	if (v > lim) return lim;
	if (v < nlim) return nlim;
	return v;
}

/// Fracción `num/den` en 12 bits (0..4095) con `0 <= num <= den`, por división
/// shift-subtract de 12 iteraciones: sin `__udivsi3` y sin desbordar `u32`.
[[nodiscard]] constexpr u16 ratio_q12(u32 num, u32 den) {
	if (den == 0u) return 0u;
	u32 rem = num;
	u16 q = 0u;
	for (u8 i = 0u; i < 12u; ++i) {
		rem <<= 1u;
		q = static_cast<u16>(q << 1u);
		if (rem >= den) {
			rem -= den;
			q = static_cast<u16>(q | 1u);
		}
	}
	return q;
}

/// Punto de corte de la arista `a`(dentro) → `b`(fuera) con el plano.
/// Con escalares de representación cruda (`Fixed`) se interpola en 12 bits sobre los
/// valores crudos (precisión sub-unidad, sin libcalls ni productos de 64 bits); con
/// escalares fraccionarios nativos (`float`) se usa el cociente directo.
template <class S, class W>
[[nodiscard]] constexpr CamVertex<S> cut_edge(const CamVertex<S>& a, const CamVertex<S>& b, W da,
					      W db) {
	if (da == db) return a;
	if constexpr (requires { eng::math::scalar_traits<W>::to_wide(da); } &&
		      requires { eng::math::scalar_traits<S>::to_wide(a.x); }) {
		using TW = decltype(eng::math::scalar_traits<W>::to_wide(da));
		using TS = decltype(eng::math::scalar_traits<S>::to_wide(a.x));
		const TW num = eng::math::scalar_traits<W>::to_wide(da);
		const TW den = eng::math::scalar_traits<W>::to_wide(da - db);
		const u16 q = ratio_q12(static_cast<u32>(num), static_cast<u32>(den));
		auto lerp = [q](S ca, S cb) -> S {
			const TS ra = eng::math::scalar_traits<S>::to_wide(ca);
			const TS rb = eng::math::scalar_traits<S>::to_wide(cb);
			const TS dro = rb - ra;
			const u32 ad = static_cast<u32>(dro < 0 ? -dro : dro);
			const u32 mag = eng::math::mulu16(static_cast<u16>(ad), q) >> 12u;
			const TS out = dro < 0 ? ra - static_cast<TS>(mag)
					       : ra + static_cast<TS>(mag);
			return eng::math::scalar_traits<S>::from_wide(out);
		};
		return CamVertex<S> {lerp(a.x, b.x), lerp(a.y, b.y), lerp(a.z, b.z)};
	} else {
		const W t = da / (da - db);
		const S s = static_cast<S>(t);
		return CamVertex<S> {a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s,
				     a.z + (b.z - a.z) * s};
	}
}

} // namespace detail3d

/// **Recorta un polígono en espacio de cámara** contra near y los cuatro planos laterales
/// del frustum. Devuelve el número de vértices de salida (0 si no queda nada).
/// `out` debe tener capacidad ≥ `n + 6` (con caras ≤ 8: 16 sobra).
template <class S>
[[nodiscard]] inline u8 clip_camera_polygon(const CamVertex<S>* in, u8 n, const Camera3D<S>& cam,
					    CamVertex<S>* out, u8 cap) noexcept {
	if (in == nullptr || out == nullptr || n < 3u || cap < 6u) return 0u;
	if (n > 16u) return 0u;
	using detail3d::Plane;
	using T = eng::math::scalar_traits<S>;
	const S zero = T::zero();
	const S one = T::one();
	const S planes_near = cam.near_z;
	const Plane<S> planes[5] = {
		Plane<S> {zero, zero, one, -planes_near}, // z >= near
		Plane<S> {cam.focal, zero, cam.half_w, zero},
		Plane<S> {-cam.focal, zero, cam.half_w, zero},
		Plane<S> {zero, cam.focal, cam.half_h, zero},
		Plane<S> {zero, -cam.focal, cam.half_h, zero},
	};
	CamVertex<S> a[16] {};
	CamVertex<S> b[16] {};
	for (u8 i = 0u; i < n; ++i) {
		a[i].x = detail3d::saturate_coord(in[i].x);
		a[i].y = detail3d::saturate_coord(in[i].y);
		a[i].z = detail3d::saturate_coord(in[i].z);
	}
	u8 count = n;
	CamVertex<S>* cur = a;
	CamVertex<S>* nxt = b;
	for (const Plane<S>& pl : planes) {
		if (count == 0u) break;
		u8 m = 0u;
		for (u8 i = 0u; i < count; ++i) {
			// Sin `%` por variable: en 68000 es `__modsi3` y este bucle es caliente.
			const u8 j = static_cast<u8>((i + 1u == count) ? 0u : (i + 1u));
			const CamVertex<S>& p = cur[i];
			const CamVertex<S>& q = cur[j];
			const auto dp = pl.dist(p);
			const auto dq = pl.dist(q);
			const auto zero_w = decltype(dp) {};
			const bool pin = dp >= zero_w;
			const bool qin = dq >= zero_w;
			if (pin) {
				if (m >= 16u) return 0u;
				nxt[m++] = p;
			}
			if (pin != qin) {
				if (m >= 16u) return 0u;
				nxt[m++] = detail3d::cut_edge<S>(p, q, dp, dq);
			}
		}
		if (m < 3u) return 0u;
		count = m;
		CamVertex<S>* tmp = cur;
		cur = nxt;
		nxt = tmp;
	}
	// Ajuste final: la interpolación truncada puede dejar el corte marginalmente fuera de
	// un plano. Se acota a la intersección EXACTA para que el contrato aguante en el
	// Blitter (|sx-cx| <= half_w sin depender del redondeo).
	for (u8 i = 0u; i < count; ++i) {
		CamVertex<S> v = cur[i];
		if (v.z < cam.near_z) v.z = cam.near_z;
		const S xmax = T::mul_div(v.z, cam.half_w, cam.focal);
		const S ymax = T::mul_div(v.z, cam.half_h, cam.focal);
		if (v.x > xmax) v.x = xmax;
		if (v.x < -xmax) v.x = -xmax;
		if (v.y > ymax) v.y = ymax;
		if (v.y < -ymax) v.y = -ymax;
		out[i] = v;
	}
	return count;
}

/// Proyecta un vértice ya recortado (`z >= near_z > 0`). Devuelve `false` si el contrato
/// no se cumple (`z <= 0`); el llamador no debe llegar aquí con ese caso.
/// `sx/sy` son píxeles (`s16`) por contrato de `playfield::Surface`.
template <class S>
[[nodiscard]] inline bool project_camera_vertex(const CamVertex<S>& v, const Camera3D<S>& cam, s16& sx,
						s16& sy) noexcept {
	using T = eng::math::scalar_traits<S>;
	if (!(v.z > T::zero())) return false;
	const S offx = T::mul_div(v.x, cam.focal, v.z);
	const S offy = T::mul_div(v.y, cam.focal, v.z);
	sx = static_cast<s16>(cam.cx + T::to_int(offx));
	sy = static_cast<s16>(cam.cy + T::to_int(offy));
	return true;
}

/// Área firmada ×2 del polígono de pantalla (positiva = CCW = cara visible con el winding
/// del engine). Coordenadas de pantalla (píxeles): productos ≤ 320·256, suma segura en `s32`.
[[nodiscard]] inline s32 screen_area2(const s16* xs, const s16* ys, u8 n) noexcept {
	s32 area = 0;
	for (u8 i = 0u; i < n; ++i) {
		// Sin `%` por variable (libcall __modsi3 en 68000); este bucle corre por cara.
		const u8 j = static_cast<u8>((i + 1u == n) ? 0u : (i + 1u));
		area += eng::math::mul_wide(xs[i], ys[j]) - eng::math::mul_wide(xs[j], ys[i]);
	}
	return area;
}

/// Clave de profundidad de un objeto ya transformado (suma de `z` de sus vértices; mayor =
/// más lejos). El llamador ordena los sólidos de MAYOR a MENOR antes de pintar (painter).
template <class S>
[[nodiscard]] inline s32 solid_depth_key(eng::Span<const math3d::Vec3t<S>> world) noexcept {
	s32 sum = 0;
	for (eng::usize i = 0; i < world.size(); ++i) {
		sum += static_cast<s32>(eng::math::scalar_traits<S>::to_int(world[i].v[2]));
	}
	return sum;
}

/// **Pinta un sólido n-gon** (caras convexas) sobre `surface`.
///
/// Buffers del llamador: `world` (capacidad = vértices), `cam_scratch`/`xs`/`ys`
/// (capacidad ≥ 16). `color_of(face_index)` da el índice de paleta (sombreado del juego).
/// `double_sided` pinta también las caras con área negativa (mallas abiertas).
/// Devuelve el número de caras emitidas.
template <class S, class Model, class ColorFn>
inline u32 render_solid(const math3d::PolyMeshViewT<S>& mesh, const Model& model,
			const Camera3D<S>& cam, playfield::Surface& target, ColorFn&& color_of,
			math3d::Vec3t<S>* world, CamVertex<S>* cam_scratch, s16* xs, s16* ys,
			bool double_sided = false) noexcept {
	const u32 nv = mesh.vertex_count();
	const u32 nf = mesh.face_count();
	if (world == nullptr || cam_scratch == nullptr || xs == nullptr || ys == nullptr) return 0u;
	if (nv == 0u || nf == 0u) return 0u;
	math3d::mesh_transform(mesh.vertices, model,
			       eng::Span<math3d::Vec3t<S>>(world, static_cast<eng::usize>(nv)));
	u32 drawn = 0;
	for (u32 fi = 0u; fi < nf; ++fi) {
		const eng::Span<const u16> idx = mesh.face_indices(fi);
		const u8 n = static_cast<u8>(idx.size());
		if (n < 3u || n > 8u) continue; // caras convexas ≤ 8 (contrato)
		bool valid = true;
		for (u8 k = 0u; k < n; ++k) {
			if (idx[k] >= nv) {
				valid = false;
				break;
			}
			const math3d::Vec3t<S>& v = world[idx[k]];
			cam_scratch[k].x = detail3d::saturate_coord(v.v[0]);
			cam_scratch[k].y = detail3d::saturate_coord(v.v[1]);
			cam_scratch[k].z = detail3d::saturate_coord(v.v[2]);
		}
		if (!valid) continue;
		CamVertex<S> clipped[16] {};
		const u8 m = clip_camera_polygon<S>(cam_scratch, n, cam, clipped, 16u);
		if (m < 3u) continue;
		bool ok = true;
		for (u8 k = 0u; k < m; ++k) {
			if (!project_camera_vertex<S>(clipped[k], cam, xs[k], ys[k])) {
				ok = false;
				break;
			}
		}
		if (!ok) continue;
		const s32 area = screen_area2(xs, ys, m);
		if (area == 0) continue; // degenerada (canto de perfil)
		if (!double_sided && area < 0) continue; // back-face
		if (target.fill_polygon(xs, ys, m, static_cast<u8>(color_of(fi)))) {
			++drawn;
		}
	}
	return drawn;
}

/// **Pinta una malla de triángulos** (`MeshViewT`) por el mismo pipeline.
template <class S, class Model, class ColorFn>
inline u32 render_mesh(const math3d::MeshViewT<S>& mesh, const Model& model, const Camera3D<S>& cam,
		       playfield::Surface& target, ColorFn&& color_of, math3d::Vec3t<S>* world,
		       CamVertex<S>* cam_scratch, s16* xs, s16* ys, bool double_sided = false) noexcept {
	const u32 nv = mesh.vertex_count();
	const u32 nf = mesh.face_count();
	if (world == nullptr || cam_scratch == nullptr || xs == nullptr || ys == nullptr) return 0u;
	if (nv == 0u || nf == 0u) return 0u;
	math3d::mesh_transform(mesh.vertices, model,
			       eng::Span<math3d::Vec3t<S>>(world, static_cast<eng::usize>(nv)));
	u32 drawn = 0;
	for (u32 fi = 0u; fi < nf; ++fi) {
		const math3d::Face& f = mesh.faces[fi];
		const u16 idx[3] = {f.a, f.b, f.c};
		bool valid = true;
		for (u8 k = 0u; k < 3u; ++k) {
			if (idx[k] >= nv) {
				valid = false;
				break;
			}
			const math3d::Vec3t<S>& v = world[idx[k]];
			cam_scratch[k].x = detail3d::saturate_coord(v.v[0]);
			cam_scratch[k].y = detail3d::saturate_coord(v.v[1]);
			cam_scratch[k].z = detail3d::saturate_coord(v.v[2]);
		}
		if (!valid) continue;
		CamVertex<S> clipped[16] {};
		const u8 m = clip_camera_polygon<S>(cam_scratch, 3u, cam, clipped, 16u);
		if (m < 3u) continue;
		bool ok = true;
		for (u8 k = 0u; k < m; ++k) {
			if (!project_camera_vertex<S>(clipped[k], cam, xs[k], ys[k])) {
				ok = false;
				break;
			}
		}
		if (!ok) continue;
		const s32 area = screen_area2(xs, ys, m);
		if (area == 0) continue;
		if (!double_sided && area < 0) continue;
		if (target.fill_polygon(xs, ys, m, static_cast<u8>(color_of(fi)))) {
			++drawn;
		}
	}
	return drawn;
}

} // namespace eng::graphics
