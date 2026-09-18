// Demo 117 - bobs3d (PORTE 1:1 de demoscene-repo-orig/effects/bobs3d/bobs3d.c)
//
// Recrea el efecto original "bobs3d": el objeto `pilka` (malla obj2c de `lib3d`) rota en
// 3D y cada uno de sus 60 vertices proyectados se dibuja como un BOB OR intercalado
// (chispa de 48x32, un solo blit por objeto: altura = alto*planos) sobre un playfield
// de 3 planos. El fondo es un segundo playfield (2 planos, "carrion-metro") con la
// paleta reescrita POR LINEA por el Copper (color0 + colores 9/10/11), en doble
// playfield.
//
// Displays/registros del original (`SetupMode(MODE_DUALPF, 3+2)`, `SetupBitplaneFetch` y
// `SetupDisplayWindow` para MODE_LORES, X(32), Y(0), 256x256):
//   BPLCON0 = 0x5600 (5 planos + DBLPF + COLOR)      BPL1MOD = 64, BPL2MOD = 32
//   DIWSTRT/DIWSTOP = 0x2CA1                         DDFSTRT/DDFSTOP = 0x48/0xC0
//   punteros DPF intercalados: BPL1/3/5PT = screen 0/1/2; BPL2/4PT = carrion 0/1.
//
// Fidelidad y capa de engine: la matematica 4.12 del `TransformVertices` original se
// resuelve con `eng::math::projector` (mismo `MULVERTEX` empaquetado, backend 68000) y el
// BOB OR intercalado con `eng::graphics::bob` + `FramePlan` (que ya implementa el camino
// de `DrawObject`). Los assets (`data/*.c`) son los del repo original, copiados tal cual.
//
// Build/run:
//   bash ./tools/build/build-demo.sh demos/amiga/117_bobs3d --debug
//   bash ./tools/run/run-demo.sh demos/amiga/117_bobs3d
#include <eng/core/affine.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/bob.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga/object3d.hpp>
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

// --- Assets verbatim del demoscene ------------------------------------------
// Los `.c` del original asumen macros de seccion y tipos de su libc. Se aportan shims
// minimos para incluirlos sin reescribir sus datos.
using u_short = eng::u16;
#define __data
#define __rodata
#define __data_chip
#define bobs_bpl_section
#define carrion_bpl_section
#define carrion_cols_pixels_section

enum { BM_STATIC = 0x40, BM_INTERLEAVED = 0x04 };
enum { PM_RGB12 = 9 };

struct BitmapT {
	eng::u16 width;
	eng::u16 height;
	eng::u16 depth;
	eng::u16 bytesPerRow;
	eng::u16 bplSize;
	eng::u8 flags;
	void* planes[8];
};

struct PixmapT {
	int type;
	eng::s16 width;
	eng::s16 height;
	void* pixels;
};

// Interruptores de diagnostico visual por capa (validacion incremental con vision):
//   K_117_BG=0   -> fondo (carrion + degradado por linea) en negro.
//   K_117_BOBS=0 -> solo fondo, sin BOBs (el playfield del objeto se limpia a 0).
#ifndef K_117_BG
#define K_117_BG 1
#endif
#ifndef K_117_BOBS
#define K_117_BOBS 1
#endif
// 1 = lote de BOBs del backend (constantes del blit fijadas una vez, atlas denso y 3
// palabras fieles). 0 = camino generico `graphics::bob` + `FramePlan` (comparativa).
#ifndef K_117_BATCH
#define K_117_BATCH 1
#endif
// Diagnostico de coste: 0 desactiva la fase (para medirla aislada).
#ifndef K_117_CLEAR
#define K_117_CLEAR 1
#endif
#ifndef K_117_WORK
#define K_117_WORK 1
#endif
// Diagnostico: filas por BOB del lote (96 = real; 1 = aisla el coste FIJO por BOB,
// es decir setup de registros + espera, con el trabajo de Blitter minimo).
#ifndef K_117_BLITROWS
#define K_117_BLITROWS kBobHeight
#endif

using eng::object3d::Mesh3D;
#include "data/pilka.c"
#include "data/flares32.c"
#include "data/carrion-metro-data.c"
#include "data/carrion-metro-pal.c"

namespace {

namespace obj = eng::object3d;
namespace copper = eng::copper;
namespace graphics = eng::graphics;

using eng::s16;
using eng::s32;
using eng::s8;
using eng::u8;
using eng::u16;
using eng::u32;

// Geometria del original.
constexpr u16 kWidth = 256;
constexpr u16 kHeight = 256;
constexpr u8 kPlanes = 3;              // playfield del objeto (BOBs)
constexpr u8 kCarrionPlanes = 2;       // playfield del fondo
constexpr u16 kBytesPerRow = kWidth / 8u;      // 32
constexpr u32 kScreenPlaneBytes = static_cast<u32>(kBytesPerRow) * kHeight; // 8192
constexpr u32 kScreenBytes = kScreenPlaneBytes * kPlanes;                    // 24576
constexpr u8 kRing = 2;                // doble buffer de pantalla

// BOB: chispa 48x32x3; el atlas original es denso (bytesPerRow 6) con 16 frames de 32
// filas. Se reempaqueta a filas con palabra de guarda para el contrato de `bob.hpp`.
constexpr u16 kBobW = 48;
constexpr u16 kBobH = 32;
constexpr u8 kBobPlanes = 3;
constexpr u8 kBobFrames = 16;
constexpr u32 kBobSrcRow = 6;                                  // atlas original
constexpr u32 kBobSrcFrame = static_cast<u32>(kBobH) * kBobPlanes * kBobSrcRow; // 576
constexpr u32 kBobSheetRow = 8;                                // fila con guarda
constexpr u32 kBobSheetFrame = static_cast<u32>(kBobH) * kBobPlanes * kBobSheetRow; // 768
constexpr u32 kBobSheetBytes = kBobFrames * kBobSheetFrame;    // 12288

// Ruta fiel (lote): atlas DENSO del original (6 B/fila, sin guarda), 3 palabras por fila.
constexpr u32 kBobDenseFrame = kBobSrcFrame;                   // 576
constexpr u32 kBobDenseBytes = kBobFrames * kBobDenseFrame;    // 9216
constexpr u16 kBobWords = kBobW / 16u;                         // 3
constexpr u16 kBobHeight = kBobH * kBobPlanes;                 // 96
constexpr s16 kBobDestModulo = static_cast<s16>(kBytesPerRow - kBobWords * 2u); // 26
constexpr u32 kBobMaxEntries = 64;

// Display del original.
constexpr u16 kFirstLine = 0x2cu;
constexpr u16 kDiwstrt = 0x2ca1;
constexpr u16 kDiwstop = 0x2ca1;
constexpr u16 kDdfstrt = 0x0048;
constexpr u16 kDdfstop = 0x00c0;
constexpr u16 kBplcon0 = 0x5600; // BPU=5 + DBLPF + COLOR
constexpr u16 kBplcon1 = 0x0000;
constexpr u16 kBplcon2 = 0x0024; // PF1P/PF2P al fondo (SetupMode)
constexpr u16 kBpl1mod = 64;     // WIDTH/8 * (DEPTH-1)
constexpr u16 kBpl2mod = 32;     // WIDTH/8 * (carrion_depth-1)

constexpr u32 kCopperListBytes = 8192;
constexpr u32 kCopperBlockBytes = kCopperListBytes * kRing;

// `X(x)` del demoscene = HP(x + DIWHP=0x81); el Copper compara con `hp>>1` y mascara
// 0xfffe, que es el formato de `Scheduler::wait_position_safe`.
constexpr u8 x_hpos(u16 x) {
	return static_cast<u8>((x + 0x81u) >> 1u);
}

// Secciones de profiling (tools/debug/profile.mjs). Diagnostico del reparto del frame.
enum {
	kProfClear = 0,
	kProfTransform = 1,
	kProfDraw = 2,
	kProfBlits = 3,
	kProfInstall = 4,
	kProfCount = 5,
};

/// Recorrido del original: transforma los 60 vertices (sin culling) y guarda la
/// proyeccion en `vertex`. Usa el `projector` del engine, que reproduce el
/// empaquetado `(c0+y)(c1+x)+c2*z-xy` del `MULVERTEX` original.
void transform_all_vertices(obj::Object3D& object) {
	using Proj = eng::math::projector<eng::math3d::Affine3>;
	const Proj::cache pc = Proj::make(object.objectToWorld);
	void* objdat = object.objdat;
	s16* group = object.vertexGroups;
	do {
		s16 i;
		while ((i = *group++)) {
			obj::Point3D* p = obj::point3d(objdat, i);
			obj::Point3D* v = obj::vertex3d(objdat, i);
			const eng::math::Projected3 pr = Proj::project(pc, p->x, p->y, p->z);
			v->x = static_cast<s16>(eng::math::div_wide(pr.xp, static_cast<s16>(pr.zp)) + kWidth / 2u);
			v->y = static_cast<s16>(eng::math::div_wide(pr.yp, static_cast<s16>(pr.zp)) + kHeight / 2u);
			v->z = static_cast<s16>(pr.zp);
		}
	} while (*group);
}

struct Bobs3DDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		ENG_PROF_INIT(kProfCount);
		if (!backend.configure_memory({160u * 1024u, 4u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011701u);
			return;
		}

		m_screen_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kRing * kScreenBytes, 16);
		m_bob_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kBobSheetBytes, 16);
		m_bob_dense_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kBobDenseBytes, 16);
		m_carrion_block = backend.memory().chip.allocate_block<eng::PlaneTag>(carrion_size, 16);
		m_copper_block = backend.memory().chip.allocate_block<eng::CopperTag>(kCopperBlockBytes, 16);
		if (!m_screen_block.valid() || !m_bob_block.valid() || !m_bob_dense_block.valid() ||
		    !m_carrion_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011702u);
			return;
		}

		build_bob_sheet();
		copy_bob_dense();
#if K_117_BG
		copy_carrion();
#endif

		for (u8 a = 0; a < kRing; ++a) {
			if (!build_copper(a)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00011703u);
				return;
			}
		}
		backend.takeover_display(m_copper_ptrs[0]);

		obj::new_object3d(m_object, pilka);
		// fx4i(-256): (-256) << 4 en 4.12 = -4096.
		m_object.translate.z = static_cast<s16>(static_cast<u16>(-256) << 4u);

		eng::debug::mark_ready(g_eng_run_status, static_cast<u32>(pilka.vertices));
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (m_screen_block.view.data() == nullptr) {
			return;
		}

		ENG_PROF_FRAME();
		const u8 active = m_active;
		eng::PlaneBytes screen = m_screen_block.view.subspan(
			static_cast<u32>(active) * kScreenBytes, kScreenBytes);

		// El original limpia el bitmap intercalado con un solo blit (altura = alto*planos).
		ENG_PROF_BEGIN(kProfClear);
#if K_117_CLEAR
		backend.blitter_clear(screen, 1, kBytesPerRow, static_cast<u32>(kBytesPerRow),
				      kWidth, static_cast<u16>(kHeight * kPlanes));
#endif
		ENG_PROF_END(kProfClear);

#if K_117_WORK
		m_object.rotate.x = m_object.rotate.y = m_object.rotate.z =
			static_cast<s16>(context.frame.frame_index * 12u);

		ENG_PROF_BEGIN(kProfTransform);
		obj::update_object_transformation(m_object);
		transform_all_vertices(m_object);
		ENG_PROF_END(kProfTransform);

#if K_117_BOBS && K_117_BATCH
		// Fusor: calculo del vertice + programacion del blit en el mismo bucle.
		ENG_PROF_BEGIN(kProfBlits);
		backend.set_blitter_priority(true);
		draw_bobs_stream(backend, screen.data());
		ENG_PROF_END(kProfBlits);
#else
		ENG_PROF_BEGIN(kProfDraw);
		m_plan.clear();
#if K_117_BOBS
		draw_bobs(screen.data());
#endif
		ENG_PROF_END(kProfDraw);
		ENG_PROF_BEGIN(kProfBlits);
		backend.execute_frame_plan(m_plan);
		ENG_PROF_END(kProfBlits);
#endif
#endif // K_117_WORK

		ENG_PROF_BEGIN(kProfInstall);
		backend.install_copper_list(m_copper_ptrs[active]);
		ENG_PROF_END(kProfInstall);
		m_active = static_cast<u8>(active ^ 1u);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Reempaqueta el atlas denso `_bobs_bpl` (6 B/fila) a filas con palabra de guarda
	/// (8 B/fila), que es el contrato de `eng::graphics::bob` para el desplazamiento fino.
	void build_bob_sheet() {
		u8* dst = m_bob_block.view.data();
		const u8* src = reinterpret_cast<const u8*>(_bobs_bpl);
		for (u32 f = 0; f < kBobFrames; ++f) {
			for (u32 r = 0; r < static_cast<u32>(kBobH) * kBobPlanes; ++r) {
				u8* d = dst + f * kBobSheetFrame + r * kBobSheetRow;
				const u8* s = src + f * kBobSrcFrame + r * kBobSrcRow;
				for (u32 b = 0; b < kBobSrcRow; ++b) {
					d[b] = s[b];
				}
				d[6] = 0;
				d[7] = 0;
			}
		}
	}

	/// Copia el atlas DENSO original (6 B/fila) a Chip RAM para el lote fiel.
	void copy_bob_dense() {
		u8* dst = m_bob_dense_block.view.data();
		const u8* src = reinterpret_cast<const u8*>(_bobs_bpl);
		for (u32 i = 0; i < kBobDenseBytes; ++i) {
			dst[i] = src[i];
		}
	}

	/// Calcula el vertice y lanza su BOB en el MISMO bucle (estructura de `DrawObject`),
	/// via el lote en streaming del backend: sin array intermedio.
	void draw_bobs_stream(eng::amiga::MinimalBackend& backend, u8* screen) {
		void* objdat = m_object.objdat;
		s16* group = m_object.vertexGroups;
		const u8* sheet = m_bob_dense_block.view.data();

		backend.blitter_or_bobs_begin(kBobWords, K_117_BLITROWS, 0, kBobDestModulo);
		do {
			s16 v;
			while ((v = *group++)) {
				obj::Point3D* data = obj::vertex3d(objdat, v);
				s16 x = static_cast<s16>(data->x - 16);
				const s16 y = static_cast<s16>(data->y - 16);
				s16 z = data->z;

				z >>= 4;
				z -= static_cast<s16>(-256);
				z += 128 - 32;
				z = static_cast<s16>(z + z + z - 32);
				z = static_cast<s16>(z & ~31);
				if (z < 0) {
					z = 0;
				} else if (z > bobs_height - kBobH) {
					z = bobs_height - kBobH;
				}

				s16 x_start = static_cast<s16>(x & ~15);
				if (x_start < 0) {
					x_start = 0;
				}

				backend.blitter_or_bobs_one(
					sheet + static_cast<u32>(z >> 5) * kBobDenseFrame,
					screen + static_cast<s32>(y) * static_cast<s32>(kBytesPerRow * kPlanes) +
						(static_cast<s32>(x_start) >> 3),
					static_cast<u8>(x & 15));
			}
		} while (*group);
		backend.blitter_or_bobs_end();
	}

	void copy_carrion() {
		u16* dst = reinterpret_cast<u16*>(m_carrion_block.view.data());
		const u16* src = reinterpret_cast<const u16*>(_carrion_bpl);
		for (u32 i = 0; i < carrion_size / 2u; ++i) {
			dst[i] = src[i];
		}
	}

	/// Dibuja un BOB OR por vertice, como `DrawObject`: posicion (x-16, y-16), `z`
	/// selecciona el frame (chispa) del atlas y el blit es intercalado (1 job/objeto).
	void draw_bobs(u8* screen) {
		void* objdat = m_object.objdat;
		s16* group = m_object.vertexGroups;

		graphics::Bob bob {};
		bob.sheet = m_bob_block.view.data();
		bob.width = kBobW;
		bob.height = kBobH;
		bob.planes = kBobPlanes;
		bob.frame_count = kBobFrames;
		bob.frame_stride = kBobSheetFrame;
		bob.sheet_row_bytes = kBobSheetRow;
		bob.layout = graphics::BobLayout::Interleaved;
		bob.draw = graphics::BobDraw::Or;
		bob.erase = graphics::BobErase::None;

		const graphics::BobTarget target {
			screen, kBytesPerRow, 0u, kBobPlanes, graphics::BobLayout::Interleaved};

		do {
			s16 v;
			while ((v = *group++)) {
				obj::Point3D* data = obj::vertex3d(objdat, v);
				s16 x = data->x;
				s16 y = data->y;
				s16 z = data->z;

				x -= 16;
				y -= 16;

				z >>= 4;
				z -= static_cast<s16>(-256);
				z += 128 - 32;
				z = static_cast<s16>(z + z + z - 32);
				z = static_cast<s16>(z & ~31);
				if (z < 0) {
					z = 0;
				} else if (z > bobs_height - kBobH) {
					z = bobs_height - kBobH;
				}

				graphics::bob_draw(m_plan, bob, static_cast<u8>(z >> 5), x, y, target);
			}
		} while (*group);
	}

	/// Copper de una pantalla `active`: setup de display DPF + punteros intercalados +
	/// paleta base y, por cada una de las 256 lineas, el bloque del original
	/// (`CopWaitSafe(Y(i-1),X(288))` -> color0=0; `CopWaitSafe(Y(i),X(0))` ->
	/// colores 9/10/11 y color0=bgcol).
	bool build_copper(u8 active) {
		const eng::Bytes<eng::CopperTag> slice = m_copper_block.view.subspan(
			static_cast<u32>(active) * kCopperListBytes, kCopperListBytes);
		copper::Scheduler sched { eng::Block<eng::CopperTag> { slice, m_copper_block.kind } };

		// BLTPRI (BLITHOG) como el original: el Blitter NO cede slots a la CPU. Sin esto,
		// el CPU (en `_WaitBlitter`) compite por el bus con el Blitter y el lote de BOBs
		// se ralentiza. Ver `EnableDMA(DMAF_RASTER|DMAF_BLITTER|DMAF_BLITHOG)` en bobs3d.c.
		sched.move(copper::Register::DMACON,
			   static_cast<u16>(copper::DmaSetClear | copper::DmaMaster | copper::DmaCopper |
					    copper::DmaBitplane | copper::DmaBlitter |
					    copper::DmaBlitterPriority));
		sched.move(copper::Register::BPLCON0, kBplcon0);
		sched.move(copper::Register::BPLCON1, kBplcon1);
		sched.move(copper::Register::BPLCON2, kBplcon2);
		sched.move(copper::Register::DIWSTRT, kDiwstrt);
		sched.move(copper::Register::DIWSTOP, kDiwstop);
		sched.move(copper::Register::DDFSTRT, kDdfstrt);
		sched.move(copper::Register::DDFSTOP, kDdfstop);
		sched.move(copper::Register::BPL1MOD, kBpl1mod);
		sched.move(copper::Register::BPL2MOD, kBpl2mod);

		const u32 screen_off = static_cast<u32>(active) * kScreenBytes;
		sched.move_bitplane_pointer(0, m_screen_block.view.address(static_cast<s32>(screen_off)));
		sched.move_bitplane_pointer(1, m_carrion_block.view.address(0));
		sched.move_bitplane_pointer(2, m_screen_block.view.address(static_cast<s32>(screen_off + 32u)));
		sched.move_bitplane_pointer(3, m_carrion_block.view.address(32));
		sched.move_bitplane_pointer(4, m_screen_block.view.address(static_cast<s32>(screen_off + 64u)));

		sched.emit_palette(bobs_colors, 0, 8);
#if K_117_BG
		sched.move(copper::color_register(0), carrion_cols_pixels[0]);

		const u8 hp_left = x_hpos(0);
		const u8 hp_right = x_hpos(320 - 32);
		for (u16 i = 0; i < kHeight; ++i) {
			const u32 idx = static_cast<u32>(i) * 4u;
			const u16 bgcol = carrion_cols_pixels[idx];
			sched.wait_position_safe(static_cast<u16>(kFirstLine + i - 1u), hp_right);
			sched.move(copper::color_register(0), 0);
			sched.wait_position_safe(static_cast<u16>(kFirstLine + i), hp_left);
			sched.move(copper::color_register(9), carrion_cols_pixels[idx + 1u]);
			sched.move(copper::color_register(10), carrion_cols_pixels[idx + 2u]);
			sched.move(copper::color_register(11), carrion_cols_pixels[idx + 3u]);
			sched.move(copper::color_register(0), bgcol);
		}
#endif
		sched.end();

		m_copper_ptrs[active] = sched.data();
		return sched.ok();
	}

	u8 m_active = 0;
	eng::Block<eng::PlaneTag> m_screen_block {};
	eng::Block<eng::PlaneTag> m_bob_block {};
	eng::Block<eng::PlaneTag> m_bob_dense_block {};
	eng::Block<eng::PlaneTag> m_carrion_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	const u16* m_copper_ptrs[kRing] = {nullptr, nullptr};
	graphics::FramePlan m_plan {};
	obj::Object3D m_object {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	Bobs3DDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
