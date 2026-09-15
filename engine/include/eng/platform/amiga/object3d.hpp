#pragma once

/// \file object3d.hpp
/// Modelo de objeto/malla 3D del demoscene (`lib3d`) portado **1:1**: formato
/// empaquetado que genera `obj2c` y recorrido de vértices/aristas/caras con flags.
///
/// Fidelidad de layout (clave): los índices de los grupos son **offsets en bytes**
/// sobre `objdat`, y los structs conservan la disposición del original, porque el
/// efecto recorre los grupos con macros (`NODE3D(i) = objdat + i - 2`, etc.). Las
/// primitivas matemáticas (matrices 4.12, `normfx`, `div16`) vienen de `math2d`/
/// `math3d`, ya validadas por HOST-010/011.
///
/// Uso:
///   Object3D obj {};
///   new_object3d(obj, pilka);          // enlaza el mesh (sin alloc dinámica)
///   obj.rotate.x = obj.rotate.y = obj.rotate.z = frame * 8;
///   update_object_transformation(obj);

#include <eng/core/word.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/core/types.hpp>
#include <eng/retro/fixed_q.hpp>

namespace eng::object3d {

using eng::s16;
using eng::s32;
using eng::s8;
using eng::u8;
using eng::u16;
using eng::math::div16;
using eng::retro::normfx;

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
	void* data = nullptr;
	s16* vertexGroups = nullptr;
	s16* edgeGroups = nullptr;
	s16* faceGroups = nullptr;
	s16* objects = nullptr;
};

/// Objeto 3D: mesh enlazado + estado de transformación + cámara en espacio objeto.
struct Object3D {
	void* objdat = nullptr;
	s16* vertexGroups = nullptr;
	s16* edgeGroups = nullptr;
	s16* faceGroups = nullptr;
	s16* objects = nullptr;

	Point3D rotate {};
	Point3D scale {};
	Point3D translate {};

	math3d::Affine3 objectToWorld {}; // objeto -> mundo (RATIO 4.12 + LONGITUD)
	math3d::Affine3 worldToObject {}; // mundo -> objeto

	Point3D camera {}; // posición de cámara en espacio objeto
};

/// Enlaza el mesh al objeto (equivalente a `NewObject3D` sin reservar memoria: el
/// `Object3D` es del llamador). `scale` queda a 1.0 (4.12).
inline void new_object3d(Object3D& object, Mesh3D& mesh) {
	object.objdat = mesh.data;
	object.vertexGroups = mesh.vertexGroups;
	object.edgeGroups = mesh.edgeGroups;
	object.faceGroups = mesh.faceGroups;
	object.objects = mesh.objects;
	object.scale.x = static_cast<s16>(1 << 12);
	object.scale.y = static_cast<s16>(1 << 12);
	object.scale.z = static_cast<s16>(1 << 12);
}

// --- Acceso al `objdat` empaquetado (macros del original) --------------------
inline u8* objdat_byte(void* objdat, s16 i) {
	return static_cast<u8*>(objdat) + static_cast<s32>(i);
}
inline Node3D* node3d(void* objdat, s16 i) {
	return reinterpret_cast<Node3D*>(objdat_byte(objdat, static_cast<s16>(i - 2)));
}
inline Point3D* point3d(void* objdat, s16 i) {
	return reinterpret_cast<Point3D*>(objdat_byte(objdat, i));
}
inline Point3D* vertex3d(void* objdat, s16 i) {
	return reinterpret_cast<Point3D*>(objdat_byte(objdat, static_cast<s16>(i + 6)));
}
inline Edge* edge3d(void* objdat, s16 i) {
	return reinterpret_cast<Edge*>(objdat_byte(objdat, i));
}
inline Face* face3d(void* objdat, s16 i) {
	return reinterpret_cast<Face*>(objdat_byte(objdat, i));
}
inline FaceIndex* face_indices(Face* face) {
	return reinterpret_cast<FaceIndex*>(reinterpret_cast<u8*>(face) + 10);
}

/// Actualiza `objectToWorld`/`worldToObject` y la cámara en espacio objeto.
/// Port 1:1 de `UpdateObjectTransformation` (lib3d).
inline void update_object_transformation(Object3D& object) {
	const Point3D& r = object.rotate;
	const Point3D& s = object.scale;
	const Point3D& t = object.translate;

	// objeto -> mundo: Rx * Ry * Rz * S * T
	{
		math3d::Affine3& a = object.objectToWorld;
		math3d::load_rotate(a.m, static_cast<u16>(r.x), static_cast<u16>(r.y), static_cast<u16>(r.z));
		math3d::scale(a.m, s.x, s.y, s.z);
		a.t = eng::math::Vec<3, eng::retro::q0> {{eng::retro::q0 {t.x}, eng::retro::q0 {t.y}, eng::retro::q0 {t.z}}};
	}

	// mundo -> objeto: T * S * Rz * Ry * Rx
	{
		math3d::Affine3 m_scale {};
		m_scale.m = math3d::Mat3::identity();
		m_scale.t = eng::math::Vec<3, eng::retro::q0> {{eng::retro::q0 {static_cast<s16>(-t.x)},
			eng::retro::q0 {static_cast<s16>(-t.y)}, eng::retro::q0 {static_cast<s16>(-t.z)}}};
		// 1/s en 4.12: numerador 1.0 en 8.24 (`kOne8_24`) para que el cociente de
		// `div16` (16 bits) quede ya en 4.12 sin normalizar.
		m_scale.m.m[0][0] = eng::retro::q12 {div16(eng::retro::kOne8_24, s.x)};
		m_scale.m.m[1][1] = eng::retro::q12 {div16(eng::retro::kOne8_24, s.y)};
		m_scale.m.m[2][2] = eng::retro::q12 {div16(eng::retro::kOne8_24, s.z)};

		math3d::Mat3 m_rotate = math3d::Mat3::identity();
		math3d::load_reverse_rotate(m_rotate, static_cast<u16>(-r.x), static_cast<u16>(-r.y),
					    static_cast<u16>(-r.z));
		object.worldToObject = eng::math::compose(m_scale, math3d::Affine3 {m_rotate, {}});
	}

	// cámara en espacio objeto (la cámara está en (0,0,0) del mundo)
	{
		const math3d::Affine3& M = object.worldToObject;
		const math3d::P3 t = M.t;
		object.camera.x = static_cast<s16>(eng::math::dot(M.m.row(0), t).v);
		object.camera.y = static_cast<s16>(eng::math::dot(M.m.row(1), t).v);
		object.camera.z = static_cast<s16>(eng::math::dot(M.m.row(2), t).v);
	}
}

} // namespace eng::object3d
