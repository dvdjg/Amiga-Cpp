// Demo 116 - flatshade-convex (IMPORTE FIEL de demoscene-repo-orig/effects/flatshade-convex)
//
// Objeto CONVEXO `pilka` girando, con SOMBREADO PLANO: se calcula la luz de cada
// cara (`update_face_visibility`: producto escalar normal??vista normalizado con la
// tabla `kInvSqrt`, sin sqrt en runtime), la visibilidad de aristas del solido
// convexo (`update_edge_visibility_convex`: XOR de la luz de las caras adyacentes,
// que cancela las aristas internas y deja silueta + aristas visibles) y se dibujan
// esas aristas por Blitter (`blitter_line_eor`, ONEDOT+EOR) replicadas en cada plano
// segun el color; despues se rellena el hueco con `blitter_area_fill` (area fill
// XOR). El recorrido del object model (`Object3D` + grupos por offsets de byte) y la
// proyeccion (`transform_vertices`) son el port 1:1 de `lib3d`.
//
// Display y doble buffer: 256x256x4 con los registros del original
// (`SetupPlayfield(MODE_LORES,4,X(32),Y(0),256,256)`); doble buffer con swap de
// copperlist por frame.
#include <eng/core/math2d.hpp>
#include <eng/core/object3d.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};

// Perfilado por secciones: cabecera magica para localizarla por canal lateral y
// deltas de ciclos de CPU (contador del periferico de depuracion 0xB7E928).
// v[0]=clear v[1]=transform/culling v[2]=edges v[3]=fill v[4]=draw(edges+fill) v[5]=update total
struct EngProf {
	eng::u32 magic;
	eng::u32 v[16];
};
__attribute__((used)) volatile EngProf g_eng_prof { 0x50524f46u, {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u} };
}

// Argumentos de las rutinas asm de support/flatshade_asm.s. GLOBAL y `extern "C"`
// (fuera del namespace anonimo) para que el asm lo referencie, como g_fire_args.
extern "C" {
struct FlatShadeAsmArgs {
	void* obj;            // Object3D* (objdat=0, vertexGroups=4, edgeGroups=8, faceGroups=12, objectToWorld=38, camera=86)
	eng::u8* planes;      // base del buffer de bitplanes activo (fs_draw_edges)
	const eng::u16* invsqrt; // kInvSqrt[512]
	eng::s16* bbox;       // g_bbox[4] (bx0,bx1,by0,by1)
};
FlatShadeAsmArgs g_fs_args {};
void fs_update_face_visibility(void);
void fs_update_edge_visibility_convex(void);
void fs_transform_vertices(void);
void fs_draw_edges(void);
}

#ifndef K_FLATSHADE_ASM
// Rutinas calientes en ASM (support/flatshade_asm.s): port fiel del original con
// registros fijos, como fire_loop.s. POR DEFECTO 0 (version C++ canonica): la ruta
// asm alcanza READY (el crash de `g_fs_args` esta corregido) pero el render sale
// deformado (bug pendiente en `fs_transform_vertices`). Poner a 1 para experimentar.
#define K_FLATSHADE_ASM 0
#endif

namespace {
inline eng::u32 rcycles() {
	return *reinterpret_cast<volatile eng::u32*>(0xB7E928u);
}
}

// --- Datos del efecto (copiados TAL CUAL del demoscene) ----------------------
using eng::object3d::Mesh3D;
#define __data
using u_short = eng::u16;
#include "data/pilka.c"
#include "data/flatshade-pal.c"
#undef __data

namespace {

namespace obj = eng::object3d;
namespace copper = eng::copper;

// Geometria del original (256x256, 4 planos).
constexpr eng::u16 kWidth = 256;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 4;
constexpr eng::u16 kBytesPerRow = kWidth / 8; // 32
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kHeight;
constexpr eng::u8 kBuffers = 3;               // triple buffer (pipeline clear/fill solapado)
constexpr eng::u32 kBitmapBytes = kPlaneBytes * kPlanes * kBuffers;
constexpr eng::u32 kCopperPerList = 512;

// Registros del display del original (MOD_LORES, X(32), Y(0), 256x256), identicos
// al port del `wireframe` (mismos `SetupPlayfield`/`SetupBitplaneFetch`).
constexpr eng::u16 kDiwstrt = 0x2ca1;
constexpr eng::u16 kDiwstop = 0x2ca1;
constexpr eng::u16 kDdfstrt = 0x0048;
constexpr eng::u16 kDdfstop = 0x00c0;
constexpr eng::u16 kBplcon0 = 0x4000; // 4 planos, color
constexpr eng::u16 kBplcon1 = 0x0000;

/// Maximo de vertices por cara del modelo `pilka` (poligonos de hasta 8 lados).
constexpr eng::u16 kMaxFaceVerts = 8;

/// Bounding-box de pantalla del objeto (actualizada en `transform_vertices`); acota
/// el `area fill` a la zona del objeto en vez de barrer las 1024 lineas del bitmap.
/// `g_bbox[4]` = {bx0, bx1, by0, by1}; lo escribe tambien `fs_transform_vertices`
/// (rutina asm, via `g_fs_args.bbox`).
eng::s16 g_bbox[4] = {32767, -32768, 32767, -32768};

// Tabla de luz de lib3d (`UpdateFaceVisibility`): 65535/sqrt(x), x=0..511.
// Normaliza el producto escalar sin sqrt en runtime.
constexpr eng::u16 kInvSqrt[512] = {
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

/// `x >> 16` (como `swap16` del original: intercambia las dos mitades de 32 bits).
constexpr eng::s16 hi16(eng::s32 x) {
	return static_cast<eng::s16>(static_cast<eng::u32>(x) >> 16);
}

/// Port de `UpdateFaceVisibility` (lib3d): back-face culling + color de luz por
/// cara (0..15) normalizando el producto escalar con `kInvSqrt` (sin sqrt).
void update_face_visibility(obj::Object3D& object) {
	const eng::s16 cx = object.camera.x;
	const eng::s16 cy = object.camera.y;
	const eng::s16 cz = object.camera.z;
	void* objdat = object.objdat;
	eng::s16* group = object.faceGroups;
	eng::s16 f;
	do {
		while ((f = *group++)) {
			obj::Face* face = obj::face3d(objdat, f);
			eng::s16 px, py, pz;
			{
				const eng::s16 i = obj::face_indices(face)[0].vertex;
				const obj::Point3D* p = obj::point3d(objdat, i);
				px = static_cast<eng::s16>(cx - p->x);
				py = static_cast<eng::s16>(cy - p->y);
				pz = static_cast<eng::s16>(cz - p->z);
			}
			eng::s16* fn = face->normal;
			// `mul16`/`mulu16` (no `(s32)a * b`): fuerzan `muls.w`/`mulu.w` de 16 bits y
			// evitan `__mulsi3`. Producto punto + luz = ~7 muls por cara.
			eng::s32 v = eng::math2d::mul16(fn[0], px) +
				     eng::math2d::mul16(fn[1], py) +
				     eng::math2d::mul16(fn[2], pz);
			if (v >= 0) {
				// s = |v| en la parte alta (magnitud^2), clamp 511.
				eng::s16 s = hi16(eng::math2d::mul16(px, px) +
						  eng::math2d::mul16(py, py) +
						  eng::math2d::mul16(pz, pz));
				if (s > 511) s = 511;
				const eng::s16 vv = hi16(v);
				const eng::u32 res = eng::math2d::mulu16(static_cast<eng::u16>(static_cast<eng::s16>(vv)),
									 kInvSqrt[static_cast<eng::u16>(s)]) >> 16;
				face->flags = static_cast<eng::s8>(res);
			} else if (face->material < 0) {
				eng::s16 s = hi16(eng::math2d::mul16(px, px) +
						  eng::math2d::mul16(py, py) +
						  eng::math2d::mul16(pz, pz));
				if (s > 511) s = 511;
				const eng::s16 vv = hi16(-v);
				const eng::u32 res = eng::math2d::mulu16(static_cast<eng::u16>(static_cast<eng::s16>(vv)),
									 kInvSqrt[static_cast<eng::u16>(s)]) >> 16;
				face->flags = static_cast<eng::s8>(res);
			} else {
				face->flags = -1;
			}
		}
	} while (*group);
}

/// Port de `UpdateEdgeVisibilityConvex` (flatshade-convex): por cada cara visible
/// marca sus vertices y **XOR-ea** el color de luz (`face->flags`) en sus aristas.
/// El XOR cancela las aristas compartidas por dos caras visibles; quedan la silueta
/// y las aristas internas visibles (las que dibuja `draw_edges_area_fill`).
void update_edge_visibility_convex(obj::Object3D& object) {
	const eng::s8 s = 1;
	void* objdat = object.objdat;
	eng::s16* group = object.faceGroups;
	eng::s16 f;
	do {
		while ((f = *group++)) {
			obj::Face* face = obj::face3d(objdat, f);
			const eng::s8 flags = face->flags;
			if (flags >= 0) {
				eng::s16* index = reinterpret_cast<eng::s16*>(obj::face_indices(face));
				eng::s16 vertices = static_cast<eng::s16>(face->count - 3);
				eng::s16 i;
				i = *index++; obj::node3d(objdat, i)->flags = s;
				i = *index++; obj::edge3d(objdat, i)->flags = static_cast<eng::s8>(obj::edge3d(objdat, i)->flags ^ flags);
				i = *index++; obj::node3d(objdat, i)->flags = s;
				i = *index++; obj::edge3d(objdat, i)->flags = static_cast<eng::s8>(obj::edge3d(objdat, i)->flags ^ flags);
				do {
					i = *index++; obj::node3d(objdat, i)->flags = s;
					i = *index++; obj::edge3d(objdat, i)->flags = static_cast<eng::s8>(obj::edge3d(objdat, i)->flags ^ flags);
				} while (--vertices != -1);
			}
		}
	} while (*group);
}

// Port de los macros del original. `mul16` (y no `(s32)a * b`) es intencionado:
// con el cast a 32 bits el compilador emite `__mulsi3` (mult. 32x32 por software,
// ~10x mas cara) en vez de `muls.w` de 16x16. Ver `OPTIMIZACION_GPP_68000.md`.
#define MULVERTEX1(D, E) { \
	eng::s16 t0 = static_cast<eng::s16>((*v++) + y); \
	eng::s16 t1 = static_cast<eng::s16>((*v++) + x); \
	eng::s32 t2 = eng::math2d::mul16(*v++, z); \
	v++; \
	D = static_cast<eng::s32>(((eng::math2d::mul16(t0, t1) + t2 - xy) >> 4) + E); \
}

#define MULVERTEX2(D) { \
	eng::s16 t0 = static_cast<eng::s16>((*v++) + y); \
	eng::s16 t1 = static_cast<eng::s16>((*v++) + x); \
	eng::s32 t2 = eng::math2d::mul16(*v++, z); \
	eng::s16 t3 = *v++; \
	D = static_cast<eng::s32>(eng::math2d::normfx(eng::math2d::mul16(t0, t1) + t2 - xy)) + t3; \
}

/// Port de `TransformVertices`: transforma y proyecta (perspectiva con `div16`)
/// los vertices marcados, guardando (x,y,zp) en `Node3D::vertex`. Actualiza la
/// **bounding-box** de pantalla (g_bx0..g_by1) para acotar el area fill.
void transform_vertices(obj::Object3D& object) {
	eng::math3d::Mat3x3& M = object.objectToWorld;
	void* objdat = object.objdat;
	eng::s16* group = object.vertexGroups;

	eng::s32 m0 = (static_cast<eng::s32>(M.x) - eng::math2d::normfx(static_cast<eng::s32>(M.m00) * M.m01)) << 8;
	eng::s32 m1 = (static_cast<eng::s32>(M.y) - eng::math2d::normfx(static_cast<eng::s32>(M.m10) * M.m11)) << 8;
	M.z = static_cast<eng::s16>(M.z - eng::math2d::normfx(static_cast<eng::s32>(M.m20) * M.m21));

	g_bbox[0] = 32767; g_bbox[1] = -32768; g_bbox[2] = 32767; g_bbox[3] = -32768;
	do {
		eng::s16 i;
		while ((i = *group++)) {
			obj::Node3D* node = obj::node3d(objdat, i);
			if (node->flags) {
				eng::s16* pt = reinterpret_cast<eng::s16*>(node);
				eng::s16* v = reinterpret_cast<eng::s16*>(&M);
				eng::s16 x, y, z, zp;
				eng::s32 xy, xp, yp;

				*pt++ = 0;
				x = *pt++;
				y = *pt++;
				z = *pt++;
				// `mul16` (no `(s32)x * y`): evita un `__mulsi3` por vertice (era ~18k/frame).
				xy = eng::math2d::mul16(x, y);

				MULVERTEX1(xp, m0);
				MULVERTEX1(yp, m1);
				MULVERTEX2(zp);

				const eng::s16 sx = static_cast<eng::s16>(eng::math2d::div16(xp, zp) + kWidth / 2);
				const eng::s16 sy = static_cast<eng::s16>(eng::math2d::div16(yp, zp) + kHeight / 2);
				*pt++ = sx;
				*pt++ = sy;
				*pt++ = zp;

				if (sx < g_bbox[0]) g_bbox[0] = sx;
				if (sx > g_bbox[1]) g_bbox[1] = sx;
				if (sy < g_bbox[2]) g_bbox[2] = sy;
				if (sy > g_bbox[3]) g_bbox[3] = sy;
			}
		}
	} while (*group);
}

/// Ruta ALTERNATIVA (ruta B, `-DFLATSHADE_FAITHFUL=0`): rellena cada cara VISIBLE
/// (poligono proyectado) con su color de luz mediante `blitter_fill_polygon`
/// (mascara + contorno ONEDOT + area fill inclusivo + cookie-cut). Da caras solidas
/// sin depender de la paridad del area fill XOR, a cambio de mas blits por cara.
void draw_faces(obj::Object3D& object, eng::PlaneBytes planes, eng::amiga::MinimalBackend& backend,
		eng::MaskBuffer mask) {
	void* objdat = object.objdat;
	eng::s16* group = object.faceGroups;
	eng::s16 xs[kMaxFaceVerts];
	eng::s16 ys[kMaxFaceVerts];
	do {
		eng::s16 f;
		while ((f = *group++)) {
			obj::Face* face = obj::face3d(objdat, f);
			if (face->flags < 0 || face->count < 3 || face->count > kMaxFaceVerts) {
				continue;
			}
			const obj::FaceIndex* idx = obj::face_indices(face);
			for (eng::s16 k = 0; k < face->count; ++k) {
				const obj::Point3D* v = obj::vertex3d(objdat, idx[k].vertex);
				xs[k] = v->x;
				ys[k] = v->y;
			}
			backend.blitter_fill_polygon(planes, kPlanes, kBytesPerRow, kPlaneBytes,
						     xs, ys, static_cast<eng::u8>(face->count),
						     static_cast<eng::u8>(face->flags), mask);
		}
	} while (*group);
}

// Seleccion de ruta y perfilado por secciones (todo por defecto a 0/1).
//   -DFLATSHADE_FAITHFUL=0  -> ruta alternativa por cara (blitter_fill_polygon).
//   -DFLATSHADE_LINE_OR=1   -> lineas OR en vez de EOR (solo ruta fiel).
//   -DFLATSHADE_SKIP_CLEAR/EDGES/FILL=1 -> omite esa seccion (perfilar por diferencia).
#ifndef FLATSHADE_FAITHFUL
#define FLATSHADE_FAITHFUL 1
#endif
#ifndef FLATSHADE_LINE_OR
#define FLATSHADE_LINE_OR 0
#endif
#ifndef FLATSHADE_SKIP_CLEAR
#define FLATSHADE_SKIP_CLEAR 0
#endif
#ifndef FLATSHADE_SKIP_EDGES
#define FLATSHADE_SKIP_EDGES 0
#endif
#ifndef FLATSHADE_SKIP_FILL
#define FLATSHADE_SKIP_FILL 0
#endif
#ifndef FLATSHADE_FILL_BBOX
#define FLATSHADE_FILL_BBOX 0
#endif
#ifndef FLATSHADE_PROFILE
// Metricas extra por arista (longitud total) para el perfilado; fuera de la build normal.
#define FLATSHADE_PROFILE 0
#endif
#ifdef FLATSHADE_SKIP_ALL
#undef FLATSHADE_SKIP_CLEAR
#undef FLATSHADE_SKIP_EDGES
#undef FLATSHADE_SKIP_FILL
#define FLATSHADE_SKIP_CLEAR 1
#define FLATSHADE_SKIP_EDGES 1
#define FLATSHADE_SKIP_FILL 1
#endif

/// Dibuja las aristas VISIBLES (`edgeColor > 0`) con `blitter_line_eor` (ONEDOT+EOR)
/// replicadas en cada plano seg??n el color de arista (camino FIEL del original
/// `DrawObject`). `edge->flags > 0` => arista visible; se limpia tras dibujarla para
/// que el XOR del siguiente frame parta de cero. Las aristas con `edgeColor == 0`
/// (canceladas por dos caras visibles de igual luz) se saltan. `bltdpt` apunta a la
/// BASE del bitmap (`planes.data()`, como el original), NO a la direccion calculada:
/// en modo linea el primer pixel va por D, y dejarlo siempre en la base mantiene la
/// paridad par/impar del contorno correcta en los v??rtices (sin ello el area fill
/// filtra una raya horizontal por v??rtice). NO rellena: el fill se lanza aparte
/// (`area_fill_planes`) para poder solaparlo con el transform del frame siguiente.
void draw_edges(obj::Object3D& object, eng::PlaneBytes planes,
		eng::amiga::MinimalBackend& backend) {
	const eng::u32 t0 = rcycles();
	eng::u32 n_edges = 0u;
	eng::u32 n_lines = 0u;
	eng::u32 px_total = 0u;
	void* objdat = object.objdat;
	eng::s16* group = object.edgeGroups;
	eng::s16 e;
#if !FLATSHADE_SKIP_EDGES
	// Setup com??n del modo l??nea EOR (ONEDOT) UNA vez por frame, como el preludio de
	// `DrawObject` del original (`bltafwm/alwm=-1, bltadat=0x8000, bltbdat=0xffff,
	// bltcmod/bltdmod=WIDTH/8`). `blitter_line_eor_continue` solo reprograma los 8
	// registros de cada arista/plano (macro `DRAWLINE`). No espera aqu??: el primer
	// `continue` sincroniza con el clear.
	backend.blitter_lines_eor_begin(kBytesPerRow);
	do {
		while ((e = *group++)) {
			obj::Edge* edge = obj::edge3d(objdat, e);
			const eng::s8 edgeColor = edge->flags;
			if (edgeColor > 0) {
				++n_edges;
				edge->flags = 0;
				const obj::Point3D* a = obj::vertex3d(objdat, edge->point[0]);
				const obj::Point3D* b = obj::vertex3d(objdat, edge->point[1]);
				eng::s16 x0 = a->x;
				eng::s16 y0 = a->y;
				eng::s16 x1 = b->x;
				eng::s16 y1 = b->y;
				if (y0 == y1) {
					continue;
				}
				if (y0 > y1) {
					eng::s16 t = x0; x0 = x1; x1 = t;
					t = y0; y0 = y1; y1 = t;
				}
#if FLATSHADE_PROFILE
				// Solo diagnostico (perfilado): fuera del bucle caliente en la build normal.
				{
					const eng::s16 dx = static_cast<eng::s16>(x1 - x0);
					const eng::s16 dy = static_cast<eng::s16>(y1 - y0);
					px_total += static_cast<eng::u32>(dx > dy ? dx : dy);
				}
#endif
#if FLATSHADE_LINE_OR
				for (eng::u8 p = 0; p < kPlanes; ++p) {
					if ((edgeColor & (1 << p)) != 0) {
						++n_lines;
						backend.blitter_line(planes.subspan(
							static_cast<eng::u32>(p) * kPlaneBytes, kPlaneBytes),
							kBytesPerRow, x0, y0, x1, y1);
					}
				}
#else
				// Par??metros Bresenham calculados UNA vez por arista (independientes
				// del plano) y reutilizados en los N planos del color, como el original
				// (avanza `bltcpt += plane_bytes` sin recalcular el octante).
				eng::amiga::MinimalBackend::LineEorParams line;
				if (backend.blitter_line_eor_prepare(line, kBytesPerRow, x0, y0, x1, y1)) {
					for (eng::u8 p = 0; p < kPlanes; ++p) {
						if ((edgeColor & (1 << p)) != 0) {
							++n_lines;
							backend.blitter_line_eor_draw(line, planes.data() +
										       static_cast<eng::u32>(p) * kPlaneBytes,
										       planes.data());
						}
					}
				}
#endif
			}
		}
	} while (*group);
#else
	(void)group; (void)e; (void)objdat;
#endif
	const eng::u32 t1 = rcycles();
	g_eng_prof.v[10] = n_edges;
	g_eng_prof.v[11] = n_lines;
	g_eng_prof.v[12] = px_total;
	g_eng_prof.v[2] = t1 - t0;
}

/// Lanza (o espera) el area fill `XOR` sobre `planes`. `wait=false` lo lanza sin
/// esperarlo: el buffer no se muestra hasta el swap del siguiente update, as?? que el
/// wait final se absorbe en la espera de VBlank del engine (o en el primer wait del
/// update siguiente), igual que el `WaitBlitter` pegado a `TaskWaitVBlank` del
/// original. Con `FLATSHADE_FILL_BBOX` se acota a la bounding-box del objeto.
void area_fill_planes(eng::PlaneBytes planes, eng::amiga::MinimalBackend& backend, bool wait) {
	const eng::u32 t0 = rcycles();
#if !FLATSHADE_SKIP_FILL
#if FLATSHADE_FILL_BBOX
	if (g_bbox[1] >= g_bbox[0] && g_bbox[3] >= g_bbox[2]) {
		eng::s16 x0 = static_cast<eng::s16>(g_bbox[0] - 2); if (x0 < 0) x0 = 0;
		eng::s16 y0 = static_cast<eng::s16>(g_bbox[2] - 2); if (y0 < 0) y0 = 0;
		eng::s16 x1 = static_cast<eng::s16>(g_bbox[1] + 2); if (x1 > 255) x1 = 255;
		eng::s16 y1 = static_cast<eng::s16>(g_bbox[3] + 2); if (y1 > 255) y1 = 255;
		const eng::u16 wx0 = static_cast<eng::u16>(x0) & 0xfff0u;
		const eng::u16 wx1 = static_cast<eng::u16>(x1) | 0x000fu;
		const eng::u16 words = static_cast<eng::u16>((static_cast<eng::u16>(wx1 - wx0) + 16u) >> 4);
		const eng::u16 rows = static_cast<eng::u16>(y1 - y0 + 1);
		for (eng::u8 p = 0; p < kPlanes; ++p) {
			backend.blitter_area_fill_rect(
				planes.subspan(static_cast<eng::u32>(p) * kPlaneBytes, kPlaneBytes),
				kBytesPerRow, wx0, y0, words, rows, wait || static_cast<eng::u8>(p + 1u) == kPlanes);
		}
	}
#else
	// El area fill XOR se lanza sobre el contorno (`draw_edges`). El buffer se muestra
	// en el pr??ximo swap; `wait=false` deja que el wait final lo absorba la espera de
	// VBlank o el primer wait del update siguiente (el buffer nunca se ve a medias).
	backend.blitter_area_fill(planes, kPlanes, kBytesPerRow, kPlaneBytes, kWidth, kHeight, wait);
#endif
#endif
	const eng::u32 t1 = rcycles();
	g_eng_prof.v[3] = t1 - t0;
	g_eng_prof.v[4] = t1 - t0;
}

#if K_FLATSHADE_ASM
/// Rellena `g_fs_args` para las rutinas asm. `obj`/`invsqrt`/`bbox` son constantes;
/// `planes` cambia por frame (buffer activo de los edges).
inline void prepare_fs_args(eng::PlaneBytes planes, obj::Object3D& object) {
	g_fs_args.obj = &object;
	g_fs_args.planes = planes.data();
	g_fs_args.invsqrt = kInvSqrt;
	g_fs_args.bbox = g_bbox;
}
#endif

struct FlatShadeDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({112u * 1024u, 4u * 1024u, 4u * 1024u});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011601u);
			return;
		}

		m_bitplane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kBitmapBytes, 16);
		m_copper_block = backend.memory().chip.allocate_block<eng::CopperTag>(kBuffers * kCopperPerList, 16);
		m_mask_block = backend.memory().chip.allocate_block<eng::MaskTag>(kPlaneBytes, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_mask_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011602u);
			return;
		}

		for (eng::u8 b = 0; b < kBuffers; ++b) {
			if (!build_copper(b)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00011603u);
				return;
			}
		}
		backend.takeover_display(m_copper_ptrs[0]);

		// El original activa `DMAF_BLITHOG` (BLTPRI): el Blitter no cede slots de bus a
		// la CPU durante el fill/lines. Con la pipeline, el transform corre durante el
		// fill; si el emulador congela la CPU con BLITHOG, este override se desactiva
		// en la build (ver bitacora). Se deja activo para medir ambos efectos.
		backend.set_blitter_priority(true);

		obj::new_object3d(m_object, pilka);
		m_object.translate.z = static_cast<eng::s16>(-4000); // fx4i(-250)

		// Pipeline de doble/triple buffer: el estado del objeto para el primer dibujo se
		// precalcula aqu?? (lo que en `update` ocurre durante el fill del frame previo).
		m_angle = 0;
		m_object.rotate.x = m_object.rotate.y = m_object.rotate.z = m_angle;
		obj::update_object_transformation(m_object);
#if K_FLATSHADE_ASM
		prepare_fs_args(planes_of(0), m_object);
		fs_update_face_visibility();
		fs_update_edge_visibility_convex();
		fs_transform_vertices();
#else
		update_face_visibility(m_object);
		update_edge_visibility_convex(m_object);
		transform_vertices(m_object);
#endif

		// Pre-clear todos los buffers: el primer `update` dibuja sobre el 1?? sin esperar.
		for (eng::u8 b = 0; b < kBuffers; ++b) {
			backend.blitter_clear(planes_of(b), kPlanes, kBytesPerRow, kPlaneBytes, kWidth, kHeight, true);
		}
		m_draw_buf = 0;
		m_display_buf = static_cast<eng::u8>(kBuffers - 1u); // el takeover muestra el 0

		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(pilka.faces));
	}

	/// Pipeline de 3 buffers: el `update` dibuja sobre `m_draw_buf` (ya pre-clearado),
	/// lanza su fill sin esperarlo y, MIENTRAS el Blitter lo hace, precalcula el estado
	/// (transform/culling/luz) del frame siguiente y pre-cleara el buffer que ese frame
	/// usar??. Con 3 buffers el buffer a limpiar ni se muestra ni se dibuja, as?? que el
	/// clear queda escondido bajo el fill. El wait del fill se absorbe en la espera de
	/// VBlank del engine o en el primer wait del siguiente update: el buffer nunca se ve
	/// a medias (se muestra en el swap del update siguiente).
	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (m_bitplane_block.view.data() == nullptr) {
			return;
		}

		const eng::u8 buf = m_draw_buf;
		const eng::u8 show = m_display_buf;
		eng::PlaneBytes planes = planes_of(buf);

		const eng::u32 t0 = rcycles();

		// 1) Esperar el fill del buffer que vamos a mostrar (lanzado en el update
		//    anterior); la espera de VBlank ya lo ha absorbido casi siempre.
		backend.wait_blitter();

		// 2) Swap: mostrar el buffer dibujado (y rellenado) el frame pasado.
		backend.install_copper_list(m_copper_ptrs[show]);

		// 3) Contorno sobre el buffer pre-clearado (estado del objeto ya precalculado).
#if FLATSHADE_FAITHFUL
#if K_FLATSHADE_ASM
		prepare_fs_args(planes, m_object);
		fs_draw_edges();
#else
		draw_edges(m_object, planes, backend);
#endif
#else
		draw_faces(m_object, planes, backend, m_mask_block.view);
#endif
		const eng::u32 t1 = rcycles();

		// 4) Lanzar el fill SIN esperarlo: se mostrar?? en el swap del pr??ximo update.
		area_fill_planes(planes, backend, false);

		// 5) Durante el fill (Blitter ocupado), precalcular el estado del frame siguiente
		//    (transform + luz + visibilidad de aristas) sobre el object model.
		m_angle = static_cast<eng::s16>(m_angle + 8);
		m_object.rotate.x = m_object.rotate.y = m_object.rotate.z = m_angle;
		obj::update_object_transformation(m_object);
		const eng::u32 ta = rcycles();
#if K_FLATSHADE_ASM
		prepare_fs_args(planes, m_object);
		fs_update_face_visibility();
		const eng::u32 tb = rcycles();
		fs_update_edge_visibility_convex();
		const eng::u32 tc = rcycles();
		fs_transform_vertices();
#else
		update_face_visibility(m_object);
		const eng::u32 tb = rcycles();
		update_edge_visibility_convex(m_object);
		const eng::u32 tc = rcycles();
		transform_vertices(m_object);
#endif
		const eng::u32 t2 = rcycles();
		g_eng_prof.v[6] = ta - t1; // update_object_transformation
		g_eng_prof.v[7] = tb - ta; // update_face_visibility
		g_eng_prof.v[8] = tc - tb; // update_edge_visibility_convex
		g_eng_prof.v[9] = t2 - tc; // transform_vertices

		// 6) Pre-clear del buffer que usar?? el PR??XIMO update (`buf+1`: ni en pantalla
		//    ni en dibujo ahora, el mostrado es `buf-1`): se lanza sin esperar y queda
		//    colgado tras el fill.
#if !FLATSHADE_SKIP_CLEAR
		const eng::u8 next_buf = static_cast<eng::u8>((buf + 1u) % kBuffers);
		backend.blitter_clear(planes_of(next_buf), kPlanes, kBytesPerRow, kPlaneBytes, kWidth, kHeight, false);
#endif

		const eng::u32 t3 = rcycles();

		g_eng_prof.v[0] = t1 - t0;
		g_eng_prof.v[1] = t2 - t1;
		g_eng_prof.v[5] = t3 - t0;

		m_display_buf = buf;
		m_draw_buf = static_cast<eng::u8>((buf + 1u) % kBuffers);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	eng::PlaneBytes planes_of(eng::u8 buf) const {
		return m_bitplane_block.view.subspan(
			static_cast<eng::u32>(buf) * kPlanes * kPlaneBytes, kPlanes * kPlaneBytes);
	}

	bool build_copper(eng::u8 buf) {
		const eng::Bytes<eng::CopperTag> slice = m_copper_block.view.subspan(
			static_cast<eng::u32>(buf) * kCopperPerList, kCopperPerList);
		copper::Scheduler sched { eng::Block<eng::CopperTag> { slice, m_copper_block.kind } };
		const eng::PlaneBytes planes = planes_of(buf);
		sched.emit_planes_display(kDiwstrt, kDiwstop, kDdfstrt, kDdfstop, kBytesPerRow,
					  kBplcon0, kPlanes, planes, kPlaneBytes);
		sched.move(copper::Register::BPLCON1, kBplcon1);
		sched.emit_palette(eng::PaletteWords { flatshade_colors, 16u }, 0, 16);
		sched.end();
		m_copper_ptrs[buf] = sched.data();
		return sched.ok();
	}

	bool m_memory_ok = false;
	eng::u8 m_draw_buf = 0;
	eng::u8 m_display_buf = 0;
	eng::s16 m_angle = 0;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::MaskTag> m_mask_block {};
	const eng::u16* m_copper_ptrs[kBuffers] = {nullptr, nullptr, nullptr};
	eng::object3d::Object3D m_object {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	FlatShadeDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}

