// Demo 080 - fire-rgb (PORTE 1:1 de demoscene-repo-orig/effects/fire-rgb/fire-rgb.c)
//
// Fuego en 80x64 -> chunky -> C2P por Blitter -> HAM6 320x256 con cuadruplicado de
// lineas por Copper. La simulacion de fuego (MainLoop/fastrand/RandomizeBottom) se
// copia VERBATIM (asm a mano); el C2P de 13 fases es `MinimalBackend::c2p_4bpp_step`
// (portado de ChunkyToPlanar) y los bits HAM fijos van por `set_bitplane_dat`.
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/drivers/ham_scene.hpp>
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

// Tipos del original para las tablas copiadas tal cual.
using uint16_t = eng::u16;
using uint32_t = eng::u32;

namespace {

namespace copper = eng::copper;

constexpr eng::u16 kWidth = 80;      // ancho del fuego
constexpr eng::u16 kHeight = 64;     // alto del fuego
constexpr eng::u16 kScreenW = kWidth * 4;   // 320
constexpr eng::u16 kScreenH = kHeight * 4;  // 256
constexpr eng::u16 kBytesPerRow = kScreenW / 8; // 40
constexpr eng::u16 kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kHeight; // 2560
constexpr eng::u32 kBitmapBytes = kPlaneBytes * kPlanes;                        // 10240
constexpr eng::u16 kChunkyBytes = static_cast<eng::u16>(kScreenW * kHeight / 2); // 10240 (4bpp)
constexpr eng::u32 kChunkyBuffer = kChunkyBytes * 2u;                           // chunky + planar

/// Paleta 0..15 a negro (CopLoadColor(cp, 0, 15, 0)): en HAM el color sale de los
/// bits de modificacion (planos 4/5), no de la paleta base.
constexpr eng::u16 kZeroPalette[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

/// DIAG usado: saltar el C2P confirma que el display esta bien (pantalla negra
/// con planos a 0) y que el patron lo mete el C2P (su mascara `bltcdat`). Se puede
/// forzar desde el build con `-DK_DIAG_SKIP_C2P=1` para medir su coste.
#ifndef K_DIAG_SKIP_C2P
#define K_DIAG_SKIP_C2P 0
#endif
constexpr bool kDiagSkipC2p = (K_DIAG_SKIP_C2P != 0);

/// DIAG/experimento: activa el "blitter nasty" (`DMACON` BLTPRI) para que el Blitter
/// tenga **prioridad de bus sobre la CPU**: el DMA del C2P avanza a plena velocidad
/// aunque el fuego este corriendo (la CPU se detiene y luego recupera). `-DK_BLIT_NASTY=1`.
#ifndef K_BLIT_NASTY
#define K_BLIT_NASTY 0
#endif
constexpr bool kBlitNasty = (K_BLIT_NASTY != 0);

} // namespace

// --- Datos: tabla de color del fuego generada en C++23 constexpr -------------
#include "data/dualtab.hpp"

// Buffer de argumentos para la rutina asm del fuego. GLOBAL y `extern "C"` (fuera
// del namespace anonimo) para que `support/fire_loop.s` lo referencie.
extern "C" {
eng::u32 g_fire_args[4] = {0, 0, 0, 0}; // [0]=chunky, [1]=fire, [2]=dt, [3]=iters
void fire_loop(void);
}

namespace {

namespace amiga = eng::amiga;
namespace drivers = eng::graphics::drivers;

short *chunky[2];
short *fire;
eng::u8* screen_planes[2][kPlanes];
short active = 0;

// --- Simulacion de fuego (VERBATIM de fire-rgb.c) ---------------------------

int fastrand(void) {
	static int m[2] = { static_cast<int>(0x3E50B28Cu), static_cast<int>(0xD461A7F9u) };
	int a, b;
	asm volatile("move.l (%2)+,%0\n"
		     "move.l (%2),%1\n"
		     "swap   %1\n"
		     "add.l  %0,(%2)\n"
		     "add.l  %1,-(%2)\n"
		     : "=d" (a), "=d" (b)
		     : "a" (m));
	return a;
}

void RandomizeBottom(void) {
	int r;
	short i;
	short *bufPtr;
	bufPtr = &(fire[kWidth * kHeight - 1]);
	for (i = 1; i <= kWidth * 2; i += 5) {
		r = fastrand();
		*bufPtr-- = static_cast<short>((r & 0x3F) * 4);
		r >>= 6;
		*bufPtr-- = static_cast<short>((r & 0x3F) * 4);
		r >>= 6;
		*bufPtr-- = static_cast<short>((r & 0x3F) * 4);
		r >>= 6;
		*bufPtr-- = static_cast<short>((r & 0x3F) * 4);
		r >>= 6;
		*bufPtr-- = static_cast<short>((r & 0x3F) * 4);
	}
}

// Rutas del bucle de fuego: ASM (la del original, por defecto) y C++ (respaldo).
// El ASM inline del original fija `a0..a6` para los 6 punteros + `dt`; GCC-15
// **ignora esos pins** y no puede asignar los 7 registros de direccion, asi que no
// compila (el original lo hacia con un GCC mas antiguo que si los respetaba). El
// bucle vive ahora en `support/fire_loop.s` (GAS), que el build ensambla junto al
// resto de `support/*.s`. La ruta C++ (`MainLoopC`) queda como respaldo/DIAG.
#ifndef K_FIRE_ASM
#define K_FIRE_ASM 1
#endif

#define FIRE_ITER_C() \
		vl = (*Eptr++) + (*Bptr++) + (*Dptr++) + (*Cptr++); \
		hi = dt[(static_cast<uint16_t>(vl >> 16) >> 2) & 0xFFu]; \
		lo = dt[(static_cast<uint16_t>(vl) >> 2) & 0xFFu]; \
		*chunkyPtr++ = static_cast<uint16_t>(hi); \
		*chunkyPtr++ = static_cast<uint16_t>(lo); \
		hi = (hi & 0xFFFF0000u) | ((lo >> 16) & 0xFFFFu); \
		*Aptr++ = hi;

/// Version C++ (default seguro; misma matematica, sin asm fragil).
void MainLoopC(void) {
	short i;
	uint16_t* chunkyPtr = reinterpret_cast<uint16_t *>(chunky[active]);
	uint32_t* Aptr = reinterpret_cast<uint32_t *>(fire);
	uint32_t* Bptr = reinterpret_cast<uint32_t *>(&fire[kWidth - 1]);
	uint32_t* Cptr = reinterpret_cast<uint32_t *>(&fire[kWidth]);
	uint32_t* Dptr = reinterpret_cast<uint32_t *>(&fire[kWidth + 1]);
	uint32_t* Eptr = reinterpret_cast<uint32_t *>(&fire[kWidth * 2]);
	const uint32_t* dt = fire_rgb::kDualTab.v;

	for (i = 0; i < (kWidth * kHeight - 2 * kWidth) / 8; ++i) {
		uint32_t vl, hi, lo;
		FIRE_ITER_C(); FIRE_ITER_C(); FIRE_ITER_C(); FIRE_ITER_C();
	}
}
#undef FIRE_ITER_C

/// Version ASM del original: rutina .s aparte (`support/fire_loop.s`). Sacarla del
/// C++ evita el problema de GCC-15, que **ignora los pins** `register asm("aN")` y
/// no puede asignar los 7 registros de direccion (a0..a6) que exige el bucle.
/// Los punteros se pasan por MEMORIA (`g_fire_args`), no por pila: evita dudas de ABI.
void MainLoop(void) {
#if K_FIRE_ASM
	g_fire_args[0] = reinterpret_cast<eng::u32>(chunky[active]);
	g_fire_args[1] = reinterpret_cast<eng::u32>(fire);
	g_fire_args[2] = reinterpret_cast<eng::u32>(fire_rgb::kDualTab.v);
	g_fire_args[3] = static_cast<eng::u32>((kWidth * kHeight - 2 * kWidth) / 8);
	fire_loop();
#else
	MainLoopC();
#endif
}

struct FireDemo {
	void init(amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// Chip RAM: chunky (x2) + fuego + bitplanes y copperlist del driver (x2).
		m_memory_ok = backend.configure_memory({128u * 1024u, 4u * 1024u, 4u * 1024u});
		if (!m_memory_ok) { eng::debug::mark_failed(g_eng_run_status, 0x00008001u); return; }

		m_block = backend.memory().chip.allocate(
			kChunkyBuffer * 2u + static_cast<eng::u32>(kWidth) * kHeight * 2u, 16);
		if (!m_block.valid()) { eng::debug::mark_failed(g_eng_run_status, 0x00008002u); return; }

		eng::u8* p = static_cast<eng::u8*>(m_block.data);
		m_chunky[0] = p; p += kChunkyBuffer;
		m_chunky[1] = p; p += kChunkyBuffer;
		m_fire = reinterpret_cast<short*>(p);
		// Enlaza los globales que usan las funciones copiadas.
		chunky[0] = reinterpret_cast<short*>(m_chunky[0]);
		chunky[1] = reinterpret_cast<short*>(m_chunky[1]);
		fire = m_fire;

		// Display HAM + cuadruplicado: una instancia del driver por buffer (doble
		// buffer). El driver reserva los bitplanes y la copperlist y expone los
		// punteros; la demo ya no calcula DIW/DDF ni palabras de Copper.
		drivers::HamSceneConfig scene_cfg {};
		scene_cfg.rows = kHeight;
		scene_cfg.planes = kPlanes;
		scene_cfg.bytes_per_row = kBytesPerRow;
		scene_cfg.bplcon0 = 0x7a00u;         // HAM6 (BPU=7, COLOR, HAM)
		scene_cfg.first_line = 0x2cu;
		scene_cfg.row_repeat = 4u;           // cuadruplicado de lineas
		scene_cfg.bplcon1_shift = 0x0022u;   // dither fino alterno del original
		scene_cfg.palette = kZeroPalette;    // CopLoadColor(0,15,0)
		scene_cfg.palette_count = 16u;
		scene_cfg.reverse_plane_ptrs = true; // BPLxPT como el original (bpl[3..0])
		for (eng::u8 b = 0; b < 2; ++b) {
			if (!m_scene[b].init(backend.memory(), scene_cfg)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00008003u);
				return;
			}
			for (eng::u8 pl = 0; pl < kPlanes; ++pl) m_planes[b][pl] = m_scene[b].plane(pl);
		}

		for (eng::u8 b = 0; b < 2; ++b) {
			for (eng::u8 pl = 0; pl < kPlanes; ++pl) {
				eng::u8* q = m_planes[b][pl];
				for (eng::u32 i = 0; i < kPlaneBytes; ++i) q[i] = 0u;
			}
			for (eng::u32 i = 0; i < kChunkyBuffer; ++i) m_chunky[b][i] = 0u;
		}
		for (eng::u32 i = 0; i < static_cast<eng::u32>(kWidth) * kHeight; ++i) m_fire[i] = 0;

		m_scene[0].takeover(backend);

		// Bits HAM fijos de los planos 4/5 (rgbb: 0111 / 1100), como el original.
		backend.set_bitplane_dat(4, 0x7777);
		backend.set_bitplane_dat(5, 0xcccc);

		// El C2P se encadena por la **IRQ de blit** (nivel 3): `on_blit` programa la
		// fase siguiente al terminar cada blit.
		m_backend = &backend;
		backend.set_blit_service(&FireDemo::on_blit, this);
		if (kBlitNasty) {
			backend.set_blitter_priority(true);   // Blitter con prioridad sobre la CPU.
		}

		m_init_ok = true;
		eng::debug::mark_ready(g_eng_run_status, 0x0080u);
	}

	void update(amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok) return;
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);

		RandomizeBottom();
		MainLoop();

#if K_FIRE_ASM
		// El C2P lo encadena la IRQ de blit, que instala la copperlist al completar.
		// Si por lo que sea no llego a completarse (raro), completalo aqui.
		if (m_c2p_pending) {
			FinishC2p(backend);
			m_scene[m_c2p_buf].install(backend);
			m_c2p_pending = false;
		}

		// Arranca el C2P del buffer recien simulado (fase 0, sin esperar); la IRQ de
		// blit encadena las fases 1..12 y marca `m_c2p_done`.
		if (!kDiagSkipC2p) {
			m_c2p.chunky = m_chunky[active];
			m_c2p.bytes = kChunkyBytes;
			for (eng::u8 pl = 0; pl < kPlanes; ++pl) m_c2p.planes[pl] = m_planes[active][pl];
			m_c2p.phase = 0;
			if (!backend.c2p_4bpp_program(m_c2p)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00008004u);
				return;
			}
			m_c2p_pending = true;
			m_c2p_irq = true;
			m_c2p_buf = active;
		} else {
			m_scene[active].install(backend);
		}
		active ^= 1;
#else
		MainLoop();

		{	// DIAG: suma del buffer de fuego (comprobar que se forma).
			eng::u32 s = 0;
			for (eng::u32 i = 0; i < static_cast<eng::u32>(kWidth) * kHeight; ++i) {
				s += static_cast<eng::u16>(m_fire[i]);
			}
			g_eng_run_status.detail = s;
		}

		// C2P: 13 fases (sincrono) del plano `active` a sus bitplanes.
		if (!kDiagSkipC2p) {
			amiga::MinimalBackend::C2p4State s {};
			s.chunky = m_chunky[active];
			s.bytes = kChunkyBytes;
			for (eng::u8 pl = 0; pl < kPlanes; ++pl) s.planes[pl] = m_planes[active][pl];
			// 13 fases (todas): las impares NO son redundantes (saltarlas rompe el
			// C2P). Probablemente el Blitter deja los punteros avanzados y la impar
			// procesa el bloque siguiente con el mismo bltsize.
			for (eng::u8 ph = 0; ph < 13; ++ph) {
				if (!backend.c2p_4bpp_step(s)) { eng::debug::mark_failed(g_eng_run_status, 0x00008004u); return; }
			}
		}

		// Swap de buffer (la copperlist del driver apunta a los 4 planos de `active`).
		m_scene[active].install(backend);
		active ^= 1;
#endif
	}

	void render(amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	/// Tarea de la **IRQ de blit** (nivel 3): cada blit terminado programa la fase
	/// siguiente del C2P. Es el mecanismo fiel del original (`ChunkyToPlanar` en la IRQ
	/// de blit), sin que la CPU espere al Blitter.
	static void on_blit(void* user, eng::u16 vpos) {
		(void)vpos;
		auto* self = static_cast<FireDemo*>(user);
		if (!self->m_c2p_irq || self->m_backend == nullptr) {
			return;
		}
		if (self->m_c2p.phase < 13u) {
			self->m_backend->c2p_4bpp_program(self->m_c2p);
		}
		if (self->m_c2p.phase >= 13u) {
			self->m_c2p_irq = false;
			self->m_c2p_pending = false;
			// Instala la copperlist del buffer recien convertido AQUI (no en el proximo
			// update): el Copper la recarga en el siguiente VBlank, asi que el display
			// muestra el buffer ya convertido y NUNCA el que se esta convirtiendo (evita
			// el tearing de la zona caliente).
			self->m_scene[self->m_c2p_buf].install(*self->m_backend);
		}
	}

private:
#if K_FIRE_ASM
	/// Salvaguarda: completa el C2P pendiente bloqueando (si la IRQ no llego a cerrarlo).
	void FinishC2p(amiga::MinimalBackend& backend) {
		if (!m_c2p_pending) return;
		m_c2p_irq = false;
		while (m_c2p.phase < 13u) {
			while (backend.blitter_busy()) {}
			backend.c2p_4bpp_program(m_c2p);
		}
		while (backend.blitter_busy()) {}
		m_c2p_pending = false;
	}
#endif

	bool m_memory_ok = false;
	bool m_init_ok = false;
	eng::MemoryBlock m_block {};
	eng::u8* m_chunky[2] = {nullptr, nullptr};
	eng::u8* m_planes[2][kPlanes] = {};
	short* m_fire = nullptr;
	// Display HAM + cuadruplicado (una instancia del driver por buffer).
	drivers::HamScene m_scene[2] {};
	amiga::MinimalBackend* m_backend = nullptr;
	// Pipeline del C2P: la fase 0 la arranca `update`; las fases 1..12 las encadena la
	// IRQ de blit (`on_blit`), que marca `m_c2p_done` al terminar.
	amiga::MinimalBackend::C2p4State m_c2p {};
	bool m_c2p_pending = false;
	bool m_c2p_irq = false;
	eng::u8 m_c2p_buf = 0;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	amiga::MinimalBackend backend {};
	FireDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
