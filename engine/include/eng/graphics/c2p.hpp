#pragma once

/// \file c2p.hpp
/// chunky 4bpp -> planar. Port FIEL del c2p_1x1_4 de Mikael Kalms (1999) a C++.
///
/// Convierte un framebuffer chunky (1 byte por pixel, nibble bajo = indice 0..15)
/// al formato planar de 4 bitplanes que lee el DMA de Agnus. Es una transposicion
/// de matriz de bits (amiga-bootcamp `08_graphics/pixel_conversion.md`): en vez de
/// extraer bit a bit, reproduce el "merge" de Kalms (16 pixels/iteracion, mascaras
/// $0f0f0f0f -> $00ff00ff -> $55555555 -> $33333333) y escribe por words.
///
/// Optimizacion de codigo: en m68k (big-endian) las cargas/escrituras se hacen con
/// `u32`/`u16` NATIVOS (`move.l (a0)+`, `move.w d0,(a5)+`), de modo que g++ genera
/// exactamente el mismo patron de acceso que el asm de Kalms en `support/c2p_1x1_4.s`.
/// En host little-endian se usan helpers de byte-swap (solo para test host); el
/// resultado es identico en ambos. Ver leccion "no usar byte-a-byte en el hot path"
/// en LIBRARIES-CPP23-IMPORT-ROADMAP.md.
///
/// Contrato:
///   - `width_px` MULTIPLO DE 16.
///   - 1 byte chunky por pixel, nibble BAJO = indice (0..15).
///   - `plane_stride_bytes`: paso en BYTES entre el inicio de un bitplane y el siguiente.
///   - `planes`: destino, 4 planos a `planes + p*plane_stride_bytes`.
///   - `bytes_per_row = width_px/8` (filas contiguas por plano, sin BPLMOD).

#include <eng/core/types.hpp>

namespace eng::graphics {

namespace c2p_detail {

/// Carga un u32 desde 4 bytes chunky. En big-endian (m68k) es una lectura nativa;
/// en little-endian (host de test) se invierte para reproducir el orden del 68000.
inline u32 load_be(const u8* p) {
#if defined(__m68k__)
	return *reinterpret_cast<const u32*>(p);
#else
	return (static_cast<u32>(p[0]) << 24) |
	       (static_cast<u32>(p[1]) << 16) |
	       (static_cast<u32>(p[2]) << 8) |
	       (static_cast<u32>(p[3]));
#endif
}

/// Escribe una word (2 bytes) big-endian, como hace `move.w d0,(a5)+` del 68000.
inline void store_word_be(u8* p, u16 v) {
#if defined(__m68k__)
	*reinterpret_cast<u16*>(p) = v;
#else
	p[0] = static_cast<u8>((v >> 8) & 0xffu);
	p[1] = static_cast<u8>(v & 0xffu);
#endif
}

} // namespace c2p_detail

inline void c2p_1x1_4(
	unsigned long width_px,
	unsigned long height_px,
	unsigned long plane_stride_bytes,
	const void* chunky,
	void* planes
) {
	const u32 width = static_cast<u32>(width_px);
	const u32 height = static_cast<u32>(height_px);
	const u32 stride = static_cast<u32>(plane_stride_bytes);
	const u32 row_bytes = width >> 3u; // bytes por fila en un plano (width/8)
	const u8* src = static_cast<const u8*>(chunky);
	u8* base = static_cast<u8*>(planes);

	const u32 m_f = 0x0f0f0f0fu; // separa nibbles
	const u32 m_b = 0x00ff00ffu; // separa bytes
	const u32 m_2 = 0x55555555u; // separa bits pares/impares
	const u32 m_3 = 0x33333333u; // separa planos

	for (u32 y = 0; y < height; ++y) {
		const u8* srow = src + y * width;
		u8* w0 = base + y * row_bytes;            // plano0
		u8* w1 = base + stride + y * row_bytes;   // plano1
		u8* w2 = w1 + stride;                     // plano2
		u8* w3 = w2 + stride;                     // plano3

		for (u32 x = 0; x < width; x += 16) {
			const u8* b = srow + x;
			// Carga en el MISMO orden que el asm: d0, d2, d1, d3.
			u32 d0 = c2p_detail::load_be(b + 0);  // pixels 0-3
			u32 d2 = c2p_detail::load_be(b + 4);  // pixels 4-7
			u32 d1 = c2p_detail::load_be(b + 8);  // pixels 8-11
			u32 d3 = c2p_detail::load_be(b + 12); // pixels 12-15

			// --- Fase 1: empaquetar nibbles (2 pixels por byte) ---
			d0 = ((d0 & m_f) << 4) | (d2 & m_f);
			d1 = ((d1 & m_f) << 4) | (d3 & m_f);

			// --- Fase 2: separar bytes ($00ff00ff, shift 8) ---
			{
				u32 t = (d1 >> 8) ^ d0;
				t &= m_b;
				d0 ^= t;
				d1 ^= (t << 8);
			}

			// --- Fase 3: separar bits ($55555555, shift 1) ---
			{
				u32 t = (d1 >> 1) ^ d0;
				t &= m_2;
				d0 ^= t;
				d1 ^= (t << 1);
			}

			// --- Fase 4: intercambio cruzado de words ---
			//   d0 = [A B], d1 = [C D]  ->  d0 = [A C], d1 = [B D]
			{
				const u32 A = d0 >> 16, B = d0 & 0xffffu;
				const u32 C = d1 >> 16, D = d1 & 0xffffu;
				d0 = (A << 16) | C;
				d1 = (B << 16) | D;
			}

			// --- Fase 5: separar planos ($33333333, shift 2) ---
			{
				u32 t = (d1 >> 2) ^ d0;
				t &= m_3;
				d0 ^= t;
				d1 ^= (t << 2);
			}

			// --- Escritura: 2 bytes por plano (16 pixels), orden del asm ---
			// El asm: plano2=low(d0), plano3=high(d0), plano0=low(d1), plano1=high(d1),
			// cada word big-endian (move.w). Escribimos words nativas para que g++
			// emita move.w en vez de move.b sueltos.
			const u32 off = x >> 3u; // byte offset dentro de la fila (16 px = 2 bytes)
			c2p_detail::store_word_be(w0 + off, static_cast<u16>(d1 & 0xffffu));        // plano0 = low(d1)
			c2p_detail::store_word_be(w1 + off, static_cast<u16>((d1 >> 16) & 0xffffu)); // plano1 = high(d1)
			c2p_detail::store_word_be(w2 + off, static_cast<u16>(d0 & 0xffffu));        // plano2 = low(d0)
			c2p_detail::store_word_be(w3 + off, static_cast<u16>((d0 >> 16) & 0xffffu)); // plano3 = high(d0)
		}
	}
}

} // namespace eng::graphics