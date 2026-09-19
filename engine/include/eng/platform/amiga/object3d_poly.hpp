#pragma once

/// \file object3d_poly.hpp
/// **Adaptador `obj2c` -> `math3d`**: construye un `math3d::PolyMeshView` (caras **n-gon**)
/// desde un `Object3D` del formato empaquetado de `obj2c`, para alimentar el orden por
/// parches (`math3d::mesh_patches_order`) y el relleno convexo **sin triangular**. La `pilka`
/// (icosaedro truncado: 20 hexágonos + 12 pentágonos) es el caso de uso: 32 caras frente a
/// las 116 que saldrían al triangular.
///
/// Los buffers son del llamador (sin heap). Los `FaceIndex` del formato son **offsets de
/// byte**; aquí se mapean al índice de vértice (0..N-1) del `PolyMeshView`.

#include <eng/core/mesh3d.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/platform/amiga/object3d.hpp>

namespace eng::object3d {

/// Cuántos vértices/caras/índices se escribieron en los buffers del llamador.
struct PolyMeshCounts {
	eng::u32 vertices = 0;
	eng::u32 faces = 0;
	eng::u32 indices = 0;
};

/// Construye la vista n-gon de `object`. Requiere:
/// - `verts` (>= nº de vértices), `offsets` (scratch `s16`, mismo tamaño),
/// - `indices` (>= suma de índices de todas las caras),
/// - `faces` (>= nº de caras).
/// Devuelve los conteos usados; si algo no cabe, corta y devuelve lo construido hasta ahí.
template <class S = eng::coord>
inline PolyMeshCounts build_poly_mesh(const Object3D& object, eng::Span<eng::math3d::Vec3t<S>> verts,
				      eng::Span<s16> offsets, eng::Span<eng::u16> indices,
				      eng::Span<eng::math3d::FaceSpan> faces,
				      eng::math3d::PolyMeshViewT<S>& out) {
	using T = eng::math::scalar_traits<S>;
	PolyMeshCounts c {};
	const eng::Span<eng::u8> bytes = object_bytes(object);

	// 1) Vértices: recorrido de los grupos (cada entrada es el offset de byte del punto
	//    original; `point3d` sigue el esquema del formato).
	const s16* g = object.vertexGroups;
	if (g != nullptr) {
		do {
			s16 off;
			while ((off = *g++) != 0) {
				if (c.vertices >= verts.size() || c.vertices >= offsets.size()) {
					g = nullptr;
					break;
				}
				const Point3D* p = point3d(bytes, off);
				verts[c.vertices] = eng::math3d::Vec3t<S> {
					{T::from_int(p->x.v), T::from_int(p->y.v), T::from_int(p->z.v)}};
				offsets[c.vertices] = off;
				++c.vertices;
			}
		} while (g != nullptr && *g != 0);
	}

	// 2) Caras: cada `FaceIndex.vertex` (offset de byte) se mapea al índice de vértice.
	bool room = true;
	const s16* fg = object.faceGroups;
	if (fg != nullptr) {
		do {
			s16 foff;
			while (room && (foff = *fg++) != 0) {
				Face* f = face3d(bytes, foff);
				const u32 cnt = static_cast<u32>(f->count);
				if (f->count < 3) {
					continue;
				}
				const u32 save = c.indices;
				if (c.faces >= faces.size() || c.indices + cnt > indices.size()) {
					room = false;
					break;
				}
				const FaceIndex* fi = face_indices(f);
				for (u32 k = 0; k < cnt; ++k) {
					u32 vi = 0;
					bool found = false;
					for (u32 m = 0; m < c.vertices; ++m) {
						if (offsets[m] == fi[k].vertex) {
							vi = m;
							found = true;
							break;
						}
					}
					if (!found) {
						room = false;
						break;
					}
					indices[c.indices++] = static_cast<u16>(vi);
				}
				if (!room) {
					c.indices = save; // no dejar una cara a medias
					break;
				}
				faces[c.faces].first = static_cast<u16>(save);
				faces[c.faces].count = static_cast<u16>(cnt);
				++c.faces;
			}
		} while (room && *fg != 0);
	}

	out.vertices = eng::Span<const eng::math3d::Vec3t<S>>(verts.data(), c.vertices);
	out.indices = eng::Span<const eng::u16>(indices.data(), c.indices);
	out.faces = eng::Span<const eng::math3d::FaceSpan>(faces.data(), c.faces);
	return c;
}

} // namespace eng::object3d
