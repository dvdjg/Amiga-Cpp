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
#include <eng/core/util/type_traits.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/face_style.hpp>

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

/// **Vista mundo→cámara** para `render3d`: posición del mundo que ocupa la cámara
/// (origen del espacio de cámara) y rotación **ortogonal** que lleva del mundo a
/// cámara. La rotación se aporta ya construida (Q4.12) para no meter trigonometría
/// en el renderer; el juego la compone con sus ángulos o con `math3d::load_rotate`.
///
/// \tparam S escalar de las posiciones (en la máquina `eng::coord` = q0).
template <class S = eng::coord>
struct CameraView {
	math3d::Vec3t<S> position {}; ///< posición de la cámara en el mundo.
	eng::math::Mat<3, eng::real> rotation {}; ///< rotación mundo→cámara (Q4.12).
};

/// Lleva un punto de mundo a espacio de cámara: `R · (world - position)`.
/// \tparam S escalar de la posición (mismo que el `CameraView`).
/// \param view posición y rotación de la cámara.
/// \param world punto en coordenadas de mundo.
/// \return punto en espacio de cámara, listo para `render_solid`/`project_camera_vertex`.
/// \pre `view.rotation` es una rotación ortogonal (el llamador la construye así).
template <class S>
[[nodiscard]] inline math3d::Vec3t<S> world_to_camera(const CameraView<S>& view,
						      const math3d::Vec3t<S>& world) noexcept {
	const math3d::Vec3t<S> d {
		{world.v[0] - view.position.v[0], world.v[1] - view.position.v[1],
		 world.v[2] - view.position.v[2]}};
	math3d::Vec3t<S> out {};
	for (int i = 0; i < 3; ++i) {
		auto acc = view.rotation.m[i][0] * d.v[0];
		acc = acc + view.rotation.m[i][1] * d.v[1];
		acc = acc + view.rotation.m[i][2] * d.v[2];
		out.v[i] = eng::math::scalar_traits<S>::norm_from(acc);
	}
	return out;
}

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

	/// Distancia firmada al plano (producto interno sin normalizar).
	/// \param v vértice de cámara.
	/// \return valor proporcional a la distancia; positivo = delante del plano.
	[[nodiscard]] constexpr auto dist(const CamVertex<S>& v) const {
		// `d` se suma directamente: `inner(d, one) == d` en la escala cruda del escalar
		// (para `Fixed` es `(d.v * one.v) >> E == d.v`; para `float`, `d * 1`), asi que
		// multiplicarlo por uno solo anadia un `muls.w` por plano y vertice (medido con
		// L1-004: es ~25 % del test de frustum).
		return eng::math::scalar_traits<S>::inner(a, v.x) +
		       eng::math::scalar_traits<S>::inner(b, v.y) +
		       eng::math::scalar_traits<S>::inner(c, v.z) + d;
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

/// Los cinco planos del frustum (near + laterales) en el orden que usan el clip y el
/// test de contención. Una sola definición para que ambos no se desincronicen.
template <class S>
struct CameraPlanes {
	Plane<S> planes[5] {};
};

/// Construye los cinco planos del frustum a partir de la cámara.
/// \param cam cámara con focal y semianchos.
/// \return near + cuatro planos laterales en el orden del clip/test.
template <class S>
[[nodiscard]] constexpr CameraPlanes<S> camera_planes(const Camera3D<S>& cam) {
	const S zero = eng::math::scalar_traits<S>::zero();
	const S one = eng::math::scalar_traits<S>::one();
	return CameraPlanes<S> {{
		Plane<S> {zero, zero, one, -cam.near_z}, // z >= near
		Plane<S> {cam.focal, zero, cam.half_w, zero},
		Plane<S> {-cam.focal, zero, cam.half_w, zero},
		Plane<S> {zero, cam.focal, cam.half_h, zero},
		Plane<S> {zero, -cam.focal, cam.half_h, zero},
	}};
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

/// Punto de corte de una arista cruzada por el plano. Canoniza el sentido como
/// `a`(dentro) → `b`(fuera), porque el winding de dos caras que comparten una arista es
/// opuesto y la interpolación fija de 12 bits debe producir el mismo punto en ambos sentidos.
/// Con escalares de representación cruda (`Fixed`) se interpola en 12 bits sobre los
/// valores crudos (precisión sub-unidad, sin libcalls ni productos de 64 bits); con
/// escalares fraccionarios nativos (`float`) se usa el cociente directo.
template <class S, class W>
[[nodiscard]] constexpr CamVertex<S> cut_edge(const CamVertex<S>& a, const CamVertex<S>& b, W da,
						      W db) {
	const W zero {};
	const CamVertex<S>* first = &a;
	const CamVertex<S>* second = &b;
	if (da < zero && db >= zero) {
		first = &b;
		second = &a;
		const W distance = da;
		da = db;
		db = distance;
	}
	if (da == db) return *first;
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
		return CamVertex<S> {lerp(first->x, second->x), lerp(first->y, second->y),
				     lerp(first->z, second->z)};
	} else if constexpr (eng::util::is_integral_v<S>) {
		// Host/int: la interpolación en 12 bits sobre s64 evita el truncado de la
		// división entera (`t = 0` para |da| < |da-db|). En 68000 `S` es `Fixed` y
		// toma la rama `to_wide`; este camino no se instancia en target.
		const s64 num = static_cast<s64>(da);
		const s64 den = static_cast<s64>(da) - static_cast<s64>(db);
		if (den == 0) return *first;
		const s64 q = (num << 12) / den;
		auto lerp = [q](S ca, S cb) -> S {
			const s64 d = static_cast<s64>(cb) - static_cast<s64>(ca);
			return static_cast<S>(static_cast<s64>(ca) + ((d * q) >> 12));
		};
		return CamVertex<S> {lerp(first->x, second->x), lerp(first->y, second->y),
				     lerp(first->z, second->z)};
	} else {
		const W t = da / (da - db);
		const S s = static_cast<S>(t);
		return CamVertex<S> {first->x + (second->x - first->x) * s,
				     first->y + (second->y - first->y) * s,
				     first->z + (second->z - first->z) * s};
	}
}

} // namespace detail3d

/// **Recorta un polígono en espacio de cámara** contra near y los cuatro planos laterales
/// del frustum. Devuelve el número de vértices de salida (0 si no queda nada).
/// `cap` es la capacidad real de `out`; devuelve 0 sin escribir una salida parcial si el
/// polígono recortado no cabe. Las caras convexas de hasta 8 vértices caben en 16.
template <class S>
[[nodiscard]] inline u8 clip_camera_polygon(const CamVertex<S>* in, u8 n, const Camera3D<S>& cam,
							    CamVertex<S>* out, u8 cap) noexcept {
	if (in == nullptr || out == nullptr || n < 3u || cap < 3u) return 0u;
	if (n > 16u) return 0u;
	using detail3d::Plane;
	using T = eng::math::scalar_traits<S>;
	const detail3d::CameraPlanes<S> cam_planes = detail3d::camera_planes(cam);
	const Plane<S>* planes = cam_planes.planes;
	// Solo se leen los `n/count` elementos escritos: `a[0..n)` se copia del input y cada
	// pasada completa `nxt[0..m)` antes del swap. Evitar el zero-init elimina un memset por cara.
	CamVertex<S> a[16];
	CamVertex<S> b[16];
	for (u8 i = 0u; i < n; ++i) {
		a[i].x = detail3d::saturate_coord(in[i].x);
		a[i].y = detail3d::saturate_coord(in[i].y);
		a[i].z = detail3d::saturate_coord(in[i].z);
	}
	u8 count = n;
	CamVertex<S>* cur = a;
	CamVertex<S>* nxt = b;
	for (u8 pi = 0u; pi < 5u; ++pi) {
		const Plane<S>& pl = planes[pi];
		if (count == 0u) break;
		using Dist = decltype(pl.dist(cur[0]));
		Dist dist[16];
		bool inside[16];
		bool all_inside = true;
		bool all_outside = true;
		// Una distancia por vértice/plano: las caras enteras dentro no se copian y las
		// enteras fuera se descartan sin recorrer aristas ni volver a calcular `dist`.
		for (u8 i = 0u; i < count; ++i) {
			dist[i] = pl.dist(cur[i]);
			const Dist zero {};
			inside[i] = dist[i] >= zero;
			all_inside = all_inside && inside[i];
			all_outside = all_outside && !inside[i];
		}
		if (all_inside) continue;
		if (all_outside) return 0u;
		u8 m = 0u;
		for (u8 i = 0u; i < count; ++i) {
			// Sin `%` por variable: en 68000 es `__modsi3` y este bucle es caliente.
			const u8 j = static_cast<u8>((i + 1u == count) ? 0u : (i + 1u));
			const CamVertex<S>& p = cur[i];
			const CamVertex<S>& q = cur[j];
			const bool pin = inside[i];
			const bool qin = inside[j];
			if (pin) {
				if (m >= 16u) return 0u;
				nxt[m++] = p;
			}
			if (pin != qin) {
				if (m >= 16u) return 0u;
				nxt[m++] = detail3d::cut_edge<S>(p, q, dist[i], dist[j]);
			}
		}
		if (m < 3u) return 0u;
		count = m;
		CamVertex<S>* tmp = cur;
		cur = nxt;
		nxt = tmp;
	}
	// `cap` pertenece al buffer del llamador, no al scratch interno: rechazar antes de
	// copiar evita sobreescribir memoria cuando una cara no cabe en la capacidad declarada.
	if (count > cap) return 0u;
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

/// **Fast path de frustum**: `true` si TODOS los vértices están ESTRICTAMENTE dentro del
/// frustum (near + laterales). Un vértice sobre un plano devuelve `false` (el clip resuelve
/// el caso límite). Con `true` el llamador puede proyectar sin recortar: ahorra 5 planos ×
/// vértice por cara y las copias de los buffers de recorte.
template <class S>
[[nodiscard]] inline bool camera_contains_vertices(const Camera3D<S>& cam, const CamVertex<S>* verts,
							   u32 n) noexcept {
	if (verts == nullptr || n == 0u) return false;
	const detail3d::CameraPlanes<S> cp = detail3d::camera_planes(cam);
	for (u32 i = 0u; i < n; ++i) {
		for (u8 pi = 0u; pi < 5u; ++pi) {
			const auto d = cp.planes[pi].dist(verts[i]);
			const auto zero = decltype(d) {};
			if (!(d > zero)) return false;
		}
	}
	return true;
}

/// Igual, para vértices de malla (`Vec3t<S>`, ya en espacio de cámara).
template <class S>
[[nodiscard]] inline bool camera_contains_vertices(const Camera3D<S>& cam,
							   const math3d::Vec3t<S>* verts, u32 n) noexcept {
	if (verts == nullptr || n == 0u) return false;
	const detail3d::CameraPlanes<S> cp = detail3d::camera_planes(cam);
	for (u32 i = 0u; i < n; ++i) {
		const CamVertex<S> v {verts[i].v[0], verts[i].v[1], verts[i].v[2]};
		for (u8 pi = 0u; pi < 5u; ++pi) {
			const auto d = cp.planes[pi].dist(v);
			const auto zero = decltype(d) {};
			if (!(d > zero)) return false;
		}
	}
	return true;
}

/// `true` si todos los vértices están en o delante del plano near. Permite conservar la
/// proyección directa seguida del clip 2D cuando solo pueden cruzarse los laterales; si un
/// vértice cruza near, hay que recortar en cámara antes de dividir por z.
template <class S>
[[nodiscard]] inline bool camera_vertices_at_or_beyond_near(const Camera3D<S>& cam,
								    const math3d::Vec3t<S>* verts,
								    u32 n) noexcept {
	if (verts == nullptr || n == 0u) return false;
	for (u32 i = 0u; i < n; ++i) {
		if (verts[i].v[2] < cam.near_z) return false;
	}
	return true;
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

/// Area firmada x2 del poligono de pantalla (y hacia abajo): negativa = CCW desde fuera = visible;
/// positiva = trasera. Coordenadas de pantalla (pixeles): suma segura en `s32`.
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

/// **Pinta un sólido n-gon con estilo de cara** (caras convexas) sobre `surface`.
/// Es la ruta que usa la escena G1/G3: `style_of(face_index)` devuelve
/// `FaceStyle` (color + patrón + flags) y `Surface::fill_polygon` aplica el patrón.
///
/// Buffers del llamador: `world` (capacidad = vértices), `cam_scratch`/`xs`/`ys`
/// (capacidad ≥ 16). `double_sided` pinta también las caras con área negativa
/// (mallas abiertas). Devuelve el número de caras emitidas.
/// \tparam StyleFn callable `FaceStyle(u32 face_index)`.
template <class S, class Model, class StyleFn>
inline u32 render_solid_styled(const math3d::PolyMeshViewT<S>& mesh, const Model& model,
			       const Camera3D<S>& cam, playfield::Surface& target, StyleFn&& style_of,
			       math3d::Vec3t<S>* world, CamVertex<S>* cam_scratch, s16* xs, s16* ys,
			       bool double_sided = false) noexcept {
	const u32 nv = mesh.vertex_count();
	const u32 nf = mesh.face_count();
	if (world == nullptr || cam_scratch == nullptr || xs == nullptr || ys == nullptr) return 0u;
	if (nv == 0u || nf == 0u) return 0u;
	math3d::mesh_transform(mesh.vertices, model,
			       eng::Span<math3d::Vec3t<S>>(world, nv));
	// Fast path: si el objeto entero está dentro del frustum, ninguna cara necesita clip.
	const bool all_inside = camera_contains_vertices<S>(cam, world, nv);
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
		u8 m = n;
		if (!all_inside) {
			CamVertex<S> clipped[16] {};
			m = clip_camera_polygon<S>(cam_scratch, n, cam, clipped, 16u);
			if (m < 3u) continue;
			for (u8 k = 0u; k < m; ++k) cam_scratch[k] = clipped[k];
		}
		bool ok = true;
		for (u8 k = 0u; k < m; ++k) {
			if (!project_camera_vertex<S>(cam_scratch[k], cam, xs[k], ys[k])) {
				ok = false;
				break;
			}
		}
		if (!ok) continue;
		const s32 area = screen_area2(xs, ys, m);
		if (area == 0) continue; // degenerada (canto de perfil)
		if (!double_sided && area > 0) continue; // back-face (area positiva = CW en pantalla)
		if (target.fill_polygon(xs, ys, m, style_of(fi))) {
			++drawn;
		}
	}
	return drawn;
}

/// **Silueta de un solido convexo** (ruta P8): proyecta todos los vertices, calcula
/// la envolvente convexa 2D y rellena UNA sola vez con `color`. Es la ruta barata
/// para un solido opaco de material plano: 1 fill por objeto en vez de 1 por cara.
/// Requiere que todos los vertices esten delante del near (si no, devuelve 0 y el
/// llamador debe caer a `render_solid_styled`). No usa painter entre caras: el
/// objeto se pinta completo con `color` (silueta opaca).
/// \param hull buffer de indices de capacidad >= nv (maximo 16).
/// \return 1 si pinto la silueta; 0 si no se pudo (near/capacidad/vacio).
template <class S, class Model>
inline u32 render_convex_silhouette(const math3d::PolyMeshViewT<S>& mesh, const Model& model,
				    const Camera3D<S>& cam, playfield::Surface& target, u8 color,
				    math3d::Vec3t<S>* world, CamVertex<S>* cam_scratch, s16* xs,
				    s16* ys, u16* hull, u16 hull_capacity) noexcept {
	const u32 nv = mesh.vertex_count();
	if (world == nullptr || cam_scratch == nullptr || xs == nullptr || ys == nullptr ||
	    hull == nullptr || nv < 3u || nv > 16u || hull_capacity < nv) {
		return 0u;
	}
	math3d::mesh_transform(mesh.vertices, model,
			       eng::Span<math3d::Vec3t<S>>(world, nv));
	for (u32 i = 0u; i < nv; ++i) {
		cam_scratch[i].x = detail3d::saturate_coord(world[i].v[0]);
		cam_scratch[i].y = detail3d::saturate_coord(world[i].v[1]);
		cam_scratch[i].z = detail3d::saturate_coord(world[i].v[2]);
		if (!(cam_scratch[i].z > cam.near_z)) {
			return 0u;
		}
		if (!project_camera_vertex<S>(cam_scratch[i], cam, xs[i], ys[i])) {
			return 0u;
		}
	}
	u16 order[16] {};
	for (u16 i = 0u; i < nv; ++i) {
		order[i] = i;
	}
	for (u16 i = 1u; i < nv; ++i) {
		const u16 key = order[i];
		u16 j = i;
		while (j > 0u) {
			const u16 prev = order[j - 1u];
			const bool after = xs[prev] > xs[key] ||
					   (xs[prev] == xs[key] && ys[prev] > ys[key]);
			if (!after) {
				break;
			}
			order[j] = prev;
			--j;
		}
		order[j] = key;
	}
	/// Producto cruzado 2D `(a-o) x (b-o)`; positivo = giro a la izquierda.
	const auto cross = [&](u16 o, u16 a, u16 b) -> s32 {
		const s32 ax = xs[a] - xs[o];
		const s32 ay = ys[a] - ys[o];
		const s32 bx = xs[b] - xs[o];
		const s32 by = ys[b] - ys[o];
		return ax * by - ay * bx;
	};
	u16 h[32] {};
	u16 k = 0u;
	for (u16 i = 0u; i < nv; ++i) {
		while (k >= 2u && cross(h[k - 2u], h[k - 1u], order[i]) <= 0) {
			--k;
		}
		h[k++] = order[i];
	}
	const u16 lower = k + 1u;
	for (u16 i = nv - 1u; i > 0u; --i) {
		const u16 p = order[i - 1u];
		while (k >= lower && cross(h[k - 2u], h[k - 1u], p) <= 0) {
			--k;
		}
		h[k++] = p;
	}
	if (k > 1u) {
		--k; // el primer punto se repite al cerrar
	}
	if (k < 3u || k > 16u) {
		return 0u;
	}
	s16 hx[16] {};
	s16 hy[16] {};
	for (u16 i = 0u; i < k; ++i) {
		hx[i] = xs[h[i]];
		hy[i] = ys[h[i]];
	}
	return target.fill_polygon(hx, hy, k, color) ? 1u : 0u;
}
/// **Pinta un sólido n-gon con color plano** (sin patrón). Envoltorio de
/// `render_solid_styled` que conserva la firma histórica: `color_of(face_index)`
/// devuelve el índice de paleta.
/// \tparam ColorFn callable convertible a `u8`.
template <class S, class Model, class ColorFn>
inline u32 render_solid(const math3d::PolyMeshViewT<S>& mesh, const Model& model,
			const Camera3D<S>& cam, playfield::Surface& target, ColorFn&& color_of,
			math3d::Vec3t<S>* world, CamVertex<S>* cam_scratch, s16* xs, s16* ys,
			bool double_sided = false) noexcept {
	return render_solid_styled(
		mesh, model, cam, target,
		[&](u32 face) -> graphics::FaceStyle {
			return graphics::FaceStyle {static_cast<u8>(color_of(face)), 0u, 0u};
		},
		world, cam_scratch, xs, ys, double_sided);
}

/// Arista única de una malla para la ruta de alambre: índice menor primero.
struct WireEdge {
	u16 a = 0;
	u16 b = 0;
};

/// **Recorta un segmento en espacio de cámara** contra near + los cuatro laterales.
/// Devuelve `false` si queda completamente fuera. Misma convención de planos y ajuste
/// final que `clip_camera_polygon`; se usa en `render_wire` para no proyectar
/// coordenadas inválidas ni desbordar `s16`.
template <class S>
[[nodiscard]] inline bool clip_camera_segment(const Camera3D<S>& cam, CamVertex<S> a,
					      CamVertex<S> b, CamVertex<S>& out_a,
					      CamVertex<S>& out_b) noexcept {
	using detail3d::Plane;
	using T = eng::math::scalar_traits<S>;
	const detail3d::CameraPlanes<S> cp = detail3d::camera_planes(cam);
	a.x = detail3d::saturate_coord(a.x);
	a.y = detail3d::saturate_coord(a.y);
	a.z = detail3d::saturate_coord(a.z);
	b.x = detail3d::saturate_coord(b.x);
	b.y = detail3d::saturate_coord(b.y);
	b.z = detail3d::saturate_coord(b.z);
	for (u8 pi = 0u; pi < 5u; ++pi) {
		const Plane<S>& pl = cp.planes[pi];
		using Dist = decltype(pl.dist(a));
		const Dist zero {};
		const Dist da = pl.dist(a);
		const Dist db = pl.dist(b);
		const bool ain = da >= zero;
		const bool bin = db >= zero;
		if (!ain && !bin) return false;
		if (ain != bin) {
			const CamVertex<S> cut = detail3d::cut_edge<S>(a, b, da, db);
			if (!ain) {
				a = cut;
			} else {
				b = cut;
			}
		}
	}
	// Mismo ajuste final que el clip de polígono: el corte truncado no debe dejar
	// coordenadas fuera del viewport ni z < near (contrato del Blitter/Surface).
	out_a = a;
	out_b = b;
	CamVertex<S>* const ends[2] = {&out_a, &out_b};
	for (u8 e = 0u; e < 2u; ++e) {
		CamVertex<S>& v = *ends[e];
		if (v.z < cam.near_z) v.z = cam.near_z;
		const S xmax = T::mul_div(v.z, cam.half_w, cam.focal);
		const S ymax = T::mul_div(v.z, cam.half_h, cam.focal);
		if (v.x > xmax) v.x = xmax;
		if (v.x < -xmax) v.x = -xmax;
		if (v.y > ymax) v.y = ymax;
		if (v.y < -ymax) v.y = -ymax;
	}
	return true;
}

/// **Pinta una malla n-gon en alambre** con aristas únicas. Transforma la malla,
/// deduplica aristas (índice menor primero), recorta cada segmento en espacio de
/// cámara (near + laterales) y lo proyecta; `target.draw_line` recorta en 2D.
///
/// \param mesh malla n-gon convexa (caras de 3..8 vértices).
/// \param model transformación afín de la malla.
/// \param cam cámara (espacio de cámara; ver `CameraView`).
/// \param target superficie de destino; el llamador la limpia antes.
/// \param color índice de paleta de las líneas.
/// \param world scratch de `mesh.vertex_count()` vértices.
/// \param edges scratch de aristas únicas; capacidad `edge_cap` (una malla cúbica
///        usa 12; un sólido de 30 vértices, hasta 60). Si se llena, se omiten las
///        aristas sobrantes (no se desborda).
/// \param edge_cap capacidad de `edges`; debe ser > 0.
/// \param cam_scratch scratch de al menos 2 `CamVertex` (se reutiliza el de 16).
/// \param xs,ys scratch de al menos 2 `s16` (se reutiliza el de 16).
/// \param plan `FramePlan` opcional para que un `BlitterRaster` encole líneas en
///        vez de rasterizarlas por CPU; vacío = ruta CPU (`Surface::draw_line`).
/// \param op operación de rasterizado (`Copy` por defecto; `Xor` usa la racha EOR
///        del Blitter, útil para alambre).
/// \return número de aristas dibujadas.
/// \pre La malla y los buffers no son nulos; `edge_cap > 0`.
template <class S, class Model>
inline u32 render_wire(const math3d::PolyMeshViewT<S>& mesh, const Model& model,
		       const Camera3D<S>& cam, playfield::Surface& target, u8 color,
		       math3d::Vec3t<S>* world, WireEdge* edges, u16 edge_cap,
		       CamVertex<S>* cam_scratch, s16* xs, s16* ys,
		       eng::Ref<graphics::FramePlan> plan = {},
		       playfield::RasterOp op = playfield::RasterOp::Copy) noexcept {
	const u32 nv = mesh.vertex_count();
	const u32 nf = mesh.face_count();
	if (world == nullptr || edges == nullptr || cam_scratch == nullptr || xs == nullptr ||
	    ys == nullptr || nv == 0u || nf == 0u || edge_cap == 0u) {
		return 0u;
	}
	math3d::mesh_transform(mesh.vertices, model,
			       eng::Span<math3d::Vec3t<S>>(world, nv));
	u16 edge_count = 0u;
	for (u32 fi = 0u; fi < nf; ++fi) {
		const eng::Span<const u16> idx = mesh.face_indices(fi);
		const u8 n = static_cast<u8>(idx.size());
		if (n < 3u || n > 8u) continue;
		for (u8 k = 0u; k < n; ++k) {
			const u8 j = static_cast<u8>((k + 1u == n) ? 0u : (k + 1u));
			u16 ia = idx[k];
			u16 ib = idx[j];
			if (ia >= nv || ib >= nv || ia == ib) continue;
			if (ia > ib) {
				const u16 tmp = ia;
				ia = ib;
				ib = tmp;
			}
			bool found = false;
			for (u16 e = 0u; e < edge_count; ++e) {
				if (edges[e].a == ia && edges[e].b == ib) {
					found = true;
					break;
				}
			}
			if (!found && edge_count < edge_cap) {
				edges[edge_count++] = WireEdge {ia, ib};
			}
		}
	}
	u32 drawn = 0u;
	for (u16 e = 0u; e < edge_count; ++e) {
		const math3d::Vec3t<S>& wa = world[edges[e].a];
		const math3d::Vec3t<S>& wb = world[edges[e].b];
		CamVertex<S> a {wa.v[0], wa.v[1], wa.v[2]};
		CamVertex<S> b {wb.v[0], wb.v[1], wb.v[2]};
		if (!clip_camera_segment<S>(cam, a, b, a, b)) continue;
		if (!project_camera_vertex<S>(a, cam, xs[0], ys[0])) continue;
		if (!project_camera_vertex<S>(b, cam, xs[1], ys[1])) continue;
		if (target.draw_line(xs[0], ys[0], xs[1], ys[1], color, plan, op)) {
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
			       eng::Span<math3d::Vec3t<S>>(world, nv));
	const bool all_inside = camera_contains_vertices<S>(cam, world, nv);
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
		u8 m = 3u;
		if (!all_inside) {
			CamVertex<S> clipped[16] {};
			m = clip_camera_polygon<S>(cam_scratch, 3u, cam, clipped, 16u);
			if (m < 3u) continue;
			for (u8 k = 0u; k < m; ++k) cam_scratch[k] = clipped[k];
		}
		bool ok = true;
		for (u8 k = 0u; k < m; ++k) {
			if (!project_camera_vertex<S>(cam_scratch[k], cam, xs[k], ys[k])) {
				ok = false;
				break;
			}
		}
		if (!ok) continue;
		const s32 area = screen_area2(xs, ys, m);
		if (area == 0) continue;
		if (!double_sided && area > 0) continue;
		if (target.fill_polygon(xs, ys, m, static_cast<u8>(color_of(fi)))) {
			++drawn;
		}
	}
	return drawn;
}

} // namespace eng::graphics
