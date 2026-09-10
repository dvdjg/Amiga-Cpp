// ============================================================================
// Demo 060: "music pt" — música Protracker (.mod) por PTPlayer (Frank Wille).
// ============================================================================
//
// Demuestra la capa de música con un módulo Protracker generado en memoria
// (sin activo binario): un tono cuadrado en bucle sobre una nota C-2, reproducido
// por `eng::audio::PtPlayer` (modo CIA). Completa el `MusicPlayer` de
// `ENGINE_DESIGN.md` §2.6 junto al SFX mixer y P61.
//
// Evidencia: `mark_ready` guarda DMACONR (bits altos) + nº de canales del mixer
// (bits bajos). Con música activa, DMACONR muestra los bits AUDxEN de los canales
// que usa ptplayer.

#include <eng/audio/music_player.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
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

namespace {

constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 6;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

// Módulo Protracker mínimo: 1 muestra + 1 patrón (64 filas, nota C-2).
constexpr eng::u32 kModSize = 2364;   // 20 + 31*30 + 1+1+128 + 4 + 1024 + 256
constexpr eng::u32 kSampleOffset = 2108; // patrón 0 (1084..2108) + muestras
constexpr eng::u32 kSampleLen = 256;

struct MusicPtDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			96u * 1024u,
			8u * 1024u,
			4u * 1024u,
		});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006001u);
			return;
		}

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		m_mod_block = backend.memory().chip.allocate(kModSize, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_mod_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006002u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);

		build_mod(static_cast<eng::u8*>(m_mod_block.data));

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006003u);
			return;
		}

		backend.takeover_display(m_copper_ptr);

		// Música: PtPlayer (modo CIA). El orden canónico es música antes que mixer.
		eng::audio::MusicModule mod {
			eng::Span<const eng::u8>(static_cast<const eng::u8*>(m_mod_block.data), kModSize)
		};
		m_music_ok = m_music.play(mod);

		// No se marca READY todavía: se confirma en update() cuando el DMA de audio
		// de la música esté activo (el primer _mt_dmaon llega tras la 1.ª interrupción CIA).
		m_init_ok = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok || m_confirmed) {
			return;
		}
		// Tras 100 frames la interrupción CIA ya ha debido disparar _mt_dmaon.
		if (context.frame.frame_index == 100u) {
			const eng::u16 dmaconr = *reinterpret_cast<volatile eng::u16*>(0xdff002u);
			if ((dmaconr & 0x0Fu) != 0u) {
				eng::debug::mark_ready(g_eng_run_status, (static_cast<eng::u32>(dmaconr) << 16u) | 1u);
			} else {
				// Diagnóstico: SR (máscara de interrupciones) + INTREQR + DMACONR.
				eng::u16 sr = 0;
				__asm__ volatile("move.w %%sr,%0" : "=d"(sr) : : "cc");
				const eng::u16 intreqr = *reinterpret_cast<volatile eng::u16*>(0xdff01eu);
				eng::debug::mark_failed(g_eng_run_status,
					(static_cast<eng::u32>(dmaconr) << 16u) | (static_cast<eng::u32>(sr & 0xffu) << 8u) | (intreqr & 0x00ffu));
			}
			m_confirmed = true;
		}
		(void)backend;
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Construye un módulo Protracker mínimo (M.K.): 1 muestra (onda cuadrada en
	/// bucle) y 1 patrón de 64 filas con la nota C-2 (período 428) en el canal 0.
	void build_mod(eng::u8* m) {
		for (eng::u32 i = 0; i < kModSize; ++i) {
			m[i] = 0;
		}

		// Cabecera de la muestra 1 (offset 20): nombre vacío, 128 words, volumen 40,
		// bucle completo (loop start 0, loop length 128 words).
		const eng::u32 h = 20;
		m[h + 22] = 0x00; m[h + 23] = 0x80; // length = 128 words
		m[h + 25] = 40;                      // volume
		m[h + 28] = 0x00; m[h + 29] = 0x80; // loop length = 128 words

		// Canción: 1 patrón, reinicio 127, orden {0}.
		m[950] = 1;
		m[951] = 127;
		m[952] = 0;

		// Firma "M.K.".
		m[1080] = 'M'; m[1081] = '.'; m[1082] = 'K'; m[1083] = '.';

		// Patrón 0 (offset 1084): 64 filas × 4 canales × 4 bytes. Nota C-2
		// (período 428 = 0x1AC) en el canal 0, muestra 1.
		for (eng::u32 row = 0; row < 64; ++row) {
			const eng::u32 base = 1084 + row * 16;
			m[base + 0] = 0x01; // sample high nibble 0, period high nibble 1
			m[base + 1] = 0x1A; // sample low nibble 1, period 0xA
			m[base + 2] = 0xC0; // period low nibble 0xC, efecto 0
			m[base + 3] = 0x00;
		}

		// Datos de la muestra (offset 2108): onda cuadrada ±48 (8 bits con signo).
		for (eng::u32 i = 0; i < kSampleLen; ++i) {
			m[kSampleOffset + i] = (i < kSampleLen / 2) ? 48u : static_cast<eng::u8>(256u - 48u);
		}
	}

	bool build_copper() {
		eng::copper::Scheduler sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,
			kBytesPerRow, 0x6200, kPlanes, m_bitplanes, kPlaneBytes
		);
		for (eng::u8 i = 0; i < 32; ++i) {
			sched.move(eng::copper::color_register(i), 0x0000);
		}
		sched.wait_line(0xf8);
		sched.move(eng::copper::Register::COLOR00, 0x0000);
		sched.end();

		m_copper_ok = sched.ok();
		m_copper_words = sched.words_used();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	bool m_memory_ok = false;
	bool m_copper_ok = false;
	bool m_music_ok = false;
	bool m_init_ok = false;
	bool m_confirmed = false;
	eng::u16 m_copper_words = 0;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_mod_block {};
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
	eng::audio::PtPlayer m_music {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	MusicPtDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
