#pragma once

/// \file aplib.hpp
/// **Descompresor aPLib** (Jørgen Ibsen; formato producido por `apultra` y por la librería
/// aPLib). Port freestanding del `expand.c` de `apultra` (Emmanuel Marty, **licencia zlib**),
/// sin diccionario externo ni flags: decodifica un bloque completo a un `Span` de destino.
///
/// El formato es un LZ con **bitstream MSB-first** y dos formas de codificar el offset:
/// coincidencia de 8+n bits (con longitud implícita por el offset) o corta de 4 bits, más
/// literales. El bucle caliente es el de copia (solapada, byte a byte).
///
/// En el 68000 se usa la rutina ASM `support/aplib_68000.s` (misma semántica); en host y en el
/// test se usa esta referencia. Ver `docs/engine/architecture/AUDIO_STREAMING.md` §7.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::audio::aplib {

/// Umbrales de longitud implícita por offset (constantes de `format.h` de apultra; coinciden
/// con las del ASM 68000).
inline constexpr eng::s32 kMinMatch3 = 1280;  ///< offset >= este: longitud +1
inline constexpr eng::s32 kMinMatch4 = 32000; ///< offset >= este (o < 128): longitud +2

namespace detail {

/// Lector de bits MSB-first del flujo comprimido (los bytes crudos se leen aparte, por el
/// mismo puntero, así que el intercalado bit/byte es exacto al del compresor).
struct Reader {
	eng::Span<const eng::u8> in {};
	eng::usize i = 0u;
	int mask = 0;
	eng::u8 bits = 0u;

	/// Lee el siguiente bit (MSB-first); `-1` si se agota el flujo.
	[[nodiscard]] int read_bit() noexcept {
		if (mask == 0) {
			if (i >= in.size()) {
				return -1;
			}
			bits = in[i++];
			mask = 128;
		}
		const int b = (bits & 128u) ? 1 : 0;
		bits = static_cast<eng::u8>(bits << 1u);
		mask >>= 1;
		return b;
	}

	/// Elias-gamma2 de aPLib (bit de continuación intercalado).
	[[nodiscard]] int read_gamma2() noexcept {
		unsigned v = 1u;
		int bit = 0;
		do {
			const int a = read_bit();
			if (a < 0) {
				return -1;
			}
			v = (v << 1u) + static_cast<unsigned>(a);
			bit = read_bit();
			if (bit < 0) {
				return -1;
			}
		} while (bit);
		return static_cast<int>(v);
	}
};

} // namespace detail

/// **Descomprime** `src` (aPLib) a `dst`. Devuelve los bytes escritos o `-1` si el flujo es
/// inválido o no cabe en `dst`.
[[nodiscard]] inline eng::s32 decompress(eng::Span<const eng::u8> src,
					 eng::Span<eng::u8> dst) noexcept {
	detail::Reader r {src};
	eng::usize out = 0u;
	eng::s32 match_offset = -1;
	int follows_literal = 3;

	if (src.size() == 0u || dst.size() == 0u) {
		return -1;
	}
	dst[out++] = src[r.i++]; // el primer byte es siempre literal

	/// Copia `len` bytes desde `offset` atrás (falso si el offset/longitud no caben).
	const auto copy_back = [&](eng::s32 offset, unsigned len) noexcept -> bool {
		if (offset <= 0 || static_cast<eng::usize>(offset) > out ||
		    out + len > dst.size()) {
			return false;
		}
		// Copia determinista izquierda→derecha (soporta solapamiento).
		for (unsigned k = 0u; k < len; ++k) {
			dst[out + k] = dst[out - static_cast<eng::usize>(offset) + k];
		}
		out += len;
		return true;
	};

	while (true) {
		int res = r.read_bit();
		if (res < 0) {
			return -1;
		}
		if (res == 0) {
			/* '0': literal */
			if (r.i >= src.size() || out >= dst.size()) {
				return -1;
			}
			dst[out++] = src[r.i++];
			follows_literal = 3;
			continue;
		}
		res = r.read_bit();
		if (res < 0) {
			return -1;
		}
		if (res == 0) {
			/* '10': offset de 8+n bits */
			int hi = r.read_gamma2();
			if (hi < 0) {
				return -1;
			}
			hi -= follows_literal;
			unsigned len = 0u;
			if (hi >= 0) {
				match_offset = hi << 8;
				if (r.i >= src.size()) {
					return -1;
				}
				match_offset |= static_cast<eng::s32>(src[r.i++]);
				const int l = r.read_gamma2();
				if (l < 0) {
					return -1;
				}
				len = static_cast<unsigned>(l);
				if (match_offset < 128 || match_offset >= kMinMatch4) {
					len += 2u;
				} else if (match_offset >= kMinMatch3) {
					len += 1u;
				}
			} else {
				/* rep-match: mismo offset que la vez anterior */
				const int l = r.read_gamma2();
				if (l < 0) {
					return -1;
				}
				len = static_cast<unsigned>(l);
			}
			follows_literal = 2;
			if (!copy_back(match_offset, len)) {
				return -1;
			}
			continue;
		}
		res = r.read_bit();
		if (res < 0) {
			return -1;
		}
		if (res == 0) {
			/* '110': 7 bits de offset + 1 de longitud */
			if (r.i >= src.size()) {
				return -1;
			}
			const unsigned command = src[r.i++];
			if (command == 0u) {
				break; // EOD
			}
			match_offset = static_cast<eng::s32>(command >> 1u);
			const unsigned len = (command & 1u) + 2u;
			follows_literal = 2;
			if (!copy_back(match_offset, len)) {
				return -1;
			}
			continue;
		}
		/* '111': offset corto de 4 bits (0 = escribir un cero) */
		unsigned short_offset = 0u;
		for (int k = 0; k < 4; ++k) {
			res = r.read_bit();
			if (res < 0) {
				return -1;
			}
			short_offset = (short_offset << 1u) | static_cast<unsigned>(res);
		}
		follows_literal = 3;
		if (short_offset != 0u) {
			if (!copy_back(static_cast<eng::s32>(short_offset), 1u)) {
				return -1;
			}
		} else {
			if (out >= dst.size()) {
				return -1;
			}
			dst[out++] = 0u;
		}
	}
	return static_cast<eng::s32>(out);
}

} // namespace eng::audio::aplib
