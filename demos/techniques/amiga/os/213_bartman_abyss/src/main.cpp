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
#include <eng/core/data/ct_array.hpp>
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
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
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

// Ondas **generadas en compilación** (`eng::ct_array`, patrón de la 107): una tabla de seno
// con valores `0..Max` en vez de la ristra de literales de la demo original. Aproximación entera
// de Bhaskara I (`sin(x°) ≈ 4x(180−x)/(40500 − x(180−x))`), suficiente para el vaivén de los BOBs.
template <eng::u16 N, eng::u8 Max>
[[nodiscard]] constexpr eng::ct_array<eng::u8, N> make_wave() noexcept {
	return eng::ct_array<eng::u8, N> {[](eng::usize i) -> eng::u8 {
		const eng::s32 deg = static_cast<eng::s32>((360u * i) / N) % 360;
		const bool neg = deg > 180;
		const eng::s32 x = neg ? deg - 180 : deg; // 0..180
		const eng::s32 p = x * (180 - x);
		const eng::s32 s = (4 * p * 256) / (40500 - p); // 0..256
		const eng::s32 v = neg ? -s : s;                // -256..256
		return static_cast<eng::u8>(((v + 256) * Max) / 512);
	}};
}
constexpr auto kWaveScroll = make_wave<64u, 15u>(); // fine-scroll de BPLCON1 (0..15)
constexpr auto kWaveY = make_wave<64u, 40u>();      // desplazamiento vertical (0..40)
constexpr auto kWaveX = make_wave<51u, 32u>();      // desplazamiento horizontal (0..32)

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
		// Carga tipada a Chip (`res::load` fija medio y alineación por dominio: planos/BOB a
		// 16, módulo a 4); el origen en `.rodata` puede estar en Fast RAM.
		m_image = eng::res::load<eng::PlaneTag>(
			backend.memory(), eng::Span<const eng::u8> {g_abyss_img, kBitmapBytes});
		m_bob_block = eng::res::load<eng::BobTag>(
			backend.memory(), eng::Span<const eng::u8> {g_abyss_bob, bob_bytes});
		m_mod_block = eng::res::load<eng::MusicTag>(
			backend.memory(), eng::Span<const eng::u8> {g_abyss_mod, mod_bytes});
		m_copper = backend.memory().chip.allocate_block<eng::CopperTag>(2048u, 16u);
		if (!m_image.valid() || !m_bob_block.valid() || !m_mod_block.valid() || !m_copper.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021305u);
			return false;
		}
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

		// La música es una **tarea de frame del mini-SO**: se avanza dentro del tick (IRQ de
		// VBlank, vía `os::start_vblank_irq`) y postea `MusicEnd` al puerto cuando termina.
		eng::os::set_frame_task(&AbyssDemo::music_task, this);

		eng::debug::mark_ready(g_eng_run_status,
				       (m_music_ok ? 0x00020000u : 0u) | 0x00002130u);
		return true;
	}

	/// Un frame de la demo: parchea el fine-scroll, limpia la banda y dibuja los 16 BOBs. La
	/// música ya se avanza en el **tick del mini-SO** (IRQ), no aquí.
	void frame(eng::u32 f) {
		const eng::u8 sin = kWaveScroll[f & 63u];
		m_scroll.set(static_cast<eng::u16>(sin | (sin << 4u))); // fine-scroll por el PatchHandle

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

		// 16 BOBs enmascarados por las ondas (una sola pasada por BOB: el helper encapsula el
		// contrato `[máscara][imagen]` del layout interleaved). Fase con avance incremental
		// (sin `% 51` por BOB, que en 68000 es un `__umodsi3` costoso).
		eng::u32 phase = f % 51u;
		eng::u8 fi = 0u;
		for (eng::u16 i = 0u; i < 16u; ++i) {
			const eng::s16 x = static_cast<eng::s16>(
				static_cast<eng::u32>(i) * 16u + static_cast<eng::u32>(kWaveX[phase]) * 2u);
			const eng::s16 y = static_cast<eng::s16>(kWaveY[((f + i) * 2u) & 63u] / 2u);
			const eng::u8* const src = m_bob + static_cast<eng::u32>(fi) * kBobFrameStride;
			if (++phase >= 51u) {
				phase = 0u;
			}
			if (++fi >= 6u) {
				fi = 0u;
			}

			eng::graphics::BlitJob bob {};
			eng::graphics::make_interleaved_masked_bob(
				bob, reinterpret_cast<const eng::u16*>(src),
				reinterpret_cast<eng::u16*>(m_bitmap +
							    static_cast<eng::u32>(kRowStride) *
								    static_cast<eng::u16>(200 + y) +
							    static_cast<eng::u32>(x >> 3)),
				kBobW, kBobH, kPlanes, kBytesPerRow, static_cast<eng::u8>(x & 15));
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

	/// Tarea de frame del mini-SO (`os::set_frame_task`): avanza la música P61 y postea
	/// `MusicEnd` cuando el módulo termina. Corre en el tick (IRQ de VBlank).
	static void music_task(void* user, eng::u16) {
		auto* self = static_cast<AbyssDemo*>(user);
		self->m_music.update();
		if (self->m_music.ended()) {
			eng::os::Msg m {};
			m.type = eng::os::MsgType::MusicEnd;
			(void)eng::os::system_port().post(m);
		}
	}

private:
	bool build_copper() {
		// `m_sched` es **miembro** para que el `PatchHandle` del fine-scroll siga válido.
		m_sched.retarget(m_copper);
		m_sched.move(copper::Register::DMACON,
			     static_cast<eng::u16>(copper::DmaSetClear | copper::DmaMaster |
						   copper::DmaCopper | copper::DmaBitplane |
						   copper::DmaBlitter));
		m_sched.move(copper::Register::BPLCON0, 0x5200u); // 5 planos + COLOR
		// BPLCON1 (fine scroll): MOVE **parcheable** por frame (handle tipado del scheduler,
		// en vez de indexar palabras de copper a mano).
		m_scroll = m_sched.patchable(copper::Register::BPLCON1, 0x0000u);
		m_sched.move(copper::Register::BPLCON2, 1u << 6u); // prioridad de playfield
		m_sched.move(copper::Register::BPL1MOD, static_cast<eng::u16>(kRowStride - kBytesPerRow));
		m_sched.move(copper::Register::BPL2MOD, static_cast<eng::u16>(kRowStride - kBytesPerRow));
		m_sched.move(copper::Register::DIWSTRT, 0x2c81u);
		m_sched.move(copper::Register::DIWSTOP, 0x2cc1u);
		m_sched.move(copper::Register::DDFSTRT, 0x0038u);
		m_sched.move(copper::Register::DDFSTOP, 0x00d0u);
		for (eng::u8 p = 0u; p < kPlanes; ++p) {
			m_sched.move_bitplane_pointer(
				p, eng::Address<eng::MemoryKind::Chip>::from_storage(m_bitmap) +
					   static_cast<eng::u32>(p) * kBytesPerRow);
		}
		for (eng::u8 i = 0u; i < 32u; ++i) {
			m_sched.move(copper::color_register(i), g_abyss_pal[i]);
		}
		// Degradado de COLOR00 en las líneas 0x41..0x4f (como `copper2` del original).
		for (eng::u8 k = 0u; k < 15u; ++k) {
			m_sched.wait_line(static_cast<eng::u8>(0x41u + k));
			const eng::u16 v = static_cast<eng::u16>(0x0111u * (k + 1u));
			m_sched.move(copper::Register::COLOR00, v);
		}
		m_sched.end();
		m_copper_words = m_sched.data();
		m_copper_ok = m_sched.ok();
		return m_copper_ok;
	}

	eng::amiga::AmigaBackend* m_backend = nullptr;
	eng::u8* m_bitmap = nullptr;
	const eng::u8* m_bob = nullptr;
	eng::u16* m_copper_words = nullptr;
	copper::SchedulerT<false> m_sched {}; ///< emisor de la copperlist (miembro: el handle vive aquí)
	copper::PatchHandle m_scroll {};      ///< MOVE de BPLCON1 parcheado por frame
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

	// Mini-SO por **IRQ de VBlank** (`os::start_vblank_irq`): el input, los timers y la tarea de
	// frame (música) corren dentro de la IRQ; el bucle principal solo drena el puerto y renderiza.
	// La salida es por el botón izquierdo del ratón (mensaje `MouseButton`).
	eng::os::MsgPort<32>& port = eng::os::system_port();
	(void)eng::os::start_vblank_irq(backend, eng::os::InputAll);

	while (!game.quit()) {
		eng::os::Msg m;
		while (port.pop(m)) {
			game.on_msg(m);
		}
		const eng::u32 frame = eng::os::frame_count();
		game.frame(frame);
		backend.wait_vblank();
		eng::debug::mark_frame(g_eng_run_status, frame);
		eng::debug::probe_when_ready(g_eng_run_status, frame);
	}

	game.shutdown();
	return 0;
}
