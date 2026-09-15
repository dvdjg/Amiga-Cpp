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
/// Formato numérico: 4.12 (ver `math2d`). Los ángulos son índices 0..4095.
///
/// ## Perfil de coste (medido en la demo 116, WinUAE-DBG, 68000, -O1)
/// Con ~60 vértices y ~32 caras por frame, el reparto del `update` es:
/// - `transform_vertices` ~57k ciclos (**el mayor**: ~9 `mul16` + 2 `div16` por
///   vértice, o sea ~6 `muls.w` + 2 `divs.w` + carga de matriz).
/// - `update_face_visibility` ~19k (producto punto + luz: ~7 muls por cara).
/// - `update_object_transformation` ~21k (la matriz Rx·Ry·Rz + su inversa).
/// - `update_edge_visibility_convex` ~11k (recorrido puro de índices, sin muls).
///
/// ## Qué genera g++ y qué sería el ideal en asm
/// - **`mul16(b, a)` / `mulu16`** (`math2d`): `muls.w`/`mulu.w` nativos. Si se
///   escribe `(s32)a * b` con operandos `s32`, g++ emite `__mulsi3` (multiplicación
///   32×32 por software, ~10× más cara). Por eso TODO producto 16×16 del camino
///   caliente debe pasar por `mul16`/`mulu16`.
/// - **`div16(a, b)`** (`math2d`): `divs.w` nativo (cociente 16 bits en la palabra
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
/// de estos hallazgos y `demos/amiga/116_flatshade_convex/README.md` para el port.

#include <eng/core/math2d.hpp>
#include <eng/core/math3d.hpp>
#include <eng/core/object3d.hpp>
#include <eng/core/types.hpp>

namespace eng::lib3d {

using eng::s16;
using eng::s32;
using eng::s8;
using eng::u16;
using eng::u32;
using object3d::Object3D;
using object3d::Point3D;

/// `x >> 16`: parte alta de un 32 bits como `s16` (el `swap16` del original).
constexpr s16 hi16(s32 x) {
	return static_cast<s16>(static_cast<u32>(x) >> 16);
}

/// Tabla `InvSqrt` de lib3d (`invsqrt` en el original): `65535 / sqrt(x)` para
/// `x = 0..511`, en `u16` con formato **0.16** (`1.0 == 1 << 16`, truncado a
/// `65535`). Normaliza la iluminación: el producto escalar
/// normal·vista se eleva al cuadrado en la parte alta y se indexa aquí para
/// obtener el color de luz 0..15 **sin calcular `sqrt` en runtime** (es el
/// `65535/sqrt(x)` precalculado; ver `README.md` de la demo 116).
inline constexpr u16 kInvSqrt[512] = {
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

/// Port de `UpdateFaceVisibility`: back-face culling + color de luz por cara
/// (0..15). `object.camera` debe estar ya en espacio objeto (lo calcula
/// `object3d::update_object_transformation`). Caras ocultas quedan a `-1`
/// (salvo `material < 0`, que usa la luz invertida, para doble cara).
///
/// Coste: producto punto + magnitud² + luz ≈ 7 `mul16`/`mulu16` por cara.
/// No usa `sqrt`: la magnitud² (parte alta) indexa `kInvSqrt`.
inline void update_face_visibility(Object3D& object) {
	const s16 cx = object.camera.x;
	const s16 cy = object.camera.y;
	const s16 cz = object.camera.z;
	void* objdat = object.objdat;
	s16* group = object.faceGroups;
	s16 f;
	do {
		while ((f = *group++)) {
			object3d::Face* face = object3d::face3d(objdat, f);
			s16 px, py, pz;
			{
				const s16 i = object3d::face_indices(face)[0].vertex;
				const Point3D* p = object3d::point3d(objdat, i);
				px = static_cast<s16>(cx - p->x);
				py = static_cast<s16>(cy - p->y);
				pz = static_cast<s16>(cz - p->z);
			}
			s16* fn = face->normal;
			// `mul16` (no `(s32)a * b`): fuerza `muls.w` de 16×16 y evita `__mulsi3`.
			s32 v = math2d::mul16(fn[0], px) + math2d::mul16(fn[1], py) + math2d::mul16(fn[2], pz);
			if (v >= 0) {
				s16 s = hi16(math2d::mul16(px, px) + math2d::mul16(py, py) + math2d::mul16(pz, pz));
				if (s > 511) s = 511;
				const s16 vv = hi16(v);
				const u32 res = math2d::mulu16(static_cast<u16>(static_cast<s16>(vv)),
							       kInvSqrt[static_cast<u16>(s)]) >> 16;
				face->flags = static_cast<s8>(res);
			} else if (face->material < 0) {
				s16 s = hi16(math2d::mul16(px, px) + math2d::mul16(py, py) + math2d::mul16(pz, pz));
				if (s > 511) s = 511;
				const s16 vv = hi16(-v);
				const u32 res = math2d::mulu16(static_cast<u16>(static_cast<s16>(vv)),
							       kInvSqrt[static_cast<u16>(s)]) >> 16;
				face->flags = static_cast<s8>(res);
			} else {
				face->flags = -1;
			}
		}
	} while (*group);
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
	void* objdat = object.objdat;
	s16* group = object.faceGroups;
	s16 f;
	do {
		while ((f = *group++)) {
			object3d::Face* face = object3d::face3d(objdat, f);
			const s8 flags = face->flags;
			if (flags >= 0) {
				s16* index = reinterpret_cast<s16*>(object3d::face_indices(face));
				s16 vertices = static_cast<s16>(face->count - 3);
				s16 i;
				i = *index++; object3d::node3d(objdat, i)->flags = s;
				i = *index++; object3d::edge3d(objdat, i)->flags = static_cast<s8>(object3d::edge3d(objdat, i)->flags ^ flags);
				i = *index++; object3d::node3d(objdat, i)->flags = s;
				i = *index++; object3d::edge3d(objdat, i)->flags = static_cast<s8>(object3d::edge3d(objdat, i)->flags ^ flags);
				do {
					i = *index++; object3d::node3d(objdat, i)->flags = s;
					i = *index++; object3d::edge3d(objdat, i)->flags = static_cast<s8>(object3d::edge3d(objdat, i)->flags ^ flags);
				} while (--vertices != -1);
			}
		}
	} while (*group);
}

namespace detail {
/// `TransformVertices` del original, pero **indexando la matriz por campos** en vez de
/// recorrerla como 12 shorts con `reinterpret_cast`. g++ genera el MISMO código (mismos
/// offsets), así que se gana seguridad sin coste. `mul16` (y no `(s32)a * b`) es
/// intencionado: con el cast a 32 bits g++ emite `__mulsi3` en vez de `muls.w`.
#define ENG_LIB3D_MULVERTEX1(D, E, c0, c1, c2) { \
	eng::s16 t0 = static_cast<eng::s16>((c0) + y); \
	eng::s16 t1 = static_cast<eng::s16>((c1) + x); \
	eng::s32 t2 = eng::math2d::mul16(c2, z); \
	D = static_cast<eng::s32>(((eng::math2d::mul16(t0, t1) + t2 - xy) >> 4) + E); \
}
#define ENG_LIB3D_MULVERTEX2(D, c0, c1, c2, c3) { \
	eng::s16 t0 = static_cast<eng::s16>((c0) + y); \
	eng::s16 t1 = static_cast<eng::s16>((c1) + x); \
	eng::s32 t2 = eng::math2d::mul16(c2, z); \
	D = static_cast<eng::s32>(eng::math2d::normfx(eng::math2d::mul16(t0, t1) + t2 - xy)) + (c3); \
}
} // namespace detail

/// Port de `TransformVertices`: transforma y proyecta (perspectiva con `div16`)
/// los vértices marcados por `update_edge_visibility_convex` (o el que ponga
/// `node->flags`), guardando `(x, y, zp)` en `Node3D::vertex`. Los no marcados se
/// dejan tal cual.
///
/// Escribe en `bbox[4] = {minX, maxX, minY, maxY}` la caja de pantalla de los
/// vértices transformados (la usa el llamador para acotar el relleno); pásale un
/// buffer del llamador. `half_w`/`half_h` son el centro de proyección (mitad del
/// viewport).
///
/// Coste: **el mayor del efecto**: ~9 `mul16` + 2 `div16` por vértice (~6 `muls.w`
/// + 2 `divs.w` más la carga de la matriz). En asm el ideal es la matriz en
/// registros y un `muls.w`/`divs.w` por operación, sin recargar `objdat`.
inline void transform_vertices(Object3D& object, s16 half_w, s16 half_h, s16 bbox[4]) {
	math3d::Affine3& M = object.objectToWorld;
	void* objdat = object.objdat;
	s16* group = object.vertexGroups;

	// m0/m1 son el termino `xy` de `MULVERTEX1` preescalado por 256 (16.8): el
	// macro le resta `xy` a un producto 8.24 y hace `>> 4` para volver a 4.12.
	// Se pasa a `mul16` (no `(s32)*(s16)`) para forzar `muls.w` y evitar `__mulsi3`.
	s32 m0 = (static_cast<s32>(M.t.v[0].v) - math2d::normfx(math2d::mul16(M.m.m[0][0].v, M.m.m[0][1].v))) << 8;
	s32 m1 = (static_cast<s32>(M.t.v[1].v) - math2d::normfx(math2d::mul16(M.m.m[1][0].v, M.m.m[1][1].v))) << 8;
	M.t.v[2] = eng::math::q0 {static_cast<s16>(M.t.v[2].v - math2d::normfx(math2d::mul16(M.m.m[2][0].v, M.m.m[2][1].v)))};

	bbox[0] = 32767; bbox[1] = -32768; bbox[2] = 32767; bbox[3] = -32768;
	do {
		s16 i;
		while ((i = *group++)) {
			object3d::Node3D* node = object3d::node3d(objdat, i);
			if (node->flags) {
				s16* pt = reinterpret_cast<s16*>(node);
				s16 x, y, z, zp;
				s32 xy, xp, yp;

				*pt++ = 0;
				x = *pt++;
				y = *pt++;
				z = *pt++;
				xy = math2d::mul16(x, y);

				ENG_LIB3D_MULVERTEX1(xp, m0, M.m.m[0][0].v, M.m.m[0][1].v, M.m.m[0][2].v);
				ENG_LIB3D_MULVERTEX1(yp, m1, M.m.m[1][0].v, M.m.m[1][1].v, M.m.m[1][2].v);
				ENG_LIB3D_MULVERTEX2(zp, M.m.m[2][0].v, M.m.m[2][1].v, M.m.m[2][2].v, M.t.v[2].v);

				const s16 sx = static_cast<s16>(math2d::div16(xp, zp) + half_w);
				const s16 sy = static_cast<s16>(math2d::div16(yp, zp) + half_h);
				*pt++ = sx;
				*pt++ = sy;
				*pt++ = zp;

				if (sx < bbox[0]) bbox[0] = sx;
				if (sx > bbox[1]) bbox[1] = sx;
				if (sy < bbox[2]) bbox[2] = sy;
				if (sy > bbox[3]) bbox[3] = sy;
			}
		}
	} while (*group);
}

} // namespace eng::lib3d
