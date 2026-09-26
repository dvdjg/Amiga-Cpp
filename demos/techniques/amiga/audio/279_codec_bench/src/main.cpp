// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/audio/279_codec_bench --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/audio/279_codec_bench
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/audio/279_codec_bench --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/audio/279_codec_bench

// ============================================================================
// Demo 279: banco de los descompresores de audio (ASM vs C++).
// ============================================================================
//
// Mide muestras/s de las rutinas ASM (`support/codec_asm.s`) y de la referencia C++, con el
// reloj TOD de la CIA-A (50 Hz), y comprueba que la salida coincide. Se ejecuta en `init` y se
// dibuja en pantalla; publica `detail = 0` si ASM == C++ en todos los codecs (bit por codec si
// difiere), para que la regresion lo valide sin depender de la imagen.
//
//   bit 0 = Fibonacci difiere   bit 1 = IMA difiere   bit 2 = integracion delta difiere
//
// Build/run:
//   bash tools/build/build-demo.sh demos/techniques/amiga/audio/279_codec_bench --release --clean
//   bash tools/run/run-demo.sh demos/techniques/amiga/audio/279_codec_bench --warp

#include <eng/api/api.hpp>
#include <eng/core/util/intmath.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/text.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <eng/audio/asm_codec.hpp>
#include <eng/audio/fib_delta.hpp>
#include <eng/audio/ima_adpcm.hpp>
#include <eng/audio/pcm_codec.hpp>

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

namespace {

using eng::audio::pcm_codec::Codec;

constexpr eng::usize kSamples = 4096u;
constexpr eng::usize kFibBytes = 2u + kSamples / 2u;
constexpr eng::usize kImaBytes = 4u + kSamples / 2u;
constexpr eng::u32 kWindowTicks = 100u; // 2 s emulados (50 Hz) por medida

// Buffers estaticos (no pila): la sintesis y las decodificaciones son grandes.
eng::u8 g_pcm[kSamples] {};
eng::u8 g_fib[kFibBytes] {};
eng::u8 g_ima[kImaBytes] {};
eng::u8 g_out[kSamples] {};
eng::u8 g_ref[kSamples] {};
eng::u8 g_delta[kSamples] {};

void make_pcm() {
	for (eng::usize i = 0u; i < kSamples; ++i) {
		const eng::usize t = i & 63u;
		const int v = (t < 32) ? -40 + 2 * static_cast<int>(t)
				       : -40 + 2 * (63 - static_cast<int>(t));
		g_pcm[i] = static_cast<eng::u8>(static_cast<eng::s8>(v));
	}
}

struct CodecBench {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_backend = &backend;
		if (!backend.configure_memory({32u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027901u);
			return;
		}
		make_pcm();
		const eng::s32 fn = eng::audio::fib_delta::encode(
		    eng::Span<const eng::u8>(g_pcm, kSamples), eng::Span<eng::u8>(g_fib, kFibBytes));
		const eng::s32 in = eng::audio::ima_adpcm::encode(
		    eng::Span<const eng::u8>(g_pcm, kSamples), eng::Span<eng::u8>(g_ima, kImaBytes));
		if (fn <= 0 || in <= 0) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027902u);
			return;
		}
		for (eng::usize i = 0u; i < kSamples; ++i) {
			g_delta[i] = g_pcm[i];
		}
		eng::audio::pcm_codec::differentiate(eng::Span<eng::u8>(g_delta, kSamples));

		// Correccion: ASM == C++ en los tres.
		const eng::s32 nfa = eng::audio::asm_codec::fib_delta_decode(
		    eng::Span<const eng::u8>(g_fib, kFibBytes), eng::Span<eng::u8>(g_out, kSamples));
		const eng::s32 nfr = eng::audio::fib_delta::decode(
		    eng::Span<const eng::u8>(g_fib, kFibBytes), eng::Span<eng::u8>(g_ref, kSamples));
		if (nfa != nfr) {
			m_detail |= 1u;
		}
		const eng::s32 nia = eng::audio::asm_codec::ima_adpcm_decode(
		    eng::Span<const eng::u8>(g_ima, kImaBytes), eng::Span<eng::u8>(g_out, kSamples));
		const eng::s32 nir = eng::audio::ima_adpcm::decode(
		    eng::Span<const eng::u8>(g_ima, kImaBytes), eng::Span<eng::u8>(g_ref, kSamples));
		if (nia != nir) {
			m_detail |= 2u;
		}
		// Deltas: ASM in-place vs C++ in-place sobre copias identicas.
		eng::u8 a2[kSamples] {};
		eng::u8 r2[kSamples] {};
		for (eng::usize i = 0u; i < kSamples; ++i) {
			a2[i] = g_delta[i];
			r2[i] = g_delta[i];
		}
		eng::audio::asm_codec::delta_integrate(eng::Span<eng::u8>(a2, kSamples));
		eng::audio::pcm_codec::integrate_deltas(eng::Span<eng::u8>(r2, kSamples));
		for (eng::usize i = 0u; i < kSamples; ++i) {
			if (a2[i] != r2[i]) {
				m_detail |= 4u;
				break;
			}
		}

		// Rendimiento: muestras/s de cada via.
		m_fib_asm = measure(fn, true, 0u);
		m_fib_cpp = measure(fn, false, 0u);
		m_ima_asm = measure(in, true, 1u);
		m_ima_cpp = measure(in, false, 1u);
		m_delta_asm = measure(0, true, 2u);
		m_delta_cpp = measure(0, false, 2u);

		eng::debug::mark_ready(g_eng_run_status, 0x00027900u | m_detail);
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		auto& d = backend.debug();
		d.clear();
		d.filled_rect(20, 20, 620, 210, 0x00081018);
		d.rect(20, 20, 620, 210, 0x00ffffff);
		d.text(36, 36, "codec bench (muestras/s; asm vs cpp)", 0x00ffffff);
		auto line = [&](eng::s16 y, const char* label, eng::u32 asmr, eng::u32 cppr,
				eng::u32 col) {
			char buf[56];
			char* p = buf;
			while (*label != '\0') {
				*p++ = *label++;
			}
			eng::util::StaticString<16> a;
			eng::util::StaticString<16> c;
			(void)eng::util::to_chars_u32(a, asmr);
			(void)eng::util::to_chars_u32(c, cppr);
			*p++ = ' ';
			for (const char* v = a.c_str(); *v != '\0';) {
				*p++ = *v++;
			}
			*p++ = ' ';
			*p++ = '/';
			*p++ = ' ';
			for (const char* v = c.c_str(); *v != '\0';) {
				*p++ = *v++;
			}
			*p = '\0';
			d.text(36, y, buf, col);
		};
		line(66, "fib  asm/cpp:", m_fib_asm, m_fib_cpp, 0x00ffff00);
		line(96, "ima  asm/cpp:", m_ima_asm, m_ima_cpp, 0x00ffc040);
		line(126, "delt asm/cpp:", m_delta_asm, m_delta_cpp, 0x0080ff80);
		eng::debug::probe_when_ready(g_eng_run_status, 0u);
	}

private:
	/// Mide muestras/s de `which` (0=fib, 1=ima, 2=delta) en la ventana `kWindowTicks`.
	eng::u32 measure(eng::s32 stream_bytes, bool use_asm, eng::u8 which) {
		const eng::u32 start = m_backend->cia_tod_ticks();
		eng::u32 elapsed = 0u;
		eng::u32 total = 0u;
		while (elapsed < kWindowTicks) {
			if (which == 0u) {
				if (use_asm) {
					(void)eng::audio::asm_codec::fib_delta_decode(
					    eng::Span<const eng::u8>(g_fib, static_cast<eng::usize>(stream_bytes)),
					    eng::Span<eng::u8>(g_out, kSamples));
				} else {
					(void)eng::audio::fib_delta::decode(
					    eng::Span<const eng::u8>(g_fib, static_cast<eng::usize>(stream_bytes)),
					    eng::Span<eng::u8>(g_out, kSamples));
				}
			} else if (which == 1u) {
				if (use_asm) {
					(void)eng::audio::asm_codec::ima_adpcm_decode(
					    eng::Span<const eng::u8>(g_ima, static_cast<eng::usize>(stream_bytes)),
					    eng::Span<eng::u8>(g_out, kSamples));
				} else {
					(void)eng::audio::ima_adpcm::decode(
					    eng::Span<const eng::u8>(g_ima, static_cast<eng::usize>(stream_bytes)),
					    eng::Span<eng::u8>(g_out, kSamples));
				}
			} else {
				// Integracion delta in-place sobre g_out (contenido irrelevante para el ritmo).
				if (use_asm) {
					eng::audio::asm_codec::delta_integrate(
					    eng::Span<eng::u8>(g_out, kSamples));
				} else {
					eng::audio::pcm_codec::integrate_deltas(
					    eng::Span<eng::u8>(g_out, kSamples));
				}
			}
			total += static_cast<eng::u32>(kSamples);
			elapsed = (m_backend->cia_tod_ticks() - start) & 0x00ffffffu;
		}
		return elapsed == 0u ? 0u : eng::util::div32(total * 50u, elapsed);
	}

	eng::amiga::AmigaBackend* m_backend = nullptr;
	eng::u32 m_fib_asm = 0u, m_fib_cpp = 0u;
	eng::u32 m_ima_asm = 0u, m_ima_cpp = 0u;
	eng::u32 m_delta_asm = 0u, m_delta_cpp = 0u;
	eng::u32 m_detail = 0u;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	static CodecBench game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);
	return 0;
}
