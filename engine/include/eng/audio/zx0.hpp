#pragma once

/// \file zx0.hpp
/// **Descompresor ZX0** (Einar Saukas, formato v2), portado del `dzx0.c` de referencia a
/// freestanding sin heap, con destino tipo `Span` y comprobación de límites. Es la pieza del
/// codec `eng::audio::pcm_codec` para el campo `compression = 0` (ZX0) de la cabecera `AUZX`
/// (ver `docs/engine/architecture/AUDIO_STREAMING.md`).
///
/// Formato (3 bloques; longitudes en Elias gamma *interlazado*):
///   - literales: `0`... (primer bloque) `Elias(len)` + len bytes;
///   - copia desde el último offset;
///   - copia desde un offset nuevo: `1 Elias(MSB+1) LSB(7b) Elias(len-1)`; `MSB == 256` = EOF.
///
/// Port fiel del `dzx0.c`: bit-reader MSB-first, *backtrack* del bit bajo del LSB y
/// `read_interlaced_elias_gamma`. El byte crudo (literal / LSB) se lee sin tocar el estado de
/// bits, igual que el `write_byte` del compresor. Referencia: https://github.com/einar-saukas/ZX0
/// (MIT). Verificado con vectores del compresor de referencia (HOST-271).

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::audio::zx0 {

/// Lector de bits del flujo comprimido (MSB-first, con el *backtrack* del bit interlace).
struct BitReader {
	eng::Span<const eng::u8> src {};
	eng::usize i = 0u;
	eng::s32 bit_mask = 0;
	eng::s32 bit_value = 0;
	eng::s32 last_byte = 0;
	bool backtrack = false;
	bool ok = true;

	[[nodiscard]] eng::s32 read_byte() noexcept {
		if (i >= src.size()) {
			ok = false;
			return 0;
		}
		last_byte = src[i++];
		return last_byte;
	}
	[[nodiscard]] eng::s32 read_bit() noexcept {
		if (backtrack) {
			backtrack = false;
			return last_byte & 1;
		}
		bit_mask >>= 1;
		if (bit_mask == 0) {
			bit_mask = 128;
			bit_value = read_byte();
		}
		return (bit_value & bit_mask) ? 1 : 0;
	}
	/// Elias gamma **interlazado** (`inverted` = v2 para el MSB del offset nuevo).
	[[nodiscard]] eng::s32 read_gamma(bool inverted) noexcept {
		eng::s32 value = 1;
		while (!read_bit()) {
			if (!ok) {
				return value;
			}
			value = (value << 1) | (read_bit() ^ (inverted ? 1 : 0));
		}
		return value;
	}
};

/// Descomprime `src` en `dst`. Devuelve los bytes escritos, o `-1` si el flujo es inválido o no
/// cabe en `dst`.
[[nodiscard]] inline eng::s32 decompress(eng::Span<const eng::u8> src, eng::Span<eng::u8> dst) noexcept {
	BitReader r {src};
	eng::usize out = 0u;
	eng::s32 last_offset = 1;

	const auto put = [&](eng::s32 v) noexcept -> bool {
		if (out >= dst.size()) {
			return false;
		}
		dst[out++] = static_cast<eng::u8>(v & 0xff);
		return true;
	};
	const auto copy_back = [&](eng::s32 offset, eng::s32 length) noexcept -> bool {
		if (offset <= 0 || static_cast<eng::usize>(offset) > out) {
			return false;
		}
		while (length-- > 0) {
			if (!put(dst[out - static_cast<eng::usize>(offset)])) {
				return false;
			}
		}
		return true;
	};

	enum class St : eng::u8 { Literals, LastOffset, NewOffset };
	St st = St::Literals;
	while (true) {
		if (st == St::Literals) {
			eng::s32 length = r.read_gamma(false);
			if (!r.ok || length < 0) {
				return -1;
			}
			for (eng::s32 k = 0; k < length; ++k) {
				const eng::s32 b = r.read_byte();
				if (!r.ok || !put(b)) {
					return -1;
				}
			}
			st = r.read_bit() ? St::NewOffset : St::LastOffset;
			continue;
		}
		if (st == St::LastOffset) {
			const eng::s32 length = r.read_gamma(false);
			if (!r.ok || !copy_back(last_offset, length)) {
				return -1;
			}
			st = r.read_bit() ? St::NewOffset : St::Literals;
			continue;
		}
		// NewOffset
		last_offset = r.read_gamma(true);
		if (!r.ok) {
			return -1;
		}
		if (last_offset == 256) {
			return static_cast<eng::s32>(out); // EOF
		}
		last_offset = last_offset * 128 - (r.read_byte() >> 1);
		if (!r.ok || last_offset <= 0) {
			return -1;
		}
		r.backtrack = true;
		const eng::s32 length = r.read_gamma(false) + 1;
		if (!r.ok || !copy_back(last_offset, length)) {
			return -1;
		}
		st = r.read_bit() ? St::NewOffset : St::Literals;
	}
}

} // namespace eng::audio::zx0
