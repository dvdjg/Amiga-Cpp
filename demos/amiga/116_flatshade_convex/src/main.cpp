// Demo 116 - flatshade-convex (IMPORTE de demoscene-repo-orig/effects/flatshade-convex)
//
// Objeto CONVEXO `pilka` girando, con SOMBREADO PLANO por cara: cada cara visible
// se rellena con su color de luz (0..15) calculado por `UpdateFaceVisibility` de
// lib3d (producto escalar normal·vista normalizado con la tabla `InvSqrt`, sin
// sqrt en runtime), y el relleno se materializa con el Blitter via
// `MinimalBackend::fill_triangles_blitter` (mascara 1 bit + area fill + cookie-cut
// por plano), reutilizando el camino ya probado en la demo 078.
//
// Diferencias con el original (documentadas): el original dibujaba las ARISTAS
// visibles (visibilidad convexa por XOR) y rellenaba con un area-fill XOR; aqui se
// rellena por CARA (fan-triangulada) con el color de luz, que es el mismo modelo
// de sombreado plano y reutiliza el Blitter del engine. El recorrido del object
// model (`Object3D` + grupos por offsets de byte) es el port 1:1 de `lib3d`.
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
constexpr eng::u8 kBuffers = 2;               // doble buffer
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

constexpr eng::u16 kMaxTris = 128;

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
			eng::s32 v = static_cast<eng::s32>(fn[0]) * px +
				     static_cast<eng::s32>(fn[1]) * py +
				     static_cast<eng::s32>(fn[2]) * pz;
			if (v >= 0) {
				// s = |v| en la parte alta (magnitud^2), clamp 511.
				eng::s16 s = hi16(static_cast<eng::s32>(px) * px +
						  static_cast<eng::s32>(py) * py +
						  static_cast<eng::s32>(pz) * pz);
				if (s > 511) s = 511;
				const eng::s16 vv = hi16(v);
				const eng::u32 res = (static_cast<eng::u32>(static_cast<eng::s16>(vv)) *
						      static_cast<eng::u32>(kInvSqrt[static_cast<eng::u16>(s)])) >> 16;
				face->flags = static_cast<eng::s8>(res);
			} else if (face->material < 0) {
				eng::s16 s = hi16(static_cast<eng::s32>(px) * px +
						  static_cast<eng::s32>(py) * py +
						  static_cast<eng::s32>(pz) * pz);
				if (s > 511) s = 511;
				const eng::s16 vv = hi16(-v);
				const eng::u32 res = (static_cast<eng::u32>(static_cast<eng::s16>(vv)) *
						      static_cast<eng::u32>(kInvSqrt[static_cast<eng::u16>(s)])) >> 16;
				face->flags = static_cast<eng::s8>(res);
			} else {
				face->flags = -1;
			}
		}
	} while (*group);
}

/// Marca los vertices que pertenecen a alguna cara visible (port del bucle de
/// `UpdateEdgeVisibilityConvex`, sin la parte de aristas). `transform_vertices`
/// proyecta solo los vertices marcados.
void mark_visible_vertices(obj::Object3D& object) {
	void* objdat = object.objdat;
	eng::s16* group = object.faceGroups;
	eng::s16 f;
	do {
		while ((f = *group++)) {
			obj::Face* face = obj::face3d(objdat, f);
			if (face->flags >= 0) {
				const obj::FaceIndex* idx = obj::face_indices(face);
				for (eng::s16 k = 0; k < face->count; ++k) {
					obj::node3d(objdat, idx[k].vertex)->flags = 1;
				}
			}
		}
	} while (*group);
}

#define MULVERTEX1(D, E) { \
	eng::s16 t0 = static_cast<eng::s16>((*v++) + y); \
	eng::s16 t1 = static_cast<eng::s16>((*v++) + x); \
	eng::s32 t2 = static_cast<eng::s32>(*v++) * z; \
	v++; \
	D = static_cast<eng::s32>(((static_cast<eng::s32>(t0) * t1 + t2 - xy) >> 4) + E); \
}

#define MULVERTEX2(D) { \
	eng::s16 t0 = static_cast<eng::s16>((*v++) + y); \
	eng::s16 t1 = static_cast<eng::s16>((*v++) + x); \
	eng::s32 t2 = static_cast<eng::s32>(*v++) * z; \
	eng::s16 t3 = *v++; \
	D = static_cast<eng::s32>(eng::math2d::normfx(static_cast<eng::s32>(t0) * t1 + t2 - xy)) + t3; \
}

/// Port de `TransformVertices`: transforma y proyecta (perspectiva con `div16`)
/// los vertices marcados, guardando (x,y,zp) en `Node3D::vertex`.
void transform_vertices(obj::Object3D& object) {
	eng::math3d::Mat3x3& M = object.objectToWorld;
	void* objdat = object.objdat;
	eng::s16* group = object.vertexGroups;

	eng::s32 m0 = (static_cast<eng::s32>(M.x) - eng::math2d::normfx(static_cast<eng::s32>(M.m00) * M.m01)) << 8;
	eng::s32 m1 = (static_cast<eng::s32>(M.y) - eng::math2d::normfx(static_cast<eng::s32>(M.m10) * M.m11)) << 8;
	M.z = static_cast<eng::s16>(M.z - eng::math2d::normfx(static_cast<eng::s32>(M.m20) * M.m21));

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
				xy = static_cast<eng::s32>(x) * y;

				MULVERTEX1(xp, m0);
				MULVERTEX1(yp, m1);
				MULVERTEX2(zp);

				*pt++ = static_cast<eng::s16>(eng::math2d::div16(xp, zp) + kWidth / 2);
				*pt++ = static_cast<eng::s16>(eng::math2d::div16(yp, zp) + kHeight / 2);
				*pt++ = zp;
			}
		}
	} while (*group);
}

/// Fan-triangula las caras VISIBLES ya proyectadas, con su color de luz.
eng::u16 build_flat_triangles(obj::Object3D& object, eng::amiga::FlatTriangle* tris) {
	void* objdat = object.objdat;
	eng::s16* group = object.faceGroups;
	eng::u16 n = 0;
	eng::s16 f;
	do {
		while ((f = *group++)) {
			obj::Face* face = obj::face3d(objdat, f);
			if (face->flags < 0 || face->count < 3) {
				continue;
			}
			const obj::FaceIndex* idx = obj::face_indices(face);
			const obj::Point3D* v0 = obj::vertex3d(objdat, idx[0].vertex);
			for (eng::s16 k = 1; k + 1 < face->count && n < kMaxTris; ++k) {
				const obj::Point3D* va = obj::vertex3d(objdat, idx[k].vertex);
				const obj::Point3D* vb = obj::vertex3d(objdat, idx[k + 1].vertex);
				tris[n].x0 = v0->x; tris[n].y0 = v0->y;
				tris[n].x1 = va->x; tris[n].y1 = va->y;
				tris[n].x2 = vb->x; tris[n].y2 = vb->y;
				tris[n].color = static_cast<eng::u8>(face->flags);
				++n;
			}
		}
	} while (*group);
	return n;
}

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

		obj::new_object3d(m_object, pilka);
		m_object.translate.z = static_cast<eng::s16>(-4000); // fx4i(-250)

		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(pilka.faces));
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (m_bitplane_block.view.data() == nullptr) {
			return;
		}

		const eng::u8 buf = m_active;
		eng::PlaneBytes planes = m_bitplane_block.view.subspan(
			static_cast<eng::u32>(buf) * kPlanes * kPlaneBytes, kPlanes * kPlaneBytes);

		backend.blitter_clear(planes, kPlanes, kBytesPerRow, kPlaneBytes, kWidth, kHeight);

		m_object.rotate.x = m_object.rotate.y = m_object.rotate.z =
			static_cast<eng::s16>(context.frame.frame_index * 8u);

		obj::update_object_transformation(m_object);
		update_face_visibility(m_object);
		mark_visible_vertices(m_object);
		transform_vertices(m_object);

		const eng::u16 n = build_flat_triangles(m_object, m_tris);
		if (n > 0) {
			backend.fill_triangles_blitter(m_tris, n, planes, kPlanes, kBytesPerRow,
						       kPlaneBytes, m_mask_block.view);
		}

		backend.install_copper_list(m_copper_ptrs[buf]);
		m_active = static_cast<eng::u8>(buf ^ 1u);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	bool build_copper(eng::u8 buf) {
		const eng::Bytes<eng::CopperTag> slice = m_copper_block.view.subspan(
			static_cast<eng::u32>(buf) * kCopperPerList, kCopperPerList);
		copper::Scheduler sched { eng::Block<eng::CopperTag> { slice, m_copper_block.kind } };
		const eng::PlaneBytes planes = m_bitplane_block.view.subspan(
			static_cast<eng::u32>(buf) * kPlanes * kPlaneBytes, kPlanes * kPlaneBytes);
		sched.emit_planes_display(kDiwstrt, kDiwstop, kDdfstrt, kDdfstop, kBytesPerRow,
					  kBplcon0, kPlanes, planes, kPlaneBytes);
		sched.move(copper::Register::BPLCON1, kBplcon1);
		sched.emit_palette(eng::PaletteWords { flatshade_colors, 16u }, 0, 16);
		sched.end();
		m_copper_ptrs[buf] = sched.data();
		return sched.ok();
	}

	bool m_memory_ok = false;
	eng::u8 m_active = 0;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::MaskTag> m_mask_block {};
	const eng::u16* m_copper_ptrs[kBuffers] = {nullptr, nullptr};
	eng::object3d::Object3D m_object {};
	eng::amiga::FlatTriangle m_tris[kMaxTris] {};
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
