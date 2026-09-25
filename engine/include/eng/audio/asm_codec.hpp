#pragma once

/// \file asm_codec.hpp
/// **Rutinas de descompresión en ASM 68000** (`support/codec_asm.s`) para los codecs de
/// `eng::audio`. Son la vía rápida en el 68000; las implementaciones C++ (`fib_delta.hpp`,
/// `ima_adpcm.hpp`, `pcm_codec.hpp`) siguen siendo la **referencia** y las que se usan en host
/// y en cualquier build sin ASM.
///
/// El contrato es idéntico al C++: mismas comprobaciones de tamaño y mismo valor de retorno
/// (`-1` si el flujo no cabe). Las tablas se pasan **por puntero** desde el C++ para que haya
/// una única fuente de verdad (no se duplican en el `.s`).
///
/// Símbolos del ASM (convención C de GCC m68k, ver `support/codec_asm.s`):
///   `eng_fib_delta_decode(src, len, dst, table) -> s32`
///   `eng_delta_integrate(buf, len) -> void`
///   `eng_ima_adpcm_decode(src, len, dst, step_table, idx_table) -> s32`
///
/// En host (`__m68k__` no definido) se usa siempre la referencia C++. La **equivalencia byte a
/// byte** del ASM se verifica en la demo `277_codec_equiv` (gate en `detail`: 0 = idéntico).

#include <eng/audio/fib_delta.hpp>
#include <eng/audio/ima_adpcm.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

#if defined(__m68k__)

extern "C" {
eng::s32 eng_fib_delta_decode(const eng::u8* src, eng::usize len, eng::u8* dst,
			      const eng::s8* table) noexcept;
void eng_delta_integrate(eng::u8* buf, eng::usize len) noexcept;
eng::s32 eng_ima_adpcm_decode(const eng::u8* src, eng::usize len, eng::u8* dst,
			      const eng::s16* step_table, const eng::s8* idx_table) noexcept;
}

namespace eng::audio::asm_codec {

/// `fib_delta::decode` por ASM (mismo contrato y comprobaciones).
[[nodiscard]] inline eng::s32 fib_delta_decode(eng::Span<const eng::u8> src,
					       eng::Span<eng::u8> dst) noexcept {
	if (src.size() < 3u) {
		return -1;
	}
	const eng::usize samples = (src.size() - 2u) * 2u;
	if (dst.size() < samples) {
		return -1;
	}
	return eng_fib_delta_decode(src.data(), src.size(), dst.data(), fib_delta::kCodeToDelta);
}

/// `ima_adpcm::decode` por ASM (mismo contrato y comprobaciones).
[[nodiscard]] inline eng::s32 ima_adpcm_decode(eng::Span<const eng::u8> src,
					       eng::Span<eng::u8> dst) noexcept {
	if (src.size() < 4u) {
		return -1;
	}
	const eng::usize samples = (src.size() - 4u) * 2u;
	if (dst.size() < samples) {
		return -1;
	}
	return eng_ima_adpcm_decode(src.data(), src.size(), dst.data(), ima_adpcm::kStepTable,
				    ima_adpcm::kIndexTable);
}

/// Integración de deltas por ASM (in-place; `len <= 65535` por el `dbra`).
inline void delta_integrate(eng::Span<eng::u8> buf) noexcept {
	if (buf.size() == 0u) {
		return;
	}
	eng_delta_integrate(buf.data(), buf.size());
}

} // namespace eng::audio::asm_codec

#else

namespace eng::audio::asm_codec {

/// En host, la referencia C++.
[[nodiscard]] inline eng::s32 fib_delta_decode(eng::Span<const eng::u8> src,
					       eng::Span<eng::u8> dst) noexcept {
	return fib_delta::decode(src, dst);
}
[[nodiscard]] inline eng::s32 ima_adpcm_decode(eng::Span<const eng::u8> src,
					       eng::Span<eng::u8> dst) noexcept {
	return ima_adpcm::decode(src, dst);
}
/// En host, integración de deltas in-place (equivalente a `pcm_codec::integrate_deltas`).
inline void delta_integrate(eng::Span<eng::u8> buf) noexcept {
	eng::u8 acc = 0u;
	for (eng::usize i = 0u; i < buf.size(); ++i) {
		acc = static_cast<eng::u8>(acc + buf[i]);
		buf[i] = acc;
	}
}

} // namespace eng::audio::asm_codec

#endif
