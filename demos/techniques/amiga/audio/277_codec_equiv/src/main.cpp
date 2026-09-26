// ============================================================================
// Demo 277: equivalencia de los descompresores ASM 68000 (`support/codec_asm.s`).
// ============================================================================
//
// Gate de equivalencia (patrón del c2p de la demo 061): en el propio Amiga se descomprimen
// los MISMOS flujos con la vía ASM y con la **referencia C++**, y se comparan **byte a byte**.
// El resultado se publica en `g_eng_run_status.detail`:
//
//   bit 0 = Fibonacci Delta difiere     (ASM vs C++)
//   bit 1 = IMA ADPCM difiere
//   bit 2 = integracion delta difiere
//   bit 3 = ZX0 difiere
//   bit 4 = Delta+ZX0 difiere
//   bit 5 = (reservado; aPLib se verifica en HOST-362)
//   bit 6 = fallo de codificacion previa (los flujos de prueba no se generaron)
//
// `detail == 0` (y `0x000400FF` de READY) = ASM identico a la referencia.
//
// Build/run/analyze (mismos wrappers):
//   bash tools/build/build-demo.sh demos/techniques/amiga/audio/277_codec_equiv --release --clean
//   bash tools/run/run-demo.sh demos/techniques/amiga/audio/277_codec_equiv --warp

#include <eng/api/api.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/text.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <eng/audio/aplib.hpp>
#include <eng/audio/asm_codec.hpp>
#include <eng/audio/fib_delta.hpp>
#include <eng/audio/ima_adpcm.hpp>
#include <eng/audio/pcm_codec.hpp>
#include <eng/audio/zx0.hpp>

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

constexpr eng::usize kSamples = 256u;

// Flujo ZX0 real (compresor de referencia) de 32 bytes, de HOST-271: literales + match + EOF.
constexpr eng::u8 kZX0[] = {0x00, 0xF5, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
			    0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0xE0, 0xD5, 0x55, 0x60};
constexpr eng::usize kZX0Out = 32u;

// Flujo aPLib real (apultra) de 96 bytes periodicos b[i] = (i%8)*16, de HOST-362.
constexpr eng::u8 kAPLib[] = {0x00, 0x01, 0x10, 0x20, 0x30, 0x40, 0x50,
			      0x60, 0x70, 0x4E, 0x08, 0xF9, 0x80, 0x00};
constexpr eng::usize kAPLibOut = 96u;

// PCM de prueba determinista (triangular suave + un tramo "plano").
void make_pcm(eng::u8* pcm) noexcept {
	for (eng::usize i = 0u; i < kSamples; ++i) {
		const eng::usize t = i & 31u;
		const int v = (t < 16) ? -32 + 4 * static_cast<int>(t)
				       : -32 + 4 * (31 - static_cast<int>(t));
		pcm[i] = static_cast<eng::u8>(static_cast<eng::s8>(i < 64u ? 0 : v));
	}
}

struct CodecEquiv {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({4096, 4096, 1024});

		eng::u8 pcm[kSamples] {};
		make_pcm(pcm);

		// --- Fibonacci Delta ---
		eng::u8 fenc[2u + (kSamples / 2u)] {};
		const eng::s32 fn = eng::audio::fib_delta::encode(
		    eng::Span<const eng::u8>(pcm, kSamples), eng::Span<eng::u8>(fenc, sizeof(fenc)));
		if (fn <= 0) {
			m_detail = static_cast<eng::u32>(m_detail | 64u);
		} else {
			eng::u8 asm_out[kSamples] {};
			eng::u8 ref_out[kSamples] {};
			const eng::s32 na = eng::audio::asm_codec::fib_delta_decode(
			    eng::Span<const eng::u8>(fenc, static_cast<eng::usize>(fn)),
			    eng::Span<eng::u8>(asm_out, kSamples));
			const eng::s32 nr = eng::audio::fib_delta::decode(
			    eng::Span<const eng::u8>(fenc, static_cast<eng::usize>(fn)),
			    eng::Span<eng::u8>(ref_out, kSamples));
			if (na != nr || na != static_cast<eng::s32>(kSamples)) {
				m_detail = static_cast<eng::u32>(m_detail | 1u);
			} else {
				for (eng::usize i = 0u; i < kSamples; ++i) {
					if (asm_out[i] != ref_out[i]) {
						m_detail = static_cast<eng::u32>(m_detail | 1u);
						break;
					}
				}
			}
		}

		// --- IMA ADPCM ---
		eng::u8 ienc[4u + (kSamples / 2u)] {};
		const eng::s32 in = eng::audio::ima_adpcm::encode(
		    eng::Span<const eng::u8>(pcm, kSamples), eng::Span<eng::u8>(ienc, sizeof(ienc)));
		if (in <= 0) {
			m_detail = static_cast<eng::u32>(m_detail | 64u);
		} else {
			eng::u8 asm_out[kSamples] {};
			eng::u8 ref_out[kSamples] {};
			const eng::s32 na = eng::audio::asm_codec::ima_adpcm_decode(
			    eng::Span<const eng::u8>(ienc, static_cast<eng::usize>(in)),
			    eng::Span<eng::u8>(asm_out, kSamples));
			const eng::s32 nr = eng::audio::ima_adpcm::decode(
			    eng::Span<const eng::u8>(ienc, static_cast<eng::usize>(in)),
			    eng::Span<eng::u8>(ref_out, kSamples));
			if (na != nr || na != static_cast<eng::s32>(kSamples)) {
				m_detail = static_cast<eng::u32>(m_detail | 2u);
			} else {
				for (eng::usize i = 0u; i < kSamples; ++i) {
					if (asm_out[i] != ref_out[i]) {
						m_detail = static_cast<eng::u32>(m_detail | 2u);
						break;
					}
				}
			}
		}

		// --- Integracion delta ---
		eng::u8 deltas[kSamples] {};
		eng::u32 seed = 0xDEADBEEFu;
		for (eng::usize i = 0u; i < kSamples; ++i) {
			seed = seed * 1664525u + 1013904223u;
			deltas[i] = static_cast<eng::u8>(seed >> 24u);
		}
		eng::u8 asm_i[kSamples] {};
		eng::u8 ref_i[kSamples] {};
		for (eng::usize i = 0u; i < kSamples; ++i) {
			asm_i[i] = deltas[i];
			ref_i[i] = deltas[i];
		}
		eng::audio::asm_codec::delta_integrate(eng::Span<eng::u8>(asm_i, kSamples));
		eng::audio::pcm_codec::integrate_deltas(eng::Span<eng::u8>(ref_i, kSamples));
		for (eng::usize i = 0u; i < kSamples; ++i) {
			if (asm_i[i] != ref_i[i]) {
				m_detail = static_cast<eng::u32>(m_detail | 4u);
				break;
			}
		}

		// --- ZX0 (ASM vs referencia C++) ---
		{
			eng::u8 za[64] {};
			eng::u8 zr[64] {};
			const eng::s32 na = eng::audio::asm_codec::zx0_decompress(
			    eng::Span<const eng::u8>(kZX0, sizeof(kZX0)),
			    eng::Span<eng::u8>(za, sizeof(za)));
			const eng::s32 nr = eng::audio::zx0::decompress(
			    eng::Span<const eng::u8>(kZX0, sizeof(kZX0)),
			    eng::Span<eng::u8>(zr, sizeof(zr)));
			if (na != nr || na != static_cast<eng::s32>(kZX0Out)) {
				m_detail = static_cast<eng::u32>(m_detail | 8u);
			} else {
				for (eng::usize i = 0u; i < kZX0Out; ++i) {
					if (za[i] != zr[i]) {
						m_detail = static_cast<eng::u32>(m_detail | 8u);
						break;
					}
				}
			}
		}

		// --- Delta+ZX0 (camino integrado del codec vs C++ en dos pasos) ---
		{
			eng::u8 d1[64] {};
			eng::u8 d2[64] {};
			const eng::s32 n1 = eng::audio::pcm_codec::decode(
			    eng::Span<const eng::u8>(kZX0, sizeof(kZX0)),
			    eng::Span<eng::u8>(d1, sizeof(d1)),
			    static_cast<eng::u8>(eng::audio::pcm_codec::Codec::DeltaZx0));
			const eng::s32 n2 = eng::audio::zx0::decompress(
			    eng::Span<const eng::u8>(kZX0, sizeof(kZX0)),
			    eng::Span<eng::u8>(d2, sizeof(d2)));
			if (n2 > 0) {
				eng::audio::pcm_codec::integrate_deltas(
				    eng::Span<eng::u8>(d2, static_cast<eng::usize>(n2)));
			}
			if (n1 != n2 || n1 != static_cast<eng::s32>(kZX0Out)) {
				m_detail = static_cast<eng::u32>(m_detail | 16u);
			} else {
				for (eng::usize i = 0u; i < kZX0Out; ++i) {
					if (d1[i] != d2[i]) {
						m_detail = static_cast<eng::u32>(m_detail | 16u);
						break;
					}
				}
			}
		}

		// --- aPLib (ASM vs referencia C++) ---
		{
			eng::u8 aa[128] {};
			eng::u8 ar[128] {};
			const eng::s32 na = eng::audio::asm_codec::aplib_decompress(
			    eng::Span<const eng::u8>(kAPLib, sizeof(kAPLib)),
			    eng::Span<eng::u8>(aa, sizeof(aa)));
			const eng::s32 nr = eng::audio::aplib::decompress(
			    eng::Span<const eng::u8>(kAPLib, sizeof(kAPLib)),
			    eng::Span<eng::u8>(ar, sizeof(ar)));
			if (na != nr || na != static_cast<eng::s32>(kAPLibOut)) {
				m_detail = static_cast<eng::u32>(m_detail | 32u);
			} else {
				for (eng::usize i = 0u; i < kAPLibOut; ++i) {
					if (aa[i] != ar[i]) {
						m_detail = static_cast<eng::u32>(m_detail | 32u);
						break;
					}
				}
			}
		}

		eng::debug::mark_ready(g_eng_run_status, 0x00040000u | m_detail);
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		auto& debug = backend.debug();
		debug.clear();
		debug.filled_rect(40, 40, 600, 150, 0x00081018);
		debug.rect(40, 40, 600, 150, 0x00ffffff);
		debug.text(56, 60, "codec asm equiv (0 = identico)", 0x00ffffff);

		eng::util::StaticString<16> s;
		(void)eng::util::to_chars_u32(s, m_detail);
		char line[32];
		line[0] = '\0';
		const char* p = "detail: ";
		char* d = line;
		while (*p != '\0') {
			*d++ = *p++;
		}
		const char* v = s.c_str();
		while (*v != '\0') {
			*d++ = *v++;
		}
		*d = '\0';
		debug.text(56, 96, line, m_detail == 0u ? 0x0080ff80 : 0x00ff6060);
		eng::debug::probe_when_ready(g_eng_run_status, 0u);
	}

private:
	eng::u32 m_detail = 0u;
	bool m_memory_ok = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	static CodecEquiv game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);
	return 0;
}
