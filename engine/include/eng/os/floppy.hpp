#pragma once

/// \file floppy.hpp
/// **Disquete a bajo nivel** (`eng::os`): control mecánico por CIA-B, DMA crudo de Paula y
/// **decodificación MFM** en CPU. Es la alternativa a `trackdisk.device` (ver
/// `docs/debugging/investigaciones/consulta-grok-disco-y-loader.md` §Decisión): no depende del OS y da control
/// total (formatos no-DOS, bootblocks, copy-protection).
///
/// El chipset **no decodifica MFM**: `DSKPT`/`DSKLEN` vuelcan la pista cruda a **Chip RAM** y la
/// CPU separa los bits de reloj. Formato AmigaDOS de un sector (fuente:
/// `WinUAE-DBG/disk.cpp:2185-2260`):
///
/// ```text
///   $4489 $4489            sync (2 words)
///   header  4 B            format(0xFF), track, sector, sectors_to_gap
///   label   16 B           (etiqueta de sector del formato)
///   hck     4 B            XOR de los longs de cabecera+etiqueta
///   dck     4 B            XOR de los longs de datos
///   data    512 B          dodd (256 words) y luego deven (256 words)
/// ```
///
/// Registros: `DSKPT $DFF020`, `DSKLEN $DFF024` (se escribe **dos veces**), `DSKSYNC $DFF07E`,
/// `ADKCON $DFF09E` (WORDSYNC), `DMACON` bit 4, `INTREQR` bit 1 (**DSKBLK**). CIA-B **PRB
/// `$BFD100`**: `/MTR`(7), `/SEL0`(3), `SIDE`(2), `DIR`(1), `STEP`(0); CIA-A **PRA `$BFE001`**:
/// `/RDY`(5), `/TK0`(4). Ficha: `docs/reference/emulators/winuae/trackdisk.md`.

#include <eng/core/types/span.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>

namespace eng::os {

/// Sync MFM de AmigaDOS (`$4489` = `$A1` con el reloj faltante). Paula lo busca con `DSKSYNC`.
inline constexpr eng::u16 kMfmSync = 0x4489u;

/// Palabras de un sector AmigaDOS (sync + cabecera + etiqueta + checksums + 512 B de datos).
inline constexpr eng::u16 kMfmWordsPerSector = 544u;
/// Bytes útiles por sector.
inline constexpr eng::u16 kSectorBytes = 512u;

/// Palabras de una revolución de una pista AmigaDOS DD (11 sectores + hueco de gap).
inline constexpr eng::u16 kMfmTrackWords = 11u * kMfmWordsPerSector + 350u; // 6334
/// Palabras recomendadas para `floppy_read_track`: **dos revoluciones**. El DMA arranca en el
/// primer sync que ve, así que el sector al que pertenece ese sync queda partido; en la segunda
/// vuelta aparece **completo** con margen, de modo que una sola lectura basta aunque la fase de
/// rotación varíe.
inline constexpr eng::u16 kMfmReadWords = 2u * kMfmTrackWords; // 12668

/// Cabecera de sector AmigaDOS.
struct FloppySectorHeader {
	eng::u8 format = 0u; ///< `0xFF` = AmigaDOS
	eng::u8 track = 0u;
	eng::u8 sector = 0u;
	eng::u8 to_gap = 0u; ///< sectores que faltan hasta el final de la pista
};

/// Decodifica un **long** MFM a 32 bits de datos. El encoder intercala `dodd`/`deven`
/// (`deven = v & 0x55555555`, `dodd = (v >> 1) & 0x55555555`) y a continuación rellena los
/// **bits de reloj** en las posiciones impares (`mfmcode`, `WinUAE-DBG/disk.cpp:2059`). El
/// lector recibe las palabras ya con los relojes OR-eados, así que se descartan con
/// `& 0x55555555` antes de reconstruir `v` (`v = deven | (dodd << 1)`).
[[nodiscard]] constexpr eng::u32 mfm_decode_long(eng::u16 dodd_hi, eng::u16 dodd_lo,
						 eng::u16 deven_hi, eng::u16 deven_lo) noexcept {
	const eng::u32 dodd =
		((static_cast<eng::u32>(dodd_hi) << 16u) | dodd_lo) & 0x55555555u;
	const eng::u32 deven =
		((static_cast<eng::u32>(deven_hi) << 16u) | deven_lo) & 0x55555555u;
	return deven | (dodd << 1u);
}

/// Decodifica una **palabra** MFM (16 bits crudos) a su byte de datos (bits de datos en las
/// posiciones pares, LSB primero). Es el camino de la cabecera/gap; los datos de un sector usan
/// `mfm_decode_long`.
[[nodiscard]] constexpr eng::u8 mfm_decode_byte(eng::u16 raw) noexcept {
	eng::u8 out = 0u;
	for (eng::u8 i = 0u; i < 8u; ++i) {
		out = static_cast<eng::u8>(out | static_cast<eng::u8>(((raw >> (2u * i)) & 1u) << i));
	}
	return out;
}

namespace detail {

/// Lee una palabra MFM (16 bits, MSB primero) desde la **posición de bit** `bit` del array de
/// palabras volcado por la DMA. El flujo de un medio real puede no estar alineado a palabra
/// (jitter de revolución, hueco de pista), así que el sync se busca a nivel de bit.
[[nodiscard]] inline eng::u16 mfm_word_at(const eng::u16* t, eng::u32 bit) noexcept {
	const eng::u32 w = bit >> 4u;
	const eng::u32 o = bit & 15u;
	if (o == 0u) {
		return t[w];
	}
	const eng::u32 a = static_cast<eng::u32>(t[w]);
	const eng::u32 b = static_cast<eng::u32>(t[w + 1u]);
	return static_cast<eng::u16>((a << o) | (b >> (16u - o)));
}

/// XOR de `pairs` longs (palabras MFM enmascaradas, sin relojes) desde la palabra en `bit`.
/// El encoder AmigaDOS calcula así hck (cabecera+etiqueta) y dck (datos)
/// (`WinUAE-DBG/disk.cpp:2231-2259`).
[[nodiscard]] inline eng::u32 mfm_xor_long_at(const eng::u16* t, eng::u32 bit,
					      eng::u32 pairs) noexcept {
	eng::u32 x = 0u;
	for (eng::u32 k = 0u; k < pairs; ++k) {
		const eng::u32 hi = static_cast<eng::u32>(mfm_word_at(t, bit + 32u * k)) & 0x5555u;
		const eng::u32 lo =
			static_cast<eng::u32>(mfm_word_at(t, bit + 32u * k + 16u)) & 0x5555u;
		x ^= (hi << 16u) | lo;
	}
	return x;
}

/// Comprueba hck/dck de un sector AmigaDOS cuyo primer sync empieza en el bit `p`, con la
/// cabecera en la palabra `h` (2 si la DMA volcó los **dos** syncs, 1 si solo el segundo: el
/// primer sync de una pareja lo consume la detección de `DSKSYNC`). hck = XOR de las 20
/// palabras cabecera+etiqueta; dck = XOR de las 512 palabras de datos.
[[nodiscard]] inline bool sector_checksums_ok(const eng::u16* t, eng::u32 p,
					      eng::u32 h) noexcept {
	const eng::u32 hck = mfm_decode_long(
		mfm_word_at(t, p + 16u * (h + 20u)), mfm_word_at(t, p + 16u * (h + 21u)),
		mfm_word_at(t, p + 16u * (h + 22u)), mfm_word_at(t, p + 16u * (h + 23u)));
	const eng::u32 dck = mfm_decode_long(
		mfm_word_at(t, p + 16u * (h + 24u)), mfm_word_at(t, p + 16u * (h + 25u)),
		mfm_word_at(t, p + 16u * (h + 26u)), mfm_word_at(t, p + 16u * (h + 27u)));
	return mfm_xor_long_at(t, p + 16u * h, 10u) == hck &&
	       mfm_xor_long_at(t, p + 16u * (h + 28u), 256u) == dck;
}

} // namespace detail

/// Busca el sector `want_sector` en una pista cruda (palabras MFM) y copia sus 512 bytes a
/// `dst`. El sync (`$4489 $4489`) se busca a nivel de **bit**, de modo que funciona aunque el
/// flujo no esté alineado a palabra. `false` si no aparece, la cabecera no es AmigaDOS
/// (`format != 0xFF`) o, con `verify_checksums`, los checksums hck/dck no cuadran. Si
/// `out_header` no es nulo, deja la cabecera encontrada. `track.size()` debe cubrir una pista.
[[nodiscard]] inline bool floppy_find_sector(eng::Span<const eng::u16> track,
					     eng::u8 want_sector, 					     eng::Span<eng::u8> dst,
					     eng::Ref<FloppySectorHeader> out_header = {},
					     bool verify_checksums = false) noexcept {
	if (dst.size() < kSectorBytes || track.size() < 4u) {
		return false;
	}
	const eng::u16* const t = track.data();
	const eng::u32 n = track.size();

	// Intenta decodificar un sector cuyo primer sync empieza en el bit `p` y cuya cabecera
	// está en la palabra `h` (2 = dos syncs volcados, 1 = solo el segundo).
	const auto try_with = [&](eng::u32 p, eng::u32 h) -> bool {
		const eng::u32 hdr = mfm_decode_long(
			detail::mfm_word_at(t, p + 16u * (h + 0u)),
			detail::mfm_word_at(t, p + 16u * (h + 1u)),
			detail::mfm_word_at(t, p + 16u * (h + 2u)),
			detail::mfm_word_at(t, p + 16u * (h + 3u)));
		const FloppySectorHeader sh {
			static_cast<eng::u8>(hdr >> 24u),
			static_cast<eng::u8>(hdr >> 16u),
			static_cast<eng::u8>(hdr >> 8u),
			static_cast<eng::u8>(hdr),
		};
		if (sh.format != 0xffu || sh.sector != want_sector) {
			return false;
		}
		if (verify_checksums && !detail::sector_checksums_ok(t, p, h)) {
			return false;
		}
		for (eng::u32 k = 0u; k < 128u; ++k) { // dodd y deven
			const eng::u32 v = mfm_decode_long(
				detail::mfm_word_at(t, p + 16u * (h + 28u + 2u * k)),
				detail::mfm_word_at(t, p + 16u * (h + 29u + 2u * k)),
				detail::mfm_word_at(t, p + 16u * (h + 284u + 2u * k)),
				detail::mfm_word_at(t, p + 16u * (h + 285u + 2u * k)));
			const eng::u32 o = 4u * k;
			dst[o + 0u] = static_cast<eng::u8>(v >> 24u);
			dst[o + 1u] = static_cast<eng::u8>(v >> 16u);
			dst[o + 2u] = static_cast<eng::u8>(v >> 8u);
			dst[o + 3u] = static_cast<eng::u8>(v);
		}
		if (out_header.valid()) {
			*out_header = sh;
		}
		return true;
	};
	const auto try_at = [&](eng::u32 p) -> bool {
		return try_with(p, 2u) || try_with(p, 1u);
	};

	// Camino rápido: syncs alineados a palabra (el caso habitual).
	for (eng::u32 i = 0u; i + kMfmWordsPerSector <= n; ++i) {
		if (t[i] == kMfmSync && try_at(i * 16u)) {
			return true;
		}
	}
	// Camino general: sync a cualquier alineación de bit (ventana deslizante de 16 bits).
	const eng::u32 nbits = n * 16u;
	const eng::u32 sector_bits = static_cast<eng::u32>(kMfmWordsPerSector) * 16u;
	eng::u32 win = 0u;
	for (eng::u32 b = 0u; b < nbits; ++b) {
		const eng::u32 bit = (static_cast<eng::u32>(t[b >> 4u]) >> (15u - (b & 15u))) & 1u;
		win = (win << 1u) | bit;
		if (b < 15u || (win & 0xffffu) != kMfmSync) {
			continue;
		}
		const eng::u32 p = b - 15u; // primer bit del sync
		if ((p & 15u) == 0u) {
			continue; // ya cubierto por el camino rápido
		}
		if (p + sector_bits > nbits) {
			break;
		}
		if (try_at(p)) {
			return true;
		}
	}
	return false;
}

/// Enciende (`on`) o apaga el motor de la unidad `unit` (0 = DF0:). Apágalo al terminar.
bool floppy_motor(eng::u16 unit, bool on);

/// ¿Hay disco? `false` si la bandeja está vacía (`DSKCHANGE`/`DSKRDY`).
bool floppy_present(eng::u16 unit);

/// Lee `dst.size()` palabras de la pista `track` (MFM crudo) a `dst` (debe ser **Chip RAM**).
/// Hace seek y lee con DMA (WORDSYNC + `DSKLEN` doble) esperando `DSKBLK`. Devuelve las palabras
/// transferidas (0 si falla). Usar al menos `kMfmReadWords` (dos revoluciones): el sector del
/// sync de arranque queda partido y solo reaparece completo con margen en la segunda vuelta.
eng::u16 floppy_read_track(eng::u16 unit, eng::u8 track, bool side, eng::Span<eng::u16> dst);

} // namespace eng::os
