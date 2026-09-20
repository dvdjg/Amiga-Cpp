// ============================================================================
// Demo 061: c2p_1x1_4 (chunky 4bpp -> planar) + rotozoom por CPU.
// ============================================================================
//
// Demuestra el valor del modo **chunky**: en un Amiga OCS sin chunky el efecto por
// píxel habría que escribirlo en planar (un bit por plano), lo que lo hace impagable.
// Aquí la CPU genera cada píxel en un framebuffer lineal de 1 byte/píxel y
// `eng::graphics::c2p_1x1_4` lo transpone a los 4 bitplanes que lee Agnus.
//
// El efecto es un **rotozoom** (`eng/graphics/effects/rotozoom.hpp`) sobre una textura
// indexada 64x64, con paleta cíclica de 16 colores. Se genera a 320x64 y el display lo
// muestra a 320x256 repitiendo cada fila 4 veces por Copper (`PlanarScene.row_repeat`), el
// mismo truco que la demo 080: llena la pantalla pagando solo 20.480 píxeles.
//
// El C2P corre en **asm 68000** (`support/c2p_1x1_4.s`, port de Kalms/Scout 1999) y su
// equivalencia byte a byte con la referencia portable C++ se comprueba en `init` y se
// publica en `g_eng_run_status.detail` (0 = idénticos). El display usa **doble buffer**
// (dos instancias del driver + swap de copperlist tras VBlank) para no desgarrar.
//
// ESTADO DE RENDIMIENTO (pendiente, ver README): el bucle del rotozoom corre en asm
// (`support/rotozoom_loop.s`, K_061_ASM=1 por defecto; ~94 ciclos/pixel, frente a ~152
// del C++) pero la animacion sigue a ~3,0 fps a pantalla completa. Con 20.480 pixeles
// por frame el presupuesto de 20 ms no da para fluido ni con un bucle idealizado (~4-5
// fps de techo): para animar sin tearing hay que reducir el area o usar pixeles gordos.
//
// Build/run (Windows nativo):
//   bash tools/build/build-demo.sh demos/amiga/061_c2p_chunky_4bpl --clean
//   <Node> dist/tools/run/run-demo.js demos/amiga/061_c2p_chunky_4bpl --warp

#include <eng/core/ct_array.hpp>
#include <eng/core/sinetable.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/c2p.hpp>
#include <eng/graphics/scene/compose.hpp>
#include <eng/graphics/effects/rotozoom.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

// Ruta de generacion del rotozoom: 0 = C++ canonica (camino seguro), 1 = bucle asm
// `support/rotozoom_loop.s`. Se puede invertir sin tocar el codigo con
// `EXTRA_DEFINES="-DK_061_ASM=1"`.
#ifndef K_061_ASM
#define K_061_ASM 1
#endif

// Numero de buffers de display: 1 = sin doble buffer, 2 = doble, 3 = triple.
// Configurable sin tocar codigo: EXTRA_DEFINES="-DK_061_BUFFERS=1".
#ifndef K_061_BUFFERS
#define K_061_BUFFERS 2
#endif
static_assert(K_061_BUFFERS >= 1 && K_061_BUFFERS <= 3, "K_061_BUFFERS fuera de rango");

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};

/// Argumentos por memoria del bucle asm del rotozoom (ver `support/rotozoom_loop.s`).
eng::u32 g_rotozoom_args[11] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/// Bucle asm del rotozoom (specializado a textura 64x64).
void rotozoom_loop();

/// c2p de Kalms/Scout (1999) portado a GAS en `support/c2p_1x1_4.s` (asm 68000).
/// Usa la ABI del ORIGINAL (`include/c2p_1x1_4.h`): d0.w chunkyx, d1.w chunkyy,
/// d5.l bplsize, a0 chunkybuffer, a1 bitplanes. No se puede declarar como funcion C
/// normal: GCC no coloca el 3.er argumento en d5. Este wrapper fija los registros.
void c2p_1x1_4_asm(eng::u32 chunkyx, eng::u32 chunkyy, eng::u32 bplsize, const void* chunky,
		   void* bpls) {
	register eng::u32 d0 __asm__("d0") = chunkyx;
	register eng::u32 d1 __asm__("d1") = chunkyy;
	register eng::u32 d5 __asm__("d5") = bplsize;
	register const void* a0 __asm__("a0") = chunky;
	register void* a1 __asm__("a1") = bpls;
	__asm__ volatile("jsr c2p_1x1_4"
			 : "+d"(d0), "+d"(d1), "+d"(d5), "+a"(a0), "+a"(a1)
			 :
			 : "memory");
}
}

namespace {

namespace scene = eng::graphics::scene;

// Geometria: se genera a 320x64 (20.480 px, la mitad de 320x256) y el driver repite
// cada fila 4 veces -> display 320x256. La fila logica (40 B) coincide con la del
// bitplane (kBytesPerRow), que es lo que el C2P espera como paso de fila.
constexpr eng::u16 kChunkyW = 320;
constexpr eng::u16 kChunkyH = 64;
constexpr eng::u8 kRepeat = 4;
constexpr eng::u8 kPlanes = 4;
constexpr eng::u16 kBytesPerRow = kChunkyW / 8;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kChunkyH; // 2560

// Paleta ciclica de 16 colores (negro -> azul -> cian -> verde -> amarillo -> rojo ->
// magenta -> violeta -> negro). Un ciclo suave hace que el rotozoom "fluya".
constexpr eng::u16 kColors[16] = {
	0x000, 0x008, 0x00f, 0x08f, 0x0ff, 0x0f8, 0x0f0, 0x8f0,
	0xff0, 0xf80, 0xf00, 0xf08, 0xf0f, 0x80f, 0x408, 0x004,
};

// Textura 64x64 indexada: plasma suave por suma de senos (reusa el generador exacto
// del engine, sin datos a mano). El indice de texel es el color 0..15.
constexpr eng::SineTable<64, 64> kTexSin {};
constexpr eng::u8 texel_at(eng::u32 r, eng::u32 c) {
	const eng::s32 v = kTexSin[static_cast<eng::u8>(r)] + kTexSin[static_cast<eng::u8>(c)] +
			   kTexSin[static_cast<eng::u8>(r + c)];
	return static_cast<eng::u8>((v >> 2) & 15);
}
constexpr eng::ct_array<eng::u8, 64u * 64u> kTexture {[](eng::usize i) -> eng::u8 {
	return texel_at(static_cast<eng::u32>(i) / 64u, static_cast<eng::u32>(i) % 64u);
}};

// Oscilacion de zoom: 98.304 +- 32.768 en 16.16 (1.5x +- 0.5x).
constexpr eng::SineTable<32768, 256> kZoomSin {};


/// Genera el rotozoom en `dst` con la ruta elegida. Ambas parten de los MISMOS
/// `RotozoomSteps`, así que deben escribir el mismo buffer byte a byte.
void render_rotozoom(const eng::graphics::Rotozoom& rot, eng::ChunkyBuffer dst, eng::u16 w,
		     eng::u16 h) {
#if K_061_ASM
	// El bucle asm mantiene la coordenada u pre-escalada (u<<6) para que la extraccion
	// del texel sea un solo `and` (ver support/rotozoom_loop.s). El giro de bits se
	// hace en u32: el asm enmascara a 28 bits, asi que el desbordamiento no importa.
	const eng::graphics::RotozoomSteps st = eng::graphics::rotozoom_steps<64, 64>(rot, w, h);
	const eng::u32 u = static_cast<eng::u32>(st.u);
	const eng::u32 du = static_cast<eng::u32>(st.du);
	const eng::u32 dv = static_cast<eng::u32>(st.dv);
	g_rotozoom_args[0] = reinterpret_cast<eng::u32>(dst.data());
	g_rotozoom_args[1] = reinterpret_cast<eng::u32>(kTexture.data());
	g_rotozoom_args[2] = w;
	g_rotozoom_args[3] = h;
	g_rotozoom_args[4] = u << 6;   // U (entrada y salida)
	g_rotozoom_args[5] = static_cast<eng::u32>(st.v); // V (entrada y salida)
	g_rotozoom_args[6] = du << 6;  // paso de u por pixel
	g_rotozoom_args[7] = dv;       // paso de v por pixel
	g_rotozoom_args[8] = dv << 6;  // avance de fila en U
	g_rotozoom_args[9] = static_cast<eng::u32>(0) - du; // avance de fila en V
	g_rotozoom_args[10] = (static_cast<eng::u32>(w) / 2u) - 1u; // media fila
	rotozoom_loop();
#else
	eng::graphics::rotozoom_into<64, 64>(eng::IndexedTexture {kTexture.data(), kTexture.size()},
					    rot, dst, w, h);
#endif
}

struct RotozoomDemo {
	bool init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// Chip: 2 escenas (planos+copperlist), 2 chunky y el banco de referencia.
		if (!backend.configure_memory({192u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006101u);
			return false;
		}

		scene::SceneResources res = scene::planar(320u, 256u, kPlanes);
		res.rows = kChunkyH;
		res.buffers = static_cast<eng::u8>(K_061_BUFFERS);
		if (!scene::compose(m_scene, backend.memory(), res,
				    scene::display(scene::kPal320x256, scene::kBplcon0_4Planes),
				    scene::palette(eng::PaletteWords {kColors, 16u}, 0u, 16u),
				    scene::row_repeat(kRepeat, 0x2cu, 0u))) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006102u);
			return false;
		}

		for (eng::u8 b = 0; b < 2u; ++b) {
			m_chunky[b] = backend.memory().chip.allocate_block<eng::ChunkyTag>(kChunkyW * kChunkyH, 4);
			if (!m_chunky[b].valid()) {
				eng::debug::mark_failed(g_eng_run_status, 0x00006103u);
				return false;
			}
		}
		m_ref = backend.memory().chip.allocate_block<eng::PlaneTag>(kPlaneBytes * kPlanes, 4);
		if (!m_ref.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006104u);
			return false;
		}

		// Frame de referencia (identidad: offset = centro, zoom 1, sin rotar): la asm
		// debe coincidir byte a byte con la version portable C++.
		const eng::graphics::Rotozoom id {
			0, 65536,
			static_cast<eng::s32>(kChunkyW / 2) << 16,
			static_cast<eng::s32>(kChunkyH / 2) << 16,
		};
		render_rotozoom(id, m_chunky[0].view, kChunkyW, kChunkyH);
#if K_061_ASM
		// Gate de la ruta asm: debe ser byte-identica a la C++ canonica. NO basta con
		// la identidad (angulo 0, sin paneo): el avance de fila y el pre-escalado de
		// `u` solo se ejercitan con rotacion/zoom/paneo, que es lo que la demo usa de
		// verdad. Se recorren varios estados y se falla con el indice del primero que
		// difiere (0x6105 = identidad, 0x6107+s = estado s).
		{
			constexpr eng::u32 kStates = 7u;
			const eng::graphics::Rotozoom states[kStates] = {
				{0, 65536, static_cast<eng::s32>(kChunkyW / 2) << 16,
				 static_cast<eng::s32>(kChunkyH / 2) << 16},   // identidad
				{64, 65536, 0, 0},                            // 90 grados
				{32, 65536, 0, 0},                            // 45 grados
				{0, 131072, 0, 0},                            // zoom 2x
				{17, 98304, 12288, 7168},                     // rotacion + zoom + paneo
				{128, 65536, 1 << 16, 3 << 16},               // media vuelta + paneo
				{200, 122880, 0x00200000, 0x00380000},        // angulo/zoom/offsets grandes
			};
			const eng::u32 pixels = static_cast<eng::u32>(kChunkyW) * kChunkyH;
			for (eng::u32 s = 0; s < kStates; ++s) {
				render_rotozoom(states[s], m_chunky[0].view, kChunkyW, kChunkyH);
				eng::graphics::rotozoom_into<64, 64>(texture(), states[s], m_chunky[1].view,
								    kChunkyW, kChunkyH);
				const eng::u8* a = m_chunky[0].view.data();
				const eng::u8* b = m_chunky[1].view.data();
				for (eng::u32 i = 0; i < pixels; ++i) {
					if (a[i] != b[i]) {
						eng::debug::mark_failed(g_eng_run_status,
									(s == 0u) ? 0x00006105u
										  : (0x00006107u + s));
						return false;
					}
				}
			}
			render_rotozoom(id, m_chunky[0].view, kChunkyW, kChunkyH);
		}
#endif
		const eng::ChunkyBuffer src = m_chunky[0].view;
		eng::u8* planes = m_scene.buffer(0).data();
		c2p_1x1_4_asm(kChunkyW, kChunkyH, m_scene.plane_bytes(), src.data(), planes);
		eng::graphics::c2p_1x1_4(kChunkyW, kChunkyH, kPlaneBytes, src.as_const(),
					 m_ref.view);

		const eng::u8* ref = m_ref.view.data();
		eng::u32 diffs = 0;
		for (eng::u32 p = 0; p < kPlanes; ++p) {
			for (eng::u32 i = 0; i < kPlaneBytes; ++i) {
				if (ref[p * kPlaneBytes + i] != planes[p * m_scene.plane_bytes() + i]) {
					++diffs;
				}
			}
		}

		m_rot = eng::graphics::Rotozoom {0, 65536, 0, 0};
		m_scene.takeover(backend);
		// `detail` = bytes distintos entre la asm y la referencia C++ (0 = identicos).
		eng::debug::mark_ready(g_eng_run_status, diffs);
		return true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		(void)backend;
		// Animacion: rota, oscila el zoom y desplaza la textura (paneo diagonal).
		m_angle = static_cast<eng::u16>((m_angle + 2u) & 0xffu);
		m_phase = static_cast<eng::u16>((m_phase + 3u) & 0xffu);
		m_rot.angle = m_angle;
		m_rot.zoom = 98304 + kZoomSin[static_cast<eng::u8>(m_phase)];
		m_rot.offset_x += 12288;
		m_rot.offset_y += 7168;

		const eng::u8 buf = m_scene.back_index();
		render_rotozoom(m_rot, m_chunky[buf % 2u].view, kChunkyW, kChunkyH);
		c2p_1x1_4_asm(kChunkyW, kChunkyH, m_scene.plane_bytes(),
			      m_chunky[buf].view.data(),
			      m_scene.back().data());
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		// Swap de copperlist tras VBlank (el motor lo garantiza antes de `render`):
		// el display muestra el buffer recien convertido, nunca el que se escribe.
		m_scene.commit();
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	static eng::IndexedTexture texture() {
		return eng::IndexedTexture {kTexture.data(), kTexture.size()};
	}

	scene::Scene m_scene {};
	eng::Block<eng::ChunkyTag> m_chunky[2] {};
	eng::Block<eng::PlaneTag> m_ref {};
	eng::graphics::Rotozoom m_rot {};
	eng::u16 m_angle = 0;
	eng::u16 m_phase = 0;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	RotozoomDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
