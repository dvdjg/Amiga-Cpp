// Demo 213 - "Bartman Abyss": port de la demo clásica de Bartman/vscode-amiga-debug
// (`BartmanBasic/main.c`) al engine, usando el **mini-SO** (`eng::os`) como bucle reactivo.
//
// Reproduce la demo original:
//   - Escena 320x256 de **5 planos interleaved** con la imagen "abyss" (assets/amiga/sprites/abyss).
//   - **Copper**: paleta de 32 colores, degradado de COLOR00 en las líneas 0x41..0x4f y
//     fine-scroll horizontal por BPLCON1 movido por seno.
//   - **16 BOBs enmascarados** (cookie-cut `$CA`) desplazados por senos (una sola pasada por BOB,
//     aprovechando el layout interleaved del BOB original: A=máscara, B=imagen).
//   - **Música P61** (ThePlayer) avanzada una vez por frame.
//   - El juego **no sondea hardware**: el latido del mini-SO (`os::tick`) latcha el VBlank y
//     pollea la entrada; el **botón izquierdo del ratón** (mensaje `MouseButton`) termina la demo.
//
// El bitmap es el propio blob incrustado en **Chip** (se dibuja *in place*, como el original, que
// no usa doble buffer); la copperlist se construye con `copper::SchedulerT` y se parchea BPLCON1
// por frame escribiendo su word de dato.
//
// Ver `docs/guides/roadmap/ROADMAP_MINI_OS.md`, `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md`
// y `docs/engine/architecture/MUSIC_PLAYER.md`.

#include <eng/api/api.hpp>
#include <eng/audio/music_player.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/os/message_pump.hpp>
#include <eng/os/os.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
// Seccion propia: al crecer `.rodata` con los assets, el orden de hunks runtime deja de casar
// con el `.map` y el runner no resuelve `g_eng_run_status` por indice; al ser el unico simbolo de
// su seccion, su direccion coincide con la base de un hunk y el runner la encuentra escaneando.
__attribute__((used, section(".eng_run_status"))) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

// --- Assets incrustados -----------------------------------------------------
// Los blobs van a `.rodata` (seccion estandar, sin hunks extra que descoloquen la
// relocalizacion de simbolos del runner) y en `init` se **copian a Chip** con el CPU: el Blitter
// necesita Chip, pero el origen puede vivir en cualquier RAM. Así la demo funciona también en
// máquinas con Fast RAM.
__asm__(".section .rodata\n.balign 4\n"
	".globl g_abyss_img\ng_abyss_img:\n"
	".incbin \"assets/amiga/sprites/abyss/abyss.bpl\"\n"
	".globl g_abyss_img_end\ng_abyss_img_end:\n"
	".globl g_abyss_bob\ng_abyss_bob:\n"
	".incbin \"assets/amiga/sprites/abyss/bob.bpl\"\n"
	".globl g_abyss_bob_end\ng_abyss_bob_end:\n"
	".globl g_abyss_mod\ng_abyss_mod:\n"
	".incbin \"assets/amiga/audio/testmod.p61\"\n"
	".globl g_abyss_mod_end\ng_abyss_mod_end:\n"
	".globl g_abyss_pal\ng_abyss_pal:\n"
	".incbin \"assets/amiga/sprites/abyss/abyss.pal\"\n"
	".globl g_abyss_pal_end\ng_abyss_pal_end:\n.balign 2\n");

extern "C" const eng::u8 g_abyss_img[];
extern "C" const eng::u8 g_abyss_img_end[];
extern "C" const eng::u8 g_abyss_bob[];
extern "C" const eng::u8 g_abyss_bob_end[];
extern "C" const eng::u8 g_abyss_mod[];
extern "C" const eng::u8 g_abyss_mod_end[];
extern "C" const eng::u16 g_abyss_pal[];

namespace {

namespace copper = eng::copper;

constexpr eng::u16 kWidth = 320;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 5;
constexpr eng::u16 kBytesPerRow = kWidth / 8u;          // 40
constexpr eng::u16 kRowStride = kBytesPerRow * kPlanes; // 200 (interleaved)
constexpr eng::u32 kBitmapBytes = static_cast<eng::u32>(kRowStride) * kHeight; // 51200

constexpr eng::u16 kBobW = 32;
constexpr eng::u16 kBobH = 16;
constexpr eng::u16 kBobWords = kBobW / 16u;              // 2 palabras/fila
constexpr eng::u32 kBobFrameStride = kBobH * kPlanes * 2u * kBobWords * 2u; // 640 B

// Senos de la demo original (BartmanBasic/main.c).
constexpr eng::u8 kSinus15[64] = {
	8, 8, 9, 10, 10, 11, 12, 12, 13, 13, 14, 14, 14, 15, 15, 15,
	15, 15, 15, 15, 14, 14, 14, 13, 13, 12, 12, 11, 10, 10, 9, 8,
	8, 7, 6, 5, 5, 4, 3, 3, 2, 2, 1, 1, 1, 0, 0, 0,
	0, 0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 7,
};
constexpr eng::u8 kSinus40[64] = {
	20, 22, 24, 26, 28, 30, 31, 33, 34, 36, 37, 38, 39, 39, 40, 40,
	40, 40, 39, 39, 38, 37, 36, 35, 34, 32, 30, 29, 27, 25, 23, 21,
	19, 17, 15, 13, 11, 10, 8, 6, 5, 4, 3, 2, 1, 1, 0, 0,
	0, 0, 1, 1, 2, 3, 4, 6, 7, 9, 10, 12, 14, 16, 18, 20,
};
constexpr eng::u8 kSinus32[51] = {
	16, 18, 20, 22, 24, 25, 27, 28, 30, 30, 31, 32, 32, 32, 32, 31,
	30, 30, 28, 27, 25, 24, 22, 20, 18, 16, 14, 12, 10, 8, 7, 5,
	4, 2, 2, 1, 0, 0, 0, 0, 1, 2, 2, 4, 5, 7, 8, 10,
	12, 14, 16,
};

struct AbyssDemo {
	bool init(eng::amiga::AmigaBackend& backend) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_backend = &backend;

		if (!backend.configure_memory({96u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021301u);
			return false;
		}

		// Copia los assets a **Chip** (el Blitter y P61 los leen por DMA): imagen (bitmap),
		// BOB (máscara+imagen) y módulo. El origen en `.rodata` puede estar en Fast RAM.
		const eng::u32 img_bytes = static_cast<eng::u32>(g_abyss_img_end - g_abyss_img);
		const eng::u32 bob_bytes = static_cast<eng::u32>(g_abyss_bob_end - g_abyss_bob);
		const eng::u32 mod_bytes = static_cast<eng::u32>(g_abyss_mod_end - g_abyss_mod);
		if (img_bytes < kBitmapBytes) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021302u);
			return false;
		}
		m_image = backend.memory().chip.allocate_block<eng::PlaneTag>(kBitmapBytes + 16u, 16u);
		m_bob_block = backend.memory().chip.allocate_block<eng::BobTag>(bob_bytes + 16u, 4u);
		m_mod_block = backend.memory().chip.allocate_block<eng::MusicTag>(mod_bytes + 16u, 4u);
		m_copper = backend.memory().chip.allocate_block<eng::CopperTag>(2048u, 16u);
		if (!m_image.valid() || !m_bob_block.valid() || !m_mod_block.valid() || !m_copper.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021305u);
			return false;
		}
		memcpy(m_image.view.data(), g_abyss_img, kBitmapBytes);
		memcpy(m_bob_block.view.data(), g_abyss_bob, bob_bytes);
		memcpy(m_mod_block.view.data(), g_abyss_mod, mod_bytes);
		m_bitmap = m_image.view.data();
		m_bob = m_bob_block.view.data();

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021303u);
			return false;
		}

		backend.takeover_display(m_copper_words);

		// Música P61 (ThePlayer): si el módulo trae samples empaquetados, se reserva el buffer
		// que exige `P61_Init`.
		const eng::Span<const eng::u8> mod(m_mod_block.view.data(), mod_bytes);
		eng::Span<eng::u8> sbuf {};
		if (eng::audio::p61_needs_sample_buffer(mod)) {
			const eng::u32 need = eng::audio::p61_sample_buffer_size(mod);
			m_sample = backend.memory().chip.allocate_block<eng::AudioTag>(need, 4u);
			if (!m_sample.valid()) {
				eng::debug::mark_failed(g_eng_run_status, 0x00021304u);
				return false;
			}
			sbuf = eng::Span<eng::u8>(m_sample.view.data(), need);
		}
		m_music_ok = m_music.play(eng::audio::MusicModule {mod}, sbuf);

		eng::debug::mark_ready(g_eng_run_status,
				       (m_music_ok ? 0x00020000u : 0u) | 0x00002130u);
		return true;
	}

	/// Un frame de la demo: avanza la música, parchea el fine-scroll, limpia la banda y dibuja
	/// los 16 BOBs. Se ejecuta entre latidos del mini-SO (`os::tick`).
	void frame(eng::u32 f) {
		m_music.update();

		const eng::u8 sin = kSinus15[f & 63u];
		m_copper_words[m_scroll_index] = static_cast<eng::u16>(sin | (sin << 4u));

		// Limpia la banda de juego (filas 200..255, los 5 planos) en un solo blit D-only.
		eng::graphics::BlitJob clear {};
		clear.kind = eng::graphics::BlitJobKind::ClearRect;
		clear.destination = eng::graphics::BlitDest {
			reinterpret_cast<eng::u16*>(m_bitmap + static_cast<eng::u32>(kRowStride) * 200u)};
		clear.words_per_row = kWidth / 16u; // 20
		clear.height = static_cast<eng::u16>(56u * kPlanes); // 280 filas interleaved
		clear.destination_modulo_bytes = 0;
		clear.bitplane_count = 1u;
		clear.minterm = 0x00u;
		clear.interleaved = true;
		(void)m_backend->blitter_submit(clear, true);

		// 16 BOBs enmascarados por senos. Fuente interleaved del BOB original: A=máscara,
		// B=imagen; un solo blit de `16*planos` filas (el truco del cookie-cut interleaved).
		// Fase del seno de 51 posiciones con avance incremental (sin `% 51` por BOB, que en
		// 68000 es un `__umodsi3` costoso).
		eng::u32 phase = f % 51u;
		eng::u8 fi = 0u;
		for (eng::u16 i = 0u; i < 16u; ++i) {
			const eng::u32 sa = kSinus32[phase];
			const eng::s16 x =
				static_cast<eng::s16>(static_cast<eng::u32>(i) * 16u + sa * 2u);
			const eng::s16 y = static_cast<eng::s16>(kSinus40[((f + i) * 2u) & 63u] / 2u);
			const eng::u8* const src = m_bob + static_cast<eng::u32>(fi) * kBobFrameStride;
			if (++phase >= 51u) {
				phase = 0u;
			}
			if (++fi >= 6u) {
				fi = 0u;
			}

			eng::graphics::BlitJob bob {};
			bob.kind = eng::graphics::BlitJobKind::MaskedBobCookieCut;
			bob.mask = eng::graphics::BlitSource {reinterpret_cast<const eng::u16*>(src)};
			bob.source =
				eng::graphics::BlitSource {reinterpret_cast<const eng::u16*>(src + 4)};
			bob.destination = eng::graphics::BlitDest {
				reinterpret_cast<eng::u16*>(m_bitmap +
							    static_cast<eng::u32>(kRowStride) *
								    static_cast<eng::u16>(200 + y) +
							    static_cast<eng::u32>(x >> 3))};
			bob.words_per_row = kBobWords;
			bob.height = static_cast<eng::u16>(kBobH * kPlanes); // 80
			bob.source_modulo_bytes = 4;
			// Cada fila del blit es una fila de UN plano del interleaved (40 B): lee/escribe
			// 2 palabras y salta a la siguiente (DMOD = 40 - 4 = 36), como el original.
			bob.destination_modulo_bytes = static_cast<eng::s16>(kBytesPerRow - 4);
			bob.bitplane_count = 1u;
			bob.source_shift = static_cast<eng::u8>(x & 15);
			bob.minterm = 0xCAu;
			bob.interleaved = true;
			(void)m_backend->blitter_submit(bob, true);
		}

		// Overlay de depuración de WinUAE (no aparece en la captura del framebuffer).
		auto& d = m_backend->debug();
		d.clear();
		d.filled_rect(static_cast<eng::s16>(f + 100u), 400,
			      static_cast<eng::s16>(f + 400u), 440, 0x0000ff00);
		d.rect(static_cast<eng::s16>(f + 90u), 380, static_cast<eng::s16>(f + 400u), 440,
		       0x000000ff);
		d.text(static_cast<eng::s16>(f + 130u), 418,
		       "abyss + mini-OS (raton izq. para salir)", 0x00ff00ff);
	}

	void on_msg(const eng::os::Msg& m) {
		switch (m.type) {
		case eng::os::MsgType::MouseButton:
			// Bit 0 = botón izquierdo (como `MouseLeft()` del original).
			if ((m.payload.mouse.buttons & 0x01u) != 0u) {
				m_quit = true;
			}
			break;
		case eng::os::MsgType::KeyDown:
			if ((m.payload.key.code & 0x7fu) == 0x45u) { // ESC
				m_quit = true;
			}
			break;
		default:
			break;
		}
	}

	void shutdown() { m_music.stop(); }

	[[nodiscard]] bool quit() const { return m_quit; }

private:
	bool build_copper() {
		copper::SchedulerT<false> sched {m_copper};
		sched.move(copper::Register::DMACON,
			   static_cast<eng::u16>(copper::DmaSetClear | copper::DmaMaster |
						 copper::DmaCopper | copper::DmaBitplane |
						 copper::DmaBlitter));
		sched.move(copper::Register::BPLCON0, 0x5200u); // 5 planos + COLOR
		// BPLCON1 (fine scroll): se parchea por frame; guardamos el índice de su word de dato.
		sched.move(copper::Register::BPLCON1, 0x0000u);
		m_scroll_index = static_cast<eng::u16>(sched.words_used() - 1u);
		sched.move(copper::Register::BPLCON2, 1u << 6u); // prioridad de playfield
		sched.move(copper::Register::BPL1MOD, static_cast<eng::u16>(kRowStride - kBytesPerRow));
		sched.move(copper::Register::BPL2MOD, static_cast<eng::u16>(kRowStride - kBytesPerRow));
		sched.move(copper::Register::DIWSTRT, 0x2c81u);
		sched.move(copper::Register::DIWSTOP, 0x2cc1u);
		sched.move(copper::Register::DDFSTRT, 0x0038u);
		sched.move(copper::Register::DDFSTOP, 0x00d0u);
		for (eng::u8 p = 0u; p < kPlanes; ++p) {
			sched.move_bitplane_pointer(
				p, eng::ChipAddress {reinterpret_cast<eng::uintptr>(m_bitmap) +
						     static_cast<eng::u32>(p) * kBytesPerRow});
		}
		for (eng::u8 i = 0u; i < 32u; ++i) {
			sched.move(copper::color_register(i), g_abyss_pal[i]);
		}
		// Degradado de COLOR00 en las líneas 0x41..0x4f (como `copper2` del original).
		for (eng::u8 k = 0u; k < 15u; ++k) {
			sched.wait_line(static_cast<eng::u8>(0x41u + k));
			const eng::u16 v = static_cast<eng::u16>(0x0111u * (k + 1u));
			sched.move(copper::Register::COLOR00, v);
		}
		sched.end();
		m_copper_words = sched.data();
		m_copper_ok = sched.ok();
		return m_copper_ok;
	}

	eng::amiga::AmigaBackend* m_backend = nullptr;
	eng::u8* m_bitmap = nullptr;
	const eng::u8* m_bob = nullptr;
	eng::u16* m_copper_words = nullptr;
	eng::u16 m_scroll_index = 0;
	eng::Block<eng::PlaneTag> m_image {};
	eng::Block<eng::BobTag> m_bob_block {};
	eng::Block<eng::MusicTag> m_mod_block {};
	eng::Block<eng::CopperTag> m_copper {};
	eng::Block<eng::AudioTag> m_sample {};
	eng::audio::P61Player m_music {};
	bool m_copper_ok = false;
	bool m_music_ok = false;
	bool m_quit = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	backend.boot();

	AbyssDemo game {};
	if (!game.init(backend)) {
		return 0;
	}

	// Mini-SO: habilita la entrada y usa `os::tick` como latido (VBlank + sondeo de entrada),
	// drenando el puerto cada frame. La salida es por el botón izquierdo del ratón.
	eng::os::input_enable(eng::os::InputAll);
	eng::os::MsgPort<32>& port = eng::os::system_port();

	eng::u32 frame = 0u;
	while (!game.quit()) {
		// Latido del mini-SO: latcha el VBlank, pollea la entrada y avanza los timers.
		eng::os::tick();
		eng::os::Msg m;
		while (port.pop(m)) {
			game.on_msg(m);
		}
		game.frame(frame);
		backend.wait_vblank();
		eng::debug::mark_frame(g_eng_run_status, frame);
		eng::debug::probe_when_ready(g_eng_run_status, frame);
		++frame;
	}

	game.shutdown();
	return 0;
}
