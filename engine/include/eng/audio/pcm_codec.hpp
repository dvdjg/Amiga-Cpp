#pragma once

/// \file pcm_codec.hpp
/// **Codec de audio PCM 8-bit con signo: Delta + RLE (ByteRun1)**, freestanding y sin heap.
///
/// Pensado para el streaming desde disquete del A500: la CPU descomprime un chunk directo a Chip
/// RAM mientras Paula reproduce el anterior (`docs/engine/architecture/AUDIO_STREAMING.md`). El
/// *delta encoding* (diferencias entre muestras consecutivas) mejora el ratio; el RLE elimina
/// tramos planos, que en audio son frecuentes.
///
/// ```text
///   PCM 8-bit ──delta──► deltas ──RLE(ByteRun1)──► flujo comprimido
///   flujo ──RLE-decode──► deltas ──integrate──► PCM 8-bit
/// ```
///
/// Formato del flujo (ByteRun1, el de PackBits):
///   - control `n` en 0..127  → copiar los siguientes `n+1` bytes literales;
///   - control `n` en 129..255 → repetir el siguiente byte `257-n` veces;
///   - control `n` == 128 → no-op.
///
/// El identificador de códec (`Codec::DeltaRle`) coincide con el campo `compression` de la
/// cabecera de archivo AUZX (`AUDIO_STREAMING.md` §2).

#include <eng/audio/zx0.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::audio::pcm_codec {

/// Identificador del códec (campo `compression` de la cabecera AUZX).
enum class Codec : eng::u8 {
	Zx0 = 0,      ///< ZX0 (Einar Saukas): PCM crudo comprimido (sin delta)
	APLib = 1,    ///< aPLib (pendiente)
	DeltaRle = 2, ///< Delta + RLE (ByteRun1)
	None = 3,     ///< PCM crudo sin compresión (p. ej. streaming de un `.raw` tal cual)
};

/// **Decodifica** `src` a PCM 8-bit con signo en `dst` (código `compression`).
/// Devuelve los bytes escritos, o `-1` si el flujo es inválido, el códec no existe o no cabe en
/// `dst`. Para Delta+RLE la integración del delta arranca con `prev = 0` (el primer byte del flujo
/// es la primera muestra en crudo); ZX0 decodifica el PCM directamente (HOST-271 con vectores del
/// compresor de referencia).
[[nodiscard]] inline eng::s32 decode(eng::Span<const eng::u8> src, eng::Span<eng::u8> dst,
				    eng::u8 compression) noexcept {
	if (compression == static_cast<eng::u8>(Codec::Zx0)) {
		return zx0::decompress(src, dst);
	}
	if (compression == static_cast<eng::u8>(Codec::None)) {
		// PCM crudo: copia directa. El chunk debe traer exactamente las muestras del buffer.
		if (src.size() != dst.size()) {
			return -1;
		}
		for (eng::usize i = 0; i < src.size(); ++i) {
			dst[i] = src[i];
		}
		return static_cast<eng::s32>(dst.size());
	}
	if (compression != static_cast<eng::u8>(Codec::DeltaRle)) {
		return -1;
	}
	eng::usize out = 0;
	eng::usize i = 0;
	while (i < src.size()) {
		const eng::u8 n = src[i++];
		if (n < 128u) {
			const eng::usize count = static_cast<eng::usize>(n) + 1u;
			if (i + count > src.size() || out + count > dst.size()) {
				return -1;
			}
			for (eng::usize k = 0; k < count; ++k) {
				dst[out++] = src[i++];
			}
		} else if (n > 128u) {
			const eng::usize count = static_cast<eng::usize>(257u - n);
			if (i >= src.size() || out + count > dst.size()) {
				return -1;
			}
			const eng::u8 v = src[i++];
			for (eng::usize k = 0; k < count; ++k) {
				dst[out++] = v;
			}
		}
		// n == 128: no-op
	}
	// Integración del delta: el flujo decodificado son diferencias; la muestra es la acumulación.
	eng::u8 acc = 0u;
	for (eng::usize k = 0; k < out; ++k) {
		acc = static_cast<eng::u8>(acc + dst[k]);
		dst[k] = acc;
	}
	return static_cast<eng::s32>(out);
}

/// **Comprime** `pcm` (8-bit con signo) a `dst` (Delta + RLE). Devuelve los bytes escritos o `-1`
/// si no cabe. El delta se calcula sobre `pcm` sin buffer temporal; las series literales se
/// agrupan en tramos de hasta 128 bytes.
[[nodiscard]] inline eng::s32 encode(eng::Span<const eng::u8> pcm, eng::Span<eng::u8> dst) noexcept {
	eng::usize out = 0;
	eng::u8 lit[128] {};
	eng::usize litn = 0;

	// Delta de la muestra `k` (con `pcm[-1] = 0`).
	const auto delta_at = [&](eng::usize k) -> eng::u8 {
		const eng::u8 prev = (k == 0u) ? eng::u8{0} : pcm[k - 1u];
		return static_cast<eng::u8>(pcm[k] - prev);
	};
	// Vuelca el buffer literal pendiente como un control 0..127 + bytes.
	const auto flush_literal = [&]() -> bool {
		if (litn == 0u) {
			return true;
		}
		if (out + 1u + litn > dst.size()) {
			return false;
		}
		dst[out++] = static_cast<eng::u8>(litn - 1u);
		for (eng::usize k = 0; k < litn; ++k) {
			dst[out++] = lit[k];
		}
		litn = 0u;
		return true;
	};

	eng::usize i = 0;
	while (i < pcm.size()) {
		const eng::u8 d = delta_at(i);
		eng::usize run = 1u;
		while (i + run < pcm.size() && run < 128u && delta_at(i + run) == d) {
			++run;
		}
		if (run >= 3u) {
			if (!flush_literal()) {
				return -1;
			}
			if (out + 2u > dst.size()) {
				return -1;
			}
			dst[out++] = static_cast<eng::u8>(257u - run); // 129..255
			dst[out++] = d;
			i += run;
		} else {
			for (eng::usize k = 0; k < run; ++k) {
				lit[litn++] = delta_at(i + k);
				if (litn == 128u && !flush_literal()) {
					return -1;
				}
			}
			i += run;
		}
	}
	if (!flush_literal()) {
		return -1;
	}
	return static_cast<eng::s32>(out);
}

} // namespace eng::audio::pcm_codec
