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

#include <eng/core/math2d.hpp>
#include <eng/core/math3d.hpp>
#include <eng/core/types.hpp>

namespace eng::object3d {

using eng::s16;
using eng::s32;
using eng::s8;
using eng::u8;
using eng::u16;
using math2d::div16;
using math2d::normfx;

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

	math3d::Mat3x3 objectToWorld {}; // objeto -> mundo
	math3d::Mat3x3 worldToObject {}; // mundo -> objeto

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
		math3d::Mat3x3& m = object.objectToWorld;
		math3d::load_rotate(m, static_cast<u16>(r.x), static_cast<u16>(r.y), static_cast<u16>(r.z));
		math3d::scale(m, s.x, s.y, s.z);
		math3d::translate(m, t.x, t.y, t.z);
	}

	// mundo -> objeto: T * S * Rz * Ry * Rx
	{
		math3d::Mat3x3 m_scale;
		math3d::load_identity(m_scale);
		m_scale.x = static_cast<s16>(-t.x);
		m_scale.y = static_cast<s16>(-t.y);
		m_scale.z = static_cast<s16>(-t.z);
		m_scale.m00 = div16(1 << 24, s.x);
		m_scale.m11 = div16(1 << 24, s.y);
		m_scale.m22 = div16(1 << 24, s.z);

		math3d::Mat3x3 m_rotate;
		math3d::load_reverse_rotate(m_rotate, static_cast<u16>(-r.x), static_cast<u16>(-r.y),
					    static_cast<u16>(-r.z));
		object.worldToObject = math3d::compose(m_scale, m_rotate);
	}

	// cámara en espacio objeto (la cámara está en (0,0,0) del mundo)
	{
		const math3d::Mat3x3& M = object.worldToObject;
		const s16 cx = M.x;
		const s16 cy = M.y;
		const s16 cz = M.z;
		object.camera.x = normfx(static_cast<s32>(M.m00) * cx + static_cast<s32>(M.m01) * cy + static_cast<s32>(M.m02) * cz);
		object.camera.y = normfx(static_cast<s32>(M.m10) * cx + static_cast<s32>(M.m11) * cy + static_cast<s32>(M.m12) * cz);
		object.camera.z = normfx(static_cast<s32>(M.m20) * cx + static_cast<s32>(M.m21) * cy + static_cast<s32>(M.m22) * cz);
	}
}

// --- Visibilidad, luz y proyección (port de lib3d) --------------------------
//
// **NO VERIFICADA** por demo (regla de verificación por demo de `AGENTS.md`):
// estas funciones (y `kLightInvSqrt`/`hi16`) solo están cubiertas por el test host
// HOST-014; ninguna demo exitosa usa todavía esta versión del engine (la 116 usa
// copias demo-locales equivalentes).

/// Tabla de luz de lib3d (`UpdateFaceVisibility`): `65535/sqrt(x)`, `x=0..511`.
/// Normaliza el producto escalar sin `sqrt` en runtime (comportamiento del original).
inline constexpr u16 kLightInvSqrt[512] = {
	0,     65535, 46340, 37837, 32768, 29308, 26755, 24770, 23170, 21845, 20724,
	19760, 18918, 18176, 17515, 16921, 16384, 15895, 15447, 15035, 14654, 14301,
	13972, 13665, 13377, 13107, 12852, 12612, 12385, 12170, 11965, 11770, 11585,
	11408, 11239, 11077, 10922, 10774, 10631, 10494, 10362, 10235, 10112, 9994,
	9880,  9769,  9663,  9559,  9459,  9362,  9268,  9177,  9088,  9002,  8918,
	8837,  8757,  8680,  8605,  8532,  8461,  8391,  8323,  8257,  8192,  8129,
	8067,  8006,  7947,  7889,  7833,  7778,  7723,  7670,  7618,  7567,  7517,
	7468,  7420,  7373,  7327,  7282,  7237,  7193,  7150,  7108,  7067,  7026,
	6986,  6947,  6908,  6870,  6832,  6796,  6759,  6724,  6689,  6654,  6620,
	6587,  6554,  6521,  6489,  6457,  6426,  6396,  6365,  6336,  6306,  6277,
	6249,  6220,  6192,  6165,  6138,  6111,  6085,  6059,  6033,  6008,  5982,
	5958,  5933,  5909,  5885,  5862,  5838,  5815,  5793,  5770,  5748,  5726,
	5704,  5683,  5661,  5640,  5620,  5599,  5579,  5559,  5539,  5519,  5500,
	5480,  5461,  5442,  5424,  5405,  5387,  5369,  5351,  5333,  5316,  5298,
	5281,  5264,  5247,  5230,  5214,  5197,  5181,  5165,  5149,  5133,  5117,
	5102,  5087,  5071,  5056,  5041,  5026,  5012,  4997,  4983,  4968,  4954,
	4940,  4926,  4912,  4898,  4885,  4871,  4858,  4844,  4831,  4818,  4805,
	4792,  4780,  4767,  4754,  4742,  4730,  4717,  4705,  4693,  4681,  4669,
	4657,  4646,  4634,  4622,  4611,  4600,  4588,  4577,  4566,  4555,  4544,
	4533,  4522,  4512,  4501,  4490,  4480,  4469,  4459,  4449,  4439,  4428,
	4418,  4408,  4398,  4389,  4379,  4369,  4359,  4350,  4340,  4331,  4321,
	4312,  4303,  4293,  4284,  4275,  4266,  4257,  4248,  4239,  4230,  4221,
	4213,  4204,  4195,  4187,  4178,  4170,  4161,  4153,  4145,  4137,  4128,
	4120,  4112,  4104,  4096,  4088,  4080,  4072,  4064,  4057,  4049,  4041,
	4033,  4026,  4018,  4011,  4003,  3996,  3988,  3981,  3974,  3966,  3959,
	3952,  3945,  3938,  3931,  3923,  3916,  3909,  3903,  3896,  3889,  3882,
	3875,  3868,  3862,  3855,  3848,  3842,  3835,  3829,  3822,  3816,  3809,
	3803,  3796,  3790,  3784,  3777,  3771,  3765,  3759,  3753,  3746,  3740,
	3734,  3728,  3722,  3716,  3710,  3704,  3698,  3692,  3687,  3681,  3675,
	3669,  3664,  3658,  3652,  3646,  3641,  3635,  3630,  3624,  3619,  3613,
	3608,  3602,  3597,  3591,  3586,  3581,  3575,  3570,  3565,  3559,  3554,
	3549,  3544,  3539,  3533,  3528,  3523,  3518,  3513,  3508,  3503,  3498,
	3493,  3488,  3483,  3478,  3473,  3468,  3464,  3459,  3454,  3449,  3444,
	3440,  3435,  3430,  3426,  3421,  3416,  3412,  3407,  3402,  3398,  3393,
	3389,  3384,  3380,  3375,  3371,  3366,  3362,  3357,  3353,  3349,  3344,
	3340,  3336,  3331,  3327,  3323,  3318,  3314,  3310,  3306,  3302,  3297,
	3293,  3289,  3285,  3281,  3277,  3273,  3269,  3265,  3260,  3256,  3252,
	3248,  3244,  3240,  3237,  3233,  3229,  3225,  3221,  3217,  3213,  3209,
	3205,  3202,  3198,  3194,  3190,  3186,  3183,  3179,  3175,  3171,  3168,
	3164,  3160,  3157,  3153,  3149,  3146,  3142,  3139,  3135,  3131,  3128,
	3124,  3121,  3117,  3114,  3110,  3107,  3103,  3100,  3096,  3093,  3089,
	3086,  3083,  3079,  3076,  3072,  3069,  3066,  3062,  3059,  3056,  3052,
	3049,  3046,  3042,  3039,  3036,  3033,  3029,  3026,  3023,  3020,  3016,
	3013,  3010,  3007,  3004,  3001,  2998,  2994,  2991,  2988,  2985,  2982,
	2979,  2976,  2973,  2970,  2967,  2964,  2961,  2958,  2955,  2952,  2949,
	2946,  2943,  2940,  2937,  2934,  2931,  2928,  2925,  2922,  2919,  2916,
	2913,  2911,  2908,  2905,  2902,  2899,
};

/// `x >> 16` (mitad alta de 32 bits, como el `swap16` del original).
[[nodiscard]] constexpr s16 hi16(s32 x) {
	return static_cast<s16>(static_cast<u32>(x) >> 16);
}

/// Port de `UpdateFaceVisibility` (lib3d): back-face culling + color de luz por
/// cara. Escribe en `Face::flags` la luz (0..15) si la cara es visible, o -1 si
/// está oculta (salvo caras `material < 0`, de doble cara). Requiere
/// `update_object_transformation` previo (usa `object.camera`). Normaliza el
/// producto escalar con `kLightInvSqrt` (sin `sqrt`); el original subestima ~6.
inline void update_face_visibility(Object3D& object) {
	const s16 cx = object.camera.x;
	const s16 cy = object.camera.y;
	const s16 cz = object.camera.z;
	void* objdat = object.objdat;
	s16* group = object.faceGroups;
	s16 f;
	do {
		while ((f = *group++)) {
			Face* face = face3d(objdat, f);
			s16 px, py, pz;
			{
				const s16 i = face_indices(face)[0].vertex;
				const Point3D* p = point3d(objdat, i);
				px = static_cast<s16>(cx - p->x);
				py = static_cast<s16>(cy - p->y);
				pz = static_cast<s16>(cz - p->z);
			}
			s16* fn = face->normal;
			// `mul16`/`mulu16` (no `(s32)a * b`): fuerzan `muls.w`/`mulu.w` de 16 bits
			// y evitan `__mulsi3`. Producto punto + luz = ~7 muls por cara.
			s32 v = math2d::mul16(fn[0], px) + math2d::mul16(fn[1], py) + math2d::mul16(fn[2], pz);
			if (v >= 0) {
				s16 s = hi16(math2d::mul16(px, px) + math2d::mul16(py, py) + math2d::mul16(pz, pz));
				if (s > 511) s = 511;
				const s16 vv = hi16(v);
				face->flags = static_cast<s8>(math2d::mulu16(static_cast<u16>(vv), kLightInvSqrt[static_cast<u16>(s)]) >> 16);
			} else if (face->material < 0) {
				s16 s = hi16(math2d::mul16(px, px) + math2d::mul16(py, py) + math2d::mul16(pz, pz));
				if (s > 511) s = 511;
				const s16 vv = hi16(-v);
				face->flags = static_cast<s8>(math2d::mulu16(static_cast<u16>(vv), kLightInvSqrt[static_cast<u16>(s)]) >> 16);
			} else {
				face->flags = -1;
			}
		}
	} while (*group);
}

/// Port de `UpdateEdgeVisibilityConvex` (flatshade-convex): por cada cara visible
/// marca sus vértices (`Node3D::flags = 1`) y **XOR-ea** el color de luz
/// (`Face::flags`) en sus aristas. El XOR cancela las aristas compartidas por dos
/// caras visibles; quedan la silueta y las aristas internas visibles.
inline void update_edge_visibility_convex(Object3D& object) {
	const s8 s = 1;
	void* objdat = object.objdat;
	s16* group = object.faceGroups;
	s16 f;
	do {
		while ((f = *group++)) {
			Face* face = face3d(objdat, f);
			const s8 flags = face->flags;
			if (flags >= 0) {
				s16* index = reinterpret_cast<s16*>(face_indices(face));
				s16 vertices = static_cast<s16>(face->count - 3);
				s16 i;
				i = *index++; node3d(objdat, i)->flags = s;
				i = *index++; edge3d(objdat, i)->flags = static_cast<s8>(edge3d(objdat, i)->flags ^ flags);
				i = *index++; node3d(objdat, i)->flags = s;
				i = *index++; edge3d(objdat, i)->flags = static_cast<s8>(edge3d(objdat, i)->flags ^ flags);
				do {
					i = *index++; node3d(objdat, i)->flags = s;
					i = *index++; edge3d(objdat, i)->flags = static_cast<s8>(edge3d(objdat, i)->flags ^ flags);
				} while (--vertices != -1);
			}
		}
	} while (*group);
}

/// Port de `TransformVertices` (lib3d): transforma y proyecta (perspectiva con
/// `div16`) los vértices **marcados** (`Node3D::flags != 0`, que fija
/// `update_edge_visibility_convex`), dejando `(sx, sy, zp)` en `Node3D::vertex`.
/// `center_x`/`center_y` son el centro del viewport (el original usa WIDTH/2,
/// HEIGHT/2). Requiere `update_object_transformation` previo.
///
/// Los productos usan `mul16` (nunca `(s32)a * b`): con el cast a 32 bits el
/// compilador emite `__mulsi3` (mult. 32x32 por software, ~10x más cara) en vez de
/// `muls.w` de 16x16. Ver `OPTIMIZACION_GPP_68000.md`.
inline void transform_vertices(Object3D& object, s16 center_x, s16 center_y) {
	math3d::Mat3x3& M = object.objectToWorld;
	void* objdat = object.objdat;
	s16* group = object.vertexGroups;

	const s32 m0 = (static_cast<s32>(M.x) - math2d::normfx(static_cast<s32>(M.m00) * M.m01)) << 8;
	const s32 m1 = (static_cast<s32>(M.y) - math2d::normfx(static_cast<s32>(M.m10) * M.m11)) << 8;
	M.z = static_cast<s16>(M.z - math2d::normfx(static_cast<s32>(M.m20) * M.m21));

	do {
		s16 i;
		while ((i = *group++)) {
			Node3D* node = node3d(objdat, i);
			if (node->flags) {
				s16* pt = reinterpret_cast<s16*>(node);
				s16* v = reinterpret_cast<s16*>(&M);
				s16 x, y, z, zp;
				s32 xy, xp, yp;

				*pt++ = 0;
				x = *pt++;
				y = *pt++;
				z = *pt++;
				xy = math2d::mul16(x, y);

				// MULVERTEX1(xp, m0): ((t0*t1 + z*t2 - x*y) >> 4) + m0.
				{
					const s16 t0 = static_cast<s16>((*v++) + y);
					const s16 t1 = static_cast<s16>((*v++) + x);
					const s32 t2 = math2d::mul16(*v++, z);
					v++;
					xp = ((math2d::mul16(t0, t1) + t2 - xy) >> 4) + m0;
				}
				// MULVERTEX1(yp, m1).
				{
					const s16 t0 = static_cast<s16>((*v++) + y);
					const s16 t1 = static_cast<s16>((*v++) + x);
					const s32 t2 = math2d::mul16(*v++, z);
					v++;
					yp = ((math2d::mul16(t0, t1) + t2 - xy) >> 4) + m1;
				}
				// MULVERTEX2(zp): normfx(...) + t3.
				{
					const s16 t0 = static_cast<s16>((*v++) + y);
					const s16 t1 = static_cast<s16>((*v++) + x);
					const s32 t2 = math2d::mul16(*v++, z);
					const s16 t3 = *v++;
					zp = static_cast<s16>(math2d::normfx(math2d::mul16(t0, t1) + t2 - xy) + t3);
				}

				*pt++ = static_cast<s16>(math2d::div16(xp, zp) + center_x);
				*pt++ = static_cast<s16>(math2d::div16(yp, zp) + center_y);
				*pt++ = zp;
			}
		}
	} while (*group);
}

} // namespace eng::object3d
