#pragma once

/// \file object3d.hpp
/// Modelo de objeto/malla 3D del demoscene (`lib3d`) portado **1:1**: formato
/// empaquetado que genera `obj2c` y recorrido de vértices/aristas/caras con flags.
///
/// Fidelidad de layout (clave): los índices de los grupos son **offsets en bytes**
/// sobre `objdat`, y los structs conservan la disposición del original, porque el
/// efecto recorre los grupos con macros (`NODE3D(i) = objdat + i - 2`, etc.). Las
/// primitivas matemáticas (matrices 4.12, `normfx`, `div_wide`) vienen de `math2d`/
/// `math3d`, ya validadas por HOST-010/011.
///
/// Uso:
///   Object3D obj {};
///   new_object3d(obj, pilka);          // enlaza el mesh (sin alloc dinámica)
///   obj.rotate.x = obj.rotate.y = obj.rotate.z = eng::retro::angle_to_radians(frame * 8);
///   update_object_transformation(obj);
///
/// **Por qué los structs siguen en `s16`.** `Point3D`/`Node3D`/`Edge`/`Face` son el
/// layout empaquetado del `objdat` (los grupos se indexan por offset de byte), así que
/// retiparlos a `Fixed` cambiaría el binario del mesh generado y rompería los macros del
/// original. La capa de cálculo que los consume **sí** es tipada y genérica:
/// `math3d::Affine3<>`, `Vec<3,eng::coord>` (`P3<>`), `math3d::load_rotate`/`scale` (con
/// el ángulo en radianes). La crudeza vive solo en el almacenamiento, no en la aritmética.

#include <eng/core/arith.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/retro/fixed_q.hpp>

namespace eng::object3d {

using eng::s16;
using eng::s32;
using eng::s8;
using eng::u8;
using eng::u16;
using eng::math::div_wide;
using eng::retro::normfx;

/// Punto/vector **tipado** para el estado de runtime del `Object3D` (posiciones, escala,
/// camara). NO es el layout empaquetado del `objdat` (ese sigue en `Point3D` crudo porque
/// lo genera `obj2c` y se indexa por offset de byte). `S` fija escalar y exponente: el
/// compilador **rechaza mezclar** tipos distintos (no hace falta `static_cast`).
template <class S>
struct Point3S {
	S x {};
	S y {};
	S z {};
};

/// Posiciones en el mundo (escalar de coordenada `eng::coord`).
using Point3C = Point3S<eng::retro::q0>;
/// Escala/ratios normalizados (escalar `eng::real`). El caller puede instanciar
/// `Point3S<S>` con otro fixed (p. ej. `Fixed<s32,E>`); el compilador rechaza mezclas.
using Point3R = Point3S<eng::retro::q12>;

/// Ángulo de rotación en **radianes** (escalar `eng::real`, por defecto `q12`). El mismo
/// tipo que la escala (`Point3R`): el álgebra 3D trabaja en radianes y la tabla retro se
/// consulta dentro del escalar (`scalar_sin<q12>`).
using Angle3 = Point3R;

/// Punto/vector 3D (mismo layout que `Point3D`).
struct Point3D {
	s16 x = 0;
	s16 y = 0;
	s16 z = 0;
};

/// Nodo de vértice: flags de visibilidad + punto original + punto transformado.
/// Layout exacto: `flags` (1 byte) + pad, `point` en offset 2, `vertex` en offset 8.
struct Node3D {
	s8 flags = 0;
	Point3D point {};
	Point3D vertex {};
};

/// Arista: flags (0/-1 o color) + 2 índices de vértice (offsets de byte).
struct Edge {
	s8 flags = 0;
	s8 pad = 0;
	s16 point[2] = {0, 0};
};

/// Par (vértice, arista) de una cara.
struct FaceIndex {
	s16 vertex = 0;
	s16 edge = 0;
};

/// Cara: normal (para culling/iluminación) + flags/material + nº de índices.
/// Los `FaceIndex` van a continuación (offset 10); se accede vía `face_indices`.
struct Face {
	s16 normal[3] = {0, 0, 0};
	s8 flags = 0;
	s8 material = 0;
	s16 count = 0;
};

/// Cabecera de malla (la que genera `obj2c`; los punteros son offsets absolutos).
struct Mesh3D {
	s16 vertices = 0;
	s16 texcoords = 0;
	s16 edges = 0;
	s16 faces = 0;
	s16 materials = 0;
	/// Blob empaquetado de `obj2c` como **bytes tipados** (los grupos lo indexan por
	/// offset de byte). Sustituye al `void*` crudo: el formato por campo lo describen los
	/// structs `Point3D`/`Node3D`/`Edge`/`Face` de más abajo.
	eng::Span<eng::u8> bytes {};
	s16* vertexGroups = nullptr;
	s16* edgeGroups = nullptr;
	s16* faceGroups = nullptr;
	s16* objects = nullptr;
};

/// Objeto 3D: mesh enlazado + estado de transformación + cámara en espacio objeto.
///
/// **Layout estable**: el asm (`flatshade_asm.s`) lee `objdat`@0, los grupos@4/8/12 y
/// `objectToWorld`@38..; `objdat_size` va **al final** para no mover esos offsets.
struct Object3D {
	eng::u8* objdat = nullptr;
	s16* vertexGroups = nullptr;
	s16* edgeGroups = nullptr;
	s16* faceGroups = nullptr;
	s16* objects = nullptr;

	Angle3 rotate {};    // ángulo en radianes (q12)
	Point3R scale {};    // escala (q12)
	Point3C translate {}; // posicion (q0)

	math3d::Affine3<> objectToWorld {}; // objeto -> mundo (RATIO 4.12 + LONGITUD)
	math3d::Affine3<> worldToObject {}; // mundo -> objeto

	Point3C camera {}; // posicion de camara en espacio objeto (q0)

	eng::u32 objdat_size = 0; // tamaño del blob (para la vista `Span<u8>`)
};

/// Enlaza el mesh al objeto (equivalente a `NewObject3D` sin reservar memoria: el
/// `Object3D` es del llamador). `scale` queda a 1.0 (4.12).
inline void new_object3d(Object3D& object, const Mesh3D& mesh) {
	object.objdat = mesh.bytes.data();
	object.objdat_size = static_cast<eng::u32>(mesh.bytes.size());
	object.vertexGroups = mesh.vertexGroups;
	object.edgeGroups = mesh.edgeGroups;
	object.faceGroups = mesh.faceGroups;
	object.objects = mesh.objects;
	object.scale = Point3R {eng::retro::q12 {1 << 12}, eng::retro::q12 {1 << 12}, eng::retro::q12 {1 << 12}};
}

/// Vista de bytes del blob de un `Object3D` (mutable): lo que consumen los accesores.
[[nodiscard]] inline eng::Span<eng::u8> object_bytes(const Object3D& object) {
	return eng::Span<eng::u8> {object.objdat, object.objdat_size};
}

/// Valida lo mínimo del descriptor de malla: que los grupos referenciados quepan en el
/// blob. Devuelve `false` si el asset está corrupto o sin `bytes`.
[[nodiscard]] inline bool mesh_validate(const Mesh3D& mesh) {
	if (mesh.bytes.empty()) {
		return false;
	}
	const eng::u32 n = static_cast<eng::u32>(mesh.bytes.size());
	const s16* groups[] = {mesh.vertexGroups, mesh.edgeGroups, mesh.faceGroups, mesh.objects};
	for (const s16* g : groups) {
		if (g == nullptr) {
			continue;
		}
		for (const s16* p = g; *p; ++p) {
			const s16 off = *p;
			if (off < 0 || static_cast<eng::u32>(off) >= n) {
				return false;
			}
		}
	}
	return true;
}

// --- Acceso al `objdat` empaquetado (macros del original) --------------------
// Reciben la vista `Span<u8>` del blob; devuelven punteros a los structs de formato.
inline eng::u8* objdat_byte(eng::Span<eng::u8> bytes, s16 i) {
	return bytes.data() + static_cast<s32>(i);
}
inline Node3D* node3d(eng::Span<eng::u8> bytes, s16 i) {
	return reinterpret_cast<Node3D*>(objdat_byte(bytes, static_cast<s16>(i - 2)));
}
inline Point3D* point3d(eng::Span<eng::u8> bytes, s16 i) {
	return reinterpret_cast<Point3D*>(objdat_byte(bytes, i));
}
inline Point3D* vertex3d(eng::Span<eng::u8> bytes, s16 i) {
	return reinterpret_cast<Point3D*>(objdat_byte(bytes, static_cast<s16>(i + 6)));
}
inline Edge* edge3d(eng::Span<eng::u8> bytes, s16 i) {
	return reinterpret_cast<Edge*>(objdat_byte(bytes, i));
}
inline Face* face3d(eng::Span<eng::u8> bytes, s16 i) {
	return reinterpret_cast<Face*>(objdat_byte(bytes, i));
}
inline FaceIndex* face_indices(Face* face) {
	return reinterpret_cast<FaceIndex*>(reinterpret_cast<eng::u8*>(face) + 10);
}

/// Actualiza `objectToWorld`/`worldToObject` y la cámara en espacio objeto.
/// Port 1:1 de `UpdateObjectTransformation` (lib3d).
inline void update_object_transformation(Object3D& object) {
	const Angle3& r = object.rotate;
	const Point3R& s = object.scale;
	const Point3C& t = object.translate;

	// objeto -> mundo: Rx * Ry * Rz * S * T
	{
		math3d::Affine3<>& a = object.objectToWorld;
		math3d::load_rotate(a.m, r.x, r.y, r.z);
		math3d::scale(a.m, s.x, s.y, s.z);
		a.t = eng::math::Vec<3, eng::retro::q0> {{t.x, t.y, t.z}};
	}

	// mundo -> objeto: T * S * Rz * Ry * Rx
	//
	// OJO — port 1:1 del original: la parte LINEAL sí se invierte (`S⁻¹Rᵀ`, con
	// `load_reverse_rotate` y `1/s`), pero la traslación queda en `-T`, no en
	// `-S⁻¹Rᵀ·T`. NO es una inversa completa: `compose(objectToWorld, worldToObject)`
	// no da la identidad en la traslación (medido: `.t = (-9965,-800,-1924)` en un caso
	// rotación+escala+traslación). La cámara en espacio objeto de 079/116 depende de este
	// comportamiento y HOST-014 lo fija, así que NO se corrige aquí. Para una inversa
	// completa de una transformación rígida usa `math3d::inverse_rigid` (HOST-055).
	{
		math3d::Affine3<> m_scale {};
		m_scale.m = math3d::Mat3<>::identity();
		m_scale.t = eng::math::Vec<3, eng::retro::q0> {{-t.x, -t.y, -t.z}};
		// 1/s en 4.12: numerador 1.0 en 8.24 (`kOne8_24`) para que el cociente de
		// `div_wide` (16 bits) quede ya en 4.12 sin normalizar.
		m_scale.m.m[0][0] = eng::retro::q12 {div_wide(eng::retro::kOne8_24, s.x.v)};
		m_scale.m.m[1][1] = eng::retro::q12 {div_wide(eng::retro::kOne8_24, s.y.v)};
		m_scale.m.m[2][2] = eng::retro::q12 {div_wide(eng::retro::kOne8_24, s.z.v)};

		math3d::Mat3<> m_rotate = math3d::Mat3<>::identity();
		math3d::load_reverse_rotate(m_rotate, -r.x, -r.y, -r.z);
		object.worldToObject = eng::math::compose(m_scale, math3d::Affine3<> {m_rotate, {}});
	}

	// cámara en espacio objeto (la cámara está en (0,0,0) del mundo)
	{
		const math3d::Affine3<>& M = object.worldToObject;
		const math3d::P3<> t = M.t;
		object.camera.x = eng::retro::q0 {static_cast<s16>(eng::math::dot(M.m.row(0), t).v)};
		object.camera.y = eng::retro::q0 {static_cast<s16>(eng::math::dot(M.m.row(1), t).v)};
		object.camera.z = eng::retro::q0 {static_cast<s16>(eng::math::dot(M.m.row(2), t).v)};
	}
}

/// Actualiza **solo** `objectToWorld` (la matriz directa), **sin** la inversa
/// `worldToObject` ni `camera`. Para efectos que unicamente proyectan vertices (p. ej.
/// `bobs3d`, que dibuja un BOB por vertice y no usa culling ni luz), ahorra el calculo
/// de `S⁻¹Rᵀ`, su `compose` y los 3 productos de la camara (~1/3 del
/// `UpdateObjectTransformation` original). La salida de `objectToWorld` es identica a la
/// de `update_object_transformation`.
inline void update_object_transformation_forward(Object3D& object) {
	const Angle3& r = object.rotate;
	const Point3R& s = object.scale;
	const Point3C& t = object.translate;
	math3d::Affine3<>& a = object.objectToWorld;
	math3d::load_rotate(a.m, r.x, r.y, r.z);
	// La mayoria de efectos (p. ej. bobs3d) usan escala 1.0 (4.12: 4096), asi que el
	// `scale` seria una identidad de 9 `muls.w`. Se salta cuando no aporta nada.
	if (s.x.v != (1 << 12) || s.y.v != (1 << 12) || s.z.v != (1 << 12)) {
		math3d::scale(a.m, s.x, s.y, s.z);
	}
	a.t = eng::math::Vec<3, eng::retro::q0> {{t.x, t.y, t.z}};
}

} // namespace eng::object3d
