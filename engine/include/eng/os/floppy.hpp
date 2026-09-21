#pragma once

/// \file floppy.hpp
/// **Disquete a bajo nivel** (`eng::os`): control mecánico por CIA-B, DMA crudo de Paula y
/// **decodificación MFM** en CPU. Es la alternativa a `trackdisk.device` (ver
/// `docs/debugging/CONSULTA-GROK-DISCO-Y-LOADER.md` §Decisión): no depende del OS y da control
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

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::os {

/// Sync MFM de AmigaDOS (`$4489` = `$A1` con el reloj faltante). Paula lo busca con `DSKSYNC`.
inline constexpr eng::u16 kMfmSync = 0x4489u;

/// Palabras de un sector AmigaDOS (sync + cabecera + etiqueta + checksums + 512 B de datos).
inline constexpr eng::u16 kMfmWordsPerSector = 544u;
/// Bytes útiles por sector.
inline constexpr eng::u16 kSectorBytes = 512u;

/// Cabecera de sector AmigaDOS.
struct FloppySectorHeader {
	eng::u8 format = 0u; ///< `0xFF` = AmigaDOS
	eng::u8 track = 0u;
	eng::u8 sector = 0u;
	eng::u8 to_gap = 0u; ///< sectores que faltan hasta el final de la pista
};

/// Decodifica un **long** MFM a 32 bits de datos. El encoder intercala `dodd`/`deven`
/// (`deven = v & 0x55555555`, `dodd = (v >> 1) & 0x55555555`); aquí se reconstruye `v`.
[[nodiscard]] constexpr eng::u32 mfm_decode_long(eng::u16 dodd_hi, eng::u16 dodd_lo,
						 eng::u16 deven_hi, eng::u16 deven_lo) noexcept {
	const eng::u32 dodd = (static_cast<eng::u32>(dodd_hi) << 16u) | dodd_lo;
	const eng::u32 deven = (static_cast<eng::u32>(deven_hi) << 16u) | deven_lo;
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

/// Busca el sector `want_sector` en una pista cruda (palabras MFM) y copia sus 512 bytes a
/// `dst`. `false` si no aparece o la cabecera no es AmigaDOS (`format != 0xFF`). Si `out_header`
/// no es nulo, deja la cabecera encontrada. `track.size()` debe cubrir al menos una pista.
[[nodiscard]] inline bool floppy_find_sector(eng::Span<const eng::u16> track,
					     eng::u8 want_sector, eng::Span<eng::u8> dst,
					     FloppySectorHeader* out_header = nullptr) noexcept {
	if (dst.size() < kSectorBytes || track.size() < 4u) {
		return false;
	}
	const eng::u16* const t = track.data();
	const eng::u32 n = track.size();
	for (eng::u32 i = 0u; i + kMfmWordsPerSector <= n; ++i) {
		if (t[i] != kMfmSync || t[i + 1u] != kMfmSync) {
			continue; // sync: dos palabras $4489
		}
		// Cabecera: 4 bytes en [i+2 .. i+5] (dodd_hi, dodd_lo, deven_hi, deven_lo).
		const eng::u32 hdr = mfm_decode_long(t[i + 2u], t[i + 3u], t[i + 4u], t[i + 5u]);
		const FloppySectorHeader h {
			static_cast<eng::u8>(hdr >> 24u),
			static_cast<eng::u8>(hdr >> 16u),
			static_cast<eng::u8>(hdr >> 8u),
			static_cast<eng::u8>(hdr),
		};
		if (h.format != 0xffu || h.sector != want_sector) {
			continue;
		}
		// Datos: dodd en [i+30 .. i+285] (128 longs), deven en [i+286 .. i+541].
		for (eng::u32 k = 0u; k < 128u; ++k) {
			const eng::u32 v = mfm_decode_long(t[i + 30u + 2u * k], t[i + 31u + 2u * k],
							   t[i + 286u + 2u * k], t[i + 287u + 2u * k]);
			const eng::u32 o = 4u * k;
			dst[o + 0u] = static_cast<eng::u8>(v >> 24u);
			dst[o + 1u] = static_cast<eng::u8>(v >> 16u);
			dst[o + 2u] = static_cast<eng::u8>(v >> 8u);
			dst[o + 3u] = static_cast<eng::u8>(v);
		}
		if (out_header != nullptr) {
			*out_header = h;
		}
		return true;
	}
	return false;
}

/// Enciende (`on`) o apaga el motor de la unidad `unit` (0 = DF0:). Apágalo al terminar.
bool floppy_motor(eng::u16 unit, bool on);

/// ¿Hay disco? `false` si la bandeja está vacía (`DSKCHANGE`/`DSKRDY`).
bool floppy_present(eng::u16 unit);

/// Lee una **pista cruda** (MFM) a `dst` (debe ser **Chip RAM**). Hace seek a `track` y lee con
/// DMA (WORDSYNC + `DSKLEN` doble) esperando `DSKBLK`. Devuelve las palabras transferidas (0 si
/// falla). `dst` debería tener al menos `kMfmWordsPerSector * 11` palabras (~6334).
eng::u16 floppy_read_track(eng::u16 unit, eng::u8 track, bool side, eng::Span<eng::u16> dst);

} // namespace eng::os
