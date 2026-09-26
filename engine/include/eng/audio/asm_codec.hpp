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
/// En host (sin `ENG_AMIGA`) se usa siempre la referencia C++. La **equivalencia byte a byte** del
/// ASM se verifica en la demo `277_codec_equiv` (gate en `detail`: 0 = idéntico). El target Amiga
/// se detecta con `ENG_AMIGA` (lo define el build), no con `__m68k__` (ver `CODING_STYLE.md`).

#include <eng/audio/aplib.hpp>
#include <eng/audio/fib_delta.hpp>
#include <eng/audio/ima_adpcm.hpp>
#include <eng/audio/zx0.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

#if defined(ENG_AMIGA)

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

/// Descompresor **ZX0** por ASM (`support/dzx0_68000.s`, Emmanuel Marty, zlib; ABI de registro
/// `a0` = comprimido, `a1` = salida). Devuelve los bytes escritos (el flujo ZX0 lleva su propio
/// marcador de fin, así que no se pasa `len`; `dst` debe tener capacidad suficiente).
[[nodiscard]] inline eng::s32 zx0_decompress(eng::Span<const eng::u8> src,
					     eng::Span<eng::u8> dst) noexcept {
	register const eng::u8* a0v asm("a0") = src.data();
	register eng::u8* a1v asm("a1") = dst.data();
	asm volatile("jsr zx0_decompress"
		     : "+a"(a0v), "+a"(a1v)
		     :
		     : "d0", "d1", "d2", "a2", "cc", "memory");
	return static_cast<eng::s32>(static_cast<eng::usize>(a1v - dst.data()));
}

/// Descompresor **aPLib** por ASM (`support/aplib_68000.s`, Emmanuel Marty, zlib). Se llama por el
/// **envoltorio C `eng_aplib_decompress`** (ABI de pila, sin inline-asm), que evita el *ICE* del
/// gcc m68k 15.1 con ABI de registro (ver `docs/reference/toolchain/m68k-gcc.md` §3.1).
/// `dst` debe tener capacidad suficiente (el flujo aPLib lleva su marcador EOD).
extern "C" eng::s32 eng_aplib_decompress(const eng::u8* src, eng::u8* dst) noexcept;

/// `aplib::decompress` por ASM (mismo contrato: `dst` con capacidad suficiente).
[[nodiscard]] inline eng::s32 aplib_decompress(eng::Span<const eng::u8> src,
					       eng::Span<eng::u8> dst) noexcept {
	return eng_aplib_decompress(src.data(), dst.data());
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

/// En host, la referencia C++ de ZX0.
[[nodiscard]] inline eng::s32 zx0_decompress(eng::Span<const eng::u8> src,
					     eng::Span<eng::u8> dst) noexcept {
	return zx0::decompress(src, dst);
}

/// En host, la referencia C++ de aPLib.
[[nodiscard]] inline eng::s32 aplib_decompress(eng::Span<const eng::u8> src,
					       eng::Span<eng::u8> dst) noexcept {
	return aplib::decompress(src, dst);
}

} // namespace eng::audio::asm_codec

#endif
