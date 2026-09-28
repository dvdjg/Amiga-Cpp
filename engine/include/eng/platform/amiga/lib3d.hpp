#pragma once

/// \file lib3d.hpp
/// Rutinas de **lib3d** (demoscene-repo-orig) sobre el modelo empaquetado de
/// `object3d`: visibilidad de caras con luz, visibilidad de aristas de un sólido
/// convexo y transformación + proyección de vértices.
///
/// Es **API pura** (sin hardware, sin STL, sin memoria dinámica): por eso vive en
/// `eng::core` y se puede validar con test host. El dibujo (Blitter de líneas /
/// area fill) NO está aquí: es backend y lo decide la plataforma.
///
/// Formato numérico: 4.12 (ver `eng/retro`). Los ángulos son índices 0..4095.
///
/// ## Perfil de coste (medido en la demo 116, WinUAE-DBG, 68000, -O1)
/// Con ~60 vértices y ~32 caras por frame, el reparto del `update` es:
/// - `transform_vertices` ~57k ciclos (**el mayor**: ~9 `mul_wide` + 2 `div_wide` por
///   vértice, o sea ~6 `muls.w` + 2 `divs.w` + carga de matriz).
/// - `update_face_visibility` ~19k (producto punto + luz: ~7 muls por cara).
/// - `update_object_transformation` ~21k (la matriz Rx·Ry·Rz + su inversa).
/// - `update_edge_visibility_convex` ~11k (recorrido puro de índices, sin muls).
///
/// ## Qué genera g++ y qué sería el ideal en asm
/// - **`mul_wide(b, a)` / `mulu16`** (`arith.hpp`): `muls.w`/`mulu.w` nativos. Si se
///   escribe `(s32)a * b` con operandos `s32`, g++ emite `__mulsi3` (multiplicación 32×32
///   por software, ~10× más cara). Por eso TODO producto 16×16 del camino caliente debe
///   pasar por `mul_wide`/`mulu16`.
/// - **`div_wide(a, b)`** (`arith.hpp`): `divs.w` nativo (cociente 16 bits en la palabra
///   baja). `a / b` en `s32` genera `__divsi3` (software). Es el libcall más caro.
/// - **Escrituras a memoria empaquetada** (`objdat + offset`): g++ no puede
///   mantener los punteros en registros de dirección (`register ... asm("aN")` se
///   ignora en GCC-15) → recarga base+offset en cada acceso. El original fija los
///   registros a mano y encadena el bucle con `dbra`. Ideal asm: base en `aN`,
///   índices en `dN`, desplazamiento con `(aN,dX.w)`, un `muls.w`/`divs.w` por
///   operación, sin recargas.
/// - **Recorrido de grupos** (`*group++` hasta 0): ideal en asm con `move.w (aN)+`
///   y `bne`. g++ lo genera razonable; el coste real del efecto está en la
///   aritmética, no aquí.
///
/// Ver `docs/guides/optimization/OPTIMIZACION_GPP_68000.md` (§9) para la bitácora
/// de estos hallazgos y `demos/techniques/amiga/effects/116_flatshade_convex/README.md` para el port.
///
/// **Por qué aquí no hay escalares nuevos.** El modelo (`object3d`) es layout crudo
/// empaquetado y sus punteros se recorren por offset; la matemática que se le aplica es
/// la ya tipada (`math3d::Affine3`, `Vec<3,Coord>`/`P3`). Por eso este fichero solo usa
/// enteros con `mul_wide`/`div_wide` (`eng/core/math/arith.hpp`): no le hace falta otro tipo de
/// escalar, solo garantizar `muls.w`/`divs.w` en el camino caliente.

#include <eng/core/math/fixed_affine.hpp>
#include <eng/core/math/arith.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/platform/amiga/object3d.hpp>
#include <eng/core/math/light.hpp>
#include <eng/core/math/inv_sqrt.hpp>
#include <eng/core/types/types.hpp>
#include <eng/retro/fixed_q.hpp>

namespace eng::lib3d {

using eng::s16;
using eng::s32;
using eng::s8;
using eng::u16;
using eng::u32;
using object3d::Object3D;
using object3d::Point3D;

/// `x >> 16`: parte alta de un 32 bits como `s16` (el `swap16` del original). Vive en el
/// núcleo de luz (`eng::math::hi16`), junto al punto de personalización `light_ops`.
using eng::math::hi16;

/// Tabla `InvSqrt` (`invsqrt` en el original): el **núcleo matemático** la define y la genera en
/// compilación (`eng/core/math/inv_sqrt.hpp`, 0.16, `1/√x`, 512 entradas) — es matemática pura, no
/// de plataforma. Aquí se re-exporta para el uso histórico de lib3d (`shade` y la demo 116).
using eng::math::kInvSqrt;

/// Port de `UpdateFaceVisibility`: back-face culling + color de luz por cara
/// (0..15). `object.camera` debe estar ya en espacio objeto (lo calcula
/// `object3d::update_object_transformation`). Caras ocultas quedan a `-1`
/// (salvo `material < 0`, que usa la luz invertida, para doble cara).
///
/// Coste: producto punto + magnitud² + luz ≈ 7 `mul_wide`/`mulu_wide` por cara.
/// No usa `sqrt`: la magnitud² (parte alta) indexa `kInvSqrt`.
inline void update_face_visibility(Object3D& object) {
	const s16 cx = object.camera.x.v;
	const s16 cy = object.camera.y.v;
	const s16 cz = object.camera.z.v;
	for (const eng::Ref<object3d::Face>& fr : object.faces()) {
		object3d::Face* face = fr.get();
		s16 px, py, pz;
		{
			const s16 i = object3d::face_indices(face)[0].vertex;
			const Point3D* p = object.point(i);
			px = static_cast<s16>(cx - p->x.v);
			py = static_cast<s16>(cy - p->y.v);
			pz = static_cast<s16>(cz - p->z.v);
		}
		// Normal = RATIO (4.12); camara-vertice = LONGITUD (entero). El producto
		// `q12*q0` da el mismo `muls.w` que `mul_wide`, con el formato explicito, y
		// aqui NO se normaliza: el original usa la escala cruda para el signo y la
		// magnitud² de la luz.
		const eng::retro::q12 nx = face->normal[0], ny = face->normal[1], nz = face->normal[2];
		const eng::retro::q0 vx {px}, vy {py}, vz {pz};
		const s32 v = (nx * vx).v + (ny * vy).v + (nz * vz).v;
		const s32 e1_sq = (vx * vx).v + (vy * vy).v + (vz * vz).v;
		if (v >= 0 || face->material < 0) {
			// Luz 0..15. `shade` usa |v| internamente (cubre la cara de espaldas con
			// material < 0); el rasgo `light_ops` la especializa por CPU (68000:
			// `mulu.w` + `swap`), sin `sqrt` en runtime.
			face->flags = static_cast<s8>(eng::math::light_ops<>::shade(v, e1_sq, kInvSqrt));
		} else {
			face->flags = -1;
		}
	}
}

/// Port de `UpdateEdgeVisibilityConvex` (flatshade-convex): por cada cara visible
/// marca sus vértices (`node->flags = 1`) y **XOR-ea** el color de luz en sus
/// aristas. El XOR cancela las aristas compartidas por dos caras visibles de igual
/// luz; quedan la silueta y las aristas internas visibles (las que se dibujan).
/// Requiere que `update_face_visibility` haya corrido antes (usa `face->flags`).
///
/// Coste: recorrido puro de índices (sin multiplicaciones).
inline void update_edge_visibility_convex(Object3D& object) {
	const s8 s = 1;
	for (const eng::Ref<object3d::Face>& fr : object.faces()) {
		object3d::Face* face = fr.get();
		const s8 flags = face->flags;
		if (flags >= 0) {
			const eng::Span<object3d::FaceIndex> fi = object3d::face_indices(face);
			object.node(fi[0].vertex)->flags = s;
			object.edge(fi[0].edge)->flags = static_cast<s8>(object.edge(fi[0].edge)->flags ^ flags);
			object.node(fi[1].vertex)->flags = s;
			object.edge(fi[1].edge)->flags = static_cast<s8>(object.edge(fi[1].edge)->flags ^ flags);
			for (s16 k = 2; k < face->count; ++k) {
				object.node(fi[k].vertex)->flags = s;
				object.edge(fi[k].edge)->flags =
					static_cast<s8>(object.edge(fi[k].edge)->flags ^ flags);
			}
		}
	}
}

// La proyección de cada vértice la resuelve `eng::math::projector` (en `affine.hpp`): el
// truco del empaquetado de dos `muls.w` es del 68000 y vive en su backend de CPU, no aquí.

/// Port de `TransformVertices`: transforma y proyecta (perspectiva con `div_wide`)
/// los vértices marcados por `update_edge_visibility_convex` (o el que ponga
/// `node->flags`), guardando `(x, y, zp)` en `Node3D::vertex`. Los no marcados se
/// dejan tal cual.
///
/// Escribe en `bbox[4] = {minX, maxX, minY, maxY}` la caja de pantalla de los
/// vértices transformados (la usa el llamador para acotar el relleno); pásale un
/// buffer del llamador. `half_w`/`half_h` son el centro de proyección (mitad del
/// viewport).
///
/// Coste: **el mayor del efecto**: ~9 `mul_wide` + 2 `div_wide` por vértice (~6 `muls.w`
/// + 2 `divs.w` más la carga de la matriz). En asm el ideal es la matriz en
/// registros y un `muls.w`/`divs.w` por operación, sin recargar `objdat`.
inline void transform_vertices(Object3D& object, s16 half_w, s16 half_h, s16 bbox[4]) {
	math3d::Affine3<>& M = object.objectToWorld;

	// Lo precalculable UNA vez por matriz (términos de traslación plegados) lo guarda
	// la caché del proyector; el backend 68000 mete ahí lo que necesite.
	using Proj = eng::math::projector<math3d::Affine3<>>;
	const Proj::cache pc = Proj::make(M);

	bbox[0] = 32767; bbox[1] = -32768; bbox[2] = 32767; bbox[3] = -32768;
	for (const eng::Ref<object3d::Node3D>& nr : object.points()) {
		object3d::Node3D* node = nr.get();
		if (node->flags) {
			s16* pt = reinterpret_cast<s16*>(node); // TODO: Limpiar esto
			s16 x, y, z;

			*pt++ = 0;
			x = *pt++;
			y = *pt++;
			z = *pt++;
			const eng::math::Projected3 pr = Proj::project(pc, x, y, z);

			const s16 sx = static_cast<s16>(eng::math::div_wide(pr.xp, static_cast<s16>(pr.zp)) + half_w);
			const s16 sy = static_cast<s16>(eng::math::div_wide(pr.yp, static_cast<s16>(pr.zp)) + half_h);
			*pt++ = sx;
			*pt++ = sy;
			*pt++ = static_cast<s16>(pr.zp);

			if (sx < bbox[0]) bbox[0] = sx;
			if (sx > bbox[1]) bbox[1] = sx;
			if (sy < bbox[2]) bbox[2] = sy;
			if (sy > bbox[3]) bbox[3] = sy;
		}
	}
}

} // namespace eng::lib3d
