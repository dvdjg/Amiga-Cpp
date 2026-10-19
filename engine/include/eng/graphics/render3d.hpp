#pragma once

/// \file render3d.hpp
/// **Renderer 3D de dominio** (paquete 1 de `3D_GAME_ARCHITECTURE.md`): cámara con
/// convenio único, **clipping en espacio de cámara** (near + cuatro planos laterales)
/// ANTES de dividir por `z`, proyección acotada al viewport, culling por área y
/// relleno de caras convexas con `playfield::Surface::fill_polygon`. Sobre un playfield
/// Amiga con `PolygonFillSink` instalado (`eng::amiga::PolygonFillService`), cada cara
/// se rellena por **Blitter** (máscara 1 bpp + contorno ONEDOT + area fill inclusivo +
/// cookie-cut); sin sink, el mismo camino cae al relleno CPU (host y tests).
///
/// ```text
///   malla (4.12) ─ model ─► cámara ─► clip cámara (near+laterales) ─► proyectar ─►
///     culling por ÁREA de pantalla ─► Surface::fill_polygon (clip 2D HOST-479) ─► Blitter
/// ```
///
/// Contrato (robustez):
///   - Espacio de CÁMARA: `+x` derecha, `+y` abajo, `+z` delante, cámara en el origen.
///     Los componentes en crudo de la malla (4.12) se saturan a ±16384 al recortar: con
///     eso las distancias a los planos caben en `s32` sin desbordar (productos 16×16 con
///     `mul_wide`) y la división por `z` nunca ve `z <= 0`.
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

/// Cámara 3D del renderer. `focal` en 4.12 (4096 = 1.0). `cx/cy` = centro del viewport en
/// píxeles; `half_w/half_h` = semiancho/semialto visibles (px). `near_z` en unidades de
/// cámara (crudo 4.12 del vértice, > 0).
struct Camera3D {
	s16 focal = 4096;
	s16 cx = 160;
	s16 cy = 100;
	s16 near_z = 64;
	s16 half_w = 160;
	s16 half_h = 100;
};

/// Vértice en espacio de cámara (crudo, 4.12 como la malla).
struct CamVertex {
	s16 x = 0;
	s16 y = 0;
	s16 z = 0;
};

namespace detail3d {

/// Límite de saturación de las coordenadas de cámara (contrato del clip): mantiene las
/// distancias a los planos dentro de `s32` con productos 16×16.
inline constexpr s32 kClipLimit = 16384;

[[nodiscard]] inline s16 saturate_coord(s32 v) noexcept {
	if (v > detail3d::kClipLimit) return static_cast<s16>(detail3d::kClipLimit);
	if (v < -detail3d::kClipLimit) return static_cast<s16>(-detail3d::kClipLimit);
	return static_cast<s16>(v);
}

/// Distancia firmada a un plano `a·x + b·y + c·z + d` (≥ 0 = lado visible).
struct Plane {
	s32 a = 0;
	s32 b = 0;
	s32 c = 0;
	s32 d = 0;

	[[nodiscard]] s32 dist(const CamVertex& v) const noexcept {
		return a * static_cast<s32>(v.x) + b * static_cast<s32>(v.y) +
		       c * static_cast<s32>(v.z) + d;
	}
};

/// Fracción `num/den` en 12 bits (0..4095) con `0 <= num <= den`, por división
/// shift-subtract de 12 iteraciones: sin `__udivsi3` y sin desbordar `u32`
/// (`rem < 2·den` con `den < 2^28`).
[[nodiscard]] inline u16 ratio_q12(u32 num, u32 den) noexcept {
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

/// Punto de corte de la arista `a`(dentro) → `b`(fuera) con el plano, interpolado con
/// `ratio_q12` (redondeo al píxel del lado visible). Sin `__mulsi3`: el producto
/// `|Δ|·q` va por `mulu16` (|Δ| ≤ 32768 cabe en `u16`, `q < 4096`).
[[nodiscard]] inline CamVertex cut_edge(const CamVertex& a, const CamVertex& b, s32 da,
				       s32 db) noexcept {
	if (da == db) return a;
	const u16 q = ratio_q12(static_cast<u32>(da), static_cast<u32>(da - db));
	auto lerp = [q](s16 ca, s16 cb) -> s16 {
		const s32 d = static_cast<s32>(cb) - static_cast<s32>(ca);
		const u32 ad = static_cast<u32>(d < 0 ? -d : d);
		const u32 mag = eng::math::mulu16(static_cast<u16>(ad), q) >> 12u;
		return static_cast<s16>(d < 0 ? static_cast<s32>(ca) - static_cast<s32>(mag)
					      : static_cast<s32>(ca) + static_cast<s32>(mag));
	};
	CamVertex out {};
	out.x = lerp(a.x, b.x);
	out.y = lerp(a.y, b.y);
	out.z = lerp(a.z, b.z);
	return out;
}

} // namespace detail3d

/// **Recorta un polígono en espacio de cámara** contra near y los cuatro planos laterales
/// del frustum. Devuelve el número de vértices de salida (0 si no queda nada).
/// `out` debe tener capacidad ≥ `n + 6` (con caras ≤ 8: 16 sobra).
[[nodiscard]] inline u8 clip_camera_polygon(const CamVertex* in, u8 n, const Camera3D& cam,
					    CamVertex* out, u8 cap) noexcept {
	if (in == nullptr || out == nullptr || n < 3u || cap < 6u) return 0u;
	if (n > 16u) return 0u;
	using detail3d::Plane;
	const s32 f = cam.focal;
	const s32 hw = cam.half_w;
	const s32 hh = cam.half_h;
	const Plane planes[5] = {
		Plane {0, 0, 1, -static_cast<s32>(cam.near_z)}, // z >= near
		Plane {f, 0, hw, 0},                            // |x|·focal <= z·half_w
		Plane {-f, 0, hw, 0},
		Plane {0, f, hh, 0}, // |y|·focal <= z·half_h
		Plane {0, -f, hh, 0},
	};
	CamVertex a[16] {};
	CamVertex b[16] {};
	for (u8 i = 0u; i < n; ++i) {
		a[i].x = detail3d::saturate_coord(in[i].x);
		a[i].y = detail3d::saturate_coord(in[i].y);
		a[i].z = detail3d::saturate_coord(in[i].z);
	}
	// Sin z válido no hay polígono proyectable.
	u8 count = n;
	CamVertex* cur = a;
	CamVertex* nxt = b;
	for (const Plane& pl : planes) {
		if (count == 0u) break;
		u8 m = 0u;
		for (u8 i = 0u; i < count; ++i) {
			const u8 j = static_cast<u8>((i + 1u) % count);
			const CamVertex& p = cur[i];
			const CamVertex& q = cur[j];
			const s32 dp = pl.dist(p);
			const s32 dq = pl.dist(q);
			const bool pin = dp >= 0;
			const bool qin = dq >= 0;
			if (pin) {
				if (m >= 16u) return 0u;
				nxt[m++] = p;
			}
			if (pin != qin) {
				if (m >= 16u) return 0u;
				nxt[m++] = detail3d::cut_edge(p, q, dp, dq);
			}
		}
		if (m < 3u) return 0u;
		count = m;
		CamVertex* tmp = cur;
		cur = nxt;
		nxt = tmp;
	}
	// Ajuste entero final: la interpolación truncada puede dejar el corte marginalmente
	// fuera de un plano (unos pocos crudos). Se acota a la intersección EXACTA para que el
	// contrato aguante en el Blitter (|sx-cx| <= half_w sin depender del redondeo).
	for (u8 i = 0u; i < count; ++i) {
		CamVertex v = cur[i];
		if (v.z < cam.near_z) v.z = cam.near_z;
		const s16 xmax =
			eng::math::div_wide(eng::math::mul_wide(v.z, cam.half_w), cam.focal);
		const s16 ymax =
			eng::math::div_wide(eng::math::mul_wide(v.z, cam.half_h), cam.focal);
		if (v.x > xmax) v.x = xmax;
		if (v.x < -xmax) v.x = static_cast<s16>(-xmax);
		if (v.y > ymax) v.y = ymax;
		if (v.y < -ymax) v.y = static_cast<s16>(-ymax);
		out[i] = v;
	}
	return count;
}

/// Proyecta un vértice ya recortado (`z >= near_z > 0`). Devuelve `false` si el contrato
/// no se cumple (`z <= 0`); el llamador no debe llegar aquí con ese caso.
[[nodiscard]] inline bool project_camera_vertex(const CamVertex& v, const Camera3D& cam, s16& sx,
						s16& sy) noexcept {
	if (v.z <= 0) return false;
	const s16 z = v.z;
	sx = static_cast<s16>(cam.cx + eng::math::div_wide(
					       eng::math::mul_wide(v.x, cam.focal), z));
	sy = static_cast<s16>(cam.cy + eng::math::div_wide(
					       eng::math::mul_wide(v.y, cam.focal), z));
	return true;
}

/// Área firmada ×2 del polígono de pantalla (positiva = CCW = cara visible con el winding
/// del engine). Coordenadas de pantalla: productos ≤ 320·256, suma segura en `s32`.
[[nodiscard]] inline s32 screen_area2(const s16* xs, const s16* ys, u8 n) noexcept {
	s32 area = 0;
	for (u8 i = 0u; i < n; ++i) {
		const u8 j = static_cast<u8>((i + 1u) % n);
		area += static_cast<s32>(xs[i]) * static_cast<s32>(ys[j]) -
			static_cast<s32>(xs[j]) * static_cast<s32>(ys[i]);
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
			const Camera3D& cam, playfield::Surface& target, ColorFn&& color_of,
			math3d::Vec3t<S>* world, CamVertex* cam_scratch, s16* xs, s16* ys,
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
			cam_scratch[k].x = detail3d::saturate_coord(
				static_cast<s32>(eng::math::scalar_traits<S>::to_int(v.v[0])));
			cam_scratch[k].y = detail3d::saturate_coord(
				static_cast<s32>(eng::math::scalar_traits<S>::to_int(v.v[1])));
			cam_scratch[k].z = detail3d::saturate_coord(
				static_cast<s32>(eng::math::scalar_traits<S>::to_int(v.v[2])));
		}
		if (!valid) continue;
		CamVertex clipped[16] {};
		const u8 m = clip_camera_polygon(cam_scratch, n, cam, clipped, 16u);
		if (m < 3u) continue;
		bool ok = true;
		for (u8 k = 0u; k < m; ++k) {
			if (!project_camera_vertex(clipped[k], cam, xs[k], ys[k])) {
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
inline u32 render_mesh(const math3d::MeshViewT<S>& mesh, const Model& model, const Camera3D& cam,
		       playfield::Surface& target, ColorFn&& color_of, math3d::Vec3t<S>* world,
		       CamVertex* cam_scratch, s16* xs, s16* ys, bool double_sided = false) noexcept {
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
			cam_scratch[k].x = detail3d::saturate_coord(
				static_cast<s32>(eng::math::scalar_traits<S>::to_int(v.v[0])));
			cam_scratch[k].y = detail3d::saturate_coord(
				static_cast<s32>(eng::math::scalar_traits<S>::to_int(v.v[1])));
			cam_scratch[k].z = detail3d::saturate_coord(
				static_cast<s32>(eng::math::scalar_traits<S>::to_int(v.v[2])));
		}
		if (!valid) continue;
		CamVertex clipped[16] {};
		const u8 m = clip_camera_polygon(cam_scratch, 3u, cam, clipped, 16u);
		if (m < 3u) continue;
		bool ok = true;
		for (u8 k = 0u; k < m; ++k) {
			if (!project_camera_vertex(clipped[k], cam, xs[k], ys[k])) {
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
