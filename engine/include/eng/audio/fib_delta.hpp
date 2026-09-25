#pragma once

/// \file fib_delta.hpp
/// **Fibonacci Delta (IFF 8SVX, `sCompression = 1`)**: códec de audio 8-bit con pérdida,
/// 2:1 constante (4 bits por muestra), pensado para el 68000.
///
/// La descompresión es un nibble, una lectura de tabla de 16 bytes y un `ADD.B` por muestra:
/// tan barata que Paula puede reproducir por DMA mientras la CPU decodifica al vuelo
/// (ver `AUDIO_STREAMING.md`). El formato es el del estándar **IFF 8SVX** (EA, 1985,
/// Apéndice C «Fibonacci Delta Compression»), de modo que cualquier herramienta de PC que
/// lo implemente produce un flujo compatible.
///
/// ```text
///   flujo 8SVX:  [pad=0] [muestra inicial (s8)] [pares de nibbles: alto, luego bajo]
///   muestra[k] = muestra[k-1] + kCodeToDelta[nibble]
///   salida    = 2 * (bytes_del_flujo - 2) muestras s8 (byte con signo)
/// ```
///
/// Tabla del estándar (16 entradas): deltas pequeños exactos (Fibonacci) y grandes
/// aproximados; el extremo negativo llega a -34 porque un nibble solo cubre 16 códigos.
///
/// Referencia: Steve Hayes / Jerry Morrison, *IFF 8SVX* (Apéndice C), y
/// `docs/engine/architecture/AUDIO_STREAMING.md` §3.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::audio::fib_delta {

/// Tabla del estándar IFF 8SVX (Apéndice C): índice de nibble -> incremento de amplitud.
inline constexpr eng::s8 kCodeToDelta[16] = {
	-34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21,
};

/// **Descomprime** un flujo 8SVX (`src`) a muestras PCM 8-bit con signo (`dst`). Devuelve el
/// número de muestras escritas (`2 * (src.size() - 2)`), o `-1` si el flujo es más corto que
/// la cabecera (`pad` + muestra inicial) o no cabe en `dst`. La integración arranca en la
/// muestra inicial del flujo (`src[1]`).
[[nodiscard]] inline eng::s32 decode(eng::Span<const eng::u8> src,
				     eng::Span<eng::u8> dst) noexcept {
	if (src.size() < 3u) {
		return -1;
	}
	const eng::usize pairs = src.size() - 2u; // bytes de pares de nibbles
	const eng::usize samples = pairs * 2u;
	if (dst.size() < samples) {
		return -1;
	}
	eng::u8 x = src[1]; // muestra inicial (byte con signo, 2's complement)
	eng::usize out = 0u;
	for (eng::usize i = 0u; i < pairs; ++i) {
		const eng::u8 pair = src[2u + i];
		x = static_cast<eng::u8>(x + kCodeToDelta[pair >> 4u]);   // nibble alto
		dst[out++] = x;
		x = static_cast<eng::u8>(x + kCodeToDelta[pair & 0x0fu]); // nibble bajo
		dst[out++] = x;
	}
	return static_cast<eng::s32>(samples);
}

namespace detail {

/// Código de nibble cuyo delta de tabla es el más cercano a `target` (búsqueda lineal en la
/// tabla, 16 entradas). La tabla es monótona, de -34 a 21.
[[nodiscard]] inline eng::u8 nearest_code(int target) noexcept {
	eng::u8 best = 0u;
	int best_diff = 1 << 20;
	for (eng::u8 k = 0u; k < 16u; ++k) {
		const int d = static_cast<int>(kCodeToDelta[k]) - target;
		const int ad = d < 0 ? -d : d;
		if (ad < best_diff) {
			best_diff = ad;
			best = k;
		}
	}
	return best;
}

} // namespace detail

/// **Comprime** `pcm` (8-bit con signo) al flujo 8SVX en `dst`. Devuelve los bytes escritos
/// (`2 + ceil(muestras/2)`) o `-1` si `pcm` está vacío o no cabe.
///
/// Convención de semilla: `dst[1] = 0` y los nibbles codifican **todas** las muestras como
/// incrementos desde el acumulador (que arranca en 0), de modo que `decode(encode(x)) == x`
/// cuando los deltas son representables. El estándar pasa la semilla como parámetro a
/// `D1Unpack`, así que cualquier valor es válido para el decodificador; usar 0 es la
/// convención natural (idéntica a la de Delta+RLE).
///
/// El codificador es *greedy*: para cada muestra elige el delta tabulado más cercano a la
/// diferencia real, avanzando el acumulador. El estándar sugiere además repartir el error
/// hacia delante y hacia atrás para minimizar la distorsión global; eso mejora la calidad
/// pero **no** cambia el formato (el flujo sigue siendo válido), así que puede sustituirse.
[[nodiscard]] inline eng::s32 encode(eng::Span<const eng::u8> pcm,
				     eng::Span<eng::u8> dst) noexcept {
	if (pcm.empty() || dst.size() < 2u) {
		return -1;
	}
	dst[0] = 0u; // pad (el estándar lo ignora)
	dst[1] = 0u; // semilla (x inicial del D1Unpack)
	eng::usize out = 2u;
	eng::u8 x = 0u;
	eng::usize i = 0u;
	while (i < pcm.size()) {
		const eng::u8 hi = detail::nearest_code(
		    static_cast<int>(static_cast<eng::s8>(pcm[i])) - static_cast<int>(static_cast<eng::s8>(x)));
		x = static_cast<eng::u8>(x + kCodeToDelta[hi]);
		++i;
		eng::u8 lo = 8u; // delta 0: relleno si el número de muestras es impar
		if (i < pcm.size()) {
			lo = detail::nearest_code(static_cast<int>(static_cast<eng::s8>(pcm[i])) -
						  static_cast<int>(static_cast<eng::s8>(x)));
			x = static_cast<eng::u8>(x + kCodeToDelta[lo]);
			++i;
		}
		if (out >= dst.size()) {
			return -1;
		}
		dst[out++] = static_cast<eng::u8>((static_cast<eng::u8>(hi) << 4u) | lo);
	}
	return static_cast<eng::s32>(out);
}

} // namespace eng::audio::fib_delta
