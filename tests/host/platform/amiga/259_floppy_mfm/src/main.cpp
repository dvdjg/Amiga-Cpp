// ============================================================================
// Test HOST-259: decodificacion MFM del disquete (eng/os/floppy.hpp).
// ============================================================================
//
// Construye una pista AmigaDOS sintetica con el MISMO layout que el encoder del emulador
// (WinUAE-DBG/disk.cpp:2185-2260) y comprueba que `floppy_find_sector` recupera cada sector:
// sync, cabecera (format/track/sector), y los 512 bytes de datos (layout dodd/deven).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/platform/amiga/259_floppy_mfm

#include <cstdio>
#include <cstdint>

#include <eng/os/floppy.hpp>

using namespace eng;
using namespace eng::os;

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

/// Encoder MFM de un long: 4 words consecutivos (dodd_hi, dodd_lo, deven_hi, deven_lo).
void enc_long(u16* p, u32 v) {
	const u32 deven = v & 0x55555555u;
	const u32 dodd = (v >> 1u) & 0x55555555u;
	p[0] = static_cast<u16>(dodd >> 16u);
	p[1] = static_cast<u16>(dodd);
	p[2] = static_cast<u16>(deven >> 16u);
	p[3] = static_cast<u16>(deven);
}
void enc_dodd(u16* p, u32 v) {
	const u32 d = (v >> 1u) & 0x55555555u;
	p[0] = static_cast<u16>(d >> 16u);
	p[1] = static_cast<u16>(d);
}
void enc_deven(u16* p, u32 v) {
	const u32 d = v & 0x55555555u;
	p[0] = static_cast<u16>(d >> 16u);
	p[1] = static_cast<u16>(d);
}

/// Rellena los bits de reloj MFM en las posiciones impares: mismo algoritmo que `mfmcode`
/// (WinUAE-DBG/disk.cpp:2059), que el encoder aplica a cabecera/etiqueta/checksums/datos.
void mfmcode(u16* mfm, u32 words) {
	u32 lastword = 0u;
	for (u32 i = 0u; i < words; ++i) {
		const u32 v = static_cast<u32>(mfm[i]) & 0x55555555u;
		const u32 lv = (lastword << 16u) | v;
		const u32 nlv = 0x55555555u & ~lv;
		const u32 mfmbits = (nlv << 1u) & (nlv >> 1u);
		mfm[i] = static_cast<u16>(v | mfmbits);
		lastword = v;
	}
}

/// XOR de `pairs` longs a partir de palabras ya codificadas (bits de datos en posiciones pares).
u32 xor_long(const u16* p, u32 pairs) {
	u32 x = 0u;
	for (u32 k = 0u; k < pairs; ++k) {
		x ^= (static_cast<u32>(p[2u * k]) << 16u) | p[2u * k + 1u];
	}
	return x;
}

/// Escribe un sector AmigaDOS en `t` (544 words). `data` = 512 bytes. Los checksums hck/dck se
/// calculan como en el emulador: XOR de los longs **crudos** de cabecera+etiqueta y de datos
/// (`WinUAE-DBG/disk.cpp:2231-2259`).
void build_sector(u16* t, u8 track, u8 sec, u8 to_gap, const u8* data) {
	t[0] = kMfmSync;
	t[1] = kMfmSync;
	const u32 hdr = (0xffu << 24u) | (static_cast<u32>(track) << 16u) |
			(static_cast<u32>(sec) << 8u) | to_gap;
	enc_long(t + 2u, hdr);
	for (u32 k = 0u; k < 4u; ++k) { // etiqueta de 16 B (a cero)
		enc_dodd(t + 6u + 2u * k, 0u);
		enc_deven(t + 14u + 2u * k, 0u);
	}
	enc_long(t + 22u, xor_long(t + 2u, 10u)); // hck (cabecera + etiqueta)
	for (u32 k = 0u; k < 128u; ++k) {
		const u32 v = (static_cast<u32>(data[4u * k]) << 24u) |
			      (static_cast<u32>(data[4u * k + 1u]) << 16u) |
			      (static_cast<u32>(data[4u * k + 2u]) << 8u) | data[4u * k + 3u];
		enc_dodd(t + 30u + 2u * k, v);
		enc_deven(t + 286u + 2u * k, v);
	}
	enc_long(t + 26u, xor_long(t + 30u, 256u)); // dck (datos)
}

/// Pista de 11 sectores con datos reconocibles (sector s -> byte = s*16 + i).
void build_track(u16* track, u8 tr) {
	for (u8 s = 0u; s < 11u; ++s) {
		u8 data[512];
		for (u32 i = 0u; i < 512u; ++i) {
			data[i] = static_cast<u8>((s * 16u + i) & 0xffu);
		}
		u16* const sec = track + static_cast<u32>(s) * kMfmWordsPerSector;
		build_sector(sec, tr, s, static_cast<u8>(11u - s), data);
		// El encoder real aplica los bits de reloj desde la cabecera ([2]) hasta el final
		// de los datos ([541]); los dos sync quedan fuera (disk.cpp:2264).
		mfmcode(sec + 2u, kMfmWordsPerSector - 4u);
	}
}

void test_roundtrip() {
	u16 track[11u * kMfmWordsPerSector] {};
	build_track(track, 3u);

	for (u8 s = 0u; s < 11u; ++s) {
		u8 out[512] {};
		FloppySectorHeader hdr {};
		const bool ok = floppy_find_sector(Span<const u16> {track, 11u * kMfmWordsPerSector},
						   s, Span<u8> {out, 512u}, &hdr, true);
		check(ok, "sector encontrado");
		if (!ok) {
			continue;
		}
		check(hdr.format == 0xffu, "format = 0xFF (AmigaDOS)");
		check(hdr.track == 3u, "track = 3");
		check(hdr.sector == s, "sector correcto");
		check(hdr.to_gap == 11u - s, "to_gap correcto");
		bool data_ok = true;
		for (u32 i = 0u; i < 512u; ++i) {
			if (out[i] != static_cast<u8>((s * 16u + i) & 0xffu)) {
				data_ok = false;
				break;
			}
		}
		check(data_ok, "512 bytes de datos intactos");
	}
}

void test_decode_long() {
	// encode(v) -> (dodd_hi, dodd_lo, deven_hi, deven_lo); decode debe devolver v.
	const u32 vals[] = {0u, 0x55555555u, 0xffffffffu, 0x12345678u, 0xdeadbeefu};
	for (u32 v : vals) {
		const u32 deven = v & 0x55555555u;
		const u32 dodd = (v >> 1u) & 0x55555555u;
		check(mfm_decode_long(static_cast<u16>(dodd >> 16u), static_cast<u16>(dodd),
				      static_cast<u16>(deven >> 16u), static_cast<u16>(deven)) == v,
		      "encode/decode inverso");
	}
}

void test_clock_immunity() {
	// Las palabras reales llevan bits de reloj en las posiciones impares; el decode debe
	// ignorarlos (mascara 0x5555). Se OR-ea 0xAAAA y se exige el valor original.
	const u32 vals[] = {0x00000000u, 0xffffffffu, 0x12345678u, 0xdeadbeefu};
	for (u32 v : vals) {
		const u32 deven = v & 0x55555555u;
		const u32 dodd = (v >> 1u) & 0x55555555u;
		const u16 dh = static_cast<u16>(dodd >> 16u) | 0xaaaau;
		const u16 dl = static_cast<u16>(dodd) | 0xaaaau;
		const u16 eh = static_cast<u16>(deven >> 16u) | 0xaaaau;
		const u16 el = static_cast<u16>(deven) | 0xaaaau;
		check(mfm_decode_long(dh, dl, eh, el) == v, "bits de reloj ignorados");
	}
}

void test_bit_shift() {
	// Desplaza 3 bits todo el flujo (simula un sync no alineado a palabra); el decode debe
	// encontrarlo igual gracias a la busqueda bit a bit.
	u16 track[11u * kMfmWordsPerSector + 1u] {};
	build_track(track, 6u);
	u16 shifted[11u * kMfmWordsPerSector + 1u] {};
	for (u32 i = 0u; i < 11u * kMfmWordsPerSector + 1u; ++i) {
		const u16 next = i + 1u < 11u * kMfmWordsPerSector + 1u ? track[i + 1u] : 0u;
		shifted[i] = static_cast<u16>((track[i] << 3u) | (next >> 13u));
	}
	u8 out[512] {};
	FloppySectorHeader hdr {};
	check(floppy_find_sector(Span<const u16> {shifted, 11u * kMfmWordsPerSector + 1u}, 7u,
				 Span<u8> {out, 512u}, &hdr, true),
	      "sector tras desplazamiento de 3 bits");
	check(hdr.track == 6u && hdr.sector == 7u, "cabecera correcta tras desplazamiento");
}

void test_single_sync() {
	// La DMA consume el primer sync de la pareja y a veces solo vuelca el segundo: el decode
	// debe probar la cabecera tambien a 1 palabra del sync (h=1).
	u16 sec[kMfmWordsPerSector] {};
	u8 data[512];
	for (u32 i = 0u; i < 512u; ++i) {
		data[i] = static_cast<u8>((3u * 16u + i) & 0xffu);
	}
	build_sector(sec, 4u, 3u, 8u, data);
	mfmcode(sec + 2u, kMfmWordsPerSector - 4u);
	u16 track[kMfmWordsPerSector + 1u] {};
	track[0] = sec[0];
	for (u32 i = 2u; i < kMfmWordsPerSector; ++i) {
		track[i - 1u] = sec[i];
	}
	u8 out[512] {};
	FloppySectorHeader hdr {};
	check(floppy_find_sector(Span<const u16> {track, kMfmWordsPerSector}, 3u,
				 Span<u8> {out, 512u}, &hdr, true),
	      "sector con un solo sync");
	check(hdr.track == 4u && hdr.sector == 3u, "cabecera correcta (un sync)");
	bool data_ok = true;
	for (u32 i = 0u; i < 512u; ++i) {
		if (out[i] != static_cast<u8>((3u * 16u + i) & 0xffu)) {
			data_ok = false;
			break;
		}
	}
	check(data_ok, "datos intactos (un sync)");
}

void test_checksum_rechazo() {
	u16 track[11u * kMfmWordsPerSector] {};
	build_track(track, 7u);
	u8 out[512] {};

	// Corrompe una palabra de datos del sector 4 (la cabecera queda intacta).
	track[4u * kMfmWordsPerSector + 30u] ^= 0x0001u;
	check(floppy_find_sector(Span<const u16> {track, 11u * kMfmWordsPerSector}, 4u,
				 Span<u8> {out, 512u}, nullptr, false),
	      "sector corrupto encontrado sin verificar");
	check(!floppy_find_sector(Span<const u16> {track, 11u * kMfmWordsPerSector}, 4u,
				  Span<u8> {out, 512u}, nullptr, true),
	      "dck corrupto -> false con verificacion");
	check(floppy_find_sector(Span<const u16> {track, 11u * kMfmWordsPerSector}, 5u,
				 Span<u8> {out, 512u}, nullptr, true),
	      "otro sector sigue verificando");
}

void test_negativos() {
	u16 track[11u * kMfmWordsPerSector] {};
	build_track(track, 0u);
	u8 out[512] {};

	check(!floppy_find_sector(Span<const u16> {track, 11u * kMfmWordsPerSector}, 11u,
				  Span<u8> {out, 512u}),
	      "sector inexistente -> false");

	u8 small[256] {};
	check(!floppy_find_sector(Span<const u16> {track, 11u * kMfmWordsPerSector}, 0u,
				  Span<u8> {small, 256u}),
	      "dst pequeno -> false");

	// Pista sin sync.
	u16 nosync[64] {};
	check(!floppy_find_sector(Span<const u16> {nosync, 64u}, 0u, Span<u8> {out, 512u}),
	      "sin sync -> false");
}

} // namespace

int main() {
	test_decode_long();
	test_clock_immunity();
	test_roundtrip();
	test_bit_shift();
	test_single_sync();
	test_checksum_rechazo();
	test_negativos();

	if (failures == 0) {
		std::printf("OK: decodificacion MFM de pista AmigaDOS validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
