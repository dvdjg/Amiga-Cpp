#pragma once

/// \file ima_adpcm.hpp
/// **IMA ADPCM 4-bit** (también DVI ADPCM), códec de audio con pérdida para el 68000: escala la
/// cuantificación según la pendiente de la señal (predictor + índice de paso adaptativo) y
/// guarda **4 bits por muestra**. La decodificación es un par de lecturas de tabla, unos
/// desplazamientos y una suma/resta por muestra.
///
/// El estado (predictor de 16 bits con signo + índice de paso 0..88) es **persistente**: se
/// guarda en la cabecera del bloque para que la decodificación sea autocontenida y para poder
/// encadenar bloques (streaming) sin cortes.
///
/// Bloque (little-endian):
/// ```text
///   [0]    step_index   (0..88)
///   [1]    reservado    (0)
///   [2..3] predictor    (s16 LE; semilla del bloque)
///   [4..]  nibbles: primero el alto y luego el bajo; cada nibble es un código 4-bit
///   muestras = 2 * (tamano - 4)
///   muestra[k] (s8) = clamp(predictor, -32768, 32767) >> 8  tras aplicar el codigo k
/// ```
///
/// El encoder del engine siembra `predictor = pcm[0] << 8` e `index = 0`, y codifica **todas**
/// las muestras (la primera sale como delta 0), de modo que `decode(encode(x))` devuelve las
/// mismas muestras (salvo la pérdida propia del ADPCM).
///
/// Referencia: IMA ADPCM (tablas estándar IMA/DVI); ver `docs/engine/architecture/AUDIO_STREAMING.md` §7.1.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::audio::ima_adpcm {

/// Tabla de índices: ajuste del índice de paso por cada código de 4 bits.
inline constexpr eng::s8 kIndexTable[16] = {
	-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};

/// Tabla de pasos del estándar (89 entradas, de 7 a 32767).
inline constexpr eng::s16 kStepTable[89] = {
	7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,
	23,    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,
	73,    80,    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,
	230,   253,   279,   307,   337,   371,   408,   449,   494,   544,   598,   658,
	724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
	2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
	7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
	22385, 24623, 27086, 29794, 32767,
};

/// Estado del decodificador/codificador (predictor + índice de paso).
struct State {
	eng::s16 pred = 0;
	eng::u8 index = 0;
};

/// Aplica un código de 4 bits al estado (paso de decodificación del estándar IMA).
inline void step(State& s, eng::u8 code) noexcept {
	const eng::s32 stp = kStepTable[s.index];
	eng::s32 diff = stp >> 3;
	if (code & 1u) {
		diff += stp >> 2;
	}
	if (code & 2u) {
		diff += stp >> 1;
	}
	if (code & 4u) {
		diff += stp;
	}
	eng::s32 p = s.pred;
	p += (code & 8u) ? -diff : diff;
	if (p > 32767) {
		p = 32767;
	}
	if (p < -32768) {
		p = -32768;
	}
	s.pred = static_cast<eng::s16>(p);
	eng::s32 idx = static_cast<eng::s32>(s.index) + kIndexTable[code];
	if (idx < 0) {
		idx = 0;
	}
	if (idx > 88) {
		idx = 88;
	}
	s.index = static_cast<eng::u8>(idx);
}

/// **Decodifica** un bloque IMA ADPCM (`src`) a muestras s8 (`dst`). Devuelve el número de
/// muestras (`2*(src.size()-4)`) o `-1` si el bloque es corto o no cabe.
[[nodiscard]] inline eng::s32 decode(eng::Span<const eng::u8> src,
				     eng::Span<eng::u8> dst) noexcept {
	if (src.size() < 4u) {
		return -1;
	}
	const eng::usize pairs = src.size() - 4u;
	const eng::usize samples = pairs * 2u;
	if (dst.size() < samples) {
		return -1;
	}
	State s {};
	s.index = src[0] > 88u ? 88u : src[0];
	s.pred = static_cast<eng::s16>(static_cast<eng::u16>(src[2]) |
				       static_cast<eng::u16>(static_cast<eng::u16>(src[3]) << 8u));
	eng::usize out = 0u;
	for (eng::usize i = 0u; i < pairs; ++i) {
		const eng::u8 pair = src[4u + i];
		step(s, static_cast<eng::u8>(pair >> 4u));
		dst[out++] = static_cast<eng::u8>(static_cast<eng::s8>(s.pred >> 8));
		step(s, static_cast<eng::u8>(pair & 0x0fu));
		dst[out++] = static_cast<eng::u8>(static_cast<eng::s8>(s.pred >> 8));
	}
	return static_cast<eng::s32>(samples);
}

namespace detail {

/// Elige el código de 4 bits que mejor aproxima `delta` con el paso actual (búsqueda estándar).
[[nodiscard]] inline eng::u8 best_code(eng::s32 stp, eng::s32 delta) noexcept {
	eng::u8 code = 0u;
	if (delta < 0) {
		code = 8u;
		delta = -delta;
	}
	if (delta >= stp) {
		code = static_cast<eng::u8>(code | 4u);
		delta -= stp;
	}
	if (delta >= (stp >> 1)) {
		code = static_cast<eng::u8>(code | 2u);
		delta -= (stp >> 1);
	}
	if (delta >= (stp >> 2)) {
		code = static_cast<eng::u8>(code | 1u);
	}
	return code;
}

} // namespace detail

/// **Comprime** `pcm` (s8) a un bloque IMA ADPCM en `dst`. Devuelve los bytes escritos
/// (`4 + ceil(muestras/2)`) o `-1` si `pcm` está vacío o no cabe. La semilla es
/// `predictor = pcm[0] << 8` (la primera muestra sale como delta 0).
[[nodiscard]] inline eng::s32 encode(eng::Span<const eng::u8> pcm,
				     eng::Span<eng::u8> dst) noexcept {
	if (pcm.empty() || dst.size() < 5u) {
		return -1;
	}
	State s {};
	s.pred = static_cast<eng::s16>(static_cast<eng::s16>(static_cast<eng::s8>(pcm[0])) * 256);
	// Siembra el indice de paso con la primera pendiente: evita el transitorio inicial (con
	// `index = 0` el paso es 7 y no sigue la senal hasta que el indice sube).
	eng::s32 idx = 0u;
	if (pcm.size() > 1u) {
		const eng::s32 d0 = ((static_cast<eng::s32>(static_cast<eng::s8>(pcm[1])) -
				      static_cast<eng::s32>(static_cast<eng::s8>(pcm[0])))
				     << 8);
		const eng::s32 ad0 = d0 < 0 ? -d0 : d0;
		while (idx < 88 && kStepTable[idx] < ad0) {
			++idx;
		}
	}
	s.index = static_cast<eng::u8>(idx);
	dst[0] = s.index; // step_index
	dst[1] = 0u; // reservado
	dst[2] = static_cast<eng::u8>(static_cast<eng::u16>(s.pred) & 0xffu);
	dst[3] = static_cast<eng::u8>(static_cast<eng::u16>(s.pred) >> 8u);
	eng::usize out = 4u;
	eng::usize i = 0u;
	while (i < pcm.size()) {
		const eng::u8 hi = detail::best_code(kStepTable[s.index],
						     (static_cast<eng::s32>(static_cast<eng::s8>(pcm[i])) << 8) -
							 static_cast<eng::s32>(s.pred));
		step(s, hi);
		++i;
		eng::u8 lo = 0u;
		if (i < pcm.size()) {
			lo = detail::best_code(kStepTable[s.index],
					       (static_cast<eng::s32>(static_cast<eng::s8>(pcm[i])) << 8) -
						   static_cast<eng::s32>(s.pred));
			step(s, lo);
			++i;
		}
		if (out >= dst.size()) {
			return -1;
		}
		dst[out++] = static_cast<eng::u8>((static_cast<eng::u8>(hi) << 4u) | lo);
	}
	return static_cast<eng::s32>(out);
}

} // namespace eng::audio::ima_adpcm
