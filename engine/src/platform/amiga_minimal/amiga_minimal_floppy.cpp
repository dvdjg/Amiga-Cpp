// ============================================================================
// Backend Amiga del disquete a bajo nivel (`eng/os/floppy.hpp`).
// ============================================================================
//
// Control mecanico por CIA-B PRB ($BFD100) + DMA crudo de Paula (DSKPT/DSKLEN doble, WORDSYNC)
// + espera de DSKBLK. No usa `trackdisk.device` ni dos.library. Referencias:
// AHRM Table 8-5 (CIA-B/CIA-A del subsistema de disco) y la ficha del emulador
// `docs/reference/emulators/winuae/trackdisk.md`.

#include <eng/os/floppy.hpp>

#include "amiga_minimal_internal.hpp"

namespace {

namespace d = eng::amiga::detail;

// CIA-B PRB ($BFD100): salidas de control de disco. CIA-A PRA ($BFE001): entradas de estado.
// (AHRM Table 8-5. Ojo: `amiga-bootcamp/01_hardware/common/floppy_hardware.md` dice PRA por
// error; el registro es PRB.)
volatile eng::u8* const ciab_prb = reinterpret_cast<volatile eng::u8*>(0xbfd100u);
volatile eng::u8* const ciaa_pra = reinterpret_cast<volatile eng::u8*>(0xbfe001u);

// Registros custom (offsets en palabras).
constexpr eng::u16 kDskpt = 0x020u / 2u;
constexpr eng::u16 kDsklen = 0x024u / 2u;
constexpr eng::u16 kDsksync = 0x07eu / 2u;
constexpr eng::u16 kAdkcon = 0x09eu / 2u;
constexpr eng::u16 kDmaDisk = 0x0010u;    // DMACON bit 4
constexpr eng::u16 kIntDskblk = 0x0002u;  // INTREQ/INTREQR bit 1
constexpr eng::u16 kAdkWordsync = 0x8400u; // SETCLR | WORDSYNC (bit 10)
constexpr eng::u16 kDsklenMax = 0x3fffu;   // DSKLEN: longitudes de 14 bits

// Bits de CIA-B PRB (activos a 0 salvo SIDE/DIR).
constexpr eng::u8 kMtr = 0x80u;  // /MTR
constexpr eng::u8 kSel0 = 0x08u; // /SEL0 (DF0)
constexpr eng::u8 kSide = 0x04u; // SIDE (WinUAE: cara = 1 - bit, disk.cpp:3489)
constexpr eng::u8 kDir = 0x02u;  // DIR (0 = hacia el centro; pista 0 esta fuera)
constexpr eng::u8 kStep = 0x01u; // /STEP (pulso)

// Bits de CIA-A PRA.
constexpr eng::u8 kRdy = 0x20u;  // /RDY (0 = listo)
constexpr eng::u8 kTk0 = 0x10u;  // /TK0 (0 = en pista 0)
constexpr eng::u8 kChng = 0x04u; // /CHNG (0 = cambio de disco pendiente)

eng::u8 prb() { return *ciab_prb; }
void prb_set(eng::u8 v) { *ciab_prb = v; }

/// Espera activa (no usa timers del OS). Aproximada; de sobra para los minimos mecanicos.
void spin(eng::u32 n) {
	for (volatile eng::u32 i = 0u; i < n; ++i) {
	}
}

/// Pulso de /STEP (alto -> bajo -> alto) con la espera mecanica minima entre pasos.
void step_pulse() {
	prb_set(static_cast<eng::u8>(prb() & ~kStep));
	spin(400u);
	prb_set(static_cast<eng::u8>(prb() | kStep));
	spin(30000u); // >= 3 ms entre pasos
}

/// Lleva el cabezal a la pista 0 (pasos hacia fuera, DIR=1) mirando /TK0.
void seek_track0() {
	prb_set(static_cast<eng::u8>(prb() | kDir));
	for (eng::u16 i = 0u; i < 120u; ++i) {
		if ((*ciaa_pra & kTk0) == 0u) {
			break;
		}
		step_pulse();
	}
}

/// Posiciona el cabezal en `track` (desde la pista 0, pasos hacia dentro).
void seek_track(eng::u8 track) {
	seek_track0();
	prb_set(static_cast<eng::u8>(prb() & ~kDir));
	spin(18000u); // >= 18 ms al invertir la direccion
	for (eng::u8 i = 0u; i < track; ++i) {
		step_pulse();
	}
}

} // namespace

bool eng::os::floppy_motor(eng::u16 unit, bool on) {
	if (unit != 0u) {
		return false; // solo DF0 por ahora
	}
	eng::u8 v = prb();
	v = on ? static_cast<eng::u8>(v & ~kMtr) : static_cast<eng::u8>(v | kMtr);
	v = on ? static_cast<eng::u8>(v & ~kSel0) : static_cast<eng::u8>(v | kSel0);
	prb_set(v);
	if (on) {
		// El motor tarda en girar a plena velocidad; el AHRM pide esperar /RDY (o 500 ms).
		for (eng::u32 i = 0u; i < 4000000u; ++i) {
			if ((*ciaa_pra & kRdy) == 0u) {
				break;
			}
		}
	}
	return true;
}

bool eng::os::floppy_present(eng::u16 unit) {
	if (unit != 0u) {
		return false;
	}
	// Con el motor encendido, /RDY = 0 indica disco presente y girando.
	return (*ciaa_pra & kRdy) == 0u && (*ciaa_pra & kChng) == 0u;
}

eng::u16 eng::os::floppy_read_track(eng::u16 unit, eng::u8 track, bool side,
				    eng::Span<eng::u16> dst) {
	if (unit != 0u || dst.size() < kMfmWordsPerSector) {
		return 0u; // hace falta al menos un sector
	}
	// DSKLEN solo admite longitudes de 14 bits; se redondea a palabra.
	eng::u16 words = static_cast<eng::u16>(dst.size() & ~1u);
	if (words > kDsklenMax) {
		words = kDsklenMax;
	}
	seek_track(track);
	// El parametro `side` es el indice de cara del ADF (0 = primera cara). WinUAE calcula
	// `side = 1 - ((prb >> 2) & 1)` (disk.cpp:3489), asi que el bit SIDE va invertido:
	// cara 0 -> SIDE = 1 (bit puesto), cara 1 -> SIDE = 0.
	prb_set(side ? static_cast<eng::u8>(prb() & ~kSide) : static_cast<eng::u8>(prb() | kSide));
	spin(2000u);

	// DMA crudo: WORDSYNC + DSKSYNC, puntero a Chip RAM, DSKLEN (doble escritura).
	// Limpiar WORDSYNC antes de armarlo rearma la deteccion de sync tras una lectura previa.
	d::custom_base[kAdkcon] = 0x0400u; // SETCLR=0: borra WORDSYNC
	d::custom_base[kAdkcon] = kAdkWordsync;
	d::custom_base[kDsksync] = kMfmSync;
	d::write_custom_pointer(kDskpt, dst.data());
	d::custom_base[d::custom_dmacon_offset] =
		static_cast<eng::u16>(d::dma_setclr | d::dma_master | d::dma_copper | kDmaDisk);
	// DSKLEN=0 deja `prevlen` sin DMAEN, de modo que la primera escritura cargue y la segunda
	// dispare (necesario al rearmar tras una lectura previa).
	d::custom_base[kDsklen] = 0u;
	const eng::u16 len = static_cast<eng::u16>(0x8000u | words);
	d::custom_base[kDsklen] = len;
	d::custom_base[kDsklen] = len; // segunda escritura: dispara la DMA

	// Espera de fin de bloque (DSKBLK), con tope anti-bloqueo (acotado para no agotar el arranque).
	eng::u32 guard = 0x007fffffu;
	while ((d::custom_base[d::custom_intreqr_offset] & kIntDskblk) == 0u) {
		if (--guard == 0u) {
			break;
		}
	}
	d::custom_base[d::custom_intreq_offset] = kIntDskblk; // limpiar el flag
	d::custom_base[d::custom_dmacon_offset] = kDmaDisk; // SETCLR=0: borra la DMA de disco
	return guard != 0u ? words : 0u;
}
