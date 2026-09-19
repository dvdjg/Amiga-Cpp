#pragma once

/// \file mesh_renderer.hpp
/// Rasterizador de mallas 3D sobre una `Surface` (el contexto de dibujo del engine).
///
/// Compone el pipeline completo a alto nivel:
///   1. transformar la malla al mundo (`mesh_transform`, de `mesh3d`);
///   2. back-face culling + orden de pintado (`mesh_painter_order`);
///   3. proyectar a pantalla (`project_perspective`, `div_wide` 4.12);
///   4. rellenar cada cara visible con `Surface::fill_polygon`.
///
/// Todo con buffers del llamador (sin heap) y a través de `Surface`, así que
/// funciona igual en EHB / single 4 planos / DPF: la app nunca ve planos ni
/// registros. Reutiliza `mesh3d`/`math3d`/`Surface` (no duplica).

#include <eng/core/arith.hpp>
#include <eng/core/mesh3d.hpp>
#include <eng/field/surface.hpp>

namespace eng::graphics {

/// Proyección en perspectiva 4.12 → pantalla: `s = centro + div_wide(coord*focal, z)`.
/// `z` debe ser > 0 (delante de la cámara); `focal` en 4.12 (256 = 0.0625, 4096 = 1.0).
/// Genérica sobre el escalar del vértice (`S`): convierte por entero crudo con
/// `scalar_traits`, así sirve para `Fixed`, `int` o `float`.
template <class S>
inline void project_perspective(const math3d::Vec3t<S>& v, s16 focal, s16 cx, s16 cy,
				s16& sx, s16& sy) {
	using T = eng::math::scalar_traits<S>;
	const s16 x = static_cast<s16>(T::to_int(v.v[0]));
	const s16 y = static_cast<s16>(T::to_int(v.v[1]));
	const s16 zc = static_cast<s16>(T::to_int(v.v[2]));
	const s16 z = zc != 0 ? zc : 1;
	// `mul_wide` (16×16→32 nativo): `(s32)coord * focal` acabaría en `__mulsi3` en 68000.
	sx = static_cast<s16>(eng::math::div_wide(eng::math::mul_wide(x, focal), z) + cx);
	sy = static_cast<s16>(eng::math::div_wide(eng::math::mul_wide(y, focal), z) + cy);
}

/// Rasteriza las caras (triángulos) de `mesh` sobre `surface`, con el color que
/// devuelva `color_of(face_index)`. Buffers del llamador: `world` (n_vertices),
/// `order` (n_faces) y `sx`/`sy` (n_vertices). Devuelve el número de caras pintadas.
///
/// `model` transforma la malla al mundo; `camera` es la posición de cámara en
/// espacio objeto (mismo convenio que `mesh_painter_order`). `double_sided` para
/// mallas abiertas (p. ej. suelos).
/// `Model` es cualquier afín (`Affine<N,SR,SL>`): el renderer no sabe de qué formato es.
template <typename ColorFn, typename Model>
inline u32 mesh_render_filled(const math3d::MeshView& mesh, const Model& model,
			      const math3d::Vec3& camera, s16 focal, s16 cx, s16 cy,
			      math3d::Vec3* world, math3d::FaceOrder* order,
			      s16* sx, s16* sy, field::Surface& surface, ColorFn color_of,
			      bool double_sided = false) {
	const u32 nv = mesh.vertex_count();
	const u32 nf = mesh.face_count();
	if (world == nullptr || order == nullptr || sx == nullptr || sy == nullptr) return 0;
	math3d::mesh_transform(mesh.vertices, model, Span<math3d::Vec3>(world, nv));
	const u32 n = math3d::mesh_painter_order(mesh, Span<const math3d::Vec3>(world, nv),
						 camera, Span<math3d::FaceOrder>(order, nf),
						 double_sided);
	for (u32 i = 0; i < nv; ++i) {
		project_perspective(world[i], focal, cx, cy, sx[i], sy[i]);
	}
	u32 drawn = 0;
	for (u32 k = 0; k < n; ++k) {
		const math3d::Face& f = mesh.faces[order[k].index];
		const s16 xs[3] = {sx[f.a], sx[f.b], sx[f.c]};
		const s16 ys[3] = {sy[f.a], sy[f.b], sy[f.c]};
		if (surface.fill_polygon(xs, ys, 3, static_cast<u8>(color_of(order[k].index)))) {
			++drawn;
		}
	}
	return drawn;
}

/// Igual que `mesh_render_filled` pero para una malla **n-gon** (`PolyMeshView`): es la
/// forma natural del raster Amiga (rellena polígonos convexos, sin triangular caras de
/// 5-6 lados). Usa el orden/culling por **normal almacenada** (`mesh_patches_order_lit`,
/// `ConvexPatchesLit`, apto para 68000) y rellena cada cara visible con
/// `Surface::fill_polygon(xs, ys, count, color)`.
///
/// `color_of(face_index)` devuelve el color de la cara (p. ej. su luz plana, como en
/// `update_face_visibility`). Más de `kMaxPolyVerts` vértices por cara se omiten.
/// Buffers del llamador: `world` (n_vertices), `order` (n_faces) y `sx`/`sy` (n_vertices).
template <typename ColorFn, typename Model>
inline u32 mesh_render_poly_filled(const math3d::PolyMeshView& mesh, const Model& model,
				   const math3d::Vec3& camera, s16 focal, s16 cx, s16 cy,
				   math3d::Vec3* world, math3d::FaceOrder* order, s16* sx, s16* sy,
				   field::Surface& surface, ColorFn color_of, bool double_sided = false) {
	constexpr u32 kMaxPolyVerts = 12u; // convexo tras culling; tope del relleno
	const u32 nv = mesh.vertex_count();
	const u32 nf = mesh.face_count();
	if (world == nullptr || order == nullptr || sx == nullptr || sy == nullptr) return 0;
	math3d::mesh_transform(mesh.vertices, model, Span<math3d::Vec3>(world, nv));
	const u32 n = math3d::mesh_patches_order_lit(mesh, Span<const math3d::Vec3>(world, nv),
						    camera, Span<math3d::FaceOrder>(order, nf),
						    double_sided);
	for (u32 i = 0; i < nv; ++i) {
		project_perspective(world[i], focal, cx, cy, sx[i], sy[i]);
	}
	s16 xs[kMaxPolyVerts];
	s16 ys[kMaxPolyVerts];
	u32 drawn = 0;
	for (u32 k = 0; k < n; ++k) {
		const math3d::FaceSpan fs = mesh.faces[order[k].index];
		if (fs.count < 3u || fs.count > kMaxPolyVerts) continue;
		for (u16 v = 0u; v < fs.count; ++v) {
			const u16 vi = mesh.indices[fs.first + v];
			xs[v] = sx[vi];
			ys[v] = sy[vi];
		}
		if (surface.fill_polygon(xs, ys, static_cast<u8>(fs.count),
					 static_cast<u8>(color_of(order[k].index)))) {
			++drawn;
		}
	}
	return drawn;
}

/// Rasteriza la malla en **alambre**: transforma, culling + pintor y dibuja las 3
/// aristas de cada cara visible con `Surface::draw_line` (`color` fijo). Las
/// aristas compartidas se dibujan dos veces (barato y sin estado de aristas).
/// Devuelve el número de caras procesadas. Buffers del llamador como en
/// `mesh_render_filled`.
template <typename Model>
inline u32 mesh_render_wire(const math3d::MeshView& mesh, const Model& model,
			    const math3d::Vec3& camera, s16 focal, s16 cx, s16 cy,
			    math3d::Vec3* world, math3d::FaceOrder* order,
			    s16* sx, s16* sy, field::Surface& surface, u8 color,
			    bool double_sided = false) {
	const u32 nv = mesh.vertex_count();
	const u32 nf = mesh.face_count();
	if (world == nullptr || order == nullptr || sx == nullptr || sy == nullptr) return 0;
	math3d::mesh_transform(mesh.vertices, model, Span<math3d::Vec3>(world, nv));
	const u32 n = math3d::mesh_painter_order(mesh, Span<const math3d::Vec3>(world, nv),
						 camera, Span<math3d::FaceOrder>(order, nf),
						 double_sided);
	for (u32 i = 0; i < nv; ++i) {
		project_perspective(world[i], focal, cx, cy, sx[i], sy[i]);
	}
	for (u32 k = 0; k < n; ++k) {
		const math3d::Face& f = mesh.faces[order[k].index];
		surface.draw_line(sx[f.a], sy[f.a], sx[f.b], sy[f.b], color);
		surface.draw_line(sx[f.b], sy[f.b], sx[f.c], sy[f.c], color);
		surface.draw_line(sx[f.c], sy[f.c], sx[f.a], sy[f.a], color);
	}
	return n;
}

} // namespace eng::graphics
